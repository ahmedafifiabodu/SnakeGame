#!/usr/bin/env bash
#
# Deploy one NEON COIL relay to Google Cloud.
#
# The relay is a byte pump with no state, so a "deployment" is genuinely just a
# small VM with a long-lived TCP listener on it. That is what makes running one
# per region reasonable rather than ambitious: there is nothing to shard, nothing
# to replicate, and nothing that has to be redeployed when the game changes.
#
# Run once per region. Each gets its own letter, which becomes the first
# character of every code that relay hands out -- so a guest typing a code is
# dialled to the region the session is actually in.
#
#   ./tools/deploy_relay.sh --region me-west1 --tag M --name middle-east \
#       --repo https://github.com/you/neoncoil.git
#
# The zone is picked for you. Small machines run out per-zone, and a relay
# does not care which zone it lives in, so every zone in the region is tried
# rather than making a person re-run this with a different letter on the end.
#
# Prints the netconfig.txt line to paste when it finishes.
#
# Needs the gcloud CLI, authenticated, with a project selected:
#   gcloud auth login && gcloud config set project <your-project>

set -euo pipefail

REGION=""
ZONE=""
TAG=""
NAME=""
REPO=""
PORT=45700
MACHINE="e2-micro"

usage()
{
    cat <<'USAGE'
Usage: deploy_relay.sh --region <r> --tag <L> --name <n> --repo <url> [--zone <z>]

  --region   GCP region, e.g. me-west1
  --zone     Zone to try first, e.g. me-west1-b. Optional: without it every
             zone in the region is tried in turn, and with it the rest are
             still tried if that one has no capacity.
  --tag      One letter for this region's codes, e.g. M. Avoid I, L and O:
             codes get read aloud, and those are the ones people mishear.
  --name     Short name for the VM and the static IP, e.g. middle-east
  --repo     Git URL of this repository, cloned on the box to build the relay
  --port     Listen port (default 45700)
  --machine  Machine type (default e2-micro)
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --region)  REGION="$2"; shift 2 ;;
        --zone)    ZONE="$2"; shift 2 ;;
        --tag)     TAG="$2"; shift 2 ;;
        --name)    NAME="$2"; shift 2 ;;
        --repo)    REPO="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        --machine) MACHINE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unrecognised argument: $1" >&2; usage; exit 2 ;;
    esac
done

for required in REGION TAG NAME REPO; do
    if [ -z "${!required}" ]; then
        echo "missing --$(echo "$required" | tr '[:upper:]' '[:lower:]')" >&2
        usage
        exit 2
    fi
done

VM="neoncoil-relay-${NAME}"
IP_NAME="neoncoil-relay-${NAME}-ip"
FIREWALL="neoncoil-relay"
NETWORK_TAG="neoncoil-relay"

# --- firewall ---------------------------------------------------------------
#
# One rule for the whole fleet, matched by network tag rather than by instance,
# so adding a region later needs nothing here. This is the only inbound port in
# the entire system, and it is opened once, by you, on machines you control.
if ! gcloud compute firewall-rules describe "$FIREWALL" >/dev/null 2>&1; then
    echo "creating firewall rule $FIREWALL (tcp:$PORT)"
    gcloud compute firewall-rules create "$FIREWALL" \
        --direction=INGRESS \
        --action=ALLOW \
        --rules="tcp:${PORT}" \
        --source-ranges=0.0.0.0/0 \
        --target-tags="$NETWORK_TAG" \
        --description="NEON COIL relay"
else
    echo "firewall rule $FIREWALL already exists"
fi

# --- static address ---------------------------------------------------------
#
# Reserved, not ephemeral. The address ships inside netconfig.txt beside the
# game, so an IP that changes on reboot breaks every copy already installed.
if ! gcloud compute addresses describe "$IP_NAME" --region="$REGION" >/dev/null 2>&1; then
    echo "reserving static address $IP_NAME in $REGION"
    gcloud compute addresses create "$IP_NAME" --region="$REGION"
fi

ADDRESS="$(gcloud compute addresses describe "$IP_NAME" --region="$REGION" --format='value(address)')"

# --- startup script ---------------------------------------------------------
#
# Builds on first boot rather than shipping a binary, so there is nothing to
# cross-compile and nothing to keep in sync by hand. NEONCOIL_RELAY_ONLY keeps
# SFML's Graphics, Window and Audio out of it, so no X11, OpenGL or ALSA is
# needed on a box that has no screen.
STARTUP="$(mktemp)"
trap 'rm -f "$STARTUP"' EXIT

cat > "$STARTUP" <<STARTUP_SCRIPT
#!/usr/bin/env bash
set -euxo pipefail

# Already built: this is a reboot, not a first boot.
if [ -x /opt/neoncoil/neoncoil-relay ]; then
    systemctl start neoncoil-relay || true
    exit 0
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates

# e2-micro has 1 GB of RAM and the SFML build will be killed part-way through
# without this, with no message that says so.
if [ ! -f /swapfile ]; then
    fallocate -l 2G /swapfile
    chmod 600 /swapfile
    mkswap /swapfile
    swapon /swapfile
fi

rm -rf /tmp/neoncoil
git clone --depth 1 "${REPO}" /tmp/neoncoil
cd /tmp/neoncoil

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEONCOIL_RELAY_ONLY=ON
cmake --build build -- -j2

mkdir -p /opt/neoncoil
install -m 0755 build/bin/neoncoil-relay /opt/neoncoil/neoncoil-relay

cat > /etc/systemd/system/neoncoil-relay.service <<UNIT
[Unit]
Description=NEON COIL relay (${NAME})
After=network.target

[Service]
ExecStart=/opt/neoncoil/neoncoil-relay --port ${PORT} --region ${TAG} --quiet
Restart=always
RestartSec=2
DynamicUser=yes

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable --now neoncoil-relay
STARTUP_SCRIPT

# --- the machine ------------------------------------------------------------
#
# Zones run out of small machines. ZONE_RESOURCE_POOL_EXHAUSTED is not a
# misconfiguration and not something waiting fixes reliably -- it means that
# zone has no e2-micro to give right now, and the one next door probably does.
# Since a relay does not care which zone it lives in, the script tries them all
# rather than making a person re-run it with a different letter.
#
# The address is reserved per REGION, so every zone below reuses the same one.
ZONES="$(gcloud compute zones list --filter="region:(${REGION})" --format='value(name)' | sort)"

if [ -z "$ZONES" ]; then
    echo "no zones found in $REGION -- is the region name right, and the Compute API enabled?" >&2
    exit 1
fi

# A zone named on the command line is tried first; the rest follow as fallbacks.
if [ -n "$ZONE" ]; then
    ZONES="$ZONE $(echo "$ZONES" | grep -v "^${ZONE}$" | tr '\n' ' ')"
fi

for candidate in $ZONES; do
    if gcloud compute instances describe "$VM" --zone="$candidate" >/dev/null 2>&1; then
        echo "instance $VM already exists in $candidate -- delete it first to redeploy" >&2
        exit 1
    fi
done

CREATED=""
LAST_ERROR=""

for candidate in $ZONES; do
    echo "creating $VM in $candidate at $ADDRESS"

    if OUTPUT="$(gcloud compute instances create "$VM" \
        --zone="$candidate" \
        --machine-type="$MACHINE" \
        --image-family=debian-12 \
        --image-project=debian-cloud \
        --boot-disk-size=10GB \
        --tags="$NETWORK_TAG" \
        --address="$ADDRESS" \
        --metadata-from-file=startup-script="$STARTUP" 2>&1)"; then
        echo "$OUTPUT"
        CREATED="$candidate"
        break
    fi

    LAST_ERROR="$OUTPUT"

    # Only capacity is worth moving zone for. A quota problem, a bad image or a
    # permission error will fail identically everywhere, and retrying it five
    # times buries the message that says what is actually wrong.
    if ! echo "$OUTPUT" | grep -q "ZONE_RESOURCE_POOL_EXHAUSTED"; then
        echo "$OUTPUT" >&2
        exit 1
    fi

    echo "  no ${MACHINE} capacity in ${candidate}, trying the next zone"
done

if [ -z "$CREATED" ]; then
    echo "$LAST_ERROR" >&2
    echo "" >&2
    echo "No zone in ${REGION} has ${MACHINE} capacity right now. Either wait, or" >&2
    echo "pass a different --machine (e2-small is the next size up), or pick" >&2
    echo "another region -- a relay two hundred kilometres further away is worth" >&2
    echo "more than one that does not exist." >&2
    exit 1
fi

ZONE="$CREATED"

cat <<DONE

Deployed. The first boot builds the relay from source, which takes a few
minutes; until it finishes the port is open but nothing is listening.

Watch it:
  gcloud compute ssh ${VM} --zone=${ZONE} --command='sudo journalctl -u google-startup-scripts -f'

Check it is up:
  gcloud compute ssh ${VM} --zone=${ZONE} --command='systemctl status neoncoil-relay --no-pager'

Then add this line to netconfig.txt beside the game:

  relay = ${TAG} | ${NAME} | ${ADDRESS} | ${PORT}

The letter has to match the --region the relay was started with. It is the first
character of every code this relay mints, and it is how a typed code finds its
way back here.
DONE

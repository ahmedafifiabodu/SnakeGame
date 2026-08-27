# Multiplayer

How NEON COIL does four-player online play, and why it does it that way.

Overview: [README](../README.md#multiplayer).

### The topology, and why there are no dedicated servers

**One player hosts. The host simulates. Everybody else sends intents and draws
what comes back.** A listen server, not a dedicated one.

This is a deliberate decision, not a default:

- **Dedicated servers would make latency worse, not better.** With four players,
  a listen server costs three of them one round trip and the host none. A
  datacentre costs all four a round trip. Unless players are spread across
  continents, the average goes up.
- **They cost money per concurrent match, forever.** The host topology costs
  nothing at ten players and nothing at a hundred thousand, because every match
  runs on somebody's PC. A VPS is cheap but not free, and it comes with a Linux
  build, a container, a deploy pipeline, monitoring and region selection.
- **The thing dedicated servers actually buy is authority against cheating.**
  There is no ladder, no economy and no persistent progression here, so there is
  nothing yet worth cheating for. Revisit this when there is.

What *is* worth having from a dedicated server — one authoritative copy of the
rules — is already here. `MatchSimulation` is the only place the rules exist, it
runs on the host, and the host draws its own board from the same snapshot it
sends everyone else. A guest cannot desync, because a guest never simulates.

The one genuine gap in this topology is **NAT traversal**: two players behind
routers cannot accept each other's connections. That is solved here by a relay
this repository ships and you run — see [Hosting a session](#hosting-a-session).
Both ends dial *out* to it, so no player forwards a port, and it lands exactly
where the design left room for it: a `ServerTransport` / `ClientTransport` pair,
with the session layer, the protocol, the lobby and the simulation untouched.

### Transport

TCP, over SFML's own network module, so multiplayer adds no third-party
dependency — it is a CMake flag on a library the game already links.

TCP for a real-time game is a choice worth defending. The session is four
players, a twenty-hertz snapshot rate and roughly a kilobyte per snapshot, and
every message the protocol defines is one the receiver genuinely must not miss:
lobby state, match start, the final standings. Hand-rolling reliability and
ordering on UDP would buy back one round trip on a dropped packet and cost
several hundred lines that can be got subtly wrong. The game steps eight times a
second; that round trip is not visible.

`ServerTransport` and `ClientTransport` are abstract for exactly this reason — a
UDP, relay or Steam backend slots in underneath without the session layer
noticing.

Networking runs on the game thread, inside `update()`. Sockets are non-blocking
and polled once a frame, which removes every locking question in exchange for at
most one frame of latency. The single exception is the client's connect, which
resolves and handshakes on a worker thread so the menu does not freeze for the
length of a TCP timeout.

### What goes over the wire

The **arena is sent as a packed bitset**, not as a seed. The generator is
deterministic, but it draws from `std::uniform_int_distribution`, whose sequence
is *not* specified across standard library implementations — a Linux client would
generate a different board from a Windows host. A 56×32 board is 224 bytes.
Sending it removes the whole class of problem for the price of one small message,
and it is checked tile-for-tile by `--nettest`.

**Snapshots are absolute, never deltas.** Whole snake bodies, whole food list,
capped at 64 segments per snake so the worst case stays bounded. A client that
misses anything still converges on the next snapshot.

**There is no client-side prediction.** A guest's snake turns when the host says
it turns. On a grid game stepping eight times a second that costs well under one
step of felt latency, and it saves a reconciliation system that would have to be
kept bug-for-bug identical with the host's rules. `InputCommand` already carries a
sequence number, so prediction can be added later without a protocol break.

Every length read off the wire is bounded before anything is allocated, and a
client that has not completed the handshake cannot touch the lobby or the
simulation.

### Matchmaking

`IMatchmaker` is an interface with one implementation today: **LAN discovery**,
which needs no server and no account, so it works the moment the game is
installed.

Clients probe and hosts answer, rather than hosts beaconing. Only the host binds
the fixed discovery port, so any number of clients can browse on the same machine
as a host — which is what makes it possible to test a full four-player session
with four processes on one PC.

`QUICK MATCH` searches for a couple of seconds, joins the **fullest session that
still has room**, and hosts one if there is nothing. Filling one lobby beats
scattering four players across three sessions that never start.

A directory service, a Steam lobby search or a ranked queue all fit behind the
same interface.

### Player identity, and the space left for accounts

**There is no login, and none is implemented.** There is, deliberately, a shaped
hole where one goes.

Everything keys off `net::PlayerIdentity`, whose `id` is an **opaque string that
nothing outside `net/Identity.h` is allowed to interpret**. Today
`LocalIdentityProvider` generates a GUID on first run and keeps it in a file next
to the executable, so a player has a stable identity across runs without ever
having made an account. A joining client presents a `JoinTicket`; the host calls
`IIdentityProvider::verify` and nothing else ever inspects it.

Adding accounts is therefore: write a provider whose `verify` checks a signed
token, and install it instead — one line, in `App::run`. The lobby, the protocol,
the matchmaker and the simulation do not change, because none of them knows what
an id is. `PlayerIdentity::authenticated` already travels in the lobby and is
already rendered on each player card as `GUEST`; the seat-uniqueness rule already
keys off it. `--nettest` proves the seam by running the entire session layer
against a provider it has never seen.

### Configuration

Nothing in `net/` reads a literal port, timeout or match rule. They all come from
`NetConfig`, resolved once at startup: built-in defaults, then `netconfig.txt`
next to the executable if it exists, then the command line. See
[`netconfig.example.txt`](netconfig.example.txt) — every key is optional, and
multiplayer works on a local network with the file absent.

Match rules are host-authoritative and travel to guests inside `MatchStart`, so a
host can retune a match without guests having the same config file.

### Joining, leaving and things going wrong

- **A player leaving mid-match** takes their snake with them and the match
  carries on. No pause, no vote, no stall.
- **A fifth player** is refused with a reason on screen, not dropped silently. So
  is somebody trying to join a match already in progress; the lobby reopens when
  it ends.
- **The host quitting** sends every guest a reason before the socket closes, and
  they land back on the multiplayer menu knowing why.
- **Host migration is not implemented.** With the authoritative state already
  serialisable and the transport already abstract, it is an addition rather than
  a redesign — but it is not there, and a host leaving ends the session.
- **There is no pause.** Pausing a four-player match means pausing everybody,
  which is a host power this mode does not need.

### Testing it

```bash
./NeonCoil --nettest
```

Sixty-two checks, in one process, over real sockets, with no window — and it is
the only way this part of the game can be verified without four people in a room.

The first half runs a host and three clients on loopback: filling the lobby,
refusing a fifth player, refusing a duplicate account, letting two guests share
one local guest id, readying up, starting a match, checking the arena arrived
intact tile-for-tile, steering, a player leaving mid-match, playing the match out,
reading the standings, returning to the lobby, the host quitting, and LAN
discovery finding an advertised session.

The second half stands a relay up inside the test process and does it all again
through it, including a code typed the way a person would type it. Details under
[Hosting a session](#hosting-a-session).

Layouts are checked the same way every other screen is:

```bash
./NeonCoil --uidump netmenu
./NeonCoil --uidump lobby
./NeonCoil --uidump netplay
```

And the multiplayer screenshots on this page are captured the same way the
single-player ones are: by the build, not by hand. `--screenshot lobby` and
`--screenshot netplay` stand a real four-seat session up inside the process — one
host, three guests over loopback, the real handshake, the real authoritative
simulation. The guests are steered by a small look-ahead that prefers open ground
and closes on food, so the board is alive rather than four corpses. The only thing
staged is that all four players happen to be on one machine.

```bash
./NeonCoil --screenshot lobby docs/shot_lobby.png --frames 90
./NeonCoil --screenshot netplay docs/shot_netplay.png --frames 620
./NeonCoil --capture netplay frames --frames 1400 --skip 430 --every 5
python tools/make_gif.py frames docs/demo_netplay.gif --width 720 --fps 14
```

To play it for real on one machine, start two or more copies from the build
output. Give each its own identity file if you want them to look like different
people:

```bash
./NeonCoil --name ALFA
./NeonCoil --name BRAVO --netconfig bravo.txt
```

The first hosts; the rest find it under `SESSIONS NEARBY`, or join `127.0.0.1`.

### Hosting a session

There is no server to run for a normal game and nothing for a player to
configure. A host is a copy of the game with a host button pressed.

**HOST ON THIS NETWORK** opens a TCP listener and answers discovery probes.
Everyone on the same wifi sees the session under `SESSIONS NEARBY` and clicks it.
Nothing to set up, no relay, no account. This is the whole story for a LAN party
or two people in one house.

**HOST ONLINE** does not listen at all. It dials *out* to the relay, which hands
back a six-character code. Guests type the code and dial out to the same relay,
and the relay pumps bytes between them. Neither end ever accepts an inbound
connection, so **nobody forwards a port** — and because it never relies on
punching a hole through a NAT, it works where hole punching does not: behind
carrier-grade NAT, on a phone hotspot, on a locked-down office network.

That is the same shape as Steam Datagram Relay, except you run the relay instead
of Valve, and you do not need a Steam AppID to do it.

### Running the relay

The relay ships in this repository as a second binary, `neoncoil-relay`, built by
the same CMake configuration. Locally it comes out of a normal build:

```bash
cmake --build build --target neoncoil-relay
```

**On a server, build it relay-only.** `-DNEONCOIL_RELAY_ONLY=ON` leaves SFML's
Graphics, Window and Audio modules out entirely, so a bare box needs a compiler
and CMake and nothing else — no X11, no OpenGL, no FreeType, no ALSA.

First check the CMake version, because most stable distributions ship one too old
for SFML 3.1 and the failure happens deep inside the fetched dependency rather
than anywhere obvious:

```bash
cmake --version   # needs 3.28+; Debian 12 ships 3.25
```

If it is older, install a current one over the top — no repository to add:

```bash
curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz | sudo tar -xz --strip-components=1 -C /usr/local
```

Nothing else needs installing. SFML's network module wants mbedTLS and libssh2,
and this project builds both from pinned source on every platform rather than
looking for system copies — `apt install libmbedtls-dev` would be the wrong fix
anyway, since Debian 12 ships mbedTLS 2.28 and SFML 3.1 wants 3.6.

Then:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEONCOIL_RELAY_ONLY=ON
cmake --build build
./build/bin/neoncoil-relay --port 45700
```

On a 1 GB instance, add swap first and cap the parallelism, or the compile is
killed part-way through with no useful message:

```bash
sudo fallocate -l 2G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
cmake --build build -- -j2
```

Or skip installing a toolchain on the server at all:

```bash
docker build -f Dockerfile.relay -t neoncoil-relay .
docker run -d --restart=always -p 45700:45700 --name relay neoncoil-relay
```

Then point the game at it, once, in the `netconfig.txt` that ships beside the
executable:

```
relay_host = relay.yourdomain.com
relay_port = 45700
```

Players never see any of that. They press `HOST ONLINE`, read out a code, and
their friends type it in.

Because the relay never parses a game message, that image and that binary do not
need rebuilding when the game changes.

**What it costs.** `--nettest` measures this rather than guessing at it, and the
number is small: a snapshot of four short snakes is about **150 bytes**, and the
64-segment length cap puts the worst case near **1.2 KB**. At the default 20 Hz
with three guests that is **9 KB/s of egress early in a match and about 70 KB/s
at the absolute ceiling** — call it 30 to 260 MB per match-hour, typically
around 100 MB.

Only the outbound half counts: the host uploads one snapshot and the relay fans
it out, so the host's narrow upstream carries one copy rather than three, and
what the relay receives is ingress, which is free almost everywhere.

At ~100 MB per match-hour, **10 TB of monthly egress is on the order of a hundred
thousand match-hours**. Bandwidth is not what will limit this, and neither is
CPU: forwarding opaque blobs is not work. Halving `snapshot_hz` to 10 halves the
figure again with no visible difference on a game that steps eight times a
second.

**Why it stays cheap.** The relay never parses a game message, never runs the
rules, and holds no game state. It is a rendezvous table and a byte pump. That
means no per-match CPU, no memory that scales with snake length, and — the part
that matters over years — **no redeploy when the game changes**. The game
protocol can be rewritten entirely and the relay does not care.

It also means the relay is not a dedicated server and should not be confused for
one. The host still simulates; authority never leaves the host.

**Where to run it.** The requirements are unusually low: a public IPv4 address, a
few hundred MB of RAM, and the ability to hold a long-lived TCP listener. No
disk, no database, no GPU. That puts it inside several providers' permanently
free tiers — the constraint to check is always **monthly egress**, since at
~100 MB per match-hour even a modest allowance covers far more play than a small
game will generate. An IPv6-only box is the one thing to avoid: plenty of
players are still IPv4-only and would simply be unable to connect.

**Deploying it.** It links only SFML Network and System — no window, no GPU, no
X11, no audio — so a bare Linux box is enough. As a systemd unit:

```ini
[Unit]
Description=NEON COIL relay
After=network.target

[Service]
ExecStart=/opt/neoncoil/neoncoil-relay --port 45700 --quiet
Restart=always
User=neoncoil

[Install]
WantedBy=multi-user.target
```

Open TCP `45700` on the relay machine's firewall. That is the only port
forwarding in the entire system, it is done once, by you, on a machine you
control — and never by a player.

### Which host button to use

| | Reaches | Needs |
|---|---|---|
| `HOST ON THIS NETWORK` | Same wifi / LAN | Nothing |
| `HOST ONLINE` | Anywhere | A relay you run, configured once in `netconfig.txt` |
| `JOIN BY ADDRESS` | A host that is listening | The host's address, and a forwarded port if across the internet |

`JOIN BY ADDRESS` is kept for direct connections — LAN debugging, a dedicated box
on a network you control — but it is no longer the answer for internet play, and
no player has to reach for it.

### Testing it

```bash
./NeonCoil --nettest
```

Sixty-two checks. The second half stands a relay up inside the test process and
plays a whole match through it: registering, handing out a code, two guests
joining by code (one of them typed in lowercase with a dash in it, the way a
person would), refusing an unknown code, the arena crossing the relay intact
tile-for-tile, snapshots, a guest leaving mid-match, the standings, and the host
quitting.

To try it by hand on one machine, in three terminals:

```bash
./neoncoil-relay --port 45700
```

Then create a `netconfig.txt` next to `NeonCoil` containing
`relay_host = 127.0.0.1`, and start two copies of the game. One presses
`HOST ONLINE` and reads the code off the lobby; the other types it into
`SESSION CODE` and presses `JOIN BY CODE`.
### Where this goes next

The architecture was shaped so that each of these is an addition rather than a
rewrite:

| Want | Costs |
|---|---|
| Accounts / login | A new `IIdentityProvider`. One line in `App::run`. |
| Friends, invites, presence | A new `IMatchmaker`; join codes already exist. |
| Steam, if you ever want it | A `ServerTransport` / `ClientTransport` pair over `ISteamNetworkingSockets` — the same shape as the relay pair that already exists. Not required: the relay already solves NAT traversal without it. |
| Persistent stats and progression | Keyed off `PlayerIdentity::id`, already carried through the lobby and the standings. |
| Dedicated servers | `MatchSimulation` is already headless and rules-complete. A server binary is a `main()` and a transport, not a port. |
| Client-side prediction | `InputCommand` already carries a sequence number. |


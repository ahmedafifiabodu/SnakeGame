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

`IMatchmaker` is an interface with two implementations, shown to the player as
one list under `OPEN SESSIONS`.

**LAN discovery** needs no server and no account, so it works the moment the game
is installed. Clients probe and hosts answer, rather than hosts beaconing. Only
the host binds the fixed discovery port, so any number of clients can browse on
the same machine as a host — which is what makes it possible to test a full
four-player session with four processes on one PC. Replies are matched on the
session code rather than the address, because a browser probes by broadcast *and*
to loopback, and a host on the same machine would otherwise be listed twice.

**Relay listing** asks the relay what is registered right now. Everything anybody
is hosting online is in that answer, so a player joins by picking a row instead
of being told a code by somebody they already know — which is the difference
between a game you can play with a friend and a game you can play. The query is a
blocking connect-ask-read, so it runs on a worker thread and its result is
collected on a later frame; the list on screen is a couple of seconds old, which
for a lobby list is invisible.

`CompositeMatchmaker` merges the two. A local session sorts ahead of an online
one and wins outright if the same code appears in both — same game, shorter path.
Rows are labelled `LOCAL` or `ONLINE` so a player can see which is which before
they commit to one.

`QUICK MATCH` searches for a couple of seconds, joins the **fullest session that
still has room** — preferring a local one — and hosts one if there is nothing.
Filling one lobby beats scattering four players across three sessions that never
start.

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

The first hosts; the rest find it under `OPEN SESSIONS`, or join `127.0.0.1`.

### Hosting a session

There is no server to run for a normal game and nothing for a player to
configure. A host is a copy of the game with a host button pressed.

**HOST ON THIS NETWORK** opens a TCP listener and answers discovery probes.
Everyone on the same wifi sees the session under `OPEN SESSIONS` and clicks it.
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

Give each deployment its own region letter, and point the game at all of them,
once, in the `netconfig.txt` that ships beside the executable:

```bash
./neoncoil-relay --port 45700 --region M --quiet
```

```
relay = M | MIDDLE EAST | relay-me.yourdomain.com | 45700
relay = E | EUROPE      | relay-eu.yourdomain.com | 45700
```

The letter after `--region` and the letter in the config line have to match: it
is the first character of every code that relay mints, and it is how a guest
typing a code is dialled to the right one.

Players never see any of that. They press `HOST ONLINE`, read out a code, and
their friends type it in — from whichever region is nearest them, which the game
picks on its own.

**How many.** One per cluster of players, not one per country. The relay is only
ever a detour, so what matters is that nobody's detour is long; two players in
one city are served perfectly by one relay in their region and gain nothing from
a second. Add regions when players appear somewhere the existing ones do not
reach, and let the ping column in the region picker decide it rather than a
guess at a map.

For a game selling worldwide, five covers most of it:

| Tag | Region | GCP | Covers |
|---|---|---|---|
| `U` | US CENTRAL | `us-central1` | North America |
| `E` | EUROPE | `europe-west1` | Europe, North Africa |
| `M` | MIDDLE EAST | `me-central2` | Middle East, Egypt, West Asia |
| `A` | ASIA | `asia-southeast1` | South and South-East Asia |
| `S` | S AMERICA | `southamerica-east1` | South America |

Add `asia-northeast1` (Japan/Korea) and `australia-southeast1` when players
appear there. Avoid `I`, `L` and `O` as tags for the same reason the code
alphabet leaves them out: a code that is read aloud has to survive being
misheard.

**One command per region.** `tools/deploy_relay.sh` creates the firewall rule
once for the fleet, reserves a static address, and boots a machine whose startup
script builds the relay from source and installs it as a systemd unit. It prints
the `netconfig.txt` line to paste when it finishes.

```bash
./tools/deploy_relay.sh --region me-central2 --zone me-central2-a \
    --tag M --name middle-east --repo <this repo's git url>
```

The address is reserved rather than ephemeral because it ships inside
`netconfig.txt` beside the game: an IP that changes on reboot breaks every copy
already installed.

**What a fleet does and does not fix.** A session lives on exactly one relay --
the host's. So the fleet makes every *host* close to their relay, and guests are
then as close as geography allows. Two players in one country now share a relay
in that country instead of one across an ocean, which is the whole win. A match
between Cairo and São Paulo is still a match between Cairo and São Paulo; no
placement makes that short, and nothing in this document pretends otherwise.

What the fleet does do is stop the game from being needlessly slow for people who
are near each other -- which, in practice, is most matches.

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

### Regions

One relay is a single point of distance. Every player it serves pays the gap
between themselves and it, twice per key press, whether they live next door to it
or on another continent. So the game takes a **list** of relays rather than one.

```
relay = M | MIDDLE EAST | 34.1.2.3 | 45700
relay = E | EUROPE      | 34.4.5.6 | 45700
```

**Codes carry their region.** A relay started with `--region M` stamps `M` on the
front of every code it hands out, making it seven characters instead of six. A
guest who types that code is dialled to that relay, without anybody having to say
which region the session is in. An untagged relay still mints six-character codes
and still works: that is what every deployment made before regions existed does,
and none of them have to change.

**The picker shows a live ping per region.** It is measured on the session-list
query the browser is already sending every few seconds, so it is the real path a
game would take rather than a synthetic probe — no extra message, no extra
connection, and it works against a relay binary that predates the feature.

`AUTO` is the default and resolves to whichever region is answering fastest right
now. It is the right answer for nearly every player; the picker exists for the
ones it is not right for, and for seeing *why* a session feels the way it does.

`OPEN SESSIONS` merges every region into one list, **nearest first** -- local
sessions, then by the ping of the region each one is in, then by how full it is.
With relays on several continents the list stops being a handful of equally
reachable sessions and becomes every open session on Earth, most of which this
player should not join; ordering by distance is what keeps the top of the list
the part worth reading. Truncation happens after the sort, so what falls off the
end is the far side of the world rather than whichever region answered last.

Each row is tagged with its region and that region's ping. Joining a row dials
the relay the row was found on, not the one currently selected for hosting.

### Ping

Every player's round trip is on screen: on their seat in the lobby, next to their
name in the match scoreboard, and — for the local player — top right during play.

It is measured on the heartbeat that was already being sent once a second to
prove the connection was alive. The client stamps a nonce on it, the host echoes
that nonce straight back, and the difference on the client's own clock is the
round trip. No new message, no new timer, and no clock synchronisation between
two machines — an echoed nonce needs neither side to agree what time it is.

The client then reports the number it measured on its *next* heartbeat, the host
puts it in the lobby, and the lobby goes to everybody. That is what turns "the
game feels bad" into "the game feels bad because that player is 300 ms away".

The figure is quantised by the client's frame rate — a reply is noticed on the
frame it is polled, not the instant it lands — so it reads a few milliseconds
high. It never reads low, which is the direction to be wrong in.

### Latency, and where it comes from

Three things decide how long it takes for a key press to show up on screen, and
they are not the same size.

**Distance to the relay dominates everything else.** A relayed input travels
player → relay → host, and the snapshot travels host → relay → player: two full
crossings of whatever gap sits between the players and the relay machine. Two
players in the same city with a relay on another continent pay that gap four
times over for a game whose two ends are milliseconds apart. **Run the relay near
the players.** It is one small always-on process with no state to migrate, so
moving it is a redeploy, not a migration — and it is by far the largest lever in
this document.

**Nagle's algorithm** is off on every socket in the system, game and relay alike
(`net::GameSocket`). Nagle holds a small write back until the previous one is
acknowledged, which is right for a program that writes a byte at a time and
exactly wrong for one whose every message is small and wanted now. Left on, it
adds a fraction of a round trip at each of the four hops, and the four do not
overlap.

**Nothing waits a frame to leave.** Both sessions used to pump their transport
once, at the top of update, and then queue everything they had to say -- so every
input, every snapshot and every heartbeat sat in an outgoing queue until the next
frame's pump. That is 16 ms at 60 fps on the way out and 16 ms on the way back,
added to a path that has nothing to gain from it. Both sessions now pump again at
the end, and the client flushes an input the moment it is handed one, because
input arrives from the screen after update() has already run.

**Snapshots are event-driven, not clock-driven.** The board only changes when a
snake steps, so the host sends when it steps rather than on a timer that lands
wherever it lands relative to the step. `snapshot_hz` survives as a ceiling on
the rate and as a keepalive for the clock, not as the schedule. A fixed timer sat
on average half an interval on a move that had already happened; that half
interval is gone.

What is deliberately *not* here is client-side prediction. The client holds no
simulation — it renders the host's snapshot and forwards intents — which costs
half a step of felt latency and saves a reconciliation system that would have to
stay bug-for-bug identical with the host's rules. That trade is worth revisiting
only once the relay is already close to the players, because until then it would
be shaving a millisecond off a problem measured in hundreds.

### Which host button to use

| | Reaches | Needs |
|---|---|---|
| `HOST ON THIS NETWORK` | Same wifi / LAN | Nothing |
| `HOST ONLINE` | Anywhere | A relay you run, configured once in `netconfig.txt`. Several, in different regions, if you want it to feel good everywhere |
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


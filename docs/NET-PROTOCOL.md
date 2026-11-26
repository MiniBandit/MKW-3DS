# MKW 3DS network protocol (version 5)

How consoles race each other, peer to peer. Any port (3DS, PS Vita) that speaks
this can join the same races. The source of truth is
`include/mkw/net_session.hpp` and `include/mkw/net_code.hpp`.

## Transport

UDP. Every packet starts with the same 16-byte header. All fields are
**little-endian** and **packed**; floats are IEEE 754 single precision.
Addresses inside packets are IPv4 in network byte order (as `sin_addr`).

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 4 | magic | `0x314E4B4D` ("MKN1") |
| 4 | 2 | version | 5 |
| 6 | 1 | type | see below |
| 7 | 1 | sender | the sender's lobby slot (0 = host), 255 before it has one |
| 8 | 4 | session | chosen by the host; 0 until known |
| 12 | 4 | sequence | increases with each packet a sender sends |

Ignore packets with the wrong magic or version, and (except Hello, Lobby,
Refuse, Ping, Seek and Beacon) packets whose session doesn't match.

## Finding each other

Each console asks two public STUN servers (RFC 5389 Binding request from the
game's own socket) for the address the internet sees. That address and port are
its **connection code**, typed on a number pad like a Wii friend code: the 48
bits (address << 16 | port) as 15 decimal digits, then a Luhn check digit (so
any single mistyped digit or swapped neighbours are caught), grouped 4-4-4-4.
See `net_code.hpp`.

**Same network, no code.** A host with an open lobby broadcasts Beacon (its
license name, head count, version and code) to its network's broadcast address
and 255.255.255.255, port 41750, twice a second; a joiner not yet in a lobby
also broadcasts Seek, and a host answers it with Beacon (whatever the build).
The joiner offers a race with its own build, and joining it means sending
Hello to the Beacon's sender address, as if its code had been typed. A Beacon
with another build is only reported ("a race here uses version ...").
A joiner that typed a code and then hears a Beacon carrying that same code
calls the Beacon's sender instead: the host is on its own network, where the
internet address often can't be reached (routers without hairpinning).

Every console prefers port 41750 (a joiner on it hears the hosts' Beacons) and
otherwise picks one itself: a 3DS refuses bind() to port 0. A joiner sends
Hello to the host's code; the host answers with Lobby. The host asks its
router to forward its port (UPnP IGD AddPortMapping, UDP, one-hour lease
renewed every half hour); when the router's own internet address matches the
STUN answer, outside players get in with the code alone. Otherwise, if the
host's router drops the Hello, the host types the joiner's code (Add a Player)
and sends Ping to it every half second, which opens its router.

## Packets

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 1 | Hello | joiner → host, every 20 frames while in the lobby | `u32 id, u16 garage, u8 autoDrift, u8 0, u16 reach, u16 lastRound, char version[12], char build[16], u16 name[10]` |
| 2 | Lobby | host → lobby, every 30 frames | `u16 round, u8 course, u8 engineClass, u8 mirror, u8 count, u8 started, u8 0, char build[16], LobbyPlayer[12]` |
| 3 | Ready | joiner → host, every 6 frames from the end of loading to the end of the countdown | `u16 round, u16 0, u32 time` |
| 4 | Go | host → a joiner, answering each Ready; host → all, 12 times over the half second before the start | `u16 round, u8 set, u8 0, u32 echo, u32 hostTime, i32 startIn` |
| 5 | Frame | all → all, each tick (60 a second) | see below |
| 6 | Bye | all → all, when leaving | none |
| 7 | Refuse | host → a joiner it can't take | `u32 id, char message[44]` |
| 8 | Ping | all → all in the lobby, every 30 frames | `u32 id` |
| 9 | Events | all → all, while the sender has race events not everyone has acknowledged | `u8 count, u8 0, u16 round, u16 oldest, RaceEvent[count]` (count ≤ 8) |
| 11 | Seek | joiner not yet in a lobby → its network's broadcast address and 255.255.255.255, port 41750, every 30 frames | `u32 id, char build[16]` |
| 12 | Beacon | host with a lobby → a Seek's sender, and → broadcast (as Seek) every 30 frames | `u8 count, u8 started, u16 0, char build[16], u16 name[10], char version[12], char code[16]` (code: the host's 16 digits, no hyphens, zeros until known) |

**LobbyPlayer** (34 bytes): `u32 id, u32 address, u16 port, u16 garage, u8 autoDrift, u8 0, u16 name[10]`.
Slot = position in the list; the host is slot 0 with address 0.

**id** is random per console. **build** is the 16-character build fingerprint
(`tools/build_id.py`): consoles only race the same build. **garage** is
`driver * 12 + vehicle choice` in the order of `GarageChoices`
(`include/mkw/garage.hpp`). **course** is the menu course index;
**engineClass** 0 50cc, 1 100cc, 2 150cc. **name** is the player's license
name, UTF-16, up to 10 characters (0 after the end), shown in every lobby. **reach** has a bit for each slot this
console heard directly in the last 3 seconds.

### Frame (124 bytes in total)

| Field | Type | |
|---|---|---|
| tick | u32 | the sender's simulation tick: one more each tick, from any starting point (receivers only use differences) |
| buttons | u16 | 1 accelerate, 2 brake, 4 item, 8 drift |
| stickX, stickY | s8, s8 | steering −7..7 (stickY unused) |
| trick | u8 | 0 none, 1 up, 2 down, 3 left, 4 right |
| flags | u8 | 1 use item, 2 throw backwards, 4 throw forwards |
| reach | u16 | as in Hello |
| pos | float[3] | kart position |
| fullRot, mainRot | float[4] each | quaternions x, y, z, w |
| extVel, intVel | float[3] each | velocities |
| speed | float | the kart's engine speed (KartMove), which drives it between placements |
| item, charges | u8, u8 | the sender's item window: item number (below; 20 none) and how many uses are left |
| heldKind, heldMask | u8, u8 | the items tethered to the sender's kart: item number (20 none) and a bit for each of the up to 3 places that still holds one |
| itemFlags | u8 | 1 roulette running, 2 Thunder Cloud overhead, 4 riding a Bullet Bill |
| reserved | u8 | 0 |
| round | u16 | the race this Frame belongs to (Lobby `round`); ignore other rounds |
| finish | u32 | 0 while racing, else the sender's **finishing key** (below) |
| ack | u8[12] | for each slot, the low byte of the `seq` of the next race event the sender expects from it |

`flags` is informative only since version 3: items are used through race events.

**Item numbers**: 0 Green Shell, 1 Red Shell, 2 Banana, 3 Fake Item Box,
4 Mushroom, 5 Triple Mushrooms, 6 Bob-omb, 7 Spiny Shell, 8 Lightning, 9 Star,
10 Golden Mushroom, 11 Mega Mushroom, 12 Blooper, 13 POW Block, 14 Thunder
Cloud, 15 Bullet Bill, 16 Triple Green Shells, 17 Triple Red Shells, 18 Triple
Bananas, 20 none.

## Who decides what

Every console runs the whole race, but **each console decides only for its own
kart**, and the others play what it reports:

- **Its items.** Its roulette result, item window and tethered items travel in
  every Frame. Nobody rolls a roulette or presses the item button for another
  console's kart.
- **What it throws or drops.** Each object it puts on the course (thrown,
  dropped, knocked out of it by a hit) is a Spawn event with the object's
  position and velocity and an id that is unique for that sender. Every console
  creates the same object and then moves it itself.
- **Whether it is hit.** Only a console's own kart collides with items on that
  console. When it is hit it reports the kart's reaction, the attacker, and what
  became of the object; the others play that reaction on that kart and remove
  (or detonate) the object. An object passing through another console's kart is
  not a hit until that console says so.
- **Its finish.** See "Finishing order".

Objects otherwise move, bounce, break against walls and against each other on
every console separately; those small differences are not corrected.
A console that has gone (see Race flow) has its kart driven as a CPU by each
remaining console separately, and is ranked last.

## Race events (type 9)

Things that must happen exactly once on every console. **RaceEvent** (36 bytes):

| Field | Type | |
|---|---|---|
| seq | u16 | 1, 2, 3, ... for each sender in each race |
| kind | u8 | see below |
| a, b, c, d, e | u8 each | meaning depends on `kind`; `b` is always a lobby slot or 255 |
| id | u16 | an object id of the owner named by the event (0 none) |
| reserved | u16 | 0 |
| v | float[6] | |

Every event is about the **sender's own kart**:

| Kind | Name | Meaning |
|---|---|---|
| 1 | Spawn | The sender put object `id` on the course. `a` item number (0, 1, 2, 3, 4, 6, 7 or 9), `c` flags (1 thrown/dropped backwards, 2 scattered: knocked out of the sender or a course drop, 4 it is already a blast), `d` model scale × 100, `e` frames during which it can't hit the sender, `v` position then velocity (units per frame) |
| 2 | Use | The sender used item `a` that is not an object: 4, 5, 10 (one event per boost), 8, 9, 11, 12, 13. Play it: boost that kart, start its Star or Mega, run the Lightning, Blooper or POW. Each console applies a Lightning strike, Blooper ink or POW squash to its **own** kart only and reports the result (Shrink, Hit). The Bullet Bill and Thunder Cloud are `itemFlags` instead |
| 3 | Hit | The sender's kart started damage reaction `a` (retail KartAction number: 0 spin, 2 launch, 4 knockback, 7 explosion, 11 POW, 13 crushed, ...; 255 none: only the object changed). `b` the attacker (owner of the object), `c` the object's item number, `d` what became of the object: 0 nothing, 1 broken, 2 exploded. `id` names the object: `b`'s Spawn id, or `0xFFF0 + n` for the item tethered in place *n* behind `b`'s kart (then `b`'s console removes it, or detonates a Bob-omb), or 0 for none. `v[0..2]` direction of the push |
| 4 | Shrink | The sender's kart was struck by lightning: `id` frames it stays small, `a` 1 spin-shrink, 2 by its own Thunder Cloud |
| 5 | Take | The sender's kart picked up loose Mushroom or Star (`a`) number `id` of owner `b`: remove it and boost / start the Star on the sender's kart |
| 6 | Item | Object `id` of owner `b` met an item tethered to the sender's kart: `a` 1 it broke, 2 it exploded |
| 7 | Cloud | The sender's kart bumped its Thunder Cloud onto `b`'s kart: `id` frames left on it, `v[0]` its age in frames. `b`'s console puts it over its kart (and shows it through `itemFlags`) |

**Delivery.** A sender keeps its last 32 events. Whenever some console still
racing has not acknowledged all of them (Frame `ack`), it sends every event
from the oldest unacknowledged one on, in `seq` order, 8 to an Events packet
(so usually one packet): at once when a new event is made, otherwise every 4
ticks. `oldest` is the first `seq` the sender still keeps. A receiver takes
events from each sender strictly in `seq` order (one whose `seq` is not the
next it expects is ignored and will come again); if it expects an event before
`oldest`, those were dropped by the sender and it carries on from `oldest`.
The host passes Events on like Frames.

## Finishing order

`finish` in the Frame is 0 until the sender's race is over, then its **key**;
lower keys finish ahead, equal keys by lobby slot:

- crossed the line: its race time in milliseconds (at least 1);
- still racing 30 seconds after the first key it saw (its own or another's):
  `0x80000000 + (16 − raceCompletion) × 1000000`, where raceCompletion is 1.0
  at the start and 4.0 at the end of lap 3, so whoever got further is ahead;
- a console that left or went silent without a key: `0xFFFFFF00 + slot`, given
  to it by each remaining console.

A console shows its results once it has a key for itself and for every console
it can still hear (at most 40 seconds after the first key). Positions,
points and the live rank of finished karts come from the keys alone, never
from where a console saw another kart cross the line.

## Race flow

1. **Lobby.** Joiners send Hello; the host lists them in Lobby. Everyone Pings
   everyone so routers open a direct path between each pair. The host drops a
   player who hasn't sent Hello for 5 seconds.
2. **Start.** The host sends Lobby with `started = 1`. Everyone builds the same
   grid: slot *n* starts in position *n + 1*. On its own screen each console is
   racer 0 and the others follow in slot order.
3. **Ready/Go.** After loading, a joiner sends Ready with `time` = its own
   clock in milliseconds (never 0). The host answers each Ready at once with a
   Go: `echo` = that Ready's `time`, `hostTime` = the host's clock in
   milliseconds as it sends. Once every joiner has sent a Ready (or after 30
   seconds) the host fixes the start half a second ahead and from then on
   every Go has `set = 1` and `startIn` = milliseconds from `hostTime` to the
   start (negative once it has passed); it also sends that Go to everyone
   (`echo = 0`) 12 times, every 3 frames.
   A joiner works out how far the host's clock is ahead of its own from the
   answer with the shortest round trip: `ahead = hostTime - echo - rtt / 2`,
   `rtt = now - echo`. The start on its own clock is
   `hostTime + startIn - ahead`. Every console releases the countdown at that
   moment, not when the Go happens to arrive, and until GO! runs as many ticks
   as the time since the start asks for (59.8261 a second, at most 6 a frame):
   a console whose Go came late, or that dropped frames, catches up during the
   countdown, so the race begins at the same moment on all of them.
4. **Race.** Send a Frame every tick to every other player. Every half second,
   send each player a Probe (type 10: `u32 time, u8 echo, u8[3] 0`); the
   receiver returns it at once with echo 1 (the round trip is only reported, in
   test logs).
   **Showing the other karts.** Keep each player's Frames in order of their
   `tick` and replay them a little in the past: each tick, place the kart
   where its owner had it at `local tick - delay`, on a curve through the
   Frames either side (Frames can arrive late, bunched, out of order or not at
   all). For each Frame note `lead` = the local tick it could first be shown
   at minus its `tick`; `delay` is the value 95% of the last 128 leads are
   under, plus 1, and the delay in use moves towards it by at most 0.1 a tick
   up or 0.05 down. Ticks are never compared between consoles, only their
   differences: consoles count ticks from different starting points. When the
   replay point passes the newest Frame, carry on along the kart's motion (up
   to 12 ticks) and afterwards remove the difference a quarter a tick. Never
   move a kart to a newly received position directly: Frames arrive unevenly,
   and the kart would lurch. Drive each remote kart from its newest inputs
   between placements. Never wait for the network. If the host sees that a
   player can't hear another (its reach bit is clear), it passes that sender's
   Frames, Events and Bye on to it. A console that says Bye or is silent for 5
   seconds is gone: its kart carries on as a CPU (it takes its kart back if it
   speaks again).
5. **Next race.** The host starts a new round (round + 1); joiners Hello again
   with `lastRound` set and ignore started Lobbies for rounds they've raced.

## Not in version 4 yet

- Objects are created alike everywhere but then simulated by each console:
  a shell can break on a wall or against another object on one console and
  fly on for a moment on another. Hits still agree, because only the victim's
  console decides them.
- Item boxes: each console breaks and regrows them from the karts it sees.
- Hits from the course itself (Thwomps, cars, Goombas) are reported by the
  victim like any Hit, but the other consoles may also play their own copy.
- Each console uses its own race clock for its finishing time. The clocks
  start together (Ready/Go above), but a console that freezes for more than a
  tenth of a second during the race loses that time and is not brought back.
- The other karts are shown where they were a moment ago (the replay delay:
  about 0.05 s on a good link, more on a poor one), not where they are now.
- Battle mode.

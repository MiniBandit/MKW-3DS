# MKW 3DS network protocol (version 3)

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
| 4 | 2 | version | 3 |
| 6 | 1 | type | see below |
| 7 | 1 | sender | the sender's lobby slot (0 = host), 255 before it has one |
| 8 | 4 | session | chosen by the host; 0 until known |
| 12 | 4 | sequence | increases with each packet a sender sends |

Ignore packets with the wrong magic or version, and (except Hello, Lobby,
Refuse and Ping) packets whose session doesn't match.

## Finding each other

Each console asks two public STUN servers (RFC 5389 Binding request from the
game's own socket) for the address the internet sees. That address and port are
its **connection code**: 48 bits (address << 16 | port) followed by a 7-bit
check `((v + 1) * 0x9E3779B97F4A7C15) >> 57` (64-bit), 55 bits written as 11
Crockford base-32 characters, grouped 4-4-3. See `net_code.hpp`.

The host prefers port 41750. A joiner sends Hello to the host's code; the host
answers with Lobby. If the host's router drops the Hello, the host types the
joiner's code and sends Ping to it every half second, which opens its router.

## Packets

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 1 | Hello | joiner → host, every 20 frames while in the lobby | `u32 id, u16 garage, u8 autoDrift, u8 0, u16 reach, u16 lastRound, char version[12], char build[16]` |
| 2 | Lobby | host → lobby, every 30 frames | `u16 round, u8 course, u8 engineClass, u8 mirror, u8 count, u8 started, u8 0, char build[16], LobbyPlayer[12]` |
| 3 | Ready | joiner → host, every 10 frames after loading | `u16 round` |
| 4 | Go | host → all, 6 times, and again for a late Ready | `u16 round` |
| 5 | Frame | all → all, each tick (each other tick above 4 players) | see below |
| 6 | Bye | all → all, when leaving | none |
| 7 | Refuse | host → a joiner it can't take | `u32 id, char message[44]` |
| 8 | Ping | all → all in the lobby, every 30 frames | `u32 id` |
| 9 | Events | all → all, while the sender has race events not everyone has acknowledged | `u8 count, u8 0, u16 round, RaceEvent[count]` (count ≤ 8) |

**LobbyPlayer** (14 bytes): `u32 id, u32 address, u16 port, u16 garage, u8 autoDrift, u8 0`.
Slot = position in the list; the host is slot 0 with address 0.

**id** is random per console. **build** is the 16-character build fingerprint
(`tools/build_id.py`): consoles only race the same build. **garage** is
`driver * 12 + vehicle choice` in the order of `GarageChoices`
(`include/mkw/garage.hpp`). **course** is the menu course index;
**engineClass** 0 50cc, 1 100cc, 2 150cc. **reach** has a bit for each slot this
console heard directly in the last 3 seconds.

### Frame (120 bytes in total)

| Field | Type | |
|---|---|---|
| tick | u32 | the sender's simulation tick |
| buttons | u16 | 1 accelerate, 2 brake, 4 item, 8 drift |
| stickX, stickY | s8, s8 | steering −7..7 (stickY unused) |
| trick | u8 | 0 none, 1 up, 2 down, 3 left, 4 right |
| flags | u8 | 1 use item, 2 throw backwards, 4 throw forwards |
| reach | u16 | as in Hello |
| pos | float[3] | kart position |
| fullRot, mainRot | float[4] each | quaternions x, y, z, w |
| extVel, intVel | float[3] each | velocities |
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
racing has not acknowledged all of them (Frame `ack`), it sends an Events
packet with the oldest unacknowledged events in `seq` order, up to 8: at once
when a new event is made, otherwise every 4 ticks. A receiver takes events
from each sender strictly in `seq` order and ignores ones it already has; if
the first event of a packet is beyond the one it expects, the ones between
were dropped by the sender and it carries on from there. The host passes
Events on like Frames.

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
3. **Ready/Go.** After loading, joiners send Ready; the host sends Go once all
   are ready (or after 30 seconds). Everyone releases the countdown on Go.
4. **Race.** Send a Frame every tick to every other player. Drive each remote
   kart from its newest inputs and snap it to the newest state. Never wait for
   the network. If the host sees that a player can't hear another (its reach
   bit is clear), it passes that sender's Frames, Events and Bye on to it.
   A console that says Bye or is silent for 5 seconds is gone: its kart
   carries on as a CPU (it takes its kart back if it speaks again).
5. **Next race.** The host starts a new round (round + 1); joiners Hello again
   with `lastRound` set and ignore started Lobbies for rounds they've raced.

## Not in version 3 yet

- Objects are created alike everywhere but then simulated by each console:
  a shell can break on a wall or against another object on one console and
  fly on for a moment on another. Hits still agree, because only the victim's
  console decides them.
- Item boxes: each console breaks and regrows them from the karts it sees.
- Hits from the course itself (Thwomps, cars, Goombas) are reported by the
  victim like any Hit, but the other consoles may also play their own copy.
- Each console uses its own race clock for its finishing time; clocks start
  on Go, so they differ by the delivery time of that packet.
- Battle mode.

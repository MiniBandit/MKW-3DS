# MKW 3DS network protocol (version 2)

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
| 4 | 2 | version | 2 |
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

**LobbyPlayer** (14 bytes): `u32 id, u32 address, u16 port, u16 garage, u8 autoDrift, u8 0`.
Slot = position in the list; the host is slot 0 with address 0.

**id** is random per console. **build** is the 16-character build fingerprint
(`tools/build_id.py`): consoles only race the same build. **garage** is
`driver * 12 + vehicle choice` in the order of `GarageChoices`
(`include/mkw/garage.hpp`). **course** is the menu course index;
**engineClass** 0 50cc, 1 100cc, 2 150cc. **reach** has a bit for each slot this
console heard directly in the last 3 seconds.

### Frame (96 bytes in total)

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
   bit is clear), it passes that sender's Frames and Bye on to it.
5. **Next race.** The host starts a new round (round + 1); joiners Hello again
   with `lastRound` set and ignore started Lobbies for rounds they've raced.

## Not in version 2 yet

Shared item results and hits, and an agreed finishing order. These will add
packet types.

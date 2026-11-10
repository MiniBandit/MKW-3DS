# MKW 3DS network protocol (version 1)

How two consoles race each other. Any port (3DS, PS Vita) that speaks this can
join the same races. The source of truth is `include/mkw/lan_play.hpp`.

## Transport

UDP. Every packet starts with the same 16-byte header. All fields are
**little-endian**, structures are **packed** (no padding), floats are IEEE 754
single precision. Default port **41750**.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 4 | magic | `0x314E4B4D` ("MKN1") |
| 4 | 2 | version | 1 |
| 6 | 1 | type | see below |
| 7 | 1 | sender | 0 host, 1 joiner, 255 server |
| 8 | 4 | session | chosen by the host; 0 until known |
| 12 | 4 | sequence | increases with each packet a sender sends |

Ignore packets with the wrong magic or version, and (after the handshake)
packets whose session doesn't match.

## Packets

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 1 | Hello | joiner → host, every 20 frames until Welcome | `u16 garage, u8 autoDrift` |
| 2 | Welcome | host → joiner, answering each Hello | `u8 course, u8 engineClass, u8 mirror, u8 hostAutoDrift, u16 hostGarage, u16 joinGarage` |
| 3 | Ready | either, every 10 frames until Go | none |
| 4 | Go | host → joiner, 6 times, and again for any late Ready | none |
| 5 | Frame | both, every simulation tick (60 a second) | see below |
| 6 | Bye | either, when leaving | none |
| 7 | Join | console → server | `char room[8], char version[12], char build[16]` |
| 8 | Room | server → console | `u8 isHost, u8 status, char message[46]` |

**garage** is the game's vehicle-and-driver index: `driver * 12 + vehicle choice`,
in the order of `GarageChoices` (`include/mkw/garage.hpp`). **course** is the
game's course index (the menu order). **engineClass**: 0 50cc, 1 100cc, 2 150cc.

### Frame (96 bytes in total)

| Field | Type | |
|---|---|---|
| tick | u32 | the sender's simulation tick |
| buttons | u16 | Kinoko buttons: 1 accelerate, 2 brake, 4 item, 8 drift |
| stickX, stickY | s8, s8 | steering, −7..7 (stickY unused) |
| trick | u8 | 0 none, 1 up, 2 down, 3 left, 4 right |
| flags | u8 | 1 use item, 2 throw backwards, 4 throw forwards |
| reserved | u16 | 0 |
| pos | float[3] | kart position |
| fullRot | float[4] | kart rotation quaternion x, y, z, w |
| mainRot | float[4] | main rotation quaternion x, y, z, w |
| extVel | float[3] | external velocity |
| intVel | float[3] | internal velocity |

Positions and velocities are Kinoko's (Mario Kart Wii's) world units.

## Race flow

1. **Handshake.** The joiner sends Hello until a Welcome arrives. The host's
   course, engine class and mirror setting are used. Each side now knows the
   other's garage choice and drift setting.
2. **Load.** Both build a two-player race: the local player is racer 0 and the
   other console's player racer 1. The host starts first on the grid, the joiner
   second.
3. **Start.** When loaded, each side sends Ready. The host, once it has seen
   the joiner's Ready, sends Go; both release the countdown on Go. No course
   preview is shown.
4. **Race.** Each tick, send a Frame with your racer's inputs and state. Drive
   the other racer from its newest inputs, and when a newer Frame arrives, set
   its state from the Frame (snapping). Never wait for the network: hold the
   last inputs when nothing new has arrived. UDP may drop or reorder packets,
   so keep only the newest tick.

## Online (through the relay server)

Consoles send everything to the server (`tools/server/mkw3ds_server.py`) instead
of to each other. A console sends **Join** with the room code, its version
string and its build fingerprint (`tools/build_id.py`), repeating every 30
frames until a **Room** reply arrives, and every 2 seconds while waiting alone
as host (this keeps home routers' routes open). The first console in a room is
the host. The server then forwards every other packet to the other player in the
room, unchanged, and the race flow above follows.

Room **status**: 0 OK, 1 room full, 2 build not allowed, 3 bad request; the
message says why, for display.

## Not in version 1 yet

More than two players, CPU racers, shared item results and hits, and agreed
finishing order. These will need new packet types; a version 2 header will mark
them.

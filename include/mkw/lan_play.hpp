#pragma once
#include <cstddef>
#include <cstdint>

namespace mkw::lan {
// Two-player racing over a shared Wi-Fi network (UDP, no server). The packet
// layout is documented in docs/LAN-PROTOCOL.md so other ports (the PS Vita
// port) can speak it too: little-endian, packed, no pointers or padding.
constexpr uint32_t Magic=0x314E4B4D; // "MKN1"
constexpr uint16_t Version=1;
constexpr uint16_t DefaultPort=41750;
enum class Type:uint8_t {Hello=1,Welcome=2,Ready=3,Go=4,Frame=5,Bye=6,Join=7,Room=8};

#pragma pack(push,1)
struct Header {uint32_t magic;uint16_t version;Type type;uint8_t sender;uint32_t session,sequence;};
// Joiner -> host until a Welcome arrives: the joiner's garage choice.
struct Hello {Header h;uint16_t garage;uint8_t autoDrift;};
// Host -> joiner: the race both consoles load. Garage choices are the
// game's GarageChoices indices (driver * 12 + vehicle choice).
struct Welcome {Header h;uint8_t course,engineClass,mirror,hostAutoDrift;uint16_t hostGarage,joinGarage;};
// Either side, repeated until Go: this console has loaded the race.
struct Ready {Header h;};
// Host -> joiner, repeated a few times: start the countdown now.
struct Go {Header h;};
// Every simulation tick: this player's inputs and kart state on that tick.
// flags: 1 use item, 2 throw backwards, 4 throw forwards.
struct Frame {Header h;uint32_t tick;uint16_t buttons;int8_t stickX,stickY;uint8_t trick,flags;uint16_t reserved;
              float pos[3],fullRot[4],mainRot[4],extVel[3],intVel[3];};
// Online (through the relay server, tools/server): console -> server, repeated
// until answered and every few seconds while waiting, keeping the route open.
// The build fingerprint is tools/build_id.py's; the server checks it.
struct Join {Header h;char room[8];char version[12];char build[16];};
// Server -> console: which side this console plays, or why it can't join.
enum class RoomStatus:uint8_t {Ok=0,Full=1,UnknownBuild=2,BadRequest=3};
struct Room {Header h;uint8_t host;RoomStatus status;char message[46];};
#pragma pack(pop)
static_assert(sizeof(Join)==52&&sizeof(Room)==64&&sizeof(Header)==16&&sizeof(Hello)==19&&sizeof(Welcome)==24&&sizeof(Frame)==96);

enum class Role:uint8_t {None,Host,Join,Online};

class Session {
public:
    // Reads sdmc:/3ds/mkw-3ds/lan.txt: "host [port]", "join <ip> [port] [localport]"
    // or "online <server> [room] [port]" (internet play through the relay server).
    // False when the file is absent: the game plays offline as usual.
    bool configure();
    // Main menu Online: "host [port]" (the built-in server, or sd:/3ds/mkw-3ds/online.txt).
    bool configureOnline(const char* server);
    bool active() const{return role_!=Role::None;}
    bool online() const{return relay_;}
    // Online: the room both players type in. Empty until set (the game asks).
    const char* room() const{return room_;}
    void setRoom(const char* room);
    Role role() const{return role_;}
    // Opens the socket. False (with reason()) if the network is unavailable.
    bool open();
    void close();
    // Blocking handshake before a race, drawn by the caller between polls.
    // The host's course/engine class win; returns true once both sides agree.
    // Call repeatedly (each call polls once); done() reports success.
    void handshakeStep(unsigned localGarage,bool autoDrift,unsigned course,unsigned engineClass,bool mirror);
    bool handshakeDone() const{return welcomed_;}
    // Online: the server refused this console (failed() and reason() say why).
    bool failed() const{return failed_;}
    unsigned raceCourse() const{return course_;}
    unsigned raceEngineClass() const{return engineClass_;}
    bool raceMirror() const{return mirror_;}
    unsigned peerGarage() const{return peerGarage_;}
    bool peerAutoDrift() const{return peerAutoDrift_;}
    // After the race has loaded: keep calling until it returns true, then start.
    bool readyStep();
    // Every simulation tick.
    void sendFrame(uint32_t tick,uint16_t buttons,int8_t stickX,uint8_t trick,uint8_t flags,const float pos[3],const float fullRot[4],
                   const float mainRot[4],const float extVel[3],const float intVel[3]);
    void poll();
    bool peerFrame(Frame& out) const{if(!hasFrame_)return false;out=latest_;return true;}
    uint32_t framesSinceHeard() const{return silentPolls_;}
    // The other player said goodbye (quit or left the race).
    bool peerLeft() const{return peerLeft_;}
    const char* reason() const{return reason_;}
    // Human-readable steps for the connection screen, oldest first.
    bool takeEvent(char* out,size_t size);
    void resetRace();
private:
    void send(const void* data,size_t size);
    Header header(Type type);
    void receive(const uint8_t* data,size_t size,uint32_t address,uint16_t port);
    void note(const char* format,...);
    void sendJoin();
    char events_[8][64]{};unsigned eventHead_=0,eventCount_=0,helloCount_=0;bool firstHello_=false;
    Role role_=Role::None;
    int socket_=-1;uint32_t* socBuffer_=nullptr;
    uint32_t peerAddress_=0;uint16_t peerPort_=DefaultPort,localPort_=DefaultPort;
    uint32_t session_=0,sequence_=0,lastPeerSequence_=0;
    bool peerKnown_=false,welcomed_=false,peerReady_=false,goSeen_=false,hasFrame_=false;
    unsigned course_=0,engineClass_=2,peerGarage_=0,hostGarage_=0,goRepeats_=0,pollCount_=0;bool mirror_=false,autoDrift_=false,peerAutoDrift_=false;
    uint32_t silentPolls_=0;
    bool relay_=false,roomJoined_=false,failed_=false,peerLeft_=false;char room_[9]{};char server_[64]{};unsigned joinCount_=0;char failure_[48]{};
    Frame latest_{};
    const char* reason_="";
};
Session& session();
}

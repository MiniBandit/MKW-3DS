// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mkw/net_code.hpp>
#include <mkw/net_upnp.hpp>

namespace mkw::net {
// Online and LAN racing, peer to peer: every console sends its kart straight
// to every other console (UDP). No server of ours is involved: each console
// learns its internet address from a public STUN server and shows it as a
// connection code (net_code.hpp); players swap codes to find each other.
// docs/NET-PROTOCOL.md describes the packets so other ports can join.
constexpr uint32_t Magic=0x314E4B4D; // "MKN1"
constexpr uint16_t Version=5;
constexpr uint16_t DefaultPort=41750;
constexpr unsigned MaxPlayers=12;
// A player's license name: up to 10 UTF-16 characters, 0 after the end.
constexpr unsigned NameLength=10;
enum class Type:uint8_t {Hello=1,Lobby=2,Ready=3,Go=4,Frame=5,Bye=6,Refuse=7,Ping=8,Events=9,Probe=10,Seek=11,Beacon=12};

#pragma pack(push,1)
// sender: the player's slot in the lobby (0 is the host), 255 before it has one.
struct Header {uint32_t magic;uint16_t version;Type type;uint8_t sender;uint32_t session,sequence;};
// Joiner -> host, repeated: wants a place in the next race. id is random per
// console. reach: the slots this console hears directly (bit per slot).
// lastRound: the last race this console took part in (0 none). name: the
// player's license name, shown in everyone's lobby.
struct Hello {Header h;uint32_t id;uint16_t garage;uint8_t autoDrift,reserved;uint16_t reach,lastRound;char version[12];char build[16];uint16_t name[NameLength];};
struct LobbyPlayer {uint32_t id,address;uint16_t port,garage;uint8_t autoDrift,reserved;uint16_t name[NameLength];};
// Host -> everyone in the lobby, repeated: the next race and who is in it.
// build: the host's build fingerprint; a joiner only races the same build.
struct Lobby {Header h;uint16_t round;uint8_t course,engineClass,mirror,count,started,reserved;char build[16];LobbyPlayer players[MaxPlayers];};
// Either way, in the lobby: punches a route through home routers and shows
// which consoles can hear each other.
struct Ping {Header h;uint32_t id;};
// Joiner -> host, repeated from the end of its loading to the end of the countdown.
// time: the joiner's clock (milliseconds) as it sends.
struct Ready {Header h;uint16_t round,reserved;uint32_t time;};
// Host -> a joiner, answering each Ready (echo: that Ready's time), and -> all
// when the start is set (echo 0). hostTime: the host's clock (milliseconds) as it
// sends. set 1: the race is released startIn milliseconds after hostTime (in the
// past once the race is under way); set 0: still waiting for someone to load.
// From echo and hostTime a joiner learns how far its clock is from the host's, so
// every console releases the countdown at the same moment however late its Go came.
struct Go {Header h;uint16_t round;uint8_t set,reserved;uint32_t echo,hostTime;int32_t startIn;};
// Every simulation tick. reach as in Hello. speed: the kart's engine speed
// (KartMove), which drives it between Frames.
// flags: 1 use item, 2 throw backwards, 4 throw forwards.
// item..itemFlags: the sender's own item window and tethered items (ItemKind
// numbers, 20 none; heldMask: a bit per tethered item; itemFlags: 1 roulette
// running, 2 Thunder Cloud overhead, 4 Bullet Bill). round: the race this
// belongs to (packets of another race are ignored). finish: 0 while racing,
// else the sender's finishing key (see FinishKey). ack[slot]: the low byte of
// the next race event the sender expects from that slot.
struct Frame {Header h;uint32_t tick;uint16_t buttons;int8_t stickX,stickY;uint8_t trick,flags;uint16_t reach;
              float pos[3],fullRot[4],mainRot[4],extVel[3],intVel[3],speed;
              uint8_t item,charges,heldKind,heldMask,itemFlags,reserved;uint16_t round;uint32_t finish;uint8_t ack[MaxPlayers];};
// Something that happened to the sender's own kart or items, which every
// console must play once: see docs/NET-PROTOCOL.md "Race events". seq counts
// from 1 for each sender in each race. Player numbers are lobby slots, 255 none.
enum class EventKind:uint8_t {Spawn=1,Use=2,Hit=3,Shrink=4,Take=5,Item=6,Cloud=7};
struct RaceEvent {uint16_t seq;uint8_t kind,a,b,c,d,e;uint16_t id,reserved;float v[6];};
// All -> all, when the sender has race events some console has not acknowledged:
// those events, in order. oldest: the first seq the sender still keeps (a
// receiver expecting an earlier one has lost it for good and moves on).
constexpr unsigned EventsPerPacket=8;
struct Events {Header h;uint8_t count,reserved;uint16_t round,oldest;RaceEvent events[EventsPerPacket];};
struct Bye {Header h;};
// Same Wi-Fi, no code: a joiner broadcasts Seek to DefaultPort on its network;
// a host with an open lobby answers with Beacon (its license name and head count),
// and also broadcasts it twice a second. version: the host's game version, shown
// to a joiner whose build differs. code: the host's code (digits, empty until
// known): a joiner that typed it on the same Wi-Fi calls the host directly.
struct Seek {Header h;uint32_t id;char build[16];};
struct Beacon {Header h;uint8_t count,started;uint16_t reserved;char build[16];uint16_t name[NameLength];char version[12];char code[16];};
// Host -> a console it can't take, with the reason for display.
struct Refuse {Header h;uint32_t id;char message[44];};
#pragma pack(pop)
static_assert(sizeof(Seek)==36&&sizeof(Beacon)==84);
static_assert(sizeof(Header)==16&&sizeof(Hello)==76&&sizeof(LobbyPlayer)==34&&sizeof(Lobby)==448&&sizeof(Frame)==124&&sizeof(Refuse)==64&&sizeof(RaceEvent)==36&&sizeof(Events)==310&&sizeof(Ready)==24&&sizeof(Go)==32);

// Finishing keys: lower finishes ahead. A finisher's key is its race time in
// milliseconds at the line. A console that has not finished 30 seconds after
// the first finisher reports DidNotFinish plus how far it was from the goal;
// a console that vanished without reporting ranks last, by slot.
namespace FinishKey {
constexpr uint32_t DidNotFinish=0x80000000u,Gone=0xFFFFFF00u;
constexpr unsigned WaitFrames=30*60;
inline uint32_t time(uint32_t milliseconds){return milliseconds?(milliseconds<DidNotFinish?milliseconds:DidNotFinish-1):1;}
inline uint32_t unfinished(float raceCompletion){
    const float left=16.f-raceCompletion;
    return DidNotFinish+uint32_t((left<0?0:left>16.f?16.f:left)*1000000.f);
}
inline uint32_t gone(unsigned slot){return Gone|slot;}
}
// The part of a Frame about the sender's items and finish.
// Round-trip timing in a race, every half second to each player: echo 0 asks, the
// receiver sends it straight back with echo 1 and the same time (milliseconds, the
// asker's clock). Only reported (test logs): the other karts are placed from how
// their Frames actually arrive (main.cpp, "Remote karts").
#pragma pack(push,1)
struct Probe {Header h;uint32_t time;uint8_t echo,reserved[3];};
#pragma pack(pop)
static_assert(sizeof(Probe)==24);
struct FrameState {uint8_t item=20,charges=0,heldKind=20,heldMask=0,itemFlags=0;uint32_t finish=0;};

enum class Mode:uint8_t {None,Host,Join};

class Session {
public:
    // lan.txt on the SD card (testing, same Wi-Fi): "host [port]" or "join <ip> [port]".
    // False when absent.
    bool configureFile();
    bool fromFile() const{return fromFile_;}
    // Main menu Online: host a race or join one by code.
    void configureOnline(Mode mode);
    Mode mode() const{return mode_;}
    bool active() const{return mode_!=Mode::None;}
    bool host() const{return mode_==Mode::Host;}
    // Opens the socket and asks a STUN server for this console's address.
    bool open();
    void close();
    // This console's code once known (empty before), for the other players.
    const char* code() const{return code_;}
    // Joiner: the host's code. Host: a joining player's code, so the host's
    // router lets that player in. False when the code is mistyped.
    bool addCode(const char* text);
    bool hostKnown() const{return hostKnown_;}
    // Joiner, before it has a host: a race on this Wi-Fi heard in the last
    // few seconds (its host's name and head count), and joining it.
    bool nearby(unsigned& count,const uint16_t*& name) const{count=nearby_.count;name=nearby_.name;return nearby_.address&&nearby_.age<NearbyFor;}
    bool joinNearby();
    // Joiner: a race heard on this Wi-Fi that runs another build of the game
    // (its version text, for the code page). False when none.
    bool otherVersionNearby(const char*& version) const{version=otherVersion_;return otherAge_<NearbyFor;}
    // Host: the router passes the game's port (UPnP), so players over the
    // internet get in with the host's code alone.
    bool portOpen() const{return portOpen_;}
    // Each frame on the connection screen: the local player's choices
    // (the host's course, class and mirror are the race's).
    void lobbyStep(unsigned garage,bool autoDrift,unsigned course,unsigned engineClass,bool mirror);
    // Host: start the race with the players in the lobby.
    bool canStart() const;
    void start();
    bool started() const{return started_;}
    bool failed() const{return failed_;}
    const char* reason() const{return reason_;}
    // The agreed race, valid once started().
    unsigned course() const{return course_;}
    unsigned engineClass() const{return engineClass_;}
    bool mirror() const{return mirror_;}
    unsigned count() const{return count_;}
    unsigned slot() const{return slot_;}
    unsigned garage(unsigned slot) const{return players_[slot].garage;}
    bool autoDrift(unsigned slot) const{return players_[slot].autoDrift;}
    // The lobby's player names (this console's own from setName).
    const uint16_t* name(unsigned slot) const{return players_[slot].name;}
    void setName(const uint16_t* name){for(unsigned i=0;i<NameLength;++i)name_[i]=name?name[i]:0;}
    // The host's console can be heard (joiner) / the lobby is open.
    bool inLobby() const{return mode_==Mode::Host?socket_>=0:slot_!=255;}
    bool opened() const{return socket_>=0;}
    // Lobby: players there (host included) and how many this console hears.
    unsigned lobbyCount() const{return count_;}
    unsigned heardCount() const;
    // After the race loads: call each frame until true (the start is agreed), then
    // release the countdown once sinceStart() reaches 0.
    bool readyStep();
    // Milliseconds since the moment every console releases its countdown (negative
    // before it), on this console's clock. Valid once readyStep() is true; a joiner's
    // answer sharpens while startStep() runs.
    int32_t sinceStart() const;
    // Each frame from the release to the end of the countdown: keeps the start agreed
    // (the host repeats it; a joiner keeps timing its clock against the host's).
    void startStep();
    void poll();
    void sendFrame(uint32_t tick,uint16_t buttons,int8_t stickX,uint8_t trick,uint8_t flags,const float pos[3],const float fullRot[4],
                   const float mainRot[4],const float extVel[3],const float intVel[3],float speed,const FrameState& state);
    // Race events: each reaches every other console once, in the order sent
    // (repeated until every console still racing has acknowledged it).
    void sendEvent(RaceEvent event);
    // Each tick, after the tick's events are queued.
    void eventStep();
    bool takeRaceEvent(unsigned& slot,RaceEvent& out);
    bool frame(unsigned slot,Frame& out) const{if(slot>=MaxPlayers||!hasFrame_[slot])return false;out=latest_[slot];return true;}
    // Every Frame received from this slot since the last call, oldest first (up to
    // FrameQueue; the oldest give way when more arrive at once).
    bool takeFrame(unsigned slot,Frame& out);
    uint32_t silence(unsigned slot) const{return silent_[slot];}
    // How old this slot's Frames are when they arrive, in simulation ticks (half the
    // measured round trip; 3 until measured).
    float delayTicks(unsigned slot) const{return slot<MaxPlayers&&rttMs_[slot]>0?rttMs_[slot]*.5f*60.f/1000.f:3.f;}
    // Half the quickest recent round trip, in ticks: the delay of a Frame that met no queue.
    float quickestDelayTicks(unsigned slot) const{return slot<MaxPlayers&&minRttMs_[slot]>0?minRttMs_[slot]*.5f*60.f/1000.f:delayTicks(slot);}
    bool left(unsigned slot) const{return left_[slot];}
    // Joiner: the host said goodbye (and has not sent a lobby since).
    bool hostGone() const{return hostGone_;}
    // Human-readable steps for the connection screen, oldest first.
    bool takeEvent(char* out,size_t size);
    // Before each race: keeps the players, clears the last race.
    void newRound();
    // Test builds: qa-net-beacon.txt "ip port" names a listener on the test computer;
    // each line sent there is stamped with that computer's clock, the one time base
    // two consoles (or emulators) share. Does nothing otherwise.
    void qaBeacon(const char* text);
private:
    struct Player {uint32_t id=0,address=0;uint16_t port=0,garage=0;bool autoDrift=false;uint16_t name[NameLength]{};};
    uint16_t name_[NameLength]{};
    void note(const char* format,...);
    Header header(Type type);
    void sendTo(uint32_t address,uint16_t port,const void* data,size_t size);
    void sendAll(const void* data,size_t size);
    void receive(const uint8_t* data,size_t size,uint32_t address,uint16_t port);
    void receiveStun(const uint8_t* data,size_t size);
    void stunStep();
    void sendLobby();
    int find(uint32_t address,uint16_t port) const;
    uint16_t reachMask() const;
    struct Nearby {uint32_t address=0;uint16_t port=0;uint8_t count=0;uint16_t name[NameLength]{};unsigned age=0;};
    static constexpr unsigned NearbyFor=180;
    Nearby nearby_{};uint32_t broadcast_=0;bool nearHost_=false,hostGone_=false;unsigned waitPolls_=0,byeCount_=0;
    void seekStep();
    void beaconTo(uint32_t address,uint16_t port);
    void portStep();
    // The digits of the code this joiner typed (to spot its host on the same Wi-Fi).
    char typedCode_[CodeDigits+1]{};
    char otherVersion_[12]{};unsigned otherAge_=~0u;
    PortOpener opener_{};bool portOpen_=false,portChecked_=false;uint64_t portOpenedAt_=0;
    Mode mode_=Mode::None;bool fromFile_=false,lanOnly_=false;
    int socket_=-1;uint32_t* socBuffer_=nullptr;uint16_t localPort_=0;
    uint32_t id_=0,session_=0,sequence_=0,polls_=0;
    // STUN: this console's public address, from two servers to spot routers that change ports.
    uint32_t stunAddress_[2]{};uint16_t stunPort_[2]{};uint32_t mappedAddress_[2]{};uint16_t mappedPort_[2]{};uint8_t stunTx_[2][12]{};
    unsigned stunTries_=0;bool stunDone_=false;char code_[CodeTextSize]{};
    uint32_t hostAddress_=0;uint16_t hostPort_=0;bool hostKnown_=false;
    std::array<uint32_t,16> invited_{};std::array<uint16_t,16> invitedPort_{};unsigned invitedCount_=0;
    std::array<Player,MaxPlayers> players_{};unsigned count_=0,slot_=255;
    std::array<uint32_t,MaxPlayers> direct_{};   // polls since this slot was heard directly
    std::array<uint16_t,MaxPlayers> reach_{};    // what each slot reports hearing directly
    std::array<bool,MaxPlayers> ready_{},inRound_{};
    uint16_t round_=0,lastRound_=0;unsigned course_=0,engineClass_=2,goRepeats_=0,readyPolls_=0,syncCalls_=0;bool mirror_=false;
    // The agreed start: on the host's clock, and how far that clock is ahead of this
    // console's (from the quickest Ready/Go round trip, bestRtt_).
    uint32_t startAt_=0;int32_t clockAhead_=0,bestRtt_=0x7fffffff;
    bool started_=false,goSeen_=false,failed_=false,announcedWait_=false;unsigned heardBefore_=0;
    std::array<Frame,MaxPlayers> latest_{};std::array<bool,MaxPlayers> hasFrame_{},left_{};std::array<float,MaxPlayers> rttMs_{},minRttMs_{};unsigned probeTick_=0;
    std::array<uint32_t,MaxPlayers> silent_{};
    static constexpr unsigned FrameQueue=32;
    std::array<std::array<Frame,FrameQueue>,MaxPlayers> queue_{};std::array<uint8_t,MaxPlayers> queueHead_{},queueCount_{};
    // Race events: mine not yet acknowledged by all (a ring by seq), what each
    // console has acknowledged, and what has arrived for the game to play.
    static constexpr unsigned OutboxSize=32,InboxSize=64;
    struct Incoming {uint8_t slot;RaceEvent event;};
    void resetEvents();
    std::array<RaceEvent,OutboxSize> outbox_{};std::array<Incoming,InboxSize> inbox_{};
    std::array<uint16_t,MaxPlayers> acked_{},expected_{};
    uint16_t nextSeq_=1;unsigned inboxHead_=0,inboxCount_=0,eventQuiet_=0;bool eventNew_=false;
    char events_[8][64]{};unsigned eventHead_=0,eventCount_=0;char failure_[48]{};
    const char* reason_="";
};
Session& session();
}

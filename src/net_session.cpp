// SPDX-License-Identifier: LicenseRef-MKW3DS-Online
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-ONLINE-LICENSE.txt.
#include <mkw/net_session.hpp>
#include <mkw/net_code.hpp>
#include <3ds.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "content_version.hpp" // build/: MKW_VERSION, MKW_BUILD_ID

namespace mkw::net {
namespace {
constexpr size_t SocBufferSize=0x20000; // libctru's minimum page-aligned pool; small for Old 3DS
// Polls are frames (60 a second).
constexpr unsigned HelloEvery=20,LobbyEvery=30,PingEvery=30,ReadyEvery=10,GoRepeats=6,StunEvery=30,StunTries=8;
constexpr unsigned HeardWithin=180,LobbyTimeout=300,ReadyTimeout=30*60,AskHostAfter=600;
constexpr unsigned LostAfter=300,EventsEvery=4;
constexpr uint32_t StunCookie=0x2112A442;
// Public STUN servers: they only tell a console the address the internet sees.
// sd:/3ds/mkw-3ds/stun.txt ("host port" per line) replaces them, for testing.
constexpr const char* StunHosts[2]={"stun.cloudflare.com","global.stun.twilio.com"};
constexpr uint16_t StunPorts[2]={3478,3478};

const char* dotted(uint32_t networkAddress,char* out){in_addr a{};a.s_addr=networkAddress;std::snprintf(out,16,"%s",inet_ntoa(a));return out;}
uint32_t resolve(const char* name){
    in_addr a{};if(inet_aton(name,&a))return a.s_addr;
    if(hostent* h=gethostbyname(name);h&&h->h_addrtype==AF_INET&&h->h_addr_list[0]){uint32_t v;std::memcpy(&v,h->h_addr_list[0],4);return v;}
    return 0;
}
bool sameBuild(const char build[16]){
    char mine[16]{};std::memcpy(mine,MKW_BUILD_ID,std::min(sizeof mine,std::strlen(MKW_BUILD_ID)));
    return !std::memcmp(mine,build,sizeof mine);
}
void copyBuild(char build[16]){std::memset(build,0,16);std::memcpy(build,MKW_BUILD_ID,std::min<size_t>(16,std::strlen(MKW_BUILD_ID)));}
}

Session& session(){static Session value;return value;}

void Session::note(const char* format,...){
    char* line=events_[(eventHead_+eventCount_)%8];
    va_list args;va_start(args,format);std::vsnprintf(line,64,format,args);va_end(args);
    if(eventCount_<8)++eventCount_;else eventHead_=(eventHead_+1)%8;
}
bool Session::takeEvent(char* out,size_t size){
    if(!eventCount_)return false;
    std::snprintf(out,size,"%s",events_[eventHead_]);eventHead_=(eventHead_+1)%8;--eventCount_;return true;
}

bool Session::configureFile(){
    FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/lan.txt","r");if(!f)return false;
    char mode[16]{},ip[64]{};unsigned port=DefaultPort;
    const int fields=std::fscanf(f,"%15s %63s %u",mode,ip,&port);std::fclose(f);
    if(fields>=1&&!std::strcmp(mode,"host")){
        mode_=Mode::Host;lanOnly_=fromFile_=true;localPort_=uint16_t(fields>=2?std::strtoul(ip,nullptr,10):DefaultPort);
        if(!localPort_)localPort_=DefaultPort;
        return true;
    }
    if(fields>=2&&!std::strcmp(mode,"join")){
        in_addr a{};if(!inet_aton(ip,&a)){reason_="lan.txt: bad address";return false;}
        mode_=Mode::Join;lanOnly_=fromFile_=true;hostAddress_=a.s_addr;hostPort_=uint16_t(fields>=3?port:DefaultPort);hostKnown_=true;localPort_=0;
        return true;
    }
    reason_="lan.txt: expected 'host' or 'join <ip>'";return false;
}

void Session::configureOnline(Mode mode){
    if(fromFile_)return;
    if(mode_!=mode&&socket_>=0)close();
    mode_=mode;lanOnly_=false;
    // The host keeps the usual port when it can: home routers often keep it too.
    localPort_=mode==Mode::Host?DefaultPort:0;
}

bool Session::open(){
    if(socket_>=0)return true;
    failed_=false;
    socBuffer_=static_cast<uint32_t*>(memalign(0x1000,SocBufferSize));
    if(!socBuffer_){reason_="no memory for the network";return false;}
    if(R_FAILED(socInit(socBuffer_,SocBufferSize))){std::free(socBuffer_);socBuffer_=nullptr;reason_="Wi-Fi is not available";return false;}
    socket_=::socket(AF_INET,SOCK_DGRAM,0);
    if(socket_<0){close();reason_="could not open a network socket";return false;}
    fcntl(socket_,F_SETFL,fcntl(socket_,F_GETFL,0)|O_NONBLOCK);
    auto bindTo=[&](uint16_t port){sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(port);local.sin_addr.s_addr=INADDR_ANY;
        return bind(socket_,reinterpret_cast<sockaddr*>(&local),sizeof local)==0;};
    if(!bindTo(localPort_)&&(lanOnly_||!bindTo(0))){close();reason_="the network port is in use";return false;}
    sockaddr_in bound{};socklen_t length=sizeof bound;
    if(getsockname(socket_,reinterpret_cast<sockaddr*>(&bound),&length)==0&&bound.sin_port)localPort_=ntohs(bound.sin_port);
    id_=(uint32_t(svcGetSystemTick()*2654435761u)^uint32_t(gethostid()))|1u;slot_=mode_==Mode::Host?0:255;
    session_=mode_==Mode::Host?(uint32_t(svcGetSystemTick())|1u):0;
    char ip[16];const uint32_t local=uint32_t(gethostid());
    note("> Wi-Fi ready. This console: %s:%u",dotted(local,ip),unsigned(localPort_));
    stunDone_=false;stunTries_=0;code_[0]=0;
    if(lanOnly_){
        encodeCode(ntohl(local),localPort_,code_);stunDone_=true;
        if(mode_==Mode::Join)note("> Calling the host at %s:%u",dotted(hostAddress_,ip),unsigned(hostPort_));
        return true;
    }
    // Where to ask for this console's internet address.
    unsigned servers=0;
    if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/stun.txt","r")){
        char name[64];unsigned port;
        while(servers<2&&std::fscanf(f,"%63s %u",name,&port)==2)if(uint32_t a=resolve(name)){stunAddress_[servers]=a;stunPort_[servers]=uint16_t(port);++servers;}
        std::fclose(f);
    }
    for(unsigned i=0;servers<2&&i<2;++i)if(uint32_t a=resolve(StunHosts[i])){stunAddress_[servers]=a;stunPort_[servers]=StunPorts[i];++servers;}
    for(unsigned i=servers;i<2;++i)stunAddress_[i]=0;
    mappedAddress_[0]=mappedAddress_[1]=0;mappedPort_[0]=mappedPort_[1]=0;
    if(!servers){
        note("> Can't reach the internet. Only players on this");note("  Wi-Fi can join.");
        encodeCode(ntohl(local),localPort_,code_);stunDone_=true;
    }else note("> Finding this console's internet address...");
    return true;
}

void Session::close(){
    if(socket_>=0&&count_>1){Bye bye{header(Type::Bye)};sendAll(&bye,sizeof bye);}
    if(socket_>=0){::close(socket_);socket_=-1;}
    if(socBuffer_){socExit();std::free(socBuffer_);socBuffer_=nullptr;}
    count_=0;slot_=255;started_=false;
}

Header Session::header(Type type){return Header{Magic,Version,type,uint8_t(slot_),session_,++sequence_};}

void Session::sendTo(uint32_t address,uint16_t port,const void* data,size_t size){
    if(socket_<0||!address||!port)return;
    sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(port);to.sin_addr.s_addr=address;
    sendto(socket_,data,size,0,reinterpret_cast<sockaddr*>(&to),sizeof to);
}
void Session::sendAll(const void* data,size_t size){
    for(unsigned s=0;s<count_;++s)if(s!=slot_)sendTo(players_[s].address,players_[s].port,data,size);
}
int Session::find(uint32_t address,uint16_t port) const{
    for(unsigned s=0;s<count_;++s)if(s!=slot_&&players_[s].address==address&&players_[s].port==port)return int(s);
    return -1;
}
uint16_t Session::reachMask() const{
    uint16_t mask=slot_<MaxPlayers?uint16_t(1u<<slot_):0;
    for(unsigned s=0;s<count_;++s)if(s!=slot_&&direct_[s]<HeardWithin)mask|=uint16_t(1u<<s);
    return mask;
}
unsigned Session::heardCount() const{
    unsigned n=0;for(unsigned s=0;s<count_;++s)if(s!=slot_&&direct_[s]<HeardWithin)++n;return n;
}

bool Session::addCode(const char* text){
    uint32_t address;uint16_t port;
    if(!decodeCode(text,address,port))return false;
    address=htonl(address);
    if(mode_==Mode::Join){hostAddress_=address;hostPort_=port;hostKnown_=true;char ip[16];note("> Calling the host at %s:%u",dotted(address,ip),unsigned(port));}
    else if(invitedCount_<invited_.size()){invited_[invitedCount_]=address;invitedPort_[invitedCount_++]=port;note("> Opening the way for %s",text);}
    return true;
}

void Session::stunStep(){
    if(stunDone_||socket_<0)return;
    const bool any=mappedAddress_[0]||mappedAddress_[1];
    // Both servers answered, or one did and the other is slow.
    if((mappedAddress_[0]||!stunAddress_[0])&&(mappedAddress_[1]||!stunAddress_[1])&&any)stunDone_=true;
    else if(any&&stunTries_>=3)stunDone_=true;
    else if(stunTries_>=StunTries){
        stunDone_=true;const uint32_t local=uint32_t(gethostid());encodeCode(ntohl(local),localPort_,code_);
        note("> No answer from the internet. Only players on");note("  this Wi-Fi can join.");return;
    }
    if(stunDone_){
        const unsigned i=mappedAddress_[0]?0:1;
        encodeCode(ntohl(mappedAddress_[i]),mappedPort_[i],code_);
        char ip[16];note("> Internet address: %s:%u",dotted(mappedAddress_[i],ip),unsigned(mappedPort_[i]));
        if(mappedAddress_[0]&&mappedAddress_[1]&&mappedPort_[0]!=mappedPort_[1]){
            note("> Your router changes ports for each link:");note("  some players may not be able to reach you.");
        }
        return;
    }
    if(polls_%StunEvery)return;
    ++stunTries_;
    for(unsigned i=0;i<2;++i){
        if(!stunAddress_[i]||mappedAddress_[i])continue;
        uint8_t request[20]{};request[1]=1; // Binding request, no attributes
        const uint32_t cookie=htonl(StunCookie);std::memcpy(request+4,&cookie,4);
        for(unsigned b=0;b<12;++b)stunTx_[i][b]=uint8_t((id_>>(8*(b&3)))^(b*37+i*101)^stunTries_);
        std::memcpy(request+8,stunTx_[i],12);
        sendTo(stunAddress_[i],stunPort_[i],request,sizeof request);
    }
}

void Session::receiveStun(const uint8_t* data,size_t size){
    if(size<20||data[0]!=0x01||data[1]!=0x01)return; // Binding success
    int server=-1;for(int i=0;i<2;++i)if(!std::memcmp(data+8,stunTx_[i],12))server=i;
    if(server<0)return;
    const size_t length=std::min<size_t>(size-20,(size_t(data[2])<<8)|data[3]);
    for(size_t at=20;at+4<=20+length;){
        const unsigned type=(unsigned(data[at])<<8)|data[at+1],len=(unsigned(data[at+2])<<8)|data[at+3];
        const uint8_t* v=data+at+4;
        if(at+4+len>size)break;
        if((type==0x0020||type==0x0001)&&len>=8&&v[1]==1){
            uint16_t port=uint16_t((v[2]<<8)|v[3]);uint32_t address=(uint32_t(v[4])<<24)|(uint32_t(v[5])<<16)|(uint32_t(v[6])<<8)|v[7];
            if(type==0x0020){port^=uint16_t(StunCookie>>16);address^=StunCookie;}
            mappedAddress_[server]=htonl(address);mappedPort_[server]=port;
            if(type==0x0020)break;
        }
        at+=4+((len+3)&~3u);
    }
}

void Session::sendLobby(){
    Lobby lobby{};lobby.h=header(Type::Lobby);lobby.round=round_;lobby.course=uint8_t(course_);lobby.engineClass=uint8_t(engineClass_);
    lobby.mirror=mirror_;lobby.count=uint8_t(count_);lobby.started=started_;copyBuild(lobby.build);
    for(unsigned s=0;s<count_;++s){const auto& p=players_[s];lobby.players[s]={p.id,p.address,p.port,p.garage,uint8_t(p.autoDrift),0};}
    sendAll(&lobby,sizeof lobby);
}

void Session::lobbyStep(unsigned garage,bool autoDrift,unsigned course,unsigned engineClass,bool mirror){
    poll();stunStep();
    if(mode_==Mode::Host){
        if(!count_){count_=1;slot_=0;}
        players_[0]={id_,0,0,uint16_t(garage),autoDrift};course_=course;engineClass_=engineClass;mirror_=mirror;
        // A player who stopped calling has gone back to the menu.
        for(unsigned s=1;s<count_;)if(!started_&&direct_[s]>LobbyTimeout){
            note("> Player %u left the lobby",s+1);
            for(unsigned t=s;t+1<count_;++t){players_[t]=players_[t+1];direct_[t]=direct_[t+1];reach_[t]=reach_[t+1];}
            --count_;
        }else ++s;
        if(polls_%LobbyEvery==0)sendLobby();
        if(polls_%PingEvery==0){Ping ping{header(Type::Ping),id_};for(unsigned i=0;i<invitedCount_;++i)sendTo(invited_[i],invitedPort_[i],&ping,sizeof ping);}
        return;
    }
    if(!hostKnown_||!stunDone_)return;
    if(slot_==255||!started_){
        if(polls_%HelloEvery==0){
            Hello hello{};hello.h=header(Type::Hello);hello.id=id_;hello.garage=uint16_t(garage);hello.autoDrift=autoDrift;
            hello.reach=reachMask();hello.lastRound=lastRound_;std::snprintf(hello.version,sizeof hello.version,"%s",MKW_VERSION);copyBuild(hello.build);
            sendTo(hostAddress_,hostPort_,&hello,sizeof hello);
        }
        if(slot_==255&&!announcedWait_&&polls_>=AskHostAfter&&*code_){
            announcedWait_=true;note("> No answer yet. Ask the host to press X");note("  and type your code: %s",code_);
        }
    }
    if(slot_!=255&&polls_%PingEvery==0){Ping ping{header(Type::Ping),id_};sendAll(&ping,sizeof ping);}
    const unsigned heard=heardCount();
    if(slot_!=255&&count_>2&&heard!=heardBefore_){heardBefore_=heard;note("> Linked directly to %u of %u players",heard,count_-1);}
}

bool Session::canStart() const{return mode_==Mode::Host&&count_>=2&&!started_;}
void Session::start(){
    if(!canStart())return;
    started_=true;lastRound_=round_;ready_={};ready_[0]=true;goRepeats_=0;readyPolls_=0;resetEvents();
    for(unsigned s=0;s<MaxPlayers;++s){hasFrame_[s]=false;left_[s]=false;silent_[s]=0;}
    for(unsigned n=0;n<4;++n)sendLobby();
    note("> Starting with %u players",count_);
}

bool Session::readyStep(){
    poll();++readyPolls_;
    if(mode_==Mode::Host){
        if(polls_%LobbyEvery==0)sendLobby();
        bool all=true;for(unsigned s=1;s<count_;++s)if(!ready_[s]&&!left_[s])all=false;
        if(!all&&readyPolls_<ReadyTimeout)return false;
        if(goRepeats_<GoRepeats){Go go{header(Type::Go),round_};sendAll(&go,sizeof go);++goRepeats_;}
        goSeen_=true;return true;
    }
    if(!goSeen_&&polls_%ReadyEvery==0){Ready ready{header(Type::Ready),round_};sendTo(hostAddress_,hostPort_,&ready,sizeof ready);}
    if(polls_%PingEvery==0){Ping ping{header(Type::Ping),id_};sendAll(&ping,sizeof ping);}
    return goSeen_;
}

void Session::sendFrame(uint32_t tick,uint16_t buttons,int8_t stickX,uint8_t trick,uint8_t flags,const float pos[3],const float fullRot[4],
                        const float mainRot[4],const float extVel[3],const float intVel[3],const FrameState& state){
    // Above four players, every other tick: the inputs carry the kart in between.
    if(count_>4&&(tick&1))return;
    Frame f{};f.h=header(Type::Frame);f.tick=tick;f.buttons=buttons;f.stickX=stickX;f.trick=trick;f.flags=flags;f.reach=reachMask();
    std::memcpy(f.pos,pos,sizeof f.pos);std::memcpy(f.fullRot,fullRot,sizeof f.fullRot);std::memcpy(f.mainRot,mainRot,sizeof f.mainRot);
    std::memcpy(f.extVel,extVel,sizeof f.extVel);std::memcpy(f.intVel,intVel,sizeof f.intVel);
    f.item=state.item;f.charges=state.charges;f.heldKind=state.heldKind;f.heldMask=state.heldMask;f.itemFlags=state.itemFlags;f.round=round_;f.finish=state.finish;
    for(unsigned s=0;s<MaxPlayers;++s)f.ack[s]=uint8_t(expected_[s]);
    sendAll(&f,sizeof f);
}

void Session::resetEvents(){
    nextSeq_=1;acked_.fill(1);expected_.fill(1);inboxHead_=inboxCount_=0;eventQuiet_=0;eventNew_=false;
}
void Session::sendEvent(RaceEvent event){
    if(socket_<0||!started_)return;
    // A full ring drops its oldest event: receivers skip the gap.
    event.seq=nextSeq_;event.reserved=0;outbox_[nextSeq_%OutboxSize]=event;++nextSeq_;eventNew_=true;
}
void Session::eventStep(){
    if(socket_<0||!started_)return;
    ++eventQuiet_;
    // The oldest event a console still racing has not acknowledged.
    uint16_t lowest=nextSeq_;
    for(unsigned s=0;s<count_;++s)if(s!=slot_&&!left_[s]&&silent_[s]<LostAfter)lowest=std::min(lowest,acked_[s]);
    if(nextSeq_-lowest>OutboxSize)lowest=uint16_t(nextSeq_-OutboxSize);
    if(lowest==nextSeq_||(!eventNew_&&eventQuiet_<EventsEvery))return;
    Events packet{};packet.h=header(Type::Events);packet.round=round_;packet.count=uint8_t(std::min<unsigned>(EventsPerPacket,nextSeq_-lowest));
    for(unsigned n=0;n<packet.count;++n)packet.events[n]=outbox_[(lowest+n)%OutboxSize];
    sendAll(&packet,offsetof(Events,events)+packet.count*sizeof(RaceEvent));
    eventQuiet_=0;eventNew_=false;
}
bool Session::takeRaceEvent(unsigned& slot,RaceEvent& out){
    if(!inboxCount_)return false;
    slot=inbox_[inboxHead_].slot;out=inbox_[inboxHead_].event;inboxHead_=(inboxHead_+1)%InboxSize;--inboxCount_;return true;
}

void Session::poll(){
    if(socket_<0)return;
    ++polls_;
    for(unsigned s=0;s<MaxPlayers;++s){++silent_[s];++direct_[s];}
    alignas(4) uint8_t buffer[512];
    for(unsigned n=0;n<64;++n){
        sockaddr_in from{};socklen_t length=sizeof from;
        const ssize_t got=recvfrom(socket_,buffer,sizeof buffer,0,reinterpret_cast<sockaddr*>(&from),&length);
        if(got<=0)break;
        receive(buffer,size_t(got),from.sin_addr.s_addr,ntohs(from.sin_port));
    }
}

void Session::receive(const uint8_t* data,size_t size,uint32_t address,uint16_t port){
    if(size>=20&&data[4]==0x21&&data[5]==0x12&&data[6]==0xA4&&data[7]==0x42){receiveStun(data,size);return;}
    if(size<sizeof(Header))return;
    Header h;std::memcpy(&h,data,sizeof h);
    if(h.magic!=Magic||h.version!=Version)return;
    char ip[16];
    if(h.type==Type::Hello){
        if(mode_!=Mode::Host||size<sizeof(Hello))return;
        Hello hello;std::memcpy(&hello,data,sizeof hello);
        auto refuse=[&](const char* why){Refuse r{};r.h=header(Type::Refuse);r.id=hello.id;std::snprintf(r.message,sizeof r.message,"%s",why);sendTo(address,port,&r,sizeof r);};
        if(!sameBuild(hello.build)){refuse("Different game build. Get the official one.");return;}
        if(!count_){count_=1;slot_=0;}
        unsigned s=1;while(s<count_&&players_[s].id!=hello.id)++s;
        if(s==count_){
            if(started_){refuse("A race is on. Try again after it.");return;}
            if(count_==MaxPlayers){refuse("The race is full (12 players).");return;}
            ++count_;note("> Player %u joined from %s",s+1,dotted(address,ip));
        }
        players_[s]={hello.id,address,port,hello.garage,hello.autoDrift!=0};reach_[s]=hello.reach;direct_[s]=0;silent_[s]=0;
        sendLobby();return;
    }
    if(h.type==Type::Lobby||h.type==Type::Refuse){
        if(mode_!=Mode::Join||address!=hostAddress_||port!=hostPort_)return;
        if(h.type==Type::Refuse){
            if(size<sizeof(Refuse))return;
            Refuse r;std::memcpy(&r,data,sizeof r);if(r.id!=id_)return;
            r.message[sizeof r.message-1]=0;std::snprintf(failure_,sizeof failure_,"%s",r.message);reason_=failure_;
            if(!failed_)note("> The host said: %s",r.message);
            failed_=true;return;
        }
        if(size<sizeof(Lobby))return;
        Lobby lobby;std::memcpy(&lobby,data,sizeof lobby);
        if(!sameBuild(lobby.build)){if(!failed_)note("> The host has a different game build.");failed_=true;reason_="the host has a different game build";return;}
        // An old race this console has already raced (or missed).
        if(lobby.started&&lobby.round<=lastRound_&&lastRound_)return;
        if(started_)return;
        const unsigned count=std::min<unsigned>(lobby.count,MaxPlayers);
        unsigned mine=255;
        for(unsigned s=0;s<count;++s){const auto& p=lobby.players[s];players_[s]={p.id,p.address,p.port,p.garage,p.autoDrift!=0};if(p.id==id_)mine=s;}
        players_[0].address=hostAddress_;players_[0].port=hostPort_;
        if(mine==255){if(lobby.started&&!announcedWait_){announcedWait_=true;note("> A race started without you. Wait for the next.");}return;}
        const bool first=slot_==255;
        count_=count;slot_=mine;session_=lobby.h.session;round_=lobby.round;course_=lobby.course;engineClass_=lobby.engineClass;mirror_=lobby.mirror!=0;
        direct_[0]=0;silent_[0]=0;
        if(first)note("> In the lobby as player %u",slot_+1);
        if(lobby.started){
            started_=true;lastRound_=round_;goSeen_=false;resetEvents();
            for(unsigned s=0;s<MaxPlayers;++s){hasFrame_[s]=false;left_[s]=false;silent_[s]=0;}
            note("> The host started the race: %u players",count_);
        }
        return;
    }
    if(h.type==Type::Ping){
        // Also arrives before this console has a lobby: it opens the route.
        const int s=find(address,port);if(s>=0)direct_[s]=0;
        return;
    }
    if(h.session!=session_||h.sender>=count_||h.sender==slot_)return;
    const unsigned s=h.sender;const bool direct=find(address,port)==int(s);
    if(direct)direct_[s]=0;
    silent_[s]=0;
    // The host passes packets on to players who can't hear the sender themselves.
    auto relay=[&](){if(mode_!=Mode::Host||!direct)return;for(unsigned k=1;k<count_;++k)if(k!=s&&!(reach_[k]>>s&1u))sendTo(players_[k].address,players_[k].port,data,size);};
    switch(h.type){
    case Type::Ready:if(mode_==Mode::Host){ready_[s]=true;if(goRepeats_>=GoRepeats){Go go{header(Type::Go),round_};sendTo(address,port,&go,sizeof go);}}break;
    case Type::Go:{if(size<sizeof(Go))return;Go go;std::memcpy(&go,data,sizeof go);if(mode_==Mode::Join&&go.round==round_)goSeen_=true;break;}
    case Type::Frame:{
        if(size<sizeof(Frame))return;
        Frame frame;std::memcpy(&frame,data,sizeof frame);if(frame.round!=round_)return;reach_[s]=frame.reach;
        // UDP can reorder: keep only the newest tick.
        if(!hasFrame_[s]||frame.tick>=latest_[s].tick){latest_[s]=frame;hasFrame_[s]=true;}
        // Which of this console's events the sender has: the newest number at
        // or below the next one with that low byte.
        if(slot_<MaxPlayers){const uint16_t has=uint16_t(nextSeq_-((nextSeq_-frame.ack[slot_])&0xffu));if(has>acked_[s])acked_[s]=has;}
        relay();break;
    }
    case Type::Events:{
        if(size<offsetof(Events,events))return;
        Events packet;std::memcpy(&packet,data,std::min(size,sizeof packet));if(packet.round!=round_)return;
        const unsigned count=std::min<unsigned>({packet.count,EventsPerPacket,unsigned((size-offsetof(Events,events))/sizeof(RaceEvent))});
        for(unsigned n=0;n<count&&inboxCount_<InboxSize;++n){
            const RaceEvent& event=packet.events[n];
            // In order, each once. The sender starts at its oldest, so a first
            // event beyond the expected one means the ones between are gone.
            if(event.seq<expected_[s]||(event.seq>expected_[s]&&n))continue;
            expected_[s]=uint16_t(event.seq+1);
            inbox_[(inboxHead_+inboxCount_++)%InboxSize]={uint8_t(s),event};
        }
        relay();break;
    }
    case Type::Bye:if(!left_[s])note("> Player %u left",s+1);left_[s]=true;relay();break;
    default:break;
    }
}

void Session::newRound(){
    goSeen_=false;goRepeats_=0;readyPolls_=0;ready_={};announcedWait_=false;failed_=false;resetEvents();
    for(unsigned s=0;s<MaxPlayers;++s){hasFrame_[s]=false;left_[s]=false;silent_[s]=0;}
    if(mode_==Mode::Host){
        // Joiners call again from their own menus; the lobby fills up anew.
        if(started_){++round_;started_=false;}
        if(count_>1)count_=1;
        if(!round_)round_=1;
    }else{started_=false;slot_=255;count_=0;}
}
}

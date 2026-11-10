#include <mkw/lan_play.hpp>
#include <3ds.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace mkw::lan {
namespace {
constexpr size_t SocBufferSize=0x20000; // libctru's minimum page-aligned pool; small for Old 3DS
constexpr unsigned HelloEvery=20,ReadyEvery=10,GoRepeats=6;
}
Session& session(){static Session value;return value;}

bool Session::configure(){
    FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/lan.txt","r");if(!f)return false;
    char mode[16]{},ip[64]{};unsigned port=DefaultPort,local=DefaultPort;
    const int fields=std::fscanf(f,"%15s %63s %u %u",mode,ip,&port,&local);std::fclose(f);
    if(fields>=1&&!std::strcmp(mode,"host")){
        role_=Role::Host;if(fields>=2)localPort_=uint16_t(std::strtoul(ip,nullptr,10));
        if(!localPort_)localPort_=DefaultPort;
        return true;
    }
    if(fields>=2&&!std::strcmp(mode,"join")){
        in_addr address{};if(!inet_aton(ip,&address)){reason_="lan.txt: bad address";return false;}
        role_=Role::Join;peerAddress_=address.s_addr;peerPort_=uint16_t(fields>=3?port:DefaultPort);
        localPort_=uint16_t(fields>=4?local:DefaultPort);peerKnown_=true;
        return true;
    }
    reason_="lan.txt: expected 'host' or 'join <ip>'";return false;
}

bool Session::open(){
    if(socket_>=0)return true;
    socBuffer_=static_cast<uint32_t*>(memalign(0x1000,SocBufferSize));
    if(!socBuffer_){reason_="no memory for the network";return false;}
    if(R_FAILED(socInit(socBuffer_,SocBufferSize))){std::free(socBuffer_);socBuffer_=nullptr;reason_="Wi-Fi is not available";return false;}
    socket_=::socket(AF_INET,SOCK_DGRAM,0);
    if(socket_<0){close();reason_="could not open a network socket";return false;}
    fcntl(socket_,F_SETFL,fcntl(socket_,F_GETFL,0)|O_NONBLOCK);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(localPort_);local.sin_addr.s_addr=INADDR_ANY;
    if(bind(socket_,reinterpret_cast<sockaddr*>(&local),sizeof local)<0){close();reason_="the network port is in use";return false;}
    session_=role_==Role::Host?(uint32_t(svcGetSystemTick())|1u):0;
    return true;
}

void Session::close(){
    if(socket_>=0&&peerKnown_){Header bye=header(Type::Bye);send(&bye,sizeof bye);}
    if(socket_>=0){::close(socket_);socket_=-1;}
    if(socBuffer_){socExit();std::free(socBuffer_);socBuffer_=nullptr;}
}

Header Session::header(Type type){
    return Header{Magic,Version,type,uint8_t(role_==Role::Host?0:1),session_,++sequence_};
}

void Session::send(const void* data,size_t size){
    if(socket_<0||!peerKnown_)return;
    sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(peerPort_);to.sin_addr.s_addr=peerAddress_;
    sendto(socket_,data,size,0,reinterpret_cast<sockaddr*>(&to),sizeof to);
}

void Session::poll(){
    if(socket_<0)return;
    ++pollCount_;++silentPolls_;
    alignas(4) uint8_t buffer[256];
    for(unsigned n=0;n<32;++n){
        sockaddr_in from{};socklen_t length=sizeof from;
        const ssize_t got=recvfrom(socket_,buffer,sizeof buffer,0,reinterpret_cast<sockaddr*>(&from),&length);
        if(got<=0)break;
        receive(buffer,size_t(got),from.sin_addr.s_addr,ntohs(from.sin_port));
    }
}

void Session::receive(const uint8_t* data,size_t size,uint32_t address,uint16_t port){
    if(size<sizeof(Header))return;
    Header h;std::memcpy(&h,data,sizeof h);
    if(h.magic!=Magic||h.version!=Version||h.sender==(role_==Role::Host?0:1))return;
    // Only the host accepts a new peer, and only from a Hello.
    if(role_==Role::Host&&h.type==Type::Hello&&!welcomed_){peerAddress_=address;peerPort_=port;peerKnown_=true;}
    if(!peerKnown_||address!=peerAddress_||port!=peerPort_)return;
    if(h.type!=Type::Hello&&h.type!=Type::Welcome&&h.session!=session_)return;
    silentPolls_=0;
    switch(h.type){
    case Type::Hello:{
        if(role_!=Role::Host||size<sizeof(Hello))return;
        Hello hello;std::memcpy(&hello,data,sizeof hello);peerGarage_=hello.garage;peerAutoDrift_=hello.autoDrift!=0;
        Welcome w{header(Type::Welcome),uint8_t(course_),uint8_t(engineClass_),uint8_t(mirror_),uint8_t(autoDrift_),uint16_t(hostGarage_),uint16_t(peerGarage_)};
        send(&w,sizeof w);welcomed_=true;break;
    }
    case Type::Welcome:{
        if(role_!=Role::Join||size<sizeof(Welcome))return;
        Welcome w;std::memcpy(&w,data,sizeof w);
        session_=w.h.session;course_=w.course;engineClass_=w.engineClass;mirror_=w.mirror!=0;peerGarage_=w.hostGarage;peerAutoDrift_=w.hostAutoDrift!=0;welcomed_=true;break;
    }
    case Type::Ready:
        peerReady_=true;
        // A joiner still waiting means it missed our Go.
        if(role_==Role::Host&&goSeen_){Go go{header(Type::Go)};send(&go,sizeof go);}
        break;
    case Type::Go:if(role_==Role::Join)goSeen_=true;break;
    case Type::Frame:{
        if(size<sizeof(Frame))return;
        Frame frame;std::memcpy(&frame,data,sizeof frame);
        // UDP can reorder: keep only the newest tick.
        if(!hasFrame_||frame.tick>=latest_.tick){latest_=frame;hasFrame_=true;}
        break;
    }
    case Type::Bye:hasFrame_=false;peerReady_=false;reason_="the other player left";break;
    }
}

void Session::handshakeStep(unsigned localGarage,bool autoDrift,unsigned course,unsigned engineClass,bool mirror){
    autoDrift_=autoDrift;
    if(role_==Role::Host){hostGarage_=localGarage;course_=course;engineClass_=engineClass;mirror_=mirror;}
    else if(!welcomed_&&pollCount_%HelloEvery==0){Hello hello{header(Type::Hello),uint16_t(localGarage),uint8_t(autoDrift)};send(&hello,sizeof hello);}
    poll();
}

bool Session::readyStep(){
    poll();
    if(role_==Role::Host){
        if(!peerReady_)return false;
        if(goRepeats_<GoRepeats){Go go{header(Type::Go)};send(&go,sizeof go);++goRepeats_;}
        goSeen_=true;return true;
    }
    if(!goSeen_&&pollCount_%ReadyEvery==0){Ready ready{header(Type::Ready)};send(&ready,sizeof ready);}
    return goSeen_;
}

void Session::sendFrame(uint32_t tick,uint16_t buttons,int8_t stickX,uint8_t trick,uint8_t flags,const float pos[3],const float fullRot[4],
                        const float mainRot[4],const float extVel[3],const float intVel[3]){
    Frame f{};f.h=header(Type::Frame);f.tick=tick;f.buttons=buttons;f.stickX=stickX;f.trick=trick;f.flags=flags;
    std::memcpy(f.pos,pos,sizeof f.pos);std::memcpy(f.fullRot,fullRot,sizeof f.fullRot);std::memcpy(f.mainRot,mainRot,sizeof f.mainRot);
    std::memcpy(f.extVel,extVel,sizeof f.extVel);std::memcpy(f.intVel,intVel,sizeof f.intVel);
    send(&f,sizeof f);
}

void Session::resetRace(){welcomed_=false;peerReady_=false;goSeen_=false;goRepeats_=0;hasFrame_=false;}
}

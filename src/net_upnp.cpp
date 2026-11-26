// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
// UPnP Internet Gateway Device, the parts a game console needs: find the router
// (SSDP), read its description, AddPortMapping for our UDP port and ask its
// internet address. Plain sockets and polling only, so the same file runs in a
// computer test (tools/tests/upnp_probe.cpp) against a real router.
#include <mkw/net_upnp.hpp>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __3DS__
#include <3ds.h>
#else
#include <time.h>
#endif

namespace mkw::net {
namespace {
uint64_t now(){
#ifdef __3DS__
    return osGetTime();
#else
    timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000u+uint64_t(t.tv_nsec/1000000);
#endif
}
constexpr unsigned SearchEvery=700,Searches=4,StepPatience=4000;
void nonBlocking(int fd){fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);}
bool waiting(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==EALREADY||errno==ENOTCONN;}
std::string lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
// The text between <tag> and </tag> after `from` (tags compared without case or
// namespace prefix: <u:NewExternalIPAddress> matches NewExternalIPAddress).
bool element(const std::string& xml,const char* tag,std::string& out,size_t from=0,size_t* at=nullptr){
    const std::string low=lower(xml),name=lower(tag);
    for(size_t p=low.find('<',from);p!=std::string::npos;p=low.find('<',p+1)){
        size_t end=low.find('>',p);if(end==std::string::npos)return false;
        std::string open=low.substr(p+1,end-p-1);
        if(const auto colon=open.find(':');colon!=std::string::npos)open=open.substr(colon+1);
        if(open!=name)continue;
        const size_t close=low.find("</",end);if(close==std::string::npos)return false;
        out=xml.substr(end+1,close-end-1);
        while(!out.empty()&&(out.front()==' '||out.front()=='\n'||out.front()=='\r'||out.front()=='\t'))out.erase(out.begin());
        while(!out.empty()&&(out.back()==' '||out.back()=='\n'||out.back()=='\r'||out.back()=='\t'))out.pop_back();
        if(at)*at=close;
        return true;
    }
    return false;
}
// "http://192.168.1.1:5000/rootDesc.xml" -> address, port, path.
bool splitUrl(const std::string& url,uint32_t& address,uint16_t& port,std::string& path){
    if(lower(url.substr(0,7))!="http://")return false;
    const size_t slash=url.find('/',7);
    const std::string hostPort=url.substr(7,slash==std::string::npos?std::string::npos:slash-7);
    path=slash==std::string::npos?"/":url.substr(slash);
    const size_t colon=hostPort.find(':');
    in_addr a{};if(!inet_aton(hostPort.substr(0,colon).c_str(),&a))return false;
    address=a.s_addr;port=uint16_t(colon==std::string::npos?80:std::strtoul(hostPort.c_str()+colon+1,nullptr,10));
    return port!=0;
}
// The reply's body, chunked transfer undone.
std::string body(const std::string& reply){
    const size_t split=reply.find("\r\n\r\n");if(split==std::string::npos)return {};
    std::string out=reply.substr(split+4);
    if(lower(reply.substr(0,split)).find("transfer-encoding: chunked")==std::string::npos)return out;
    std::string plain;size_t p=0;
    while(p<out.size()){
        const size_t line=out.find("\r\n",p);if(line==std::string::npos)break;
        const size_t size=std::strtoul(out.c_str()+p,nullptr,16);if(!size)break;
        plain+=out.substr(line+2,size);p=line+2+size+2;
    }
    return plain;
}
int status(const std::string& reply){
    const size_t space=reply.find(' ');return space==std::string::npos?0:std::atoi(reply.c_str()+space+1);
}
}

bool PortOpener::Http::begin(uint32_t address,uint16_t port,std::string request){
    close();
    fd=::socket(AF_INET,SOCK_STREAM,0);if(fd<0)return false;
    nonBlocking(fd);
    sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(port);to.sin_addr.s_addr=address;
    if(::connect(fd,reinterpret_cast<sockaddr*>(&to),sizeof to)!=0&&!waiting()&&errno!=EISCONN){close();return false;}
    out=std::move(request);in.clear();sent=0;connected=false;
    return true;
}
int PortOpener::Http::step(){
    if(fd<0)return -1;
    while(sent<out.size()){
        const ssize_t n=::send(fd,out.data()+sent,out.size()-sent,0);
        if(n>0){sent+=size_t(n);connected=true;continue;}
        if(n<0&&waiting())return 0;
        return -1;
    }
    char buffer[1024];
    for(;;){
        const ssize_t n=::recv(fd,buffer,sizeof buffer,0);
        if(n>0){in.append(buffer,size_t(n));if(in.size()>64*1024)return -1;continue;}
        if(n==0)return in.empty()?-1:1;
        if(!waiting())return in.empty()?-1:1;
        break;
    }
    // Some routers keep the connection open after the whole reply.
    const size_t split=in.find("\r\n\r\n");
    if(split!=std::string::npos){
        const std::string head=lower(in.substr(0,split));
        const size_t length=head.find("content-length:");
        if(length!=std::string::npos&&in.size()-split-4>=std::strtoul(head.c_str()+length+15,nullptr,10))return 1;
    }
    return 0;
}
void PortOpener::Http::close(){if(fd>=0){::close(fd);fd=-1;}}

void PortOpener::fail(const char* why){
    why_=why;state_=State::Failed;http_.close();
    if(udp_>=0){::close(udp_);udp_=-1;}
}

void PortOpener::start(uint32_t lanAddress,uint16_t port){
    stop();
    lan_=lanAddress;port_=port;tryPort_=port;attempts_=0;lease_=3600;externalAddress_=0;externalPort_=0;searches_=0;why_="";
    udp_=::socket(AF_INET,SOCK_DGRAM,0);
    if(udp_<0){fail("no socket");return;}
    nonBlocking(udp_);
    // An explicit port: a 3DS refuses bind() to port 0.
    bool bound=false;
    for(unsigned i=0;i<16&&!bound;++i){
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(uint16_t(port+2000+i));local.sin_addr.s_addr=INADDR_ANY;
        bound=::bind(udp_,reinterpret_cast<sockaddr*>(&local),sizeof local)==0;
    }
    if(!bound){fail("no port for the router search");return;}
    state_=State::Search;nextSearch_=0;deadline_=now()+SearchEvery*Searches+1500;
}

void PortOpener::stop(){
    http_.close();
    if(udp_>=0){::close(udp_);udp_=-1;}
    state_=State::Idle;
}

bool PortOpener::parseLocation(const std::string& reply){
    const std::string low=lower(reply);
    const size_t at=low.find("\nlocation:");if(at==std::string::npos)return false;
    size_t from=at+10;while(from<reply.size()&&reply[from]==' ')++from;
    const size_t end=reply.find_first_of("\r\n",from);
    return splitUrl(reply.substr(from,end==std::string::npos?std::string::npos:end-from),routerAddress_,routerPort_,descriptionPath_);
}

bool PortOpener::parseDescription(const std::string& xml){
    // The first WANIPConnection (or, on DSL routers, WANPPPConnection) service.
    for(const char* wanted:{"urn:schemas-upnp-org:service:WANIPConnection:","urn:schemas-upnp-org:service:WANPPPConnection:"}){
        std::string type;size_t at=0;
        while(element(xml,"serviceType",type,at,&at)){
            if(type.compare(0,std::strlen(wanted),wanted)!=0)continue;
            std::string control;if(!element(xml,"controlURL",control,at))return false;
            service_=type;
            if(lower(control.substr(0,7))=="http://"){uint32_t a;uint16_t p;if(!splitUrl(control,a,p,controlPath_))return false;routerAddress_=a;routerPort_=p;}
            else controlPath_=control.empty()||control[0]!='/'?"/"+control:control;
            return true;
        }
    }
    return false;
}

bool PortOpener::soap(const char* action,const std::string& arguments){
    char host[32];in_addr a{};a.s_addr=routerAddress_;std::snprintf(host,sizeof host,"%s:%u",inet_ntoa(a),unsigned(routerPort_));
    const std::string envelope=
        "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:"+std::string(action)+" xmlns:u=\""+service_+"\">"+arguments+"</u:"+action+"></s:Body></s:Envelope>\r\n";
    const std::string request="POST "+controlPath_+" HTTP/1.1\r\nHost: "+host+"\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \""+service_+"#"+action+"\"\r\nContent-Length: "+std::to_string(envelope.size())+"\r\nConnection: close\r\n\r\n"+envelope;
    deadline_=now()+StepPatience;
    return http_.begin(routerAddress_,routerPort_,request);
}

void PortOpener::step(){
    if(!busy())return;
    const uint64_t t=now();
    if(t>deadline_){fail(state_==State::Search?"no UPnP router answered":"the router stopped answering");return;}
    if(state_==State::Search){
        if(t>=nextSearch_&&searches_<Searches){
            nextSearch_=t+SearchEvery;++searches_;
            static const char* targets[]={"urn:schemas-upnp-org:device:InternetGatewayDevice:1","urn:schemas-upnp-org:service:WANIPConnection:1"};
            for(const char* target:targets){
                char search[256];
                const int n=std::snprintf(search,sizeof search,"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",target);
                sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(1900);to.sin_addr.s_addr=inet_addr("239.255.255.250");
                ::sendto(udp_,search,size_t(n),0,reinterpret_cast<sockaddr*>(&to),sizeof to);
            }
        }
        char buffer[1536];
        for(;;){
            const ssize_t n=::recv(udp_,buffer,sizeof buffer-1,0);
            if(n<=0)break;
            buffer[n]=0;
            if(!parseLocation(buffer))continue;
            char host[32];in_addr a{};a.s_addr=routerAddress_;std::snprintf(host,sizeof host,"%s:%u",inet_ntoa(a),unsigned(routerPort_));
            ::close(udp_);udp_=-1;
            if(!http_.begin(routerAddress_,routerPort_,"GET "+descriptionPath_+" HTTP/1.1\r\nHost: "+host+"\r\nConnection: close\r\n\r\n")){fail("can't reach the router");return;}
            state_=State::Describe;deadline_=t+StepPatience;return;
        }
        return;
    }
    const int r=http_.step();
    if(r==0)return;
    if(r<0){fail("the router closed the connection");return;}
    const std::string reply=http_.in;http_.close();
    if(state_==State::Describe){
        if(status(reply)!=200||!parseDescription(body(reply))){fail("the router has no internet connection service");return;}
        state_=State::Map;
    }else if(state_==State::Map){
        if(status(reply)==200){externalPort_=tryPort_;state_=State::Address;}
        else{
            std::string code;element(body(reply),"errorCode",code);
            const int error=std::atoi(code.c_str());
            // 725: only permanent mappings; 718: that outside port is taken (try the next).
            if(error==725&&lease_){lease_=0;}
            else if(error==718&&attempts_<4){++attempts_;tryPort_=uint16_t(port_+attempts_*17);}
            else{why_=error==0?"the router refused":"the router refused the port";state_=State::Failed;return;}
        }
    }else if(state_==State::Address){
        std::string text;in_addr a{};
        if(status(reply)==200&&element(body(reply),"NewExternalIPAddress",text)&&inet_aton(text.c_str(),&a))externalAddress_=a.s_addr;
        state_=State::Done;return;
    }
    if(state_==State::Map){
        in_addr a{};a.s_addr=lan_;
        const std::string args="<NewRemoteHost></NewRemoteHost><NewExternalPort>"+std::to_string(tryPort_)+"</NewExternalPort><NewProtocol>UDP</NewProtocol>"
            "<NewInternalPort>"+std::to_string(port_)+"</NewInternalPort><NewInternalClient>"+inet_ntoa(a)+"</NewInternalClient><NewEnabled>1</NewEnabled>"
            "<NewPortMappingDescription>MKW 3DS</NewPortMappingDescription><NewLeaseDuration>"+std::to_string(lease_)+"</NewLeaseDuration>";
        if(!soap("AddPortMapping",args))fail("can't reach the router");
    }else if(state_==State::Address){
        if(!soap("GetExternalIPAddress",""))state_=State::Done;
    }
}
}

// Computer test for src/net_upnp.cpp: asks this network's router to pass a UDP
// port to this computer, as the 3DS host does. Usage: upnp_probe <lan ip> <port>
#include <mkw/net_upnp.hpp>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
int main(int argc,char** argv){
    if(argc<3){std::fprintf(stderr,"usage: upnp_probe <lan ip> <port>\n");return 2;}
    mkw::net::PortOpener opener;opener.start(inet_addr(argv[1]),uint16_t(std::atoi(argv[2])));
    while(opener.busy()){opener.step();usleep(16000);}
    if(!opener.opened()){std::printf("failed: %s\n",opener.why());return 1;}
    in_addr a{};a.s_addr=opener.externalAddress();
    std::printf("opened: outside %s:%u\n",inet_ntoa(a),unsigned(opener.externalPort()));
    return 0;
}

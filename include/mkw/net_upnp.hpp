// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
#include <cstdint>
#include <string>

namespace mkw::net {
// The host asks its home router (UPnP, as game consoles do) to pass a UDP port
// to this console, so players over the internet get in with the host's code
// alone. Runs a step at a time from the lobby: nothing here blocks a frame.
// Routers without UPnP (or with it off) just don't answer; the game then
// falls back to the joining player's code (Add a Player).
class PortOpener {
public:
    // lanAddress: this console's address on its Wi-Fi (network byte order).
    void start(uint32_t lanAddress,uint16_t port);
    void step();
    void stop();
    bool busy() const{return state_!=State::Idle&&state_!=State::Done&&state_!=State::Failed;}
    bool opened() const{return state_==State::Done;}
    bool failed() const{return state_==State::Failed;}
    // Once opened(): the router's internet address (network byte order, 0 if it
    // didn't say) and the outside port it passes to us.
    uint32_t externalAddress() const{return externalAddress_;}
    uint16_t externalPort() const{return externalPort_;}
    const char* why() const{return why_;}
private:
    enum class State:uint8_t {Idle,Search,Describe,Map,Address,Done,Failed};
    struct Http {
        int fd=-1;std::string out,in;size_t sent=0;bool connected=false;
        bool begin(uint32_t address,uint16_t port,std::string request);
        // 1 finished (in holds the reply), 0 still going, -1 failed.
        int step();
        void close();
    };
    void fail(const char* why);
    bool parseLocation(const std::string& reply);
    bool parseDescription(const std::string& body);
    bool soap(const char* action,const std::string& arguments);
    State state_=State::Idle;
    uint32_t lan_=0;uint16_t port_=0;
    int udp_=-1;unsigned searches_=0;uint64_t deadline_=0,nextSearch_=0;
    Http http_{};
    uint32_t routerAddress_=0;uint16_t routerPort_=0;std::string descriptionPath_,controlPath_,service_;
    uint32_t lease_=3600;uint16_t tryPort_=0;unsigned attempts_=0;
    uint32_t externalAddress_=0;uint16_t externalPort_=0;
    const char* why_="";
};
}

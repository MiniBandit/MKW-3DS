#!/usr/bin/env python3
"""MKW 3DS online relay server: puts two consoles in a room and passes their
race packets between them. Python 3.8+ standard library only.

Consoles send to the server, never to each other, so players need no router
setup. The packet format is docs/LAN-PROTOCOL.md (include/mkw/lan_play.hpp).

    python3 mkw3ds_server.py                       listen on UDP 41750
    python3 mkw3ds_server.py --port 41750 --allowed allowed-builds.txt

allowed-builds.txt lists the build fingerprints (tools/build_id.py) the server
accepts, one per line; '#' starts a comment. It is re-read when it changes.
Any other build is told to download the official one. --allow-any turns the
check off (testing only). See docs/ONLINE-SERVER.md to set one up.
"""
import argparse,asyncio,logging,os,struct,time

MAGIC=0x314E4B4D;VERSION=1
HEADER=struct.Struct('<IHBBII')                 # magic, version, type, sender, session, sequence
JOIN=7;ROOM=8;BYE=6
JOIN_SIZE=HEADER.size+8+12+16
ROOM_OK,ROOM_FULL,ROOM_UNKNOWN_BUILD,ROOM_BAD=0,1,2,3
ROOM_SIZE=64
IDLE_SECONDS=20;MAX_PACKETS_PER_SECOND=240;MAX_ROOMS=500;PLAYERS_PER_ROOM=2

class Allowed:
    """The allowed build fingerprints, re-read when the file changes."""
    def __init__(self,path,allow_any):self.path,self.allow_any,self.mtime,self.ids=path,allow_any,None,set()
    def __contains__(self,build):
        if self.allow_any:return True
        try:mtime=os.path.getmtime(self.path)
        except OSError:return False
        if mtime!=self.mtime:
            with open(self.path,encoding='utf-8') as f:
                self.ids={line.split('#')[0].strip().lower() for line in f if line.split('#')[0].strip()}
            self.mtime=mtime;logging.info('allowed builds: %s',', '.join(sorted(self.ids)) or 'none')
        return build.lower() in self.ids

class Relay(asyncio.DatagramProtocol):
    def __init__(self,allowed):
        self.allowed=allowed;self.rooms={};self.member={};self.seen={};self.rate={};self.forwarded=0
    def connection_made(self,transport):self.transport=transport
    def reply(self,address,host,status,message):
        packet=HEADER.pack(MAGIC,VERSION,ROOM,255,0,0)+struct.pack('<BB',1 if host else 0,status)+message.encode('ascii','replace')[:45].ljust(46,b'\0')
        assert len(packet)==ROOM_SIZE;self.transport.sendto(packet,address)
    def leave(self,address,why):
        room=self.member.pop(address,None);self.seen.pop(address,None)
        if room is None:return
        members=self.rooms.get(room,[])
        if address in members:members.remove(address)
        if not members:self.rooms.pop(room,None)
        logging.info('%s left room %s (%s)',address[0],room,why)
    def datagram_received(self,data,addr):
        address=addr;now=time.monotonic()
        # Drop floods: a race sends 60 packets a second.
        second,count=self.rate.get(address,(int(now),0))
        if second!=int(now):second,count=int(now),0
        count+=1;self.rate[address]=(second,count)
        if count>MAX_PACKETS_PER_SECOND or len(data)<HEADER.size or len(data)>512:return
        magic,version,kind=HEADER.unpack_from(data)[:3]
        if magic!=MAGIC or version!=VERSION:return
        self.seen[address]=now
        if kind==JOIN:return self.join(data,address)
        if kind==BYE:
            self.forward(data,address);return self.leave(address,'quit')
        self.forward(data,address)
    def join(self,data,address):
        if len(data)<JOIN_SIZE:return self.reply(address,False,ROOM_BAD,'Bad request')
        room,version,build=(data[HEADER.size+o:HEADER.size+o+n].split(b'\0')[0].decode('ascii','replace') for o,n in ((0,8),(8,12),(20,16)))
        room=room.upper()
        if not room.isalnum():return self.reply(address,False,ROOM_BAD,'Use letters and numbers for the room')
        if build not in self.allowed:
            logging.info('%s refused: version %s build %s',address[0],version,build)
            return self.reply(address,False,ROOM_UNKNOWN_BUILD,'This build is not allowed. Get the official one.')
        current=self.member.get(address)
        if current is not None and current!=room:self.leave(address,'changed room')
        members=self.rooms.setdefault(room,[])
        if address not in members:
            if len(members)>=PLAYERS_PER_ROOM:return self.reply(address,False,ROOM_FULL,'That room is full. Try another code.')
            if len(self.rooms)>MAX_ROOMS:return self.reply(address,False,ROOM_FULL,'The server is full. Try later.')
            members.append(address);self.member[address]=room
            logging.info('%s joined room %s as %s (version %s, build %s)',address[0],room,'host' if members[0]==address else 'player',version,build)
        self.reply(address,members[0]==address,ROOM_OK,'OK')
    def forward(self,data,address):
        room=self.member.get(address)
        if room is None:return
        for other in self.rooms.get(room,[]):
            if other!=address:self.transport.sendto(data,other);self.forwarded+=1
    async def sweep(self):
        while True:
            await asyncio.sleep(5)
            now=time.monotonic()
            for address in [a for a,t in self.seen.items() if now-t>IDLE_SECONDS]:self.leave(address,'timed out')
            self.rate={a:v for a,v in self.rate.items() if a in self.seen}

async def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host',default='0.0.0.0');p.add_argument('--port',type=int,default=41750)
    p.add_argument('--allowed',default=os.path.join(os.path.dirname(os.path.abspath(__file__)),'allowed-builds.txt'))
    p.add_argument('--allow-any',action='store_true',help='accept every build (testing only)')
    a=p.parse_args()
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s')
    relay=Relay(Allowed(a.allowed,a.allow_any))
    transport,_=await asyncio.get_running_loop().create_datagram_endpoint(lambda:relay,local_addr=(a.host,a.port))
    logging.info('MKW 3DS relay on UDP %s:%d (%s)',a.host,a.port,'any build' if a.allow_any else 'builds in '+a.allowed)
    try:await relay.sweep()
    finally:transport.close()

if __name__=='__main__':
    try:asyncio.run(main())
    except KeyboardInterrupt:pass

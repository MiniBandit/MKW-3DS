#include <mkw/menu_movie_audio.hpp>
#include <mkw/asset_loader.hpp>
#include <mkw/dsp_adpcm.hpp>
#include <mkw/sound_system.hpp>
#include <mkw/snd_engine.hpp>
#include <3ds.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mkw {
namespace {
unsigned u16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
constexpr unsigned SamplesPerBuffer=4096;
int dspState=0; // 0 untried, 1 ready, -1 unavailable
int systemOutput=NDSP_OUTPUT_STEREO;
// Front pair at the movie's gain; in Surround also the rear pair, spread as the
// engine spreads music (snd::SurroundSpreadMusic, equal power).
void movieMix(int channel,float gain){
 gain=std::clamp(gain,0.f,1.f);float mix[12]={};
 if(systemOutput==NDSP_OUTPUT_SURROUND){
  static const float k=snd::SurroundSpreadMusic,n=1.f/std::sqrt(1.f+k*k);
  mix[0]=mix[1]=gain*n;mix[2]=mix[3]=gain*n*k;
 }else mix[0]=mix[1]=gain;
 ndspChnSetMix(channel,mix);
}
// MADP files are read in whole chunks from chunk-aligned offsets, so each
// read is one SD request of whole sectors.
constexpr unsigned AdpcmChunk=32u<<10;
// File and LINEAR sample buffers, shared with loader jobs. Jobs for one stream
// run in FIFO order on the single loader thread, so file access is serial.
struct Stream {
 std::FILE* file=nullptr;
 unsigned channels=0,rate=0,total=0,remaining=0,loopOffset=0;bool looping=false;
 std::array<void*,3> buffers{};
 // MADP (dsp_adpcm.hpp): remaining counts samples; raw holds read bytes
 // [rawAt, rawEnd) not yet decoded.
 bool adpcm=false;bool fileEnded=false;
 std::vector<unsigned char> raw;size_t rawAt=0,rawEnd=0;
 ~Stream(){for(auto* buffer:buffers)if(buffer)linearFree(buffer);if(file)std::fclose(file);}
 unsigned bufferBytes()const{return SamplesPerBuffer*channels*2;}
 // Reads the next chunk (looping as authored). Returns bytes, 0 at the end.
 unsigned fill(unsigned index,bool& ok){
  ok=true;
  if(adpcm)return decode(index,ok);
  if(!remaining&&looping){if(std::fseek(file,long(44+loopOffset),SEEK_SET)){ok=false;return 0;}remaining=total-loopOffset;}
  const unsigned bytes=std::min(remaining,bufferBytes());if(!bytes)return 0;
  if(std::fread(buffers[index],1,bytes,file)!=bytes){ok=false;remaining=0;return 0;}
  remaining-=bytes;DSP_FlushDataCache(buffers[index],bytes);return bytes;
 }
 // At least `bytes` undecoded bytes in raw, reading chunks as needed.
 bool have(size_t bytes){
  while(rawEnd-rawAt<bytes){
   if(fileEnded)return false;
   if(rawAt){std::memmove(raw.data(),raw.data()+rawAt,rawEnd-rawAt);rawEnd-=rawAt;rawAt=0;}
   if(raw.size()-rawEnd<AdpcmChunk)return false;
   const size_t got=std::fread(raw.data()+rawEnd,1,AdpcmChunk,file);
   rawEnd+=got;if(got<AdpcmChunk)fileEnded=true;
  }
  return true;
 }
 // Whole packets decoded into buffers[index] while they fit.
 unsigned decode(unsigned index,bool& ok){
  auto* out=static_cast<std::int16_t*>(buffers[index]);unsigned samples=0;
  while(remaining){
   if(!have(4)){ok=false;return 0;}
   const unsigned count=u16(&raw[rawAt]),bytes=u16(&raw[rawAt+2]),size=madpPacketBytes(channels,bytes);
   if(!count||count>remaining||count>SamplesPerBuffer||bytes<(count+13)/14*8||size>raw.size()-AdpcmChunk){ok=false;remaining=0;return 0;}
   if(samples+count>SamplesPerBuffer)break;
   if(!have(size)){ok=false;remaining=0;return 0;}
   const unsigned char* packet=&raw[rawAt+4];
   for(unsigned c=0;c<channels;++c){
    const unsigned char* h=packet+c*MadpChannelHeaderBytes;std::int16_t coefficients[16];
    for(unsigned k=0;k<16;++k)coefficients[k]=std::int16_t(u16(h+k*2));
    decodeDspAdpcm(packet+channels*MadpChannelHeaderBytes+c*bytes,count,coefficients,std::int16_t(u16(h+32)),std::int16_t(u16(h+34)),
                   out+size_t(samples)*channels+c,channels);
   }
   rawAt+=size;samples+=count;remaining-=count;
  }
  const unsigned written=samples*channels*2;
  if(written)DSP_FlushDataCache(buffers[index],written);
  return written;
 }
};
struct OpenJob final:AssetJob {
 std::shared_ptr<Stream> stream=std::make_shared<Stream>();std::string path;unsigned loopStart=0;
 bool ok=false;std::array<unsigned,3> filled{};
 // MADP stream: the header is checked, then playback reads from the start.
 bool openAdpcm(Stream& s){
  s.raw.resize(2*AdpcmChunk);
  if(!s.have(MadpHeaderBytes))return false;
  const unsigned char* h=s.raw.data();
  if(std::memcmp(h,"MADP",4)||u32(h+4)!=1||u32(h+24)!=MadpHeaderBytes)return false;
  s.channels=u16(h+8);s.rate=u32(h+12);s.remaining=s.total=u32(h+16);
  if((s.channels!=1&&s.channels!=2)||s.rate<8000||s.rate>48000||!s.total||looping())return false;
  s.adpcm=true;s.rawAt=MadpHeaderBytes;
  for(auto& buffer:s.buffers)if(!(buffer=linearAlloc(s.bufferBytes())))return false;
  for(unsigned i=0;i<s.buffers.size();++i){bool good;filled[i]=s.fill(i,good);if(!good)return false;}
  return true;
 }
 bool looping()const{return stream->looping;}
 void run()override{
  auto& s=*stream;s.file=std::fopen(path.c_str(),"rb");
  // An SD folder from before MADP has the decoded WAV beside it.
  if(!s.file&&path.size()>4&&!path.compare(path.size()-4,4,".adp"))s.file=std::fopen((path.substr(0,path.size()-4)+".wav").c_str(),"rb");
  if(!s.file)return;
  std::setvbuf(s.file,nullptr,_IONBF,0);
  unsigned char h[44];
  if(std::fread(h,1,4,s.file)!=4)return;
  if(!std::memcmp(h,"MADP",4)){ok=!std::fseek(s.file,0,SEEK_SET)&&openAdpcm(s);return;}
  if(std::fread(h+4,1,sizeof h-4,s.file)!=sizeof h-4||std::memcmp(h,"RIFF",4)||std::memcmp(h+8,"WAVEfmt ",8)||u32(h+16)!=16||u16(h+20)!=1||u16(h+34)!=16||std::memcmp(h+36,"data",4))return;
  s.channels=u16(h+22);s.rate=u32(h+24);s.remaining=s.total=u32(h+40);
  if((s.channels!=1&&s.channels!=2)||s.rate<8000||s.rate>48000||!s.remaining||s.remaining%(s.channels*2)||u16(h+32)!=s.channels*2||u32(h+28)!=s.rate*s.channels*2)return;
  if(loopStart>=s.total/(s.channels*2))return;
  s.loopOffset=loopStart*s.channels*2;
  if(std::fseek(s.file,0,SEEK_END)||std::ftell(s.file)!=long(44ull+s.remaining)||std::fseek(s.file,44,SEEK_SET))return;
  for(auto& buffer:s.buffers)if(!(buffer=linearAlloc(s.bufferBytes())))return;
  for(unsigned i=0;i<s.buffers.size();++i){bool good;filled[i]=s.fill(i,good);if(!good)return;}
  ok=true;
 }
};
struct FillJob final:AssetJob {
 std::shared_ptr<Stream> stream;unsigned index=0,bytes=0;bool ok=false;
 void run()override{bytes=stream->fill(index,ok);}
};
}
struct MenuMovieAudio::Impl {
 std::shared_ptr<Stream> stream,retired;
 std::shared_ptr<OpenJob> opening;
 std::array<ndspWaveBuf,3> waves{};
 std::array<std::shared_ptr<FillJob>,3> fills;
 unsigned nextRead=0,nextQueue=0;
 float volume=1;bool dsp=false,failed=false,ended=false;
 // NDSP channel: 0 when the sound system is not running, otherwise one it
 // lends us while this stream plays (sound_system.hpp reserveChannel).
 int channel=-1;bool borrowed=false;
};
MenuMovieAudio::MenuMovieAudio():impl_(std::make_unique<Impl>()){}
MenuMovieAudio::~MenuMovieAudio(){close();impl_->retired.reset();}
void MenuMovieAudio::initialize(){
 // One-time DSP component load, done at boot rather than on a page change.
 if(dspState)return;
 const Result result=ndspInit();dspState=R_SUCCEEDED(result)?1:-1;
 // Follow the System Settings sound mode (Mono/Stereo/Surround).
 if(R_SUCCEEDED(cfguInit())){u8 mode=0;if(R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof mode,0x70001,&mode))&&mode<=NDSP_OUTPUT_SURROUND)systemOutput=mode;cfguExit();}
 if(dspState>0)ndspSetOutputMode(ndspOutputMode(systemOutput));
#ifndef MKW_RELEASE
 // A failed DSP bootstrap disables all audio. Keep its result available in
 // release test builds, without logging audio streams or writing per frame.
 if(auto* log=std::fopen("sdmc:/3ds/mkw-3ds/audio-status.txt","w")){
  std::fprintf(log,"ndspInit=0x%08lx\nready=%s\noutput=%d\nfirmware=sdmc:/3ds/dspfirm.cdc\n",
      static_cast<unsigned long>(result),dspState>0?"true":"false",systemOutput);
  std::fclose(log);
 }
#endif
#ifndef MKW_RELEASE
 if(dspState<0)std::fprintf(stderr,"Opening audio unavailable: NDSP initialization failed (0x%08lx).\n",static_cast<unsigned long>(result));
#endif
}
bool MenuMovieAudio::dspReady(){return dspState>0;}
int MenuMovieAudio::outputMode(){return systemOutput;}
void MenuMovieAudio::close(){
 auto& s=*impl_;
 if(s.dsp){ndspChnWaveBufClear(s.channel);ndspChnReset(s.channel);s.dsp=false;}
 if(s.borrowed){SoundSystem::instance().releaseChannel(s.channel);s.borrowed=false;}
 s.channel=-1;
 if(s.opening){s.opening->cancel();s.opening.reset();}
 for(auto& fill:s.fills)if(fill){fill->cancel();fill.reset();}
 // The DSP may finish its current frame from these buffers: free them on a
 // later call, never in the same instant the channel is cleared.
 if(s.stream)s.retired=std::move(s.stream);
 s.waves={};s.nextRead=s.nextQueue=0;s.failed=false;s.ended=false;
}
bool MenuMovieAudio::open(const char* path,bool loop,unsigned loopStartSample){
 close();auto& s=*impl_;
 initialize();if(dspState<0)return false;
 auto job=std::make_shared<OpenJob>();job->path=path;job->loopStart=loopStartSample;job->stream->looping=loop;
 s.opening=job;s.stream=job->stream;
 AssetLoader::instance().submit(job,AssetPriority::Realtime);
 return true;
}
void MenuMovieAudio::update(){
 auto& s=*impl_;s.retired.reset();
 if(s.opening){
  if(!s.opening->done())return;
  if(s.channel<0){
   if(!SoundSystem::instance().available())s.channel=0;
   else{const int c=SoundSystem::instance().reserveChannel();if(c<0)return;s.channel=c;s.borrowed=true;}
  }
  auto job=std::move(s.opening);
  if(!job->ok){s.failed=true;s.stream.reset();return;}
  auto& stream=*s.stream;
  const int c=s.channel;
  ndspChnReset(c);ndspSetOutputMode(ndspOutputMode(systemOutput));
  ndspChnSetInterp(c,NDSP_INTERP_LINEAR);ndspChnSetRate(c,float(stream.rate));
  ndspChnSetFormat(c,stream.channels==1?NDSP_FORMAT_MONO_PCM16:NDSP_FORMAT_STEREO_PCM16);
  movieMix(c,s.volume);
  s.dsp=true;
  for(unsigned i=0;i<s.waves.size();++i){
   auto& wave=s.waves[i];wave={};wave.data_vaddr=stream.buffers[i];wave.status=NDSP_WBUF_FREE;
   if(job->filled[i]){wave.nsamples=job->filled[i]/(stream.channels*2);ndspChnWaveBufAdd(c,&wave);}
   else s.ended=true;
  }
  return;
 }
 if(!s.dsp||s.failed||!s.stream)return;
 // Recycle in playback order, not array-index order. When a slow frame
 // spans the ring wrap, scanning 0,1,2 could submit a later PCM chunk first.
 for(unsigned n=0;n<s.waves.size()&&!s.ended;++n){
  const unsigned i=s.nextRead;auto& wave=s.waves[i];
  if(s.fills[i]||(wave.status!=NDSP_WBUF_DONE&&wave.status!=NDSP_WBUF_FREE))break;
  auto fill=std::make_shared<FillJob>();fill->stream=s.stream;fill->index=i;s.fills[i]=fill;
  AssetLoader::instance().submit(fill,AssetPriority::Realtime);
  s.nextRead=(i+1)%s.waves.size();
 }
 for(unsigned n=0;n<s.waves.size();++n){
  const unsigned i=s.nextQueue;auto& fill=s.fills[i];
  if(!fill||!fill->done())break;
  auto job=std::move(fill);
  if(!job->ok){s.failed=true;return;}
  if(!job->bytes)s.ended=true;
  else{auto& wave=s.waves[i];wave.nsamples=job->bytes/(s.stream->channels*2);wave.looping=false;ndspChnWaveBufAdd(s.channel,&wave);}
  s.nextQueue=(i+1)%s.waves.size();
 }
}
void MenuMovieAudio::setVolume(float gain){
 impl_->volume=gain;if(!impl_->dsp)return;
 movieMix(impl_->channel,gain);
}
bool MenuMovieAudio::ready()const{return impl_->dsp&&!impl_->failed;}
bool MenuMovieAudio::finished()const{
 const auto& s=*impl_;if(!s.dsp||!s.ended||(s.stream&&s.stream->looping))return false;
 for(const auto& fill:s.fills)if(fill)return false;
 for(const auto& wave:s.waves)if(wave.status==NDSP_WBUF_PLAYING||wave.status==NDSP_WBUF_QUEUED)return false;
 return true;
}
void MenuMovieAudio::shutdown(){if(dspState>0)ndspExit();dspState=0;}
}

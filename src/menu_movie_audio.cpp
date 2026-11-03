#include <mkw/menu_movie_audio.hpp>
#include <mkw/asset_loader.hpp>
#include <mkw/sound_system.hpp>
#include <3ds.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace mkw {
namespace {
unsigned u16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
constexpr unsigned SamplesPerBuffer=4096;
int dspState=0; // 0 untried, 1 ready, -1 unavailable
// File and LINEAR sample buffers, shared with loader jobs. Jobs for one stream
// run in FIFO order on the single loader thread, so file access is serial.
struct Stream {
 std::FILE* file=nullptr;
 unsigned channels=0,rate=0,total=0,remaining=0,loopOffset=0;bool looping=false;
 std::array<void*,3> buffers{};
 ~Stream(){for(auto* buffer:buffers)if(buffer)linearFree(buffer);if(file)std::fclose(file);}
 unsigned bufferBytes()const{return SamplesPerBuffer*channels*2;}
 // Reads the next chunk (looping as authored). Returns bytes, 0 at the end.
 unsigned fill(unsigned index,bool& ok){
  ok=true;
  if(!remaining&&looping){if(std::fseek(file,long(44+loopOffset),SEEK_SET)){ok=false;return 0;}remaining=total-loopOffset;}
  const unsigned bytes=std::min(remaining,bufferBytes());if(!bytes)return 0;
  if(std::fread(buffers[index],1,bytes,file)!=bytes){ok=false;remaining=0;return 0;}
  remaining-=bytes;DSP_FlushDataCache(buffers[index],bytes);return bytes;
 }
};
struct OpenJob final:AssetJob {
 std::shared_ptr<Stream> stream=std::make_shared<Stream>();std::string path;unsigned loopStart=0;
 bool ok=false;std::array<unsigned,3> filled{};
 void run()override{
  auto& s=*stream;s.file=std::fopen(path.c_str(),"rb");if(!s.file)return;
  unsigned char h[44];
  if(std::fread(h,1,sizeof h,s.file)!=sizeof h||std::memcmp(h,"RIFF",4)||std::memcmp(h+8,"WAVEfmt ",8)||u32(h+16)!=16||u16(h+20)!=1||u16(h+34)!=16||std::memcmp(h+36,"data",4))return;
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
#ifndef MKW_RELEASE
 // A failed DSP bootstrap disables all audio. Keep its result available in
 // release test builds, without logging audio streams or writing per frame.
 if(auto* log=std::fopen("sdmc:/3ds/mkw-3ds/audio-status.txt","w")){
  std::fprintf(log,"ndspInit=0x%08lx\nready=%s\nfirmware=sdmc:/3ds/dspfirm.cdc\n",
      static_cast<unsigned long>(result),dspState>0?"true":"false");
  std::fclose(log);
 }
#endif
#ifndef MKW_RELEASE
 if(dspState<0)std::fprintf(stderr,"Opening audio unavailable: NDSP initialization failed (0x%08lx).\n",static_cast<unsigned long>(result));
#endif
}
bool MenuMovieAudio::dspReady(){return dspState>0;}
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
  ndspChnReset(c);ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnSetInterp(c,NDSP_INTERP_LINEAR);ndspChnSetRate(c,float(stream.rate));
  ndspChnSetFormat(c,stream.channels==1?NDSP_FORMAT_MONO_PCM16:NDSP_FORMAT_STEREO_PCM16);
  float mix[12]={};mix[0]=mix[1]=std::clamp(s.volume,0.f,1.f);ndspChnSetMix(c,mix);
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
 float mix[12]={};mix[0]=mix[1]=std::clamp(gain,0.f,1.f);ndspChnSetMix(impl_->channel,mix);
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

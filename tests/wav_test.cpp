#include "audio_wav.h"
#include <assert.h>
#include <vector>
#include <string.h>
using Bytes = std::vector<uint8_t>;
static void u32(Bytes &b, unsigned n) { for (int i=0;i<4;i++) b.push_back(n >> (8*i)); }
static void chunk(Bytes &b, const char *id, Bytes payload) { b.insert(b.end(),id,id+4);u32(b,payload.size());b.insert(b.end(),payload.begin(),payload.end());if(payload.size()%2)b.push_back(0); }
static Bytes wav(bool metadata=false) {
 Bytes b={'R','I','F','F',0,0,0,0,'W','A','V','E'};
 Bytes fmt={1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0};
 if(metadata){chunk(b,"JUNK",{1,2,3});fmt.insert(fmt.end(),{0,0});}
 chunk(b,"fmt ",fmt);chunk(b,"data",{1,0,2,0,3,0});chunk(b,"LIST",{9,9,9,9});
 unsigned n=b.size()-8;for(int i=0;i<4;i++)b[4+i]=n>>(8*i);return b;
}
static bool parse(const Bytes &b, bool decode=false) {
 FILE *f=tmpfile();assert(f);if(!b.empty())fwrite(b.data(),1,b.size(),f);wav_instance w={};bool ok=is_wav(f,&w);
 if(ok&&decode){uint8_t samples[4];decode_data d={};d.samples=samples;d.samples_capacity=sizeof(samples);assert(decode_wav(f,&d,&w)==DECODE_STATUS_CONTINUE);assert(d.frame_count==2&&samples[0]==1&&samples[2]==2);assert(decode_wav(f,&d,&w)==DECODE_STATUS_CONTINUE);assert(d.frame_count==1&&samples[0]==3);assert(decode_wav(f,&d,&w)==DECODE_STATUS_DONE);}
 fclose(f);return ok;
}
int main(){
 auto base=wav();assert(parse(base,true));assert(parse(wav(true),true));
 for(size_t n=0;n<base.size();n++)assert(!parse(Bytes(base.begin(),base.begin()+n)));
 for(int pos:{0,8,20,22,32,34}){auto b=base;b[pos]=0;assert(!parse(b));}
 for(int pos:{4,16,40}){auto b=base;for(int i=0;i<4;i++)b[pos+i]=255;assert(!parse(b));}
 auto b=base;b[40]=5;assert(!parse(b)); // incomplete PCM frame
 b=base;b[24]=0;b[25]=0;assert(!parse(b));
 FILE *f=tmpfile();wav_instance w={};decode_data d={};assert(decode_wav(f,&d,&w)==DECODE_STATUS_ERROR);fclose(f);
 puts("WAV PCM16, extended fmt, odd metadata, truncation, invalid fields and data boundary PASS");
}

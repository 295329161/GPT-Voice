#include "audio_mp3.h"
#include <assert.h>
#include <string.h>
#include <vector>
static int valid_frames;
int MP3FindSyncWord(unsigned char *p,int n){for(int i=0;i+1<n;i++)if(p[i]==255&&(p[i+1]&224)==224)return i;return -1;}
int MP3Decode(HMP3Decoder,unsigned char **p,int *n,int16_t *,int){if(*n<4||(*p)[1]!=251)return ERR_MP3_INVALID_FRAMEHEADER;*p+=4;*n-=4;valid_frames++;return 0;}
void MP3GetLastFrameInfo(HMP3Decoder,MP3FrameInfo *f){*f={44100,16,1,2};}
static void run(const std::vector<unsigned char>& bytes){FILE *f=tmpfile();assert(f);fwrite(bytes.data(),1,bytes.size(),f);rewind(f);uint8_t buffer[16]={0},pcm[32]={0};mp3_instance instance={buffer,sizeof(buffer),0,buffer,false};decode_data output={};output.samples=pcm;valid_frames=0;int calls=0;for(;calls<100;calls++){auto result=decode_mp3(nullptr,f,&output,&instance);assert(instance.read_ptr>=buffer&&instance.read_ptr<=buffer+sizeof(buffer));if(result==DECODE_STATUS_DONE||result==DECODE_STATUS_ERROR)break;}assert(calls<100);assert(valid_frames==1);fclose(f);}
static bool detect(unsigned char second, unsigned char third) {
 FILE *f=tmpfile();unsigned char header[]={255,second,third,0};fwrite(header,1,4,f);bool result=is_mp3(f);assert(ftell(f)==0);fclose(f);return result;
}
int main(){
 for(unsigned char h:{0xfb,0xfa,0xf3,0xf2,0xe3,0xe2})assert(detect(h,0x90));
 for(unsigned char h:{0xff,0xf9,0xeb,0xf0})assert(!detect(h,0x90));
 assert(!detect(0xfb,0xfc));

 std::vector<unsigned char> bad(48,0);bad[0]=255;bad[1]=240;bad[6]=255;bad[7]=251;run(bad);
 std::vector<unsigned char> id3={'I','D','3',3,0,0,0,0,0,20};id3.resize(30,255);id3.insert(id3.end(),{255,251,0,0});run(id3);
 std::vector<unsigned char> split(15,0);split.insert(split.end(),{255,251,0,0});run(split);
 puts("MP3 invalid-header progress, ID3 skipping and split-sync recovery PASS");
}

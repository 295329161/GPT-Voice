#include "core/resampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int16_t input[24000],whole[36016],split[36016];
static void test(unsigned from,unsigned to){
    for(unsigned i=0;i<from;i++)input[i]=(int16_t)(12000*sin(2*3.141592653589793*440*i/from));
    resampler_t a,b;resampler_init(&a,from,to);resampler_init(&b,from,to);
    size_t total=resampler_process(&a,input,from,whole,36016),n=0;
    for(unsigned i=0;i<from;){unsigned k=(i%71)+1;if(k>from-i)k=from-i;n+=resampler_process(&b,input+i,k,split+n,36016-n);i+=k;}
    assert(total==to);assert(n==total);assert(!memcmp(whole,split,total*2));
    double energy=0;for(size_t i=64;i<total;i++)energy+=(double)whole[i]*whole[i];
    double rms=sqrt(energy/(total-64));assert(rms>8000&&rms<9000);
    printf("%u -> %u: sample count, chunk invariance and 440 Hz level PASS\n",from,to);
}
int main(void){test(16000,24000);test(24000,16000);return 0;}

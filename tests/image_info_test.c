#include "core/image_info.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void be32(unsigned char *p,unsigned v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
int main(void) {
 unsigned w,h;
 unsigned char png[33]={137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R'};
 be32(png+16,280);be32(png+20,140);assert(image_dimensions(png,sizeof(png),&w,&h)&&w==280&&h==140);
 for(unsigned n=0;n<33;n++)assert(!image_dimensions(png,n,&w,&h));
 be32(png+16,0);assert(!image_dimensions(png,sizeof(png),&w,&h));
 be32(png+16,65536+280);assert(!image_dimensions(png,sizeof(png),&w,&h));
 be32(png+16,2048);assert(!image_dimensions(png,sizeof(png),&w,&h));
 be32(png+16,1024);be32(png+20,1025);assert(!image_dimensions(png,sizeof(png),&w,&h));
 unsigned char jpeg[32]={255,216,255,224,0,4,0,0,255,192,0,8,8,0,100,0,240,1};
 assert(image_dimensions(jpeg,sizeof(jpeg),&w,&h)&&w==240&&h==100);
 jpeg[9]=194;assert(!image_dimensions(jpeg,sizeof(jpeg),&w,&h));jpeg[9]=192;
 jpeg[10]=255;assert(!image_dimensions(jpeg,sizeof(jpeg),&w,&h));
 assert(!image_dimensions(NULL,0,&w,&h));
 puts("PNG/JPEG dimension bounds, truncation, segment length and progressive rejection PASS");
}

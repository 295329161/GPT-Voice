#include "image_info.h"
#include <string.h>
static unsigned be16(const uint8_t *p) { return (unsigned)p[0]<<8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p)<<16 | be16(p+2); }
static bool dimensions(unsigned w,unsigned h,unsigned *width,unsigned *height) {
    if (!w || !h || w>2047 || h>2047 || (uint64_t)w*h>1048576) return false;
    *width=w;*height=h;return true;
}
bool image_dimensions(const uint8_t *p,size_t n,unsigned *w,unsigned *h) {
    if(!p || !w || !h || n<24 || n>1048576) return false;
    static const uint8_t png[]={137,80,78,71,13,10,26,10};
    if(!memcmp(p,png,8)) {
        if(n<33 || be32(p+8)!=13 || memcmp(p+12,"IHDR",4))return false;
        return dimensions(be32(p+16),be32(p+20),w,h);
    }
    if(p[0]!=255 || p[1]!=0xd8)return false;
    size_t pos=2;
    while(pos<n) {
        if(p[pos++]!=255)return false;
        while(pos<n && p[pos]==255)pos++;
        if(pos>=n)return false;
        unsigned marker=p[pos++];
        if(marker==0xd9 || marker==0xda)return false;
        if(marker==0x01 || (marker>=0xd0 && marker<=0xd7))continue;
        if(pos+2>n)return false;
        unsigned size=be16(p+pos);
        if(size<2 || size>n-pos)return false;
        if(marker==0xc0) {
            if(size<8 || p[pos+2]!=8)return false;
            return dimensions(be16(p+pos+5),be16(p+pos+3),w,h);
        }
        // Tiny JPEG decoder supports baseline sequential JPEG, not progressive.
        if(marker>=0xc1 && marker<=0xcf && marker!=0xc4 && marker!=0xc8 && marker!=0xcc)return false;
        pos+=size;
    }
    return false;
}

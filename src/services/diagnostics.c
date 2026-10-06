#include "core/terminal.h"
#include "diagnostic_images.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Local serial-only test data. Never overwrite an existing directory, format
// an SD card, or touch user media. All work runs in the storage job worker.
#define TEST_DIR "/sdcard/GPTVoice_Test_1_0"
static const char marker[] = "GPT Voice 1.0.0 diagnostic fixtures\n";
static const char *names[] = {"landscape.png", "portrait.PNG", "invalid.png", "oversize.png", "tone.wav", "invalid.wav", "photo.JPEG", ".owner"};
static bool write_file(const char *name, const void *data, size_t n) {
    char path[128];
    snprintf(path,sizeof(path),"%s/%s",TEST_DIR,name);
    FILE *f=fopen(path,"wb");
    if (!f) return false;
    // SDMMC DMA cannot read flash-mapped PNG data directly on this board.
    // Stage each chunk through RAM, then verify bytes after the write.
    unsigned char block[256];
    const unsigned char *source=data;
    bool ok=true;
    for(size_t offset=0;offset<n;offset+=sizeof(block)) {
        size_t count=n-offset<sizeof(block)?n-offset:sizeof(block);
        memcpy(block,source+offset,count);
        if(fwrite(block,1,count,f)!=count) {ok=false;break;}
    }
    ok=fclose(f)==0 && ok;
    f=fopen(path,"rb");
    if(!f)return false;
    for(size_t offset=0;offset<n;offset+=sizeof(block)) {
        size_t count=n-offset<sizeof(block)?n-offset:sizeof(block);
        if(fread(block,1,count,f)!=count || memcmp(block,source+offset,count)){ok=false;break;}
    }
    fclose(f);
    printf("FIXTURE %s verified=%d\n", name, ok);
    return ok;
}
void diagnostics_fixtures(bool create) {
    if (!state->mounted) { printf("FIXTURES SD not mounted\n"); return; }
    if (create) {
        if (mkdir(TEST_DIR,0775)) { printf("FIXTURES create refused: %s\n",strerror(errno)); return; }
        bool ok=write_file(".owner",marker,sizeof(marker));
        ok=write_file("landscape.png",landscape,sizeof(landscape)) && ok;
        ok=write_file("portrait.PNG",portrait,sizeof(portrait)) && ok;
        ok=write_file("photo.JPEG",jpeg,sizeof(jpeg)) && ok;
        ok=write_file("invalid.png","invalid",7) && ok;
        ok=write_file("invalid.wav","RIFF",4) && ok;
        FILE *f=fopen(TEST_DIR "/oversize.png","wb");
        if (f) { ok=fseek(f,1048576,SEEK_SET)==0 && ok; ok=fputc(0,f)!=EOF && ok; ok=fclose(f)==0 && ok; }
        else ok=false;
        // 2 seconds of quiet 16 kHz PCM16 mono sine, sufficient for decoder QA.
        unsigned char header[44]={'R','I','F','F',0x24,0xfa,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0,'d','a','t','a',0,0xfa,0,0};
        f=fopen(TEST_DIR "/tone.wav","wb");
        if (f) {
            ok=fwrite(header,1,sizeof(header),f)==sizeof(header) && ok;
            int16_t pcm[256];
            for (int block=0;block<125;block++) {
                for(int i=0;i<256;i++)pcm[i]=(int16_t)(1200*sinf(2*M_PI*440*(block*256+i)/16000));
                ok=fwrite(pcm,1,sizeof(pcm),f)==sizeof(pcm) && ok;
            }
            ok=fclose(f)==0 && ok;
        } else ok=false;
        printf("FIXTURES created=%d directory=%s\n",ok,TEST_DIR);
    } else {
        if (media_busy_path(TEST_DIR)) { printf("FIXTURES busy\n");return; }
        char owner[sizeof(marker)]={0};
        FILE *f=fopen(TEST_DIR "/.owner","rb");
        if (!f) { printf("FIXTURES owner missing; cleanup refused\n");return; }
        size_t n=fread(owner,1,sizeof(owner),f);fclose(f);
        if(n!=sizeof(marker)||memcmp(owner,marker,sizeof(marker))){printf("FIXTURES owner mismatch; cleanup refused\n");return;}
        // The exact allowlist prevents recursive deletion of unrelated files.
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
            char path[128];snprintf(path,sizeof(path),"%s/%s",TEST_DIR,names[i]);unlink(path);
        }
        printf("FIXTURES removed=%d\n",rmdir(TEST_DIR)==0);
    }
    terminal_submit(JOB_LIST, "/sdcard", NULL, 0);
}

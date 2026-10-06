/* Touch hardware workbench: all slow jobs run outside the LVGL task. */
#include "esp32_s3_szp.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern esp_err_t qmi8658_register_read(uint8_t, uint8_t *, size_t);
extern esp_err_t qmi8658_register_write_byte(uint8_t, uint8_t);
extern esp_err_t pca9557_register_write_byte(uint8_t, uint8_t);
extern esp_err_t demo_audio_capture(void *, size_t, size_t *);
extern esp_err_t demo_audio_raw_capture(void *, size_t, size_t *);
extern esp_err_t demo_audio_output(void *, size_t, size_t *);
extern sdmmc_card_t *sdmmc_card;

enum { HOME, AUDIO, WIFI, BLE, IMU, FILES, PAGES };
enum { RECORD=1, PLAY, TONE, SAVE, SCAN_WIFI, CONNECT_WIFI, SCAN_BLE, MOUNT, UNMOUNT, LIST, OPEN, UP, CREATE, RENAME, DELETE_FILE, AUDIO_DIAG, AUDIO_LOOP };
typedef struct { int op; char a[512], b[65]; } job_t;
static QueueHandle_t jobs;
static SemaphoreHandle_t state_lock;
static char status[PAGES][768];
static bool audio_ok, imu_ok, wifi_ok, ble_ok, mounted;
static int16_t *recording;
static size_t recording_bytes;
static int volume=70;
static lv_obj_t *volume_label;
static lv_font_t ui_font;
static lv_obj_t *tabs, *labels[PAGES], *file_list, *ssid, *password, *keyboard, *meter[2], *bubble;
static char current_dir[512]="/sdcard", selected[512];
static char file_names[48][256];
static char listed_dir[512];
static bool file_dirs[48];
static int file_count;
static unsigned listing_generation, ui_generation;
static uint32_t touch_count, boot_count;
static bool boot_previous=true;
static int peaks[2];
static float roll, pitch;
static bool ble_scanning;
static uint8_t seen[32][6];
static int seen_count;
static esp_netif_t *station;

static void note(int page, const char *fmt, ...) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    va_list args; va_start(args,fmt); vsnprintf(status[page],sizeof(status[page]),fmt,args); va_end(args);
    ESP_LOGI("demo", "page=%d %s", page,status[page]);
    xSemaphoreGive(state_lock);
}
static esp_err_t reg_write(uint8_t reg, uint8_t data) { return qmi8658_register_write_byte(reg,data); }
static bool probe(uint8_t addr) {
    i2c_cmd_handle_t c=i2c_cmd_link_create();
    if (!c) return false;
    i2c_master_start(c); i2c_master_write_byte(c,addr<<1,true); i2c_master_stop(c);
    esp_err_t e=i2c_master_cmd_begin(0,c,pdMS_TO_TICKS(100)); i2c_cmd_link_delete(c); return e==ESP_OK;
}
static void wifi_event(void *a, esp_event_base_t base, int32_t id, void *data) {
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e=data;
        note(WIFI,"CONNECTED\nIP: " IPSTR "\nDHCP passed",IP2STR(&e->ip_info.ip));
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e=data;
        note(WIFI,"Disconnected (reason %u)\nCheck password / signal, then Connect",e->reason);
    }
}
static void ble_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    if (event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        if (p->scan_param_cmpl.status==ESP_BT_STATUS_SUCCESS) {
            esp_err_t e=esp_ble_gap_start_scanning(8);
            if(e!=ESP_OK) { ble_scanning=false; note(BLE,"Scan start: %s",esp_err_to_name(e)); }
        } else { ble_scanning=false; note(BLE,"Scan configuration failed"); }
    } else if(event==ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT) {
            bool known=false;
            for(int i=0;i<seen_count;i++) if(!memcmp(seen[i],p->scan_rst.bda,6)) known=true;
            if(!known && seen_count<32) memcpy(seen[seen_count++],p->scan_rst.bda,6);
            uint8_t n=0; uint8_t *name=esp_ble_resolve_adv_data(p->scan_rst.ble_adv,ESP_BLE_AD_TYPE_NAME_CMPL,&n);
            note(BLE,"Scanning BLE | %d unique (max 32)\n%.*s\n%02X:%02X:%02X:%02X:%02X:%02X  %d dBm",seen_count,n,name?(char*)name:"",p->scan_rst.bda[0],p->scan_rst.bda[1],p->scan_rst.bda[2],p->scan_rst.bda[3],p->scan_rst.bda[4],p->scan_rst.bda[5],p->scan_rst.rssi);
        } else if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            ble_scanning=false; note(BLE,"Scan complete: %d unique devices\nBLE radio receive test %s\nNo Classic Bluetooth on ESP32-S3",seen_count,seen_count?"PASS":"inconclusive");
        }
    }
}
static esp_err_t init_radios(void) {
    esp_err_t e=nvs_flash_init();
    if(e!=ESP_OK) return e; /* Never erase existing NVS automatically. */
    ESP_RETURN_ON_ERROR(esp_netif_init(),"demo","netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(),"demo","events");
    station=esp_netif_create_default_wifi_sta();
    if(!station) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg),"demo","wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),"demo","storage");
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),"demo","mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(),"demo","start");
    wifi_ok=true;
    esp_bt_controller_config_t bt=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt),"demo","BLE controller");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE),"demo","BLE enable");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(),"demo","bluedroid");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(),"demo","bluedroid enable");
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(ble_event),"demo","BLE callback");
    ble_ok=true;
    return ESP_OK;
}
static bool make_path(char *out,size_t n,const char *dir,const char *name) {
    return snprintf(out,n,"%s/%s",dir,name)<n;
}
static void list_dir(void) {
    if(!mounted) { note(FILES,"SD not mounted. Insert FAT card, tap Mount."); return; }
    DIR *d=opendir(current_dir);
    if(!d) { note(FILES,"Open directory failed: %s",strerror(errno)); return; }
    xSemaphoreTake(state_lock,portMAX_DELAY);
    snprintf(listed_dir,sizeof(listed_dir),"%s",current_dir);
    file_count=0; struct dirent *e;
    while((e=readdir(d)) && file_count<48) {
        if(e->d_name[0]=='.') continue;
        char path[768]; struct stat st;
        if(!make_path(path,sizeof(path),current_dir,e->d_name) || stat(path,&st)) continue;
        snprintf(file_names[file_count],256,"%s",e->d_name); file_dirs[file_count]=S_ISDIR(st.st_mode); file_count++;
    }
    listing_generation++;
    xSemaphoreGive(state_lock); closedir(d);
    note(FILES,"%s\n%d entries (showing first 48)",current_dir,file_count);
}
static esp_err_t write_wav(const char *path) {
    /* Unique name, exclusive create: existing user data is never overwritten. */
    FILE *f=fopen(path,"wx"); if(!f) return ESP_FAIL;
    uint32_t size=recording_bytes, riff=size+36, rate=16000, bytes_sec=64000;
    uint16_t pcm=1, ch=2, align=4,bits=16; uint32_t fmt=16;
    bool ok=fwrite("RIFF",1,4,f)==4 && fwrite(&riff,4,1,f)==1 && fwrite("WAVEfmt ",1,8,f)==8 && fwrite(&fmt,4,1,f)==1 && fwrite(&pcm,2,1,f)==1 && fwrite(&ch,2,1,f)==1 && fwrite(&rate,4,1,f)==1 && fwrite(&bytes_sec,4,1,f)==1 && fwrite(&align,2,1,f)==1 && fwrite(&bits,2,1,f)==1 && fwrite("data",1,4,f)==4 && fwrite(&size,4,1,f)==1 && fwrite(recording,1,size,f)==size;
    if(fclose(f)) ok=false;
    return ok?ESP_OK:ESP_FAIL;
}
static void audio_diagnostics(void) {
    if (recording_bytes) {
        double sum[2]={0}, energy[2]={0}; int peak[2]={0}, nz[2]={0};
        size_t frames=recording_bytes/4;
        for(size_t i=0;i<frames*2;i++) { int v=recording[i]; int ch=i%2; sum[ch]+=v; energy[ch]+=(double)v*v; if(abs(v)>peak[ch])peak[ch]=abs(v); if(v)nz[ch]++; }
        for(int ch=0;ch<2;ch++) { double dc=sum[ch]/frames; double rms=sqrt(fmax(0,energy[ch]/frames-dc*dc));
            ESP_LOGI("audio_diag","ch=%d frames=%u nonzero=%d peak=%d dc=%.2f ac_rms=%.2f dBFS=%.1f",ch,(unsigned)frames,nz[ch],peak[ch],dc,rms,20*log10(fmax(rms,0.001)/32768));
        }
    }
    for(int r=0;r<=0x4c;r++) {
        if(r>0x23&&r<0x40)continue;
        uint8_t reg=r,v=0;esp_err_t e=i2c_master_write_read_device(0,0x41,&reg,1,&v,1,pdMS_TO_TICKS(100));
        ESP_LOGI("audio_diag","ES7210 %02x=%02x %s",r,v,esp_err_to_name(e));
    }
}
static void audio_loopback(void) {
    int16_t raw[1024],tone[512];
    for(int phase=0;phase<3;phase++) {
        int vol=phase==1?35:70;
        esp_err_t e=bsp_codec_volume_set(vol,NULL);
        if(e==ESP_OK)e=bsp_codec_mute_set(false);
        if(e!=ESP_OK){note(AUDIO,"Codec setup error: %s",esp_err_to_name(e));return;}
        pa_en(phase!=0);
        double energy[4]={0},sum[4]={0},cs[4]={0},sn[4]={0};size_t frames=0;
        size_t done=0;
        for(int k=0;k<8;k++)demo_audio_raw_capture(raw,sizeof(raw),&done);
        for(int k=0;k<96;k++) {
            for(int i=0;i<256;i++)tone[2*i]=tone[2*i+1]=phase?(int16_t)(5000*sinf(2*M_PI*440*(k*256+i)/16000.f)):0;
            e=demo_audio_output(tone,sizeof(tone),&done);if(e!=ESP_OK)break;
            e=demo_audio_raw_capture(raw,sizeof(raw),&done);if(e!=ESP_OK)break;
            if(k<12)continue;
            for(size_t i=0;i<done/8;i++,frames++)for(int ch=0;ch<4;ch++){
                int v=raw[4*i+ch];sum[ch]+=v;energy[ch]+=(double)v*v;
                cs[ch]+=v*cos(2*M_PI*440*frames/16000.0);sn[ch]+=v*sin(2*M_PI*440*frames/16000.0);
            }
        }
        uint8_t reg=1,pa=0;i2c_master_write_read_device(0,0x19,&reg,1,&pa,1,pdMS_TO_TICKS(100));
        ESP_LOGI("loopback","phase=%d volume=%d PA=0x%02x frames=%u err=%s",phase,vol,pa,(unsigned)frames,esp_err_to_name(e));
        if(frames)for(int ch=0;ch<4;ch++)ESP_LOGI("loopback","slot=%d rms=%.2f tone440=%.2f",ch,sqrt(fmax(0,energy[ch]/frames-pow(sum[ch]/frames,2))),2*hypot(cs[ch],sn[ch])/frames);
    }
    pa_en(0);bsp_codec_volume_set(volume,NULL);
    for(int r=0;r<=0x37;r++){uint8_t reg=r,v=0;if(i2c_master_write_read_device(0,0x18,&reg,1,&v,1,pdMS_TO_TICKS(100))==ESP_OK)ESP_LOGI("loopback","ES8311 %02x=%02x",r,v);}
    note(AUDIO,"DAC loopback complete\nSee serial signal measurements");
}
static void audio_job(int op) {
    if(!audio_ok) { note(AUDIO,"Audio initialization failed"); return; }
    if(op==SAVE) {
        if(!mounted || !recording_bytes) {note(AUDIO,"Record first; mount SD before saving");return;}
        char path[96]; snprintf(path,sizeof(path),"/sdcard/REC_%lld.wav",esp_timer_get_time()/1000);
        note(AUDIO,"Save %s\n%s",esp_err_to_name(write_wav(path)),path); return;
    }
    esp_err_t err=ESP_OK;
    if(op==RECORD) {
        pa_en(0); recording_bytes=0; note(AUDIO,"Recording 5 seconds...\nSpeak near each microphone");
        size_t target=16000*4*5;
        if(!recording) recording=heap_caps_malloc(target,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!recording) {note(AUDIO,"PSRAM allocation failed");return;}
        int16_t discard[512]; size_t done;
        demo_audio_capture(discard,sizeof(discard),&done);
        while(recording_bytes<target) {
            size_t chunk=target-recording_bytes; if(chunk>2048) chunk=2048;
            size_t got=0; err=demo_audio_capture((uint8_t*)recording+recording_bytes,chunk,&got);
            if(err!=ESP_OK || !got) { if(!got) err=ESP_ERR_TIMEOUT; break; }
            int p[2]={0}; int16_t *s=(int16_t*)((uint8_t*)recording+recording_bytes);
            for(size_t i=0;i<got/2;i++) {int v=abs(s[i]); if(v>p[i%2])p[i%2]=v;}
            xSemaphoreTake(state_lock,portMAX_DELAY); peaks[0]=p[0];peaks[1]=p[1];xSemaphoreGive(state_lock);
            recording_bytes+=got;
        }
        if(err!=ESP_OK) recording_bytes=0;
        if(err!=ESP_OK) note(AUDIO,"Record failed: %s",esp_err_to_name(err));
        else {
            double energy[2]={0}; int peak[2]={0};
            for(size_t i=0;i<recording_bytes/2;i++){int v=recording[i];energy[i%2]+=(double)v*v;if(abs(v)>peak[i%2])peak[i%2]=abs(v);}
            double rms[2]={sqrt(energy[0]/(recording_bytes/4)),sqrt(energy[1]/(recording_bytes/4))};
            note(AUDIO,"%s | L %.0f / R %.0f dBFS\n%s",peak[0]&&peak[1]?"Captured":"SILENT CHANNEL",20*log10(fmax(rms[0],.001)/32768),20*log10(fmax(rms[1],.001)/32768),peak[0]&&peak[1]?"Tap Play to check sound":"No signal: check audio path");
            ESP_LOGI("audio_diag","record peaks L=%d R=%d rms L=%.2f R=%.2f",peak[0],peak[1],rms[0],rms[1]);
        }
    } else {
        if(op==PLAY && !recording_bytes) {note(AUDIO,"No recording yet");return;}
        err=bsp_codec_volume_set(volume,NULL);
        if(err==ESP_OK)err=bsp_codec_mute_set(volume==0);
        if(err!=ESP_OK){note(AUDIO,"Output setup: %s",esp_err_to_name(err));return;}
        double dc[2]={0}, gain=1;
        if(op==PLAY){
            size_t frames=recording_bytes/4;
            for(size_t i=0;i<frames*2;i++)dc[i%2]+=recording[i];
            dc[0]/=frames;dc[1]/=frames;
            double peak=0;
            for(size_t i=0;i<frames*2;i++)peak=fmax(peak,fabs(recording[i]-dc[i%2]));
            if(peak>=8)gain=fmin(32.0,12000.0/peak);
        }
        ESP_LOGI("audio_play","volume=%d boost=%.1f dB format=32-bit slots",volume,20*log10(gain));
        pa_en(volume>0); vTaskDelay(pdMS_TO_TICKS(20));
        note(AUDIO,op==PLAY?"Playing | level adjusted":"Playing 440 Hz test tone...");
        size_t total=op==PLAY?recording_bytes:16000*4;
        int16_t tone[512];
        for(size_t off=0;off<total;) {
            size_t n=total-off;if(n>sizeof(tone))n=sizeof(tone);
            void *buf=op==PLAY?(uint8_t*)recording+off:(uint8_t*)tone;
            if(op==TONE) {for(size_t i=0;i<n/4;i++)tone[2*i]=tone[2*i+1]=(int16_t)(5000*sinf(2*M_PI*440*(off/4+i)/16000.0f));buf=tone;}
            else {const int16_t *in=buf;for(size_t i=0;i<n/2;i++)tone[i]=(int16_t)fmax(-32768,fmin(32767,(in[i]-dc[i%2])*gain));buf=tone;}
            size_t done=0;err=demo_audio_output(buf,n,&done);if(err!=ESP_OK||!done){if(!done)err=ESP_ERR_TIMEOUT;break;}off+=done;
        }
        vTaskDelay(pdMS_TO_TICKS(80));pa_en(0);note(AUDIO,"Playback: %s\nConfirm audible sound by listening",esp_err_to_name(err));
    }
}
static void run_job(job_t *j) {
    if(j->op==AUDIO_LOOP){audio_loopback();return;}
    if(j->op==AUDIO_DIAG){audio_diagnostics();return;}
    if(j->op<=SAVE) {audio_job(j->op);return;}
    if(j->op==SCAN_WIFI) {
        if(!wifi_ok){note(WIFI,"Wi-Fi unavailable");return;}
        note(WIFI,"Scanning 2.4 GHz networks...");
        esp_err_t e=esp_wifi_scan_start(NULL,true);
        if(e!=ESP_OK){note(WIFI,"Scan: %s",esp_err_to_name(e));return;}
        wifi_ap_record_t ap[8];uint16_t n=8;e=esp_wifi_scan_get_ap_records(&n,ap);
        char text[700]="";size_t used=0;
        if(e!=ESP_OK){note(WIFI,"Results: %s",esp_err_to_name(e));return;}
        for(int i=0;i<n;i++)used+=snprintf(text+used,sizeof(text)-used,"%.32s  %d dBm\n",ap[i].ssid,ap[i].rssi);
        note(WIFI,"%u networks (top 8)\n%s",n,text);return;
    }
    if(j->op==CONNECT_WIFI) {
        if(!wifi_ok){note(WIFI,"Wi-Fi unavailable");return;}
        size_t n=strlen(j->a),p=strlen(j->b);
        if(!n||n>32||p>63){note(WIFI,"SSID 1-32 bytes, password 0-63 bytes");return;}
        wifi_config_t c={0};memcpy(c.sta.ssid,j->a,n);memcpy(c.sta.password,j->b,p);
        esp_wifi_disconnect();esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&c);
        if(e==ESP_OK) { e=esp_wifi_connect(); }
        memset(&c,0,sizeof(c));memset(j->b,0,sizeof(j->b));
        note(WIFI,"Connect request: %s\nWaiting for DHCP (check status)",esp_err_to_name(e));return;
    }
    if(j->op==SCAN_BLE) {
        if(!ble_ok){note(BLE,"BLE unavailable");return;}if(ble_scanning)return;
        seen_count=0;ble_scanning=true;
        esp_ble_scan_params_t p={.scan_type=BLE_SCAN_TYPE_ACTIVE,.own_addr_type=BLE_ADDR_TYPE_PUBLIC,.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL,.scan_interval=0x50,.scan_window=0x30,.scan_duplicate=BLE_SCAN_DUPLICATE_ENABLE};
        esp_err_t e=esp_ble_gap_set_scan_params(&p);
        if(e!=ESP_OK)ble_scanning=false;
        note(BLE,"8 second scan: %s",esp_err_to_name(e));return;
    }
    if(j->op==MOUNT) {
        if(!mounted) {esp_err_t e=bsp_sdcard_mount();mounted=e==ESP_OK;if(!mounted){note(FILES,"Mount: %s\nNo formatting performed",esp_err_to_name(e));return;}}
        strcpy(current_dir,"/sdcard");list_dir();return;
    }
    if(j->op==UNMOUNT) {
        if(!mounted) return;
        esp_err_t e=bsp_sdcard_unmount();
        if(e==ESP_OK){mounted=false;sdmmc_card=NULL;xSemaphoreTake(state_lock,portMAX_DELAY);file_count=0;listing_generation++;xSemaphoreGive(state_lock);}
        note(FILES,"Unmount: %s",esp_err_to_name(e));return;
    }
    if(!mounted){note(FILES,"Mount SD first");return;}
    if(j->op==LIST){list_dir();return;}
    if(j->op==UP){char *p=strrchr(current_dir,'/');if(p && p>current_dir+7)*p=0;list_dir();return;}
    if(j->op==OPEN){
        struct stat s;if(stat(j->a,&s)){note(FILES,"Stat: %s",strerror(errno));return;}
        if(S_ISDIR(s.st_mode)){snprintf(current_dir,sizeof(current_dir),"%s",j->a);list_dir();return;}
        FILE *f=fopen(j->a,"rb");if(!f){note(FILES,"Read: %s",strerror(errno));return;}
        char b[97];size_t n=fread(b,1,96,f);bool ok=!ferror(f);fclose(f);
        for(int i=0;i<n;i++) { if((unsigned char)b[i]<32 || (unsigned char)b[i]>126)b[i]='.'; }
        b[n]=0;
        note(FILES,"%lld bytes | read %s\n%s",(long long)s.st_size,ok?"OK":"FAILED",b);return;
    }
    if(j->op==CREATE){
        char path[128];snprintf(path,sizeof(path),"/sdcard/DEMO_%lld.txt",esp_timer_get_time()/1000);
        FILE *f=fopen(path,"wx");bool ok=false;
        if(f){const char data[]="ESP32-S3 SD read/write verification\n";ok=fwrite(data,1,sizeof(data)-1,f)==sizeof(data)-1;if(fclose(f))ok=false;
            if(ok){char b[sizeof(data)]={0};f=fopen(path,"rb");if(!f)ok=false;else{ok=fread(b,1,sizeof(data)-1,f)==sizeof(data)-1&&!memcmp(b,data,sizeof(data)-1);fclose(f);}}}
        list_dir();note(FILES,"Create + readback: %s\n%s",ok?"PASS":"FAIL",path);return;
    }
    /* Mutations are intentionally restricted to test-created root files. */
    const char *base=strrchr(j->a,'/');
    if(!base || (base-j->a)!=7 || strncmp(j->a,"/sdcard/",8) || (strncmp(base+1,"DEMO_",5)&&strncmp(base+1,"REC_",4))) {note(FILES,"Rename/delete only DEMO_ / REC_ test files");return;}
    struct stat st;if(stat(j->a,&st)||!S_ISREG(st.st_mode)){note(FILES,"Select a regular test file");return;}
    int result=-1;
    if(j->op==DELETE_FILE)result=unlink(j->a);
    if(j->op==RENAME){char path[560];snprintf(path,sizeof(path),"%s.renamed",j->a);if(stat(path,&st)==0){note(FILES,"Target already exists");return;}result=rename(j->a,path);}
    int error=errno;list_dir();note(FILES,"%s: %s",j->op==RENAME?"Rename":"Delete",result==0?"OK":strerror(error));
}
static void worker(void *arg) {job_t j;for(;;)if(xQueueReceive(jobs,&j,portMAX_DELAY))run_job(&j);}
static void enqueue(int op,const char *a,const char *b) {
    job_t j={.op=op};if(a)snprintf(j.a,sizeof(j.a),"%s",a);if(b)snprintf(j.b,sizeof(j.b),"%s",b);
    if(xQueueSend(jobs,&j,0)!=pdTRUE)note(HOME,"Busy: job queue full; try again shortly");
}
static lv_obj_t *text(lv_obj_t *p,const char *s,int x,int y,int width) {
    lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);return o;
}
static void click(lv_event_t *e){int op=(intptr_t)lv_event_get_user_data(e);touch_count++;enqueue(op,op==CONNECT_WIFI?lv_textarea_get_text(ssid):selected,op==CONNECT_WIFI?lv_textarea_get_text(password):NULL);}
static lv_obj_t *button(lv_obj_t *p,const char *s,int x,int y,int w,int op) {
    lv_obj_t *o=lv_btn_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,32);lv_obj_set_style_radius(o,10,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x24445b),0);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,s);lv_obj_center(l);if(op)lv_obj_add_event_cb(o,click,LV_EVENT_CLICKED,(void*)(intptr_t)op);return o;
}
static void choose(lv_event_t *e) {
    int i=(intptr_t)lv_event_get_user_data(e);touch_count++;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if(i<file_count)make_path(selected,sizeof(selected),listed_dir,file_names[i]);
    xSemaphoreGive(state_lock);note(FILES,"Selected:\n%s\nTap Open to read / enter",selected);
}
static void delete_confirm(lv_event_t *e) {
    lv_obj_t *box=lv_event_get_current_target(e);
    if(lv_msgbox_get_active_btn(box)==0)enqueue(DELETE_FILE,selected,NULL);
    lv_msgbox_close(box);
}
static void ask_delete(lv_event_t *e) {
    static const char *options[]={"Delete","Cancel",""};
    lv_obj_t *box=lv_msgbox_create(NULL,"Delete test file?",selected,options,true);lv_obj_set_width(box,290);lv_obj_center(box);lv_obj_add_event_cb(box,delete_confirm,LV_EVENT_VALUE_CHANGED,NULL);
}
static void edit(lv_event_t *e) {lv_keyboard_set_textarea(keyboard,lv_event_get_target(e));lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);}
static void keyboard_done(lv_event_t *e) {lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);}
static void set_volume(lv_event_t *e){volume=lv_slider_get_value(lv_event_get_target(e));lv_label_set_text_fmt(volume_label,"Volume %d",volume);}
static void brightness(lv_event_t *e){bsp_display_brightness_set(lv_slider_get_value(lv_event_get_target(e)));}
static void nav(lv_event_t *e){int n=lv_tabview_get_tab_act(tabs)+(intptr_t)lv_event_get_user_data(e);lv_tabview_set_act(tabs,(n+PAGES)%PAGES,LV_ANIM_ON);touch_count++;}
static void refresh(lv_timer_t *t) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    for(int i=1;i<PAGES;i++)lv_label_set_text(labels[i],status[i]);
    for(int i=0;i<2;i++)lv_bar_set_value(meter[i],(int)fmax(0,fmin(100,(20*log10(fmax(peaks[i],1)/32768.0)+60)*100/60)),LV_ANIM_ON);
    lv_obj_set_pos(bubble,235+(int)(fmaxf(-35,fminf(35,roll))*0.8f),92+(int)(fmaxf(-35,fminf(35,pitch))*0.8f));
    if(ui_generation!=listing_generation){
        lv_obj_clean(file_list);selected[0]=0;
        for(int i=0;i<file_count;i++){lv_obj_t *b=lv_list_add_btn(file_list,file_dirs[i]?LV_SYMBOL_DIRECTORY:LV_SYMBOL_FILE,file_names[i]);lv_obj_set_style_text_font(b,&ui_font,0);lv_obj_add_event_cb(b,choose,LV_EVENT_CLICKED,(void*)(intptr_t)i);}
        ui_generation=listing_generation;
    }
    xSemaphoreGive(state_lock);
    char home_text[256];
    snprintf(home_text,sizeof(home_text),"ESP32-S3 / 16 MB Flash\nPSRAM %.1f MB | Heap %lu KB\nTouch %lu | BOOT %lu\n%lld s | Camera skipped",esp_psram_get_size()/1048576.0,(unsigned long)esp_get_free_heap_size()/1024,(unsigned long)touch_count,(unsigned long)boot_count,esp_timer_get_time()/1000000);
    lv_label_set_text(labels[HOME],home_text);
}
static void make_ui(void) {
    ui_font=lv_font_montserrat_16;ui_font.fallback=&lv_font_simsun_16_cjk;
    lv_obj_t *screen=lv_scr_act();lv_obj_set_style_bg_color(screen,lv_color_hex(0x0b1422),0);lv_obj_set_style_text_color(screen,lv_color_hex(0xe8eff7),0);
    lv_obj_t *title=text(screen,"HARDWARE / LAB",14,9,240);lv_obj_set_style_text_color(title,lv_color_hex(0x57dfc5),0);lv_obj_set_style_text_font(title,&lv_font_montserrat_16,0);
    tabs=lv_tabview_create(screen,LV_DIR_BOTTOM,27);lv_obj_set_pos(tabs,0,31);lv_obj_set_size(tabs,320,209);lv_obj_set_style_bg_color(tabs,lv_color_hex(0x0b1422),0);
    const char *names[]={"SYS","MIC","WiFi","BLE","IMU","SD"};
    lv_obj_t *pages[PAGES];
    for(int i=0;i<PAGES;i++){
        pages[i]=lv_tabview_add_tab(tabs,names[i]);lv_obj_set_style_pad_all(pages[i],8,0);lv_obj_set_style_text_color(pages[i],lv_color_hex(0xe8eff7),0);
        labels[i]=text(pages[i],"Ready",2,1,294);lv_obj_set_style_text_font(labels[i],&ui_font,0);
    }
    lv_obj_t *tab_buttons=lv_tabview_get_tab_btns(tabs);
    lv_obj_set_style_bg_color(tab_buttons,lv_color_hex(0x142237),0);
    lv_obj_set_style_text_color(tab_buttons,lv_color_hex(0xc5d6e6),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_buttons,lv_color_hex(0x24445b),LV_PART_ITEMS|LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_buttons,lv_color_hex(0x57dfc5),LV_PART_ITEMS|LV_STATE_CHECKED);
    lv_obj_t *prev=button(screen,LV_SYMBOL_LEFT,254,1,29,0),*next=button(screen,LV_SYMBOL_RIGHT,287,1,29,0);
    lv_obj_add_event_cb(prev,nav,LV_EVENT_CLICKED,(void*)(intptr_t)-1);lv_obj_add_event_cb(next,nav,LV_EVENT_CLICKED,(void*)(intptr_t)1);
    text(pages[HOME],"Backlight",4,103,90);lv_obj_t *sl=lv_slider_create(pages[HOME]);lv_obj_set_pos(sl,105,110);lv_obj_set_size(sl,180,8);lv_slider_set_range(sl,10,100);lv_slider_set_value(sl,80,LV_ANIM_OFF);lv_obj_add_event_cb(sl,brightness,LV_EVENT_VALUE_CHANGED,NULL);
    text(pages[HOME],"Swipe pages / tap tabs / use arrows",4,142,294);
    lv_obj_set_height(labels[AUDIO],45);
    for(int i=0;i<2;i++){text(pages[AUDIO],i?"R":"L",3,49+i*18,20);meter[i]=lv_bar_create(pages[AUDIO]);lv_obj_set_pos(meter[i],24,52+i*18);lv_obj_set_size(meter[i],263,8);lv_obj_set_style_bg_color(meter[i],lv_color_hex(0x57dfc5),LV_PART_INDICATOR);}
    button(pages[AUDIO],"Record 5s",0,88,99,RECORD);button(pages[AUDIO],"Play",104,88,86,PLAY);button(pages[AUDIO],"Tone",195,88,98,TONE);button(pages[AUDIO],"Save WAV",0,130,99,SAVE);
    sl=lv_slider_create(pages[AUDIO]);lv_obj_set_pos(sl,123,144);lv_obj_set_size(sl,164,8);lv_slider_set_range(sl,0,90);lv_slider_set_value(sl,volume,LV_ANIM_OFF);lv_obj_add_event_cb(sl,set_volume,LV_EVENT_VALUE_CHANGED,NULL);
    volume_label=text(pages[AUDIO],"Volume 70",123,122,165);lv_obj_set_style_text_font(volume_label,&lv_font_montserrat_14,0);
    lv_obj_set_pos(labels[WIFI],2,43);lv_obj_set_height(labels[WIFI],300);button(pages[WIFI],"Scan",0,0,92,SCAN_WIFI);button(pages[WIFI],"Connect",100,0,108,CONNECT_WIFI);
    ssid=lv_textarea_create(pages[WIFI]);lv_obj_set_pos(ssid,0,350);lv_obj_set_size(ssid,294,38);lv_textarea_set_one_line(ssid,true);lv_textarea_set_placeholder_text(ssid,"SSID (2.4 GHz)");lv_textarea_set_max_length(ssid,32);lv_obj_add_event_cb(ssid,edit,LV_EVENT_FOCUSED,NULL);
    password=lv_textarea_create(pages[WIFI]);lv_obj_set_pos(password,0,396);lv_obj_set_size(password,294,38);lv_textarea_set_one_line(password,true);lv_textarea_set_placeholder_text(password,"Password / empty for open AP");lv_textarea_set_password_mode(password,true);lv_textarea_set_max_length(password,63);lv_obj_add_event_cb(password,edit,LV_EVENT_FOCUSED,NULL);
    button(pages[BLE],"Scan BLE (8s)",0,133,175,SCAN_BLE);
    lv_obj_set_width(labels[IMU],211);lv_obj_t *ring=lv_obj_create(pages[IMU]);lv_obj_set_pos(ring,214,66);lv_obj_set_size(ring,78,78);lv_obj_set_style_radius(ring,39,0);lv_obj_set_style_bg_color(ring,lv_color_hex(0x24445b),0);
    bubble=lv_obj_create(pages[IMU]);lv_obj_set_size(bubble,15,15);lv_obj_set_style_radius(bubble,8,0);lv_obj_set_style_bg_color(bubble,lv_color_hex(0x57dfc5),0);
    lv_obj_set_height(labels[FILES],57);
    button(pages[FILES],"Mount",0,62,70,MOUNT);button(pages[FILES],"Eject",75,62,65,UNMOUNT);button(pages[FILES],"Up",145,62,61,UP);button(pages[FILES],"Refresh",211,62,82,LIST);
    file_list=lv_list_create(pages[FILES]);lv_obj_set_pos(file_list,0,102);lv_obj_set_size(file_list,294,120);
    button(pages[FILES],"Open",0,232,65,OPEN);button(pages[FILES],"New test",70,232,93,CREATE);button(pages[FILES],"Rename",168,232,125,RENAME);
    lv_obj_t *del=button(pages[FILES],"Delete test file...",0,274,200,0);lv_obj_add_event_cb(del,ask_delete,LV_EVENT_CLICKED,NULL);
    keyboard=lv_keyboard_create(screen);lv_obj_set_size(keyboard,320,120);lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_add_event_cb(keyboard,keyboard_done,LV_EVENT_READY,NULL);lv_obj_add_event_cb(keyboard,keyboard_done,LV_EVENT_CANCEL,NULL);
    lv_timer_create(refresh,250,NULL);
}
static void sensors(void *arg) {
    for(;;){
        bool high=gpio_get_level(GPIO_NUM_0);if(!high&&boot_previous)boot_count++;boot_previous=high;
        if(imu_ok){uint8_t b[14];esp_err_t e=qmi8658_register_read(QMI8658_TEMP_L,b,sizeof(b));
            if(e==ESP_OK){int16_t v[7];for(int i=0;i<7;i++)v[i]=(int16_t)(b[2*i]|b[2*i+1]<<8);
                float ax=v[1]/8192.f,ay=v[2]/8192.f,az=v[3]/8192.f;
                xSemaphoreTake(state_lock,portMAX_DELAY);roll=atan2f(ay,az)*180/M_PI;pitch=atan2f(-ax,sqrtf(ay*ay+az*az))*180/M_PI;
                snprintf(status[IMU],sizeof(status[IMU]),"QMI8658 | LIVE\nACC [g]\n%+.2f %+.2f %+.2f\nGYRO [deg/s]\n%+.1f %+.1f %+.1f\nRoll %+.1f  Pitch %+.1f\nTemp %.1f C",ax,ay,az,v[4]/64.f,v[5]/64.f,v[6]/64.f,roll,pitch,v[0]/256.f);xSemaphoreGive(state_lock);
            }else note(IMU,"IMU read failed: %s",esp_err_to_name(e));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void console(void *arg) {
    uart_config_t cfg={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    uart_param_config(UART_NUM_0,&cfg);uart_driver_install(UART_NUM_0,1024,0,0,NULL,0);
    char line[96];size_t n=0;
    for(;;){uint8_t c;if(uart_read_bytes(UART_NUM_0,&c,1,pdMS_TO_TICKS(100))!=1)continue;
        if(c=='\r')continue;
        if(c=='\n'){
            line[n]=0;n=0;
            if(!strcmp(line,"status")){xSemaphoreTake(state_lock,portMAX_DELAY);for(int i=0;i<PAGES;i++)printf("STATUS %d %s\n",i,status[i]);xSemaphoreGive(state_lock);}
            else if(!strncmp(line,"page ",5)){int p=atoi(line+5);if(p>=0&&p<PAGES){lvgl_port_lock(0);lv_tabview_set_act(tabs,p,LV_ANIM_OFF);lvgl_port_unlock();}}
            else if(!strcmp(line,"loopback"))enqueue(AUDIO_LOOP,NULL,NULL);
            else if(!strcmp(line,"audio"))enqueue(AUDIO_DIAG,NULL,NULL);
            else if(!strcmp(line,"record"))enqueue(RECORD,NULL,NULL);
            else if(!strcmp(line,"play"))enqueue(PLAY,NULL,NULL);
            else if(!strcmp(line,"tone"))enqueue(TONE,NULL,NULL);
            else if(!strcmp(line,"wifi"))enqueue(SCAN_WIFI,NULL,NULL);
            else if(!strcmp(line,"ble"))enqueue(SCAN_BLE,NULL,NULL);
            else if(!strcmp(line,"mount"))enqueue(MOUNT,NULL,NULL);
            else if(!strcmp(line,"eject"))enqueue(UNMOUNT,NULL,NULL);
            else if(!strcmp(line,"sdtest"))enqueue(CREATE,NULL,NULL);
            else if(!strcmp(line,"save"))enqueue(SAVE,NULL,NULL);
            else if(!strcmp(line,"shot")){
                lvgl_port_lock(0);
                lv_img_dsc_t *img=lv_snapshot_take(lv_scr_act(),LV_IMG_CF_TRUE_COLOR);
                if(img){
                    printf("SHOT %u %u\n",img->header.w,img->header.h);
                    const uint16_t *px=(const uint16_t*)img->data;size_t count=img->header.w*img->header.h;
                    unsigned runs=0;
                    for(size_t i=0;i<count;){size_t run=1;while(i+run<count&&px[i+run]==px[i]&&run<65535)run++;printf("%04x%04x",(unsigned)run,px[i]);i+=run;if(++runs%64==0)vTaskDelay(1);}
                    printf("\nSHOT_END\n");lv_snapshot_free(img);
                }else printf("SHOT_FAILED\n");
                lvgl_port_unlock();
            }
            else printf("COMMANDS status/page N/record/play/tone/wifi/ble/mount/eject/sdtest/save/shot\n");
        }else if(n<sizeof(line)-1)line[n++]=c;
    }
}
void app_main(void) {
    state_lock=xSemaphoreCreateMutex();jobs=xQueueCreate(3,sizeof(job_t));assert(state_lock&&jobs);
    gpio_set_direction(GPIO_NUM_0,GPIO_MODE_INPUT);gpio_set_pull_mode(GPIO_NUM_0,GPIO_PULLUP_ONLY);
    ESP_ERROR_CHECK(bsp_i2c_init());
    for(int a=0;a<128;a++)if(a==0x19||a==0x38||a==0x18||a==0x41||a==0x6a)ESP_LOGI("demo","I2C 0x%02x: %s",a,probe(a)?"ACK":"MISSING");
    ESP_ERROR_CHECK(pca9557_register_write_byte(PCA9557_OUTPUT_PORT,0x05));
    ESP_ERROR_CHECK(pca9557_register_write_byte(PCA9557_CONFIGURATION_PORT,0xf8));
    bsp_lvgl_start();bsp_display_brightness_set(80);
    lvgl_port_lock(0);make_ui();lvgl_port_unlock();
    uint8_t id=0;
    imu_ok=qmi8658_register_read(0,&id,1)==ESP_OK&&id==5;
    if(imu_ok){imu_ok=reg_write(QMI8658_RESET,0xb0)==ESP_OK;vTaskDelay(pdMS_TO_TICKS(20));imu_ok=imu_ok&&reg_write(QMI8658_CTRL1,0x40)==ESP_OK&&reg_write(QMI8658_CTRL2,0x15)==ESP_OK&&reg_write(QMI8658_CTRL3,0x55)==ESP_OK&&reg_write(QMI8658_CTRL7,3)==ESP_OK;}
    note(IMU,imu_ok?"IMU ready":"QMI8658 unavailable");
    audio_ok=probe(0x18)&&probe(0x41)&&bsp_codec_init()==ESP_OK;
    note(AUDIO,audio_ok?"16 kHz stereo | 5 second recorder\nVolume 70 | auto playback level":"Audio initialization failed");
    esp_err_t e=init_radios();ESP_LOGI("demo","radios: %s",esp_err_to_name(e));
    note(WIFI,wifi_ok?"Scan networks, or scroll down\nto enter SSID / password":"Wi-Fi initialization failed");
    note(BLE,ble_ok?"BLE receive test ready\nTap Scan; enable a nearby BLE device":"BLE initialization failed");
    note(FILES,"Insert SD, then tap Mount\nFAT filesystem; never auto-format");
    xTaskCreate(worker,"test_jobs",8192,NULL,4,NULL);xTaskCreate(sensors,"imu",4096,NULL,3,NULL);
    xTaskCreate(console,"console",4096,NULL,2,NULL);
    ESP_LOGI("demo","READY: touch hardware demo; camera disabled");
}

#pragma once
#include <stdint.h>
#define MAINBUF_SIZE 8
#define ERR_MP3_NONE 0
#define ERR_MP3_MAINDATA_UNDERFLOW -2
#define ERR_MP3_INVALID_FRAMEHEADER -6
typedef void *HMP3Decoder;
typedef struct {int samprate,bitsPerSample,nChans,outputSamps;} MP3FrameInfo;
int MP3FindSyncWord(unsigned char *,int);
int MP3Decode(HMP3Decoder,unsigned char **,int *,int16_t *,int);
void MP3GetLastFrameInfo(HMP3Decoder,MP3FrameInfo *);

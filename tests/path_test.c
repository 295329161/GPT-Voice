#include "core/path.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 const char *valid[]={"/sdcard","/sdcard/Music/song.mp3","/sdcard/图片/风景.png"};
 const char *bad[]={"/sdcard/..","/sdcard/../private","/sdcard/dir/.","/sdcard//a","/sdcard/","/sdcardevil/a","/sdcard/a/../../b","/sdcard/a\\b","/sdcard/.. /b","/sdcard/a/"};
 for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);i++)assert(storage_path_valid(valid[i]));
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!storage_path_valid(bad[i]));
 assert(!storage_path_valid(NULL));assert(!storage_name_valid(""));assert(storage_name_valid("音乐.mp3"));puts("SD path confinement and FAT filename validation PASS");
}

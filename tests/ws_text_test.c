#include "core/ws_text.h"
#include <assert.h>
#include <string.h>
int main(void) {
    ws_text_t s = {0};
    assert(ws_text_feed(&s,1,true,4,0,"ab",2,20)==0);
    assert(ws_text_feed(&s,1,true,4,2,"cd",2,20)==1);
    assert(!strcmp(s.data,"abcd"));
    assert(ws_text_feed(&s,1,false,2,0,"ab",2,20)==0);
    assert(ws_text_feed(&s,9,true,0,0,0,0,20)==0);
    assert(ws_text_feed(&s,0,false,3,0,"c",1,20)==0);
    assert(ws_text_feed(&s,0,false,3,1,"de",2,20)==0);
    assert(ws_text_feed(&s,0,true,1,0,"f",1,20)==1);
    assert(!strcmp(s.data,"abcdef"));
    assert(ws_text_feed(&s,0,true,1,0,"x",1,20)==-1);
    assert(ws_text_feed(&s,1,false,2,0,"ab",2,3)==0);
    assert(ws_text_feed(&s,0,true,2,0,"cd",2,3)==-1);
    assert(ws_text_feed(&s,1,true,3,0,"a",1,20)==0);
    assert(ws_text_feed(&s,1,true,3,2,"b",1,20)==-1);
    assert(ws_text_feed(&s,1,false,1,0,"a",1,20)==0);
    assert(ws_text_feed(&s,0,true,0,0,0,0,20)==1);
    assert(!strcmp(s.data,"a"));
    ws_text_reset(&s);
}

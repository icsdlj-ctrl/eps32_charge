#include "hs_location.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);exit(1);}}while(0)
static bool parse(const char *s,hs_location_t *out){return hs_location_parse(s,strlen(s),out);}
int main(void){
    hs_location_t out={0},sentinel;
    const char *ok="{\"success\":true,\"city\":\"San Jose\",\"latitude\":37.3393854,\"longitude\":-121.894959,\"timezone\":{\"id\":\"America/Los_Angeles\"}}";
    CHECK(parse(ok,&out));CHECK(out.latitude_e6==37339385);CHECK(out.longitude_e6==-121894959);CHECK(!strcmp(out.city,"San Jose"));CHECK(!strcmp(out.timezone,"America/Los_Angeles"));
    sentinel=out;
    const char *bad[]={"{}","{\"success\":false}","{\"success\":true,\"city\":\"\"}",
        "{\"success\":true,\"city\":\"X\",\"latitude\":91,\"longitude\":0,\"timezone\":{\"id\":\"UTC\"}}",
        "{\"success\":true,\"city\":\"X\",\"latitude\":0,\"longitude\":181,\"timezone\":{\"id\":\"UTC\"}}",
        "{\"success\":true,\"city\":\"X\",\"latitude\":\"37\",\"longitude\":0,\"timezone\":{\"id\":\"UTC\"}}",
        "{\"success\":true,\"city\":\"X\",\"latitude\":0,\"longitude\":0,\"timezone\":{\"id\":\"UTC&foo=1\"}}",
        "{\"success\":true,\"city\":\"X\\nY\",\"latitude\":0,\"longitude\":0,\"timezone\":{\"id\":\"UTC\"}}",
        "{\"success\":true,\"city\":\"X\",\"latitude\":0,\"longitude\":0,\"timezone\":{\"id\":\"UTC\"}}garbage"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){CHECK(!parse(bad[i],&out));CHECK(!memcmp(&out,&sentinel,sizeof(out)));}
    CHECK(parse("{\"success\":true,\"city\":\"London\",\"latitude\":0,\"longitude\":0,\"timezone\":{\"id\":\"Europe/London\"}}",&out));
    CHECK(out.latitude_e6==0&&out.longitude_e6==0);CHECK(!strcmp(out.city,"London"));
    char deep[128];memset(deep,'[',20);memset(deep+20,']',20);CHECK(!hs_location_parse(deep,40,&out));
    CHECK(!hs_location_parse(ok,5000,&out));CHECK(!hs_location_parse(NULL,0,&out));
    printf("IP location parser: %u checks passed.\n",checks);return 0;
}

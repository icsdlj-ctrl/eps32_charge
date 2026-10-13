#include "hs_weather_model.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

static char *fixture;
static cJSON *document(void) { cJSON *r=cJSON_Parse(fixture); CHECK(r); return r; }
static cJSON *field(cJSON *o,const char *k) { return cJSON_GetObjectItemCaseSensitive(o,k); }
static void verify(cJSON *r, bool expected)
{
    char *body = cJSON_PrintUnformatted(r);
    hs_weather_forecast_t out,before;
    memset(&out,0xa5,sizeof(out)); before=out;
    CHECK(body);
    CHECK(hs_weather_parse(body,strlen(body),&out)==expected);
    if (!expected) CHECK(memcmp(&out,&before,sizeof(out))==0);
    free(body); cJSON_Delete(r);
}

int main(int argc,char **argv)
{
    CHECK(argc==2);
    FILE *f=fopen(argv[1],"rb"); CHECK(f);
    CHECK(fseek(f,0,SEEK_END)==0);
    long n=ftell(f); CHECK(n>0 && n<HS_WEATHER_JSON_LIMIT);
    rewind(f); fixture=calloc((size_t)n+1,1); CHECK(fixture);
    CHECK(fread(fixture,1,(size_t)n,f)==(size_t)n); fclose(f);
    hs_weather_forecast_t parsed;
    CHECK(hs_weather_parse(fixture,(size_t)n,&parsed));
    CHECK(parsed.days[0].minimum_c<=parsed.days[0].maximum_c);
    CHECK(strlen(parsed.days[0].date)==10);
    CHECK(!hs_weather_parse(NULL,10,&parsed));
    CHECK(!hs_weather_parse(fixture,0,&parsed));
    CHECK(!hs_weather_parse(fixture,(size_t)n,NULL));
    CHECK(!hs_weather_parse(fixture,HS_WEATHER_JSON_LIMIT+1,&parsed));
    CHECK(!hs_weather_parse(fixture,(size_t)n-5,&parsed));
    CHECK(!hs_weather_parse("{} trailing",11,&parsed));
    CHECK(!hs_weather_parse("{}\0junk",7,&parsed));
    CHECK(!hs_weather_parse("{}",2,&parsed));
    char deep[80]; memset(deep,'[',30); memset(deep+30,']',30);
    CHECK(!hs_weather_parse(deep,60,&parsed));

    cJSON *r=document(); cJSON_DeleteItemFromObjectCaseSensitive(r,"current"); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"temperature_2m",cJSON_CreateString("25")); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"temperature_2m",cJSON_CreateNull()); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"temperature_2m",cJSON_CreateNumber(101)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"weather_code",cJSON_CreateNumber(2.5)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"weather_code",cJSON_CreateNumber(100)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"relative_humidity_2m",cJSON_CreateNumber(101)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"wind_speed_10m",cJSON_CreateNumber(-1)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current_units"),"temperature_2m",cJSON_CreateString("F")); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"daily_units"),"precipitation_probability_max",cJSON_CreateString("fraction")); verify(r,false);
    r=document(); cJSON_DeleteItemFromArray(field(field(r,"daily"),"time"),2); verify(r,false);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"temperature_2m_min"),0,cJSON_CreateNumber(99)); verify(r,false);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"temperature_2m_max"),0,cJSON_CreateNull()); verify(r,false);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"precipitation_probability_max"),1,cJSON_CreateNumber(-1)); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"time",cJSON_CreateString("2026-13-01T12:00")); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"time",cJSON_CreateString("2026-10-13T25:00")); verify(r,false);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"time"),0,cJSON_CreateString("2026-02-30")); verify(r,false);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"time"),1,cJSON_CreateString("2026-12-31")); verify(r,false);
    r=document(); cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"relative_humidity_2m",cJSON_CreateNull()); verify(r,true);
    r=document(); cJSON_ReplaceItemInArray(field(field(r,"daily"),"precipitation_probability_max"),1,cJSON_CreateNull()); verify(r,true);
    r=document(); cJSON *dates=field(field(r,"daily"),"time");
    cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"time",cJSON_CreateString("2024-02-28T12:00"));
    cJSON_ReplaceItemInArray(dates,0,cJSON_CreateString("2024-02-28"));
    cJSON_ReplaceItemInArray(dates,1,cJSON_CreateString("2024-02-29"));
    cJSON_ReplaceItemInArray(dates,2,cJSON_CreateString("2024-03-01")); verify(r,true);
    r=document(); dates=field(field(r,"daily"),"time");
    cJSON_ReplaceItemInObjectCaseSensitive(field(r,"current"),"time",cJSON_CreateString("2025-02-28T12:00"));
    cJSON_ReplaceItemInArray(dates,0,cJSON_CreateString("2025-02-28"));
    cJSON_ReplaceItemInArray(dates,1,cJSON_CreateString("2025-02-29"));
    cJSON_ReplaceItemInArray(dates,2,cJSON_CreateString("2025-03-01")); verify(r,false);
    CHECK(hs_weather_coordinates_valid(-90000000,-180000000));
    CHECK(hs_weather_coordinates_valid(90000000,180000000));
    CHECK(!hs_weather_coordinates_valid(90000001,0));
    CHECK(!hs_weather_coordinates_valid(0,-180000001));
    CHECK(hs_weather_timezone_valid("Asia/Shanghai"));
    CHECK(hs_weather_timezone_valid("Etc/GMT+8"));
    CHECK(!hs_weather_timezone_valid("Asia/Shanghai&evil=1"));
    CHECK(!hs_weather_timezone_valid(""));
    CHECK(!hs_weather_timezone_valid(NULL));
    CHECK(strcmp(hs_weather_condition(0),"Clear")==0);
    CHECK(strcmp(hs_weather_condition(99),"Thunderstorm")==0);
    CHECK(strcmp(hs_weather_condition(4),"Unknown")==0);
    free(fixture);
    printf("PASS: %d checks; malformed responses leave prior forecast unchanged.\n",checks);
    return 0;
}

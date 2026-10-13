#include "charge_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL at %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void sample(charge_session_t *s,double current,double power,int64_t us){charge_session_update(s,true,current,power,us,3);}
int main(void){
    charge_session_t s={0};
    for(int i=0;i<=3600;i++)sample(&s,1,5,i*1000000LL);
    CHECK(fabs(s.charge_mah-1000)<1e-7);CHECK(fabs(s.energy_wh-5)<1e-9);CHECK(s.seconds==3600);
    s=(charge_session_t){0};sample(&s,1,5,0);sample(&s,2,10,1000000);
    CHECK(fabs(s.charge_mah-1.5/3.6)<1e-8);CHECK(fabs(s.energy_wh-7.5/3600)<1e-10);
    s=(charge_session_t){0};for(int i=0;i<=60;i++)sample(&s,.05f,.25,i*1000000LL);
    CHECK(s.active);CHECK(fabs(s.charge_mah-(double).05f*60/3.6)<1e-7);
    double q=s.charge_mah;
    for(int i=0;i<12;i++)sample(&s,.049,0.245,61000000LL+i*250000LL);
    CHECK(s.active&&s.charge_mah>q);sample(&s,.05f,.25,64000000LL);CHECK(s.active);
    for(int i=0;i<=12;i++)sample(&s,.049,.245,65000000LL+i*250000LL);
    CHECK(!s.active&&s.charge_mah==0&&s.energy_wh==0&&s.seconds==0);
    sample(&s,.04,.2,70000000);CHECK(!s.active&&s.charge_mah==0);
    sample(&s,1,5,71000000);sample(&s,1,5,72000000);q=s.charge_mah;
    charge_session_update(&s,false,0,0,73000000,3);sample(&s,1,5,78000000);
    CHECK(s.active&&s.charge_mah==q);sample(&s,1,5,79000000);CHECK(s.charge_mah>q);
    q=s.charge_mah;sample(&s,1,5,84000000);CHECK(s.charge_mah==q);
    sample(&s,0,0,85000000);charge_session_update(&s,false,0,0,87000000,3);
    sample(&s,0,0,88000000);sample(&s,0,0,90000000);CHECK(s.active);
    sample(&s,0,0,91000000);CHECK(!s.active&&s.charge_mah==0);
    sample(&s,1,5,92000000);sample(&s,1,5,93000000);q=s.charge_mah;
    charge_session_update(&s,true,NAN,5,94000000,3);CHECK(s.active&&s.charge_mah==q);
    charge_session_update(&s,true,1,INFINITY,95000000,3);CHECK(s.active&&s.charge_mah==q);
    sample(&s,1,5,96000000);charge_session_update(&s,true,0,0,97000000,0);
    CHECK(!s.active&&s.energy_wh==0&&s.charge_mah==0);
    charge_session_t other={0};sample(&other,1,5,0);sample(&other,1,5,1000000);
    CHECK(other.charge_mah>0&&s.charge_mah==0);
    s=(charge_session_t){0};sample(&s,1,5,0);sample(&s,-1,5,1000000);
    double energy=s.energy_wh;sample(&s,-1,5,2000000);CHECK(s.energy_wh==energy);
    s=(charge_session_t){0};sample(&s,1,5,0);sample(&s,1,5,1000000);
    sample(&s,0,0,2000000);sample(&s,0,0,6000000);CHECK(s.active);
    sample(&s,0,0,7000000);sample(&s,0,0,8000000);CHECK(s.active);
    sample(&s,0,0,9000000);CHECK(!s.active&&s.charge_mah==0);
    printf("Charge integration/threshold/reset tests: %u checks passed.\n",checks);return 0;
}

/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * Every exp-mapped knob, from every preset: 140 steps up must climb to the top, 140 down reach the bottom. */
#include <stdio.h>
#include <stdint.h>
#include "../src/core/od.h"
static uint32_t T; uint32_t od_ms(void){return T;}
#include "../src/core/od.c"
int main(){od_init();T=5000;int bad=0;
 int ids[]={K_CUT,K_A,K_D,K_R,K_LFO,K_FDEC,K_ETIME,K_RESO,K_DETUNE,K_DRIVE,K_ABPM,K_VIB};
 for(int p=0;p<OD_NPRESETS;p++){preset_load(p);
  for(unsigned j=0;j<sizeof ids/sizeof*ids;j++){int id=ids[j];float prev=k_get(id);
   for(int s=0;s<140;s++){k_set(id,k_get(id)+1.f/128.f);float v=k_get(id);if(v<prev-1e-6f&&prev<.999f){bad++;if(bad<5)printf("p%d k%d step%d %f->%f\n",p,id,s,prev,v);}prev=v;}
   if(prev<.999f){bad++;if(bad<8)printf("p%d k%d never reaches max: %f\n",p,id,prev);}
   for(int s=0;s<140;s++){k_set(id,k_get(id)-1.f/128.f);}
   if(k_get(id)>.001f){bad++;if(bad<10)printf("p%d k%d never reaches min %f\n",p,id,k_get(id));}
 }} printf("bad %d\n",bad); return bad != 0;}

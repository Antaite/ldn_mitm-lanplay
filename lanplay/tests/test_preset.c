#include "lanplay/lp_config.h"
#include "lanplay/lp_presets.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(void){
  const char *ini="[general]\nmode=internet\nserver=0\npreset=tgceo.cn:11460\n[server0]\nhost=my.nas\n";
  lp_config_t c; lp_config_parse(&c, ini, strlen(ini));
  assert(c.preset>=0 && !strcmp(lp_config_current(&c)->name, "tgceo 瑪利歐賽車 8"));
  char out[4096]; size_t n=lp_config_format(&c,out,sizeof out); lp_config_t d; lp_config_parse(&d,out,n);
  assert(d.preset==c.preset);
  d.preset=-1; n=lp_config_format(&d,out,sizeof out); lp_config_parse(&c,out,n);
  assert(c.preset==-1 && !strcmp(lp_config_current(&c)->host,"my.nas"));
  const char *ini2="[general]\npreset=gone.example:1\n"; lp_config_parse(&c,ini2,strlen(ini2)); assert(c.preset==-1 && lp_config_current(&c)==NULL);
  printf("%d presets, preset OK\n", lp_preset_count);
}

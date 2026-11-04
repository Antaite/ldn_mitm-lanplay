#include <stdlib.h>
#include "lanplay/lp_sha1.h"
#include <stdio.h>
#include <string.h>
int main(void){ const char *v[]={"","abc","The quick brown fox jumps over the lazy dog","abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
 for(int i=0;i<4;i++){uint8_t o[20]; lp_sha1(v[i],strlen(v[i]),o); for(int j=0;j<20;j++)printf("%02x",o[j]); printf("  ");
 char cmd[300]; snprintf(cmd,sizeof cmd,"printf '%%s' '%s' | sha1sum | cut -c1-40",v[i]); fflush(stdout); system(cmd);} }

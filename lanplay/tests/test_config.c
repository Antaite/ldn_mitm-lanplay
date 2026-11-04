#include "lanplay/lp_config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(void) {
    const char *ini = "\xEF\xBB\xBF[general]\r\nmode = internet\r\nserver=1\r\nip=10.13.5.6\r\n"
                      "[server0]\nhost=a.example.com\n[server1]\nname=TW\nhost=1.2.3.4\nport=11453\nuser=bob\npass=p w\n"
                      "[server2]\nname=empty\n";
    lp_config_t c; lp_config_parse(&c, ini, strlen(ini));
    assert(c.mode == LP_MODE_INTERNET); assert(c.count == 2); assert(c.current == 1);
    assert(!strcmp(c.servers[0].name, "a.example.com")); assert(c.servers[0].port == 11451);
    assert(c.servers[1].port == 11453); assert(!strcmp(c.servers[1].pass, "p w"));
    assert(c.virt_ip == 0x0A0D0506);
    char out[4096]; size_t n = lp_config_format(&c, out, sizeof out); assert(n > 0);
    lp_config_t d; lp_config_parse(&d, out, n);
    assert(!memcmp(&c, &d, sizeof c));
    lp_config_remove_server(&d, 0); assert(d.count == 1 && d.current == 0 && !strcmp(d.servers[0].name, "TW"));
    uint8_t r[2] = {36, 0}; uint32_t ip = lp_generate_virtual_ip(r); assert(lp_ip_is_valid_virtual(ip));
    char s[16]; lp_ip_to_str(ip, s); printf("gen ip %s\n%s", s, out);
    assert(lp_config_format(&c, out, 20) == 0);
    puts("config OK");
}

# Host build of the lanplay core + lwIP for protocol testing (not used on the Switch).
LWIP    := ../../third_party/lwip
LWIPDIR := $(LWIP)/src
include $(LWIPDIR)/Filelists.mk
SRC := $(COREFILES) $(CORE4FILES) $(APIFILES) $(LWIP)/contrib/unix-port/sys_arch.c ../src/lp_dns.c ../src/lp_probe.c \
       ../src/vnet.c ../src/lp_tunnel.c ../src/lp_config.c ../src/lp_sha1.c ../port/host/lp_platform_host.c lpclient.c
CFLAGS := -O1 -g -Wall -Wno-address -I../include -I$(LWIPDIR)/include -I$(LWIP)/contrib/unix-port/include -pthread
lpclient: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@
clean:
	rm -f lpclient

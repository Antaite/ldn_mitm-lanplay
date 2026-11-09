# Virtual Switch test tool (PC). `make` = Linux/macOS build, `make windows` = Windows .exe (mingw-w64).
ROOT    := ../..
LWIP    := $(ROOT)/third_party/lwip
LWIPDIR := $(LWIP)/src
include $(LWIPDIR)/Filelists.mk
LP      := $(ROOT)/lanplay
CORE    := $(COREFILES) $(CORE4FILES) $(APIFILES) $(LP)/src/lp_dns.c $(LP)/src/vnet.c $(LP)/src/lp_tunnel.c \
           $(LP)/src/lp_config.c $(LP)/src/lp_sha1.c vswitch.c vs_net.c vs_os.c
CFLAGS  := -O2 -Wall -Wno-address -DLWIP_RAW=1 -I. -I$(LP)/include -I$(LWIPDIR)/include

vswitch: $(CORE) $(LP)/port/host/lp_platform_host.c
	$(CC) $(CFLAGS) -I$(LWIP)/contrib/unix-port/include -pthread $^ $(LWIP)/contrib/unix-port/sys_arch.c -o $@ $(LDFLAGS)

MINGW ?= x86_64-w64-mingw32-gcc
windows: vswitch.exe
vswitch.exe: $(CORE) $(LP)/port/win32/lp_platform_win.c
	$(MINGW) $(CFLAGS) -I$(LWIP)/contrib/win32-port/include $^ $(LWIP)/contrib/win32-port/sys_arch.c -o $@ -static -lws2_32 -lwinmm

clean:
	rm -f vswitch vswitch.exe
.PHONY: windows clean

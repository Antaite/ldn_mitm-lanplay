#---------------------------------------------------------------------------------
# liblanplay.a : lwIP + vnet + lan-play tunnel (plain C, linked into ldn_mitm)
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif
include $(DEVKITPRO)/devkitA64/base_tools

LIBNX    ?= $(DEVKITPRO)/libnx
PORTLIBS ?= $(DEVKITPRO)/portlibs/switch

LWIPDIR  := ../third_party/lwip/src
include $(LWIPDIR)/Filelists.mk

SRCS := $(COREFILES) $(CORE4FILES) $(APIFILES) \
        src/vnet.c src/lp_bridge.c src/lp_tunnel.c src/lp_config.c src/lp_sha1.c src/lp_dns.c src/lp_probe.c

BUILD := build
OBJS  := $(addprefix $(BUILD)/,$(subst /,_,$(subst ../,,$(SRCS:.c=.o))))

ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -O2 -std=gnu11 -Wall -Wno-address -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
          $(ARCH) -D__SWITCH__ -D_GNU_SOURCE -DFD_SETSIZE=1024 \
          -Iinclude -Iport/switch -I$(LWIPDIR)/include -I$(LIBNX)/include -I$(PORTLIBS)/include

.PHONY: all clean
all: lib/liblanplay.a

lib/liblanplay.a: $(OBJS)
	@mkdir -p lib
	@rm -f $@
	$(AR) rcs $@ $(OBJS)
	@echo built ... $(notdir $@)

define compile_rule
$(BUILD)/$(subst /,_,$(subst ../,,$(1:.c=.o))): $(1) $(wildcard include/*.h include/lanplay/*.h port/switch/arch/*.h)
	@mkdir -p $(BUILD)
	@echo $(notdir $(1))
	@$(CC) $(CFLAGS) -c $(1) -o $$@
endef
$(foreach src,$(SRCS),$(eval $(call compile_rule,$(src))))

clean:
	rm -rf $(BUILD) lib

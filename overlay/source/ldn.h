#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Service s;
} LdnMitmConfigService;

Result ldnMitmSaveLogToFile(LdnMitmConfigService *s);
Result ldnMitmGetVersion(LdnMitmConfigService *s, char *version);
Result ldnMitmGetLogging(LdnMitmConfigService *s, u32 *enabled);
Result ldnMitmSetLogging(LdnMitmConfigService *s, u32 enabled);
Result ldnMitmGetEnabled(LdnMitmConfigService *s, u32 *enabled);
Result ldnMitmSetEnabled(LdnMitmConfigService *s, u32 enabled);
/* lan-play extensions (ldn_mitm-lanplay) */
Result ldnMitmGetLanPlayStatus(LdnMitmConfigService *s, void *out_status_0x100);
Result ldnMitmReloadLanPlayConfig(LdnMitmConfigService *s);
Result ldnMitmGetMode(LdnMitmConfigService *s, u32 *mode);
Result ldnMitmSetMode(LdnMitmConfigService *s, u32 mode);
Result ldnMitmScanRooms(LdnMitmConfigService *s, void *rooms, size_t rooms_size, u32 *count);
Result ldnMitmGetConfig(LdnMitmConfigService *out);
Result ldnMitmGetConfigFromService(Service* s, LdnMitmConfigService *out);

#ifdef __cplusplus
}
#endif

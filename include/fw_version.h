#ifndef FW_VERSION_H
#define FW_VERSION_H

#include "ota_ids.h"

/* FW_VER_MAJOR/MINOR/PATCH/BUILD are injected by the build (-D flags) from the
 * git tag in CI. Defaults keep local dev builds compiling. */
#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR 0
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR 0
#endif
#ifndef FW_VER_PATCH
#define FW_VER_PATCH 0
#endif
#ifndef FW_VER_BUILD
#define FW_VER_BUILD 0
#endif

#define FW_VERSION_U32  OTA_PACK_VERSION(FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH, FW_VER_BUILD)

#define FW_VER_STR_(a,b,c) "v" #a "." #b "." #c
#define FW_VER_STR__(a,b,c) FW_VER_STR_(a,b,c)
#define FW_VERSION_STR  FW_VER_STR__(FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH)

#endif /* FW_VERSION_H */

#ifndef OTA_IDS_H
#define OTA_IDS_H

#include <stdint.h>

/* Fixed product identity for Zigbee OTA. These three values MUST match across
 * the firmware, the .ota image header, and the z2m OTA index. */
#define OTA_MANUFACTURER_CODE  0xFEFEu   /* shared DIY 16-bit code */
#define OTA_IMAGE_TYPE         0x0003u   /* roller blinds (soil 0x0001, door 0x0002) */
#define OTA_MODEL_ID           "DFR-RollerBlinds"

/* Pack semver into the 32-bit Zigbee fileVersion (z2m renders the high hex
 * digits first — same scheme as the siblings):
 *   nibble7=major  nibble6=minor  byte2=patch  bytes1..0=0x0000 */
#define OTA_PACK_VERSION(major, minor, patch, build)            \
    (((uint32_t)((major) & 0xFu) << 28) |                       \
     ((uint32_t)((minor) & 0xFu) << 24) |                       \
     ((uint32_t)((patch) & 0xFFu) << 16))

#endif /* OTA_IDS_H */

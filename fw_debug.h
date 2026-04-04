/* fw_debug.h */
#ifndef _FW_DEBUG_H
#define _FW_DEBUG_H

#include <linux/firmware.h>
#include <linux/types.h>
#include "avermedia_c985.h"

/* Firmware version info structure */
struct fw_version_info {
    u32 qpsos_signature;
    u16 qpsos_version;
    u32 config_base;
    bool valid;
};

/* Extended firmware metadata */
struct fw_metadata {
    const char *name;
    size_t size;
    u32 crc32;
    struct fw_version_info version;
};

/* Functions */
u32 calculate_fw_crc32(const u8 *data, size_t size);
int parse_qpsos_header(struct c985_poc *d, const struct firmware *fw,
                       struct fw_version_info *info);
int validate_firmware_header(struct c985_poc *d, const struct firmware *fw,
                             const char *name);
void print_firmware_info(struct c985_poc *d, struct fw_metadata *meta);
int verify_firmware_in_card_memory(struct c985_poc *d, const struct firmware *fw,
                                   u32 card_addr, const char *name);
int check_firmware_compatibility(struct c985_poc *d, struct fw_metadata *vid_meta,
                                 struct fw_metadata *aud_meta);

#endif /* _FW_DEBUG_H */

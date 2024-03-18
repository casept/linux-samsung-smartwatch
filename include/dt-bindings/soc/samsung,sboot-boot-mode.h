/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SAMSUNG_SBOOT_BOOT_MODE_H
#define __SAMSUNG_SBOOT_BOOT_MODE_H

#define REBOOT_FLAG 0x12345670
/* normal boot */
#define BOOT_NORMAL (REBOOT_FLAG + 0)
/* enter S-Boot download mode */
#define BOOT_BL_DOWNLOAD (REBOOT_FLAG + 1)
/* enter S-Boot upload mode */
#define BOOT_BL_UPLOAD (REBOOT_FLAG + 2)
/* enter charging mode */
#define BOOT_CHARGING (REBOOT_FLAG + 3)
/* enter recovery mode */
#define BOOT_RECOVERY (REBOOT_FLAG + 4)
/* enter FOTA mode */
#define BOOT_FOTA (REBOOT_FLAG + 5)
/* enter bootloader FOTA (upgrade) mode */
#define BOOT_FOTA_BL (REBOOT_FLAG + 6)
/* enter image secure check fail mode */
#define BOOT_SECFAIL (REBOOT_FLAG + 7)

/* enter emergency boot mode */
#define BOOT_EMERGENCY 0

/* TODO: Implement debug, swsel, sud (requires additional arg) */

#endif

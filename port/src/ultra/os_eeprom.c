/* EEPROM: the sequel's main save device (the first game had none).
 *
 * src/system/controller_io.c probes it, reads 0x58-byte records and wipes all
 * 0x200 bytes, i.e. a 4 Kbit part: 64 blocks of 8 bytes. The port keeps that
 * image in one file next to the Controller Pak, in the raw layout emulators
 * use, so saves can move both ways. */
#include "ultra.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "sbk_os.h"

#define EEPROM_BLOCKS 64
#define EEPROM_SIZE   (EEPROM_BLOCKS * 8)

static u8 sbk_eeprom[EEPROM_SIZE];
static char sbk_eeprom_path[1024];
static int sbk_eeprom_present;

int sbk_eeprom_open(const char *path) {
    FILE *f;
    if (path == NULL) {
        const char *home = getenv("HOME");
        if (home == NULL) home = ".";
        snprintf(sbk_eeprom_path, sizeof(sbk_eeprom_path),
                 "%s/Library/Application Support/SnowboardKids2", home);
        mkdir(sbk_eeprom_path, 0755);
        snprintf(sbk_eeprom_path, sizeof(sbk_eeprom_path),
                 "%s/Library/Application Support/SnowboardKids2/eeprom.sav", home);
    } else {
        snprintf(sbk_eeprom_path, sizeof(sbk_eeprom_path), "%s", path);
    }
    sbk_eeprom_present = 1;
    f = fopen(sbk_eeprom_path, "rb");
    if (f != NULL) {
        size_t n = fread(sbk_eeprom, 1, sizeof(sbk_eeprom), f);
        fclose(f);
        printf("sbk: EEPROM %s (%u bytes)\n", sbk_eeprom_path, (unsigned)n);
    } else {
        printf("sbk: EEPROM %s (new)\n", sbk_eeprom_path);
    }
    return 0;
}

void sbk_eeprom_close(void) {
    sbk_eeprom_present = 0;
}

static void sbk_eeprom_flush(void) {
    FILE *f = fopen(sbk_eeprom_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "sbk: cannot write %s\n", sbk_eeprom_path);
        return;
    }
    fwrite(sbk_eeprom, 1, sizeof(sbk_eeprom), f);
    fclose(f);
}

s32 osEepromProbe(OSMesgQueue *mq) {
    (void)mq;
    return sbk_eeprom_present ? EEPROM_TYPE_4K : 0;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int length) {
    (void)mq;
    if (!sbk_eeprom_present) return PFS_ERR_NOPACK;
    if (address * 8 + length > EEPROM_SIZE) return PFS_ERR_INVALID;
    memcpy(buffer, sbk_eeprom + address * 8, (size_t)length);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int length) {
    (void)mq;
    if (!sbk_eeprom_present) return PFS_ERR_NOPACK;
    if (address * 8 + length > EEPROM_SIZE) return PFS_ERR_INVALID;
    memcpy(sbk_eeprom + address * 8, buffer, (size_t)length);
    sbk_eeprom_flush();
    return 0;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongRead(mq, address, buffer, 8);
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongWrite(mq, address, buffer, 8);
}

/* The 64DD: this game checks for one and falls back to the cartridge. */
OSPiHandle *osDriveRomInit(void) {
    return NULL;
}

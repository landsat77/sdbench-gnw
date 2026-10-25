#ifndef SDB_SDINFO_H
#define SDB_SDINFO_H

#include <stdbool.h>
#include <stdint.h>

/* What we know about the SD card. Any part may be missing. */
typedef struct {
	bool have_cid;          /* card identity (CMD10), SD on SPI1 only */
	uint8_t mid;            /* manufacturer ID */
	char oid[3];            /* OEM / application ID, 2 ASCII chars */
	char pnm[6];            /* product name, 5 ASCII chars */
	uint8_t prv;            /* product revision, BCD n.m */
	uint32_t psn;           /* serial number */
	uint16_t year;          /* manufacturing date */
	uint8_t month;
	bool have_csd;          /* card capacity (CMD9) */
	uint64_t card_bytes;
	bool have_volume;       /* the FAT volume (FatFs) */
	uint8_t fs_type;        /* 1 FAT12, 2 FAT16, 3 FAT32, 4 exFAT */
	uint64_t volume_bytes;
	bool have_free;
	uint64_t free_bytes;
} sd_info_t;

/* Reads the card identity and size. Call while no file is open. */
void sd_info_read(sd_info_t *info);

/* Manufacturer name from the ID (unofficial list), NULL if unknown. */
const char *sd_info_maker(uint8_t mid);

const char *sd_info_fs_name(uint8_t fs_type);

#endif

/*
 * SD card identity and size.
 *
 * The firmware ABI has no call for this (its FatFs disk_ioctl knows
 * MMC_GET_CID / GET_SECTOR_COUNT, but it is not exported), so:
 *  - card identity (CID, CMD10) and capacity (CSD, CMD9): sent on SPI1
 *    here, the way the firmware's own driver does it (user_diskio_spi.c,
 *    register-level spi_xfer: TSIZE while SPE=0, SPE, CSTART, FIFO, EOT,
 *    SPE=0; CS = PB9; CRC-7 on every command since the firmware turns
 *    CMD59 CRC checking on). Only when the card is on SPI1 (the other
 *    wiring, software SPI on the OSPI1 pins, is left alone) and only while
 *    no file is open: the bus is then idle (SPE=0, CS high).
 *  - volume size and free space: the FATFS object of the mounted volume,
 *    reached through f_opendir() (DIR.obj.fs). Read only.
 */
#include <string.h>

#include "sdinfo.h"

#ifdef HOST_BUILD

#include <stdlib.h>

/* SDB_FAKE_CARD=1: an example card, to test the screens and the log. */
void sd_info_read(sd_info_t *info)
{
	memset(info, 0, sizeof *info);
	if (!getenv("SDB_FAKE_CARD")) return;
	*info = (sd_info_t){ .have_cid = true, .mid = 0x03, .oid = "SD", .pnm = "SN32G", .prv = 0x80,
	                     .psn = 0x1234ABCD, .year = 2023, .month = 5, .have_csd = true,
	                     .card_bytes = 31914983424ull, .have_volume = true, .fs_type = 4,
	                     .volume_bytes = 31902400512ull, .have_free = true, .free_bytes = 12345678848ull };
}

#else

#include "stm32h7xx.h"
#include "ff.h"
#include "common.h"
#include "gw_core_bridge.h"

uint32_t get_SystemCoreClock(void);

#define CMD9  (0x40 + 9)    /* SEND_CSD */
#define CMD10 (0x40 + 10)   /* SEND_CID */
#define CMD17 (0x40 + 17)   /* READ_SINGLE_BLOCK */

static uint32_t cycles_per_ms;

static uint32_t now_cycles(void) { return common_emu_get_dwt_cycles(); }

static bool expired(uint32_t start, uint32_t ms)
{
	return (uint32_t)(now_cycles() - start) >= ms * cycles_per_ms;
}

static void cs_low(void) { GPIOB->BSRR = GPIO_BSRR_BR9; }
static void cs_high(void) { GPIOB->BSRR = GPIO_BSRR_BS9; }

/* Full-duplex transfer; tx NULL sends 0xFF, rx NULL drops the bytes. */
static bool spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
	SPI_TypeDef *spi = SPI1;
	volatile uint8_t *txdr = (volatile uint8_t *)&spi->TXDR;
	volatile uint8_t *rxdr = (volatile uint8_t *)&spi->RXDR;
	uint16_t tx_left = len, rx_left = len;
	uint32_t start = now_cycles();
	bool ok = true;

	MODIFY_REG(spi->CR2, SPI_CR2_TSIZE, len);
	SET_BIT(spi->CR1, SPI_CR1_SPE);
	SET_BIT(spi->CR1, SPI_CR1_CSTART);
	while (tx_left || rx_left) {
		uint32_t sr = spi->SR;
		if (tx_left && (sr & SPI_SR_TXP)) {
			*txdr = tx ? *tx++ : 0xFF;
			tx_left--;
		}
		if (rx_left && ((sr & SPI_SR_RXP) || (rx_left < 4 && (sr & SPI_SR_RXPLVL)))) {
			uint8_t b = *rxdr;
			if (rx) *rx++ = b;
			rx_left--;
		}
		if (expired(start, 100)) { ok = false; break; }
	}
	while (!(spi->SR & SPI_SR_EOT))
		if (expired(start, 100)) { ok = false; break; }
	SET_BIT(spi->IFCR, SPI_IFCR_EOTC | SPI_IFCR_TXTFC);
	CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
	return ok;
}

static uint8_t rx_byte(void)
{
	uint8_t b = 0;
	if (!spi_xfer(NULL, &b, 1)) return 0;
	return b;
}

static uint8_t crc7(const uint8_t *data, int len)
{
	uint8_t crc = 0;
	while (len--) {
		crc ^= *data++;
		for (int i = 0; i < 8; i++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x12) : (uint8_t)(crc << 1);
	}
	return crc | 1;
}

static bool wait_ready(void)
{
	uint32_t start = now_cycles();
	do {
		wdog_refresh();
		if (rx_byte() == 0xFF) return true;
	} while (!expired(start, 500));
	return false;
}

/* A command with a data block: len bytes + the CRC-16 (not checked) into
 * out[len + 2]. */
static bool read_block(uint8_t cmd, uint32_t arg, uint8_t *out, uint16_t len)
{
	uint8_t frame[6] = { cmd, (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, 0 };
	bool ok = false;

	frame[5] = crc7(frame, 5);
	cs_low();
	if (!wait_ready()) goto done;
	if (!spi_xfer(frame, NULL, 6)) goto done;
	uint8_t r = 0xFF;
	for (int n = 0; n < 32 && (r & 0x80); n++) r = rx_byte();
	if (r != 0) goto done;
	uint32_t start = now_cycles();
	uint8_t token;
	do token = rx_byte(); while (token == 0xFF && !expired(start, 200));
	if (token != 0xFE) goto done;
	ok = spi_xfer(NULL, out, len + 2);
done:
	cs_high();
	rx_byte();	/* the card releases MISO */
	return ok;
}

/* CMD9 / CMD10: 16 byte register, checked with its own CRC-7. */
static bool read_register(uint8_t cmd, uint8_t out[16])
{
	uint8_t buf[18];
	if (!read_block(cmd, 0, buf, 16)) return false;
	if (crc7(buf, 15) != buf[15]) return false;
	memcpy(out, buf, 16);
	return true;
}

static bool spi_ok;		/* CID/CSD read: sectors can be read too */
static bool block_addressing;	/* SDHC/SDXC: CMD17 takes sector numbers */

/* The firmware found the card on SPI1 and left it set up: clock on,
 * master, idle, CS (PB9) an output and high. */
static bool spi1_card(void)
{
	if (!(RCC->APB2ENR & RCC_APB2ENR_SPI1EN)) return false;
	if (!(SPI1->CFG2 & SPI_CFG2_MASTER)) return false;
	if (SPI1->CR1 & SPI_CR1_SPE) return false;
	if (((GPIOB->MODER >> 18) & 3) != 1) return false;
	if (!(GPIOB->ODR & GPIO_ODR_OD9)) return false;
	return true;
}

static void read_card(sd_info_t *info)
{
	uint8_t r[16];

	spi_ok = false;
	if (!spi1_card()) return;
	cycles_per_ms = get_SystemCoreClock() / 1000;
	if (cycles_per_ms < 1000) cycles_per_ms = 280000;
	common_emu_enable_dwt_cycles();

	if (read_register(CMD10, r)) {
		info->have_cid = true;
		info->mid = r[0];
		for (int i = 0; i < 2; i++) info->oid[i] = (r[1 + i] >= 32 && r[1 + i] < 127) ? (char)r[1 + i] : '?';
		for (int i = 0; i < 5; i++) info->pnm[i] = (r[3 + i] >= 32 && r[3 + i] < 127) ? (char)r[3 + i] : '?';
		info->prv = r[8];
		info->psn = (uint32_t)r[9] << 24 | (uint32_t)r[10] << 16 | (uint32_t)r[11] << 8 | r[12];
		info->year = (uint16_t)(2000 + (((r[13] & 0x0F) << 4) | (r[14] >> 4)));
		info->month = r[14] & 0x0F;
	}
	if (read_register(CMD9, r)) {
		uint32_t structure = r[0] >> 6;
		if (structure == 1) {	/* SDHC / SDXC: (C_SIZE + 1) * 512 KB */
			uint32_t c_size = ((uint32_t)(r[7] & 0x3F) << 16) | ((uint32_t)r[8] << 8) | r[9];
			info->card_bytes = (uint64_t)(c_size + 1) * 512 * 1024;
			info->have_csd = true;
			block_addressing = true;
			spi_ok = true;
		} else if (structure == 0) {	/* SDSC */
			uint32_t read_bl_len = r[5] & 15;
			uint32_t c_size = ((uint32_t)(r[6] & 3) << 10) | ((uint32_t)r[7] << 2) | (r[8] >> 6);
			uint32_t mult = ((r[9] & 3) << 1) | (r[10] >> 7);
			info->card_bytes = (uint64_t)(c_size + 1) << (mult + 2 + read_bl_len);
			info->have_csd = true;
			block_addressing = false;
			spi_ok = true;
		}
	}
}

/* exFAT: FatFs only knows the free space after f_getfree() (not on the
 * ABI), so count the free clusters of the allocation bitmap (one bit per
 * cluster, ~124 sectors for 16 GB). Read only, like the rest. */
static bool exfat_free(FATFS *fs, uint32_t *free_clusters)
{
	static uint8_t sector[512 + 2] __attribute__((aligned(4)));
	static const uint8_t ones[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
	uint32_t clusters = fs->n_fatent - 2, used = 0, done = 0;
	uint32_t lba = (uint32_t)fs->bitbase;

	while (done < clusters) {
		uint32_t arg = block_addressing ? lba : lba * 512;
		if (!read_block(CMD17, arg, sector, 512)) return false;
		for (int i = 0; i < 512 && done < clusters; i++) {
			uint8_t b = sector[i];
			uint32_t bits = clusters - done < 8 ? clusters - done : 8;
			if (bits < 8) b &= (uint8_t)((1u << bits) - 1);
			used += ones[b & 15] + ones[b >> 4];
			done += bits;
		}
		lba++;
	}
	*free_clusters = clusters - used;
	return true;
}

static void read_volume(sd_info_t *info)
{
	DIR dir;
	if (f_opendir(&dir, "/") != FR_OK) return;
	FATFS *fs = dir.obj.fs;
	if (fs && fs->fs_type && fs->n_fatent > 2 && fs->csize) {
		uint64_t cluster = (uint64_t)fs->csize * 512;
		info->fs_type = fs->fs_type;
		info->volume_bytes = (uint64_t)(fs->n_fatent - 2) * cluster;
		info->have_volume = true;
		if (fs->free_clst <= fs->n_fatent - 2) {
			info->free_bytes = (uint64_t)fs->free_clst * cluster;
			info->have_free = true;
		} else if (fs->fs_type == FS_EXFAT && spi_ok && spi1_card()) {
			uint32_t free_clusters;
			if (exfat_free(fs, &free_clusters)) {
				info->free_bytes = (uint64_t)free_clusters * cluster;
				info->have_free = true;
			}
		}
	}
	f_closedir(&dir);
}

void sd_info_read(sd_info_t *info)
{
	memset(info, 0, sizeof *info);
	read_card(info);
	read_volume(info);
}

#endif

const char *sd_info_maker(uint8_t mid)
{
	switch (mid) {
	case 0x01: return "Panasonic";
	case 0x02: return "Toshiba/Kioxia";
	case 0x03: return "SanDisk";
	case 0x1B: return "Samsung";
	case 0x1D: return "ADATA";
	case 0x27: return "Phison";
	case 0x28: return "Lexar";
	case 0x31: return "Silicon Power";
	case 0x41: return "Kingston";
	case 0x74: return "Transcend";
	case 0x76: return "Patriot";
	case 0x82: return "Sony";
	default: return NULL;
	}
}

const char *sd_info_fs_name(uint8_t fs_type)
{
	switch (fs_type) {
	case 1: return "FAT12";
	case 2: return "FAT16";
	case 3: return "FAT32";
	case 4: return "exFAT";
	default: return "?";
	}
}

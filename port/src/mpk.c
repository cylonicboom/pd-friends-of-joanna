#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include "fs.h"
#include "system.h"
#include "input.h"
#include "savequeue.h"
#include "mpk.h"

/*
 * See mpk.h. Layout of a 1-bank (32KB) controller pak, in 32-byte blocks:
 *   0        scratch (osPfsInitPak's RAM test writes and restores it)
 *   1,3,4,6  the pack ID and its three backups (__osRepairPackId)
 *   7        label
 *   8-15     inode table (page 1), 128 x u16
 *   16-23    inode backup (page 2)
 *   24-39    directory (pages 3-4), 16 notes x 32 bytes
 *   40-1023  data pages 5-127
 * Block 1024 and up is the accessory register space (bank select, motor):
 * writes there are ignored and reads come back zero.
 */

#define MPK_SIZE        0x8000
#define MPK_BLOCKSIZE   32
#define MPK_NUMBLOCKS   (MPK_SIZE / MPK_BLOCKSIZE)
#define MPK_MAXPAGES    1000

#define MPK_INODE_OFF   (8 * MPK_BLOCKSIZE)
#define MPK_INODE_LEN   (16 * MPK_BLOCKSIZE)   // inode + backup
#define MPK_DIR_OFF     (24 * MPK_BLOCKSIZE)
#define MPK_DIR_LEN     (16 * MPK_BLOCKSIZE)

// The pfs code reads the ID, inode and directory as native multi-byte
// structs, and on a little-endian host those must be held swapped in memory
// (controller.h flips __OSInodeUnit's byte view to match). N64 byte order is
// only ever on disk.
#if !defined(PLATFORM_N64) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MPK_HOST_SWAPS 1
#else
#define MPK_HOST_SWAPS 0
#endif

static const u16 kMpkIdBlocks[] = { 1, 3, 4, 6 };

/*
 * Every page is held in memory (32KB each) whether or not it is mounted, so
 * the index can read any of them and mounting is a pointer swap. A channel
 * maps to at most one page; a page is on at most one channel.
 */
struct mpkpage {
	char path[FS_MAXPATH + 1];
	u8 image[MPK_SIZE];
	bool dirty;          // changed since its file was written
	bool indexstale;     // changed since mpkPageScan
	s32 serial;          // PD's device serial from its filesystem, -1 if none yet
	s32 channel;         // -1 when not mounted
	u32 lastuse;
};

static struct mpkpage *g_MpkPages = NULL;
static s32 g_MpkNumPages = 0;
static s32 g_MpkChannelPage[MAXCONTROLLERS] = { -1, -1, -1, -1 };
static u32 g_MpkUseClock = 0;

/* ---- byte order --------------------------------------------------------- */

static void mpkSwap16(u8 *p) { u8 t = p[0]; p[0] = p[1]; p[1] = t; }
static void mpkSwap32(u8 *p) { mpkSwap16(p); mpkSwap16(p + 2); u8 t0 = p[0], t1 = p[1]; p[0] = p[2]; p[1] = p[3]; p[2] = t0; p[3] = t1; }
static void mpkSwap64(u8 *p) { for (s32 i = 0; i < 4; i++) { u8 t = p[i]; p[i] = p[7 - i]; p[7 - i] = t; } }

static u16 mpkRead16BE(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void mpkWrite16BE(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v & 0xff; }

/*
 * The ID checksum sums the first 28 bytes as u16 words. banks and version
 * share a word, so the sum differs between byte orders even once every field
 * is swapped correctly. Each side's checksum is checked in its own order and,
 * if it was valid, recomputed in the other - a broken ID stays broken, and
 * libultra deals with it the way it would on hardware.
 */
static bool mpkIdChecksumBE(const u8 *id, u16 *sum, u16 *isum)
{
	*sum = *isum = 0;

	for (s32 i = 0; i < 28; i += 2) {
		const u16 w = mpkRead16BE(id + i);
		*sum += w;
		*isum += (u16)~w;
	}

	return mpkRead16BE(id + 28) == *sum && mpkRead16BE(id + 30) == *isum;
}

static bool mpkIdChecksumHost(const u8 *id, u16 *sum, u16 *isum)
{
	u16 w;

	*sum = *isum = 0;

	for (s32 i = 0; i < 28; i += 2) {
		memcpy(&w, id + i, 2);
		*sum += w;
		*isum += (u16)~w;
	}

	u16 csum, icsum;
	memcpy(&csum, id + 28, 2);
	memcpy(&icsum, id + 30, 2);

	return csum == *sum && icsum == *isum;
}

static void mpkSwapIdFields(u8 *id)
{
	mpkSwap32(id + 0);   // repaired
	mpkSwap32(id + 4);   // random
	mpkSwap64(id + 8);   // serial_mid
	mpkSwap64(id + 16);  // serial_low
	mpkSwap16(id + 24);  // deviceid
	                     // banks, version: bytes
	mpkSwap16(id + 28);  // checksum
	mpkSwap16(id + 30);  // inverted_checksum
}

static void mpkSwapTables(u8 *image)
{
	for (s32 i = 0; i < MPK_INODE_LEN; i += 2) {
		mpkSwap16(image + MPK_INODE_OFF + i);
	}

	for (s32 i = 0; i < MPK_DIR_LEN; i += 32) {
		u8 *dir = image + MPK_DIR_OFF + i;
		mpkSwap32(dir + 0x0);  // game_code
		mpkSwap16(dir + 0x4);  // company_code
		mpkSwap16(dir + 0x6);  // start_page
		mpkSwap16(dir + 0xa);  // data_sum
	}
}

// disk (N64 order) -> memory
static void mpkToHost(u8 *image)
{
	if (!MPK_HOST_SWAPS) {
		return;
	}

	for (s32 i = 0; i < (s32)(sizeof(kMpkIdBlocks) / sizeof(kMpkIdBlocks[0])); i++) {
		u8 *id = image + kMpkIdBlocks[i] * MPK_BLOCKSIZE;
		u16 sum, isum;
		const bool valid = mpkIdChecksumBE(id, &sum, &isum);

		mpkSwapIdFields(id);

		if (valid) {
			mpkIdChecksumHost(id, &sum, &isum);
			memcpy(id + 28, &sum, 2);
			memcpy(id + 30, &isum, 2);
		}
	}

	mpkSwapTables(image);
}

// memory -> disk (N64 order); works on a copy
static void mpkToDisk(const u8 *image, u8 *out)
{
	memcpy(out, image, MPK_SIZE);

	if (!MPK_HOST_SWAPS) {
		return;
	}

	for (s32 i = 0; i < (s32)(sizeof(kMpkIdBlocks) / sizeof(kMpkIdBlocks[0])); i++) {
		u8 *id = out + kMpkIdBlocks[i] * MPK_BLOCKSIZE;
		u16 sum, isum;
		const bool valid = mpkIdChecksumHost(id, &sum, &isum);

		mpkSwapIdFields(id);

		if (valid) {
			mpkIdChecksumBE(id, &sum, &isum);
			mpkWrite16BE(id + 28, sum);
			mpkWrite16BE(id + 30, isum);
		}
	}

	mpkSwapTables(out);
}

/* ---- blank page --------------------------------------------------------- */

/*
 * A formatted, empty 1-bank pak in N64 byte order: the ID in all four
 * places, an inode table with every data page free (0x0003) and its
 * checksum, an empty directory. The same thing osPfsReFormat leaves, which
 * this tree does not have.
 */
static void mpkFormatBlank(u8 *image, u32 seed)
{
	u8 id[32];
	u8 inode[256];
	u16 sum, isum;
	u32 inodesum = 0;

	memset(image, 0, MPK_SIZE);
	memset(id, 0, sizeof(id));

	// repaired = -1, random/serial from the seed (the pak's identity)
	id[0] = id[1] = id[2] = id[3] = 0xff;
	for (s32 i = 4; i < 24; i++) {
		seed = seed * 1103515245u + 12345u;
		id[i] = (u8)(seed >> 16);
	}
	mpkWrite16BE(id + 24, 0x0001); // deviceid: bit 0 set = a good pak
	id[26] = 1;                    // banks
	id[27] = 0;                    // version
	mpkIdChecksumBE(id, &sum, &isum);
	mpkWrite16BE(id + 28, sum);
	mpkWrite16BE(id + 30, isum);

	for (s32 i = 0; i < (s32)(sizeof(kMpkIdBlocks) / sizeof(kMpkIdBlocks[0])); i++) {
		memcpy(image + kMpkIdBlocks[i] * MPK_BLOCKSIZE, id, sizeof(id));
	}

	// inode: system pages 0-4 zero, data pages 5-127 free
	memset(inode, 0, sizeof(inode));
	for (s32 i = 5; i < 128; i++) {
		mpkWrite16BE(inode + i * 2, 0x0003);
		inodesum += inode[i * 2] + inode[i * 2 + 1];
	}
	inode[1] = (u8)inodesum; // inode_page[0].inode_t.page holds the byte sum

	memcpy(image + MPK_INODE_OFF, inode, sizeof(inode));
	memcpy(image + MPK_INODE_OFF + 256, inode, sizeof(inode));
}

/* ---- files -------------------------------------------------------------- */

static void mpkPagePath(s32 index, char *out, size_t outlen)
{
	snprintf(out, outlen, "$S/paks/page-%04d.mpk", index);
}

static bool mpkLoad(struct mpkpage *page, const char *path)
{
	FILE *f = fsFileOpenRead(path);
	s32 len;

	if (!f) {
		return false;
	}

	len = (s32)fread(page->image, 1, MPK_SIZE, f);
	fsFileFree(f);

	if (len != MPK_SIZE) {
		sysLogPrintf(LOG_WARNING, "mpk: %s is %d bytes, not a 32KB controller pak; skipped", path, len);
		return false;
	}

	mpkToHost(page->image);
	snprintf(page->path, sizeof(page->path), "%s", path);
	page->dirty = false;
	page->indexstale = true;
	page->serial = -1;
	page->channel = -1;
	page->lastuse = 0;

	return true;
}

static bool mpkSave(struct mpkpage *slot)
{
	static u8 disk[MPK_SIZE];
	FILE *f = fsFileOpenWriteAtomic(slot->path);

	if (!f) {
		sysLogPrintf(LOG_ERROR, "mpk: could not write %s", slot->path);
		return false;
	}

	mpkToDisk(slot->image, disk);

	if (fwrite(disk, 1, MPK_SIZE, f) != MPK_SIZE) {
		fclose(f);
		sysLogPrintf(LOG_ERROR, "mpk: short write to %s, kept the old file", slot->path);
		return false;
	}

	return fsFileCommitAtomic(f, slot->path);
}

static bool mpkCreatePage(s32 index)
{
	static struct mpkpage scratch;
	char path[FS_MAXPATH + 1];

	mpkPagePath(index, path, sizeof(path));
	mpkFormatBlank(scratch.image, (u32)index * 2654435761u ^ 0x5eedfa11u ^ (u32)sysGetMicroseconds());
	mpkToHost(scratch.image);
	snprintf(scratch.path, sizeof(scratch.path), "%s", path);

	if (!mpkSave(&scratch)) {
		return false;
	}

	sysLogPrintf(LOG_NOTE, "mpk: created %s", path);
	return true;
}

/* ---- the page index ------------------------------------------------------ */

/*
 * What PD keeps in a page, read straight from the image (host order in
 * memory): the pfs directory entry for PD's note, the note's page chain from
 * the inode table, and PD's own file headers inside the note. Every header
 * on a pak carries the pak's device serial, which is how a fileguid names it.
 */
static s32 mpkPageNote(const u8 *image, u8 *note, s32 notecap)
{
	for (s32 n = 0; n < 16; n++) {
		const u8 *dir = image + MPK_DIR_OFF + n * 32;
		u32 gamecode;
		u16 company, start;

		memcpy(&gamecode, dir + 0, 4);
		memcpy(&company, dir + 4, 2);
		memcpy(&start, dir + 6, 2);

		if (company != ROM_COMPANYCODE || gamecode != ROM_GAMECODE) {
			continue;
		}

		s32 len = 0;
		u16 page = start;

		for (s32 hops = 0; hops < 128 && (page & 0xff) >= 5 && (page & 0xff) < 128 && len + 256 <= notecap; hops++) {
			u16 next;

			memcpy(note + len, image + (page & 0xff) * 256, 256);
			len += 256;
			memcpy(&next, image + MPK_INODE_OFF + (page & 0xff) * 2, 2);

			if (next == 1) {
				return len;
			}

			page = next;
		}

		return len;
	}

	return 0;
}

static void mpkPageScan(struct mpkpage *page)
{
	static u8 note[128 * 256];
	const s32 len = mpkPageNote(page->image, note, sizeof(note));
	struct pakfileheader header;

	page->serial = -1;
	page->indexstale = false;

	for (s32 offset = 0; offset + (s32)sizeof(header) <= len; ) {
		memcpy(&header, note + offset, sizeof(header));

		if (header.filelen == 0 || header.filetype == PAKFILETYPE_TERMINATOR) {
			break;
		}

		page->serial = header.deviceserial;
		break;
	}
}

static s32 mpkPageSerial(s32 index)
{
	struct mpkpage *page = &g_MpkPages[index];

	if (page->indexstale) {
		mpkPageScan(page);
	}

	return page->serial;
}

static s32 mpkFindPageBySerial(s32 serial)
{
	for (s32 i = 0; i < g_MpkNumPages; i++) {
		if (mpkPageSerial(i) == serial) {
			return i;
		}
	}

	return -1;
}

/*
 * A device serial for a new PD filesystem on a page: 13 bits, not 0, not the
 * game pak's 0xbaa, and not in use on any page. PD draws these at random
 * (pakGenerateSerial), which is fine for four physical paks and not for a
 * pool that keeps growing - two pages with one serial would make every
 * fileguid on them ambiguous.
 */
s32 mpkAllocSerial(u32 seed)
{
	for (s32 tries = 0; tries < 8192; tries++) {
		seed = seed * 1103515245u + 12345u;
		const s32 serial = 16 + (s32)((seed >> 8) % (8192 - 16));

		if (serial != 0xbaa && mpkFindPageBySerial(serial) < 0) {
			return serial;
		}
	}

	return -1;
}

/* ---- mounting --------------------------------------------------------------- */

static void mpkMap(s32 channel, s32 pageindex)
{
	const s32 old = g_MpkChannelPage[channel];

	if (old >= 0) {
		g_MpkPages[old].channel = -1;
	}

	g_MpkChannelPage[channel] = pageindex;

	if (pageindex >= 0) {
		g_MpkPages[pageindex].channel = channel;
		g_MpkPages[pageindex].lastuse = ++g_MpkUseClock;
	}
}

/*
 * Put the page whose PD filesystem has this serial on a channel, and say
 * which. A page already mounted stays where it is. Otherwise a channel with
 * no page is used, else the least recently mounted one not in `pinned` (a bit
 * per channel the caller cannot spare - a seated player's profile, the open
 * game file). The page that leaves stays in memory, dirty or not; it is
 * written by the next flush. -1 if no page has the serial or every channel is
 * pinned. The caller re-probes the channel (pakFindBySerial does).
 */
s32 mpkMountBySerial(s32 serial, u8 pinned)
{
	const s32 pageindex = mpkFindPageBySerial(serial);
	s32 channel = -1;

	if (pageindex < 0) {
		return -1;
	}

	if (g_MpkPages[pageindex].channel >= 0) {
		g_MpkPages[pageindex].lastuse = ++g_MpkUseClock;
		return g_MpkPages[pageindex].channel;
	}

	for (s32 ch = 0; ch < MAXCONTROLLERS && channel < 0; ch++) {
		if (g_MpkChannelPage[ch] < 0) {
			channel = ch;
		}
	}

	if (channel < 0) {
		u32 oldest = 0xffffffff;

		for (s32 ch = 0; ch < MAXCONTROLLERS; ch++) {
			if (!(pinned & (1 << ch)) && g_MpkPages[g_MpkChannelPage[ch]].lastuse < oldest) {
				oldest = g_MpkPages[g_MpkChannelPage[ch]].lastuse;
				channel = ch;
			}
		}
	}

	if (channel < 0) {
		return -1;
	}

	sysLogPrintf(LOG_NOTE, "mpk: %s on controller pak %d (serial %x)", g_MpkPages[pageindex].path, channel + 1, serial);
	mpkMap(channel, pageindex);

	return channel;
}

/*
 * Pages are numbered from 0 with no gaps - they are made in order and never
 * deleted (empty pages are kept for reuse) - so finding them is counting.
 * All are loaded; the first four are mounted on channels 1-4 to start with.
 */
void mpkInit(void)
{
	char path[FS_MAXPATH + 1];
	s32 numfiles;

	fsCreateDir("$S/paks");

	for (numfiles = 0; numfiles < MPK_MAXPAGES; numfiles++) {
		mpkPagePath(numfiles, path, sizeof(path));
		if (fsFileSize(path) < 0) {
			break;
		}
	}

	if (numfiles == 0 && mpkCreatePage(0)) {
		numfiles = 1;
	}

	g_MpkPages = calloc(numfiles > 0 ? numfiles : 1, sizeof(*g_MpkPages));
	g_MpkNumPages = 0;

	for (s32 i = 0; i < numfiles && g_MpkPages; i++) {
		mpkPagePath(i, path, sizeof(path));
		if (mpkLoad(&g_MpkPages[g_MpkNumPages], path)) {
			g_MpkNumPages++;
		}
	}

	for (s32 ch = 0; ch < MAXCONTROLLERS; ch++) {
		g_MpkChannelPage[ch] = -1;
	}

	for (s32 ch = 0; ch < MAXCONTROLLERS && ch < g_MpkNumPages; ch++) {
		mpkMap(ch, ch);
		sysLogPrintf(LOG_NOTE, "mpk: %s on controller pak %d", g_MpkPages[ch].path, ch + 1);
	}

	sysLogPrintf(LOG_NOTE, "mpk: %d page%s", g_MpkNumPages, g_MpkNumPages == 1 ? "" : "s");
}

u8 mpkMountedMask(void)
{
	u8 mask = 0;

	for (s32 ch = 0; ch < MAXCONTROLLERS; ch++) {
		if (g_MpkChannelPage[ch] >= 0) {
			mask |= 1 << ch;
		}
	}

	return mask;
}

void mpkFlush(void)
{
	for (s32 i = 0; i < g_MpkNumPages; i++) {
		struct mpkpage *page = &g_MpkPages[i];

		if (page->dirty) {
			page->dirty = false;
			mpkSave(page);
		}
	}
}

/* ---- the libultra bottom ------------------------------------------------ */

static struct mpkpage *mpkSlot(int channel)
{
	if (channel < 0 || channel >= MAXCONTROLLERS || g_MpkChannelPage[channel] < 0) {
		return NULL;
	}

	return &g_MpkPages[g_MpkChannelPage[channel]];
}

s32 __osContRamRead(OSMesgQueue *mq, int channel, u16 address, u8 *buffer)
{
	struct mpkpage *slot = mpkSlot(channel);

	if (!slot) {
		return PFS_ERR_NOPACK;
	}

	if (address < MPK_NUMBLOCKS) {
		memcpy(buffer, slot->image + address * MPK_BLOCKSIZE, MPK_BLOCKSIZE);
	} else {
		memset(buffer, 0, MPK_BLOCKSIZE);
	}

	return 0;
}

s32 __osContRamWrite(OSMesgQueue *mq, int channel, u16 address, u8 *buffer, int force)
{
	struct mpkpage *slot = mpkSlot(channel);

	if (!slot) {
		return PFS_ERR_NOPACK;
	}

	if (address < MPK_NUMBLOCKS && memcmp(slot->image + address * MPK_BLOCKSIZE, buffer, MPK_BLOCKSIZE) != 0) {
		memcpy(slot->image + address * MPK_BLOCKSIZE, buffer, MPK_BLOCKSIZE);
		slot->dirty = true;
		slot->indexstale = true;
		saveQueueMarkEeprom();
	}

	return 0;
}

s32 __osPfsGetStatus(OSMesgQueue *queue, int channel)
{
	return mpkSlot(channel) ? 0 : PFS_ERR_NOPACK;
}

void __osSiGetAccess(void)
{
}

void __osSiRelAccess(void)
{
}

// a channel answers if it has a page, or a pad with a motor (which the pak
// probe then finds through osMotorProbe)
s32 osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)
{
	if (pattern) {
		*pattern = mpkMountedMask();

		for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
			if (inputRumbleSupported(i)) {
				*pattern |= 1 << i;
			}
		}
	}

	return 0;
}

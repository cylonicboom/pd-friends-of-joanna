#ifndef _IN_MPK_H
#define _IN_MPK_H

#include <PR/ultratypes.h>

/*
 * Controller paks as files ("pages"). Each mounted channel holds a 32KB
 * controller pak image backed by <savedir>/paks/page-NNNN.mpk, and the real
 * libultra pfs code (src/lib/ultra/io/pfs*.c, contpfs.c) runs on top of it:
 * this file supplies only the bottom - __osContRamRead/Write, the plug and
 * status checks.
 *
 * The .mpk is the emulator container format (N64 byte order for the pfs ID,
 * inode and directory pages). PD's own data inside the note stays in the PC
 * port's layout - container compatible only, by ruling (c-pak-pages).
 */

#ifdef __cplusplus
extern "C" {
#endif

void mpkInit(void);

// channels with a page mounted, as an osPfsIsPlug-style bit pattern
u8 mpkMountedMask(void);

// write every changed page back to its file (save queue, exit)
void mpkFlush(void);

// a device serial no page uses, for a new PD filesystem on a page
s32 mpkAllocSerial(u32 seed);

// put the page with this PD serial on a channel; see mpk.c. -1 if none.
s32 mpkMountBySerial(s32 serial, u8 pinned);

// how many pages the pool has, mounted or not
s32 mpkNumPages(void);

// the PD serial of the page at this index, -1 if it has no PD filesystem yet
s32 mpkPageSerialAt(s32 index);

// page `index` of every page loaded (pool and --player): its PD serial (-1
// when it has no PD filesystem yet), a name for a person ("page-0003",
// "kara.mpk") and whether changes to it are kept only in memory. false past
// the last page.
bool mpkPageInfo(s32 index, s32 *serial, char *label, s32 labellen, s32 *readonly);

// the same, for the page whose PD filesystem has this serial; false if none
bool mpkPageInfoBySerial(s32 serial, char *label, s32 labellen, s32 *readonly);

// whether the channel holds a --player page rather than a pool page
bool mpkChannelIsExternal(s32 channel);

// The seat's --player / --player-ro (seats 0-3, in command-line order): the
// device to look on (SAVEDEVICE_CONTROLLERPAKn for a .mpk, SAVEDEVICE_GAMEPAK
// for a bare query) and the query, a profile name or an index (all digits)
// into that device's MP player files. false if the seat has none, or its pak
// could not be mounted.
bool mpkPlayerArg(s32 seat, s8 *device, const char **query);

// put a page with no PD filesystem on a channel, making a new page file if
// the pool has none spare; see mpk.c. -1 if it could not.
s32 mpkMountBlankPage(u8 pinned);

#ifdef __cplusplus
}
#endif

#endif

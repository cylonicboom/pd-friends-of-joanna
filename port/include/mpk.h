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

#ifdef __cplusplus
}
#endif

#endif

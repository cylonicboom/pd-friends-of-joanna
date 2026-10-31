#ifndef _IN_SAVEDUMP_H
#define _IN_SAVEDUMP_H

// --save-dump <file> [--save-json]: print a save device's files and exit
void saveDumpFromArgs(void);
s32 saveDumpPakSerial(const char *path, u32 *serial, char *why, size_t whylen);

#endif

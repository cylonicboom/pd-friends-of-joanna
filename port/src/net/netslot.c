/**
 * netslot.c - head and body slots on the wire. Kept apart from netmsg.c so
 * tools/nettest/slotwire_test.c can build it against the real netbuf.c with
 * the mod tables stubbed. See the block comment below for the why.
 */

#include <string.h>
#include "platform.h"
#include "types.h"
#include "system.h"
#include "mod.h"
#include "net/netbuf.h"
#include "net/netmsg.h"

/*
 * Head and body slots on the wire (proto 95: the bug where everyone's
 * wearing someone else's face). A mod head/body slot is allocated per boot
 * in roster order, and per machine, so the bare number means a different
 * face on every peer. The identity is the slot's NAME (modHeadSlotName /
 * modBodySlotName); the number is a local allocation detail. On the wire a
 * slot is {u8 num, str name}: a vanilla row sends its number and an empty
 * name; a mod row sends 0xFF and its name, which the reader resolves locally.
 * An unknown name (a mod the reader does not have, which the mod handshake
 * should already have refused) falls back to slot 0 with one log line,
 * never an out-of-range index.
 */
#define NETSLOT_MOD 0xFF

void netbufWriteHeadSlot(struct netbuf *dst, u8 num)
{
	const char *name = modHeadSlotName(num);
	netbufWriteU8(dst, name ? NETSLOT_MOD : num);
	netbufWriteStr(dst, name ? name : "");
}

void netbufWriteBodySlot(struct netbuf *dst, u8 num)
{
	const char *name = modBodySlotName(num);
	netbufWriteU8(dst, name ? NETSLOT_MOD : num);
	netbufWriteStr(dst, name ? name : "");
}

static u8 netbufReadSlot(struct netbuf *src, s32 (*forname)(const char *), const char *what)
{
	const u8 num = netbufReadU8(src);
	const char *name = netbufReadStr(src);
	if (src->error) {
		return 0;
	}
	if (name && name[0]) {
		const s32 slot = forname(name);
		if (slot >= 0 && slot <= 0xFE) {
			return (u8)slot;
		}
		sysLogPrintf(LOG_WARNING, "NET: %s slot '%s' is not loaded here; wearing slot 0", what, name);
		return 0;
	}
	return num == NETSLOT_MOD ? 0 : num;
}

u8 netbufReadHeadSlot(struct netbuf *src) { return netbufReadSlot(src, modHeadSlotForName, "head"); }
u8 netbufReadBodySlot(struct netbuf *src) { return netbufReadSlot(src, modBodySlotForName, "body"); }


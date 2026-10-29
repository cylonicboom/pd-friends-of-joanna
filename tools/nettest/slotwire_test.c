// head/body slots on the wire (port/src/net/netslot.c), against the real
// netbuf.c with the mod slot tables stubbed: two machines whose mod rows
// landed on different numbers must still agree on the face.
//
// Builds with the engine's own defines and include set, from a configured
// build dir B (types.h needs them):
//   DEF=$(grep '^C_DEFINES' $B/CMakeFiles/pd.dir/flags.make | cut -d= -f2-)
//   INC=$(grep '^C_INCLUDES' $B/CMakeFiles/pd.dir/flags.make | cut -d= -f2-)
//   cc -std=c11 -funsigned-char -fwrapv -Wall $DEF $INC -include versioninfo.h port/src/net/netslot.c port/src/net/netbuf.c tools/nettest/slotwire_test.c -o /tmp/slotwire && /tmp/slotwire
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "types.h"
#include "net/netbuf.h"
#include "net/netmsg.h"

// --- stubs: machine A allocated mod_head "hat" at 160, machine B at 171 ---
static s32 s_machine = 0; // 0 = A (writer), 1 = B (reader)
static const struct { const char *name; s32 slotA, slotB; } s_heads[] = { { "hat", 160, 171 }, { "visor", 161, 170 } };
static const struct { const char *name; s32 slotA, slotB; } s_bodies[] = { { "coat", 90, 95 } };
#define N(a) ((s32)(sizeof(a)/sizeof((a)[0])))
const char *modHeadSlotName(s32 slot) { for (s32 i = 0; i < N(s_heads); i++) if ((s_machine ? s_heads[i].slotB : s_heads[i].slotA) == slot) return s_heads[i].name; return NULL; }
const char *modBodySlotName(s32 slot) { for (s32 i = 0; i < N(s_bodies); i++) if ((s_machine ? s_bodies[i].slotB : s_bodies[i].slotA) == slot) return s_bodies[i].name; return NULL; }
s32 modHeadSlotForName(const char *n) { for (s32 i = 0; i < N(s_heads); i++) if (!strcmp(s_heads[i].name, n)) return s_machine ? s_heads[i].slotB : s_heads[i].slotA; return -1; }
s32 modBodySlotForName(const char *n) { for (s32 i = 0; i < N(s_bodies); i++) if (!strcmp(s_bodies[i].name, n)) return s_machine ? s_bodies[i].slotB : s_bodies[i].slotA; return -1; }
static s32 s_logs = 0;
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; va_list ap; va_start(ap, fmt); printf("  log: "); vprintf(fmt, ap); printf("\n"); va_end(ap); s_logs++; }

static u8 roundtrip(u8 headA, u8 bodyA, u8 *bodyB)
{
	u8 data[256];
	struct netbuf buf = { 0 };
	buf.data = data; buf.size = sizeof(data);
	s_machine = 0;
	netbufStartWrite(&buf);
	netbufWriteHeadSlot(&buf, headA);
	netbufWriteBodySlot(&buf, bodyA);
	s_machine = 1;
	netbufStartRead(&buf);
	const u8 h = netbufReadHeadSlot(&buf);
	*bodyB = netbufReadBodySlot(&buf);
	return h;
}

int main(void)
{
	int fails = 0;
	u8 b;
	// vanilla rows: the number travels unchanged
	u8 h = roundtrip(5, 3, &b); printf("vanilla head 5 body 3   -> %u %u (want 5 3)\n", h, b); fails += h != 5 || b != 3;
	h = roundtrip(0, 0, &b);    printf("vanilla head 0 body 0   -> %u %u (want 0 0)\n", h, b); fails += h != 0 || b != 0;
	// mod rows: A's 160/90 arrive as B's 171/95 - the same face, a different number
	h = roundtrip(160, 90, &b); printf("mod hat/coat A160/90    -> %u %u (want 171 95)\n", h, b); fails += h != 171 || b != 95;
	h = roundtrip(161, 3, &b);  printf("mod visor A161, body 3  -> %u %u (want 170 3)\n", h, b); fails += h != 170 || b != 3;
	// an unknown name: B has no such mod -> slot 0 + a log line, never 0xFF
	s_logs = 0;
	{
		u8 data[256]; struct netbuf buf = { 0 }; buf.data = data; buf.size = sizeof(data);
		netbufStartWrite(&buf); netbufWriteU8(&buf, 0xFF); netbufWriteStr(&buf, "nosuchhead");
		netbufStartRead(&buf); h = netbufReadHeadSlot(&buf);
		printf("unknown mod name        -> %u, %d log (want 0, 1)\n", h, s_logs); fails += h != 0 || s_logs != 1;
	}
	// a truncated packet reads as 0 with the buffer flagged, not garbage
	{
		u8 data[2] = { 0xFF, 0x05 }; struct netbuf buf = { 0 }; buf.data = data; buf.size = 2; buf.wp = 2;
		netbufStartRead(&buf); h = netbufReadHeadSlot(&buf);
		printf("truncated               -> %u error=%u (want 0, nonzero)\n", h, buf.error); fails += h != 0 || !buf.error;
	}
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails;
}

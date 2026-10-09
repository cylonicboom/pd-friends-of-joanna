#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "input.h"
#include "system.h"
#include "seatprofile.h"

// One row per Game option. These were Game.Player<n>.* in main.c; the names
// and ranges are unchanged, only the section moved.
struct seatopt {
	const char *name;
	configtype type;
	size_t offset;
	f64 min;
	f64 max;
};

#define SEATOPT(name, type, field, min, max) \
	{ name, type, offsetof(struct extplayerconfig, field), min, max }

static const struct seatopt g_SeatGameOpts[] = {
	SEATOPT("FovY",                  CFG_F32, fovy,                  5.0,  175.0),
	SEATOPT("FovAffectsZoom",        CFG_S32, fovzoom,               0,    1),
	SEATOPT("MouseAimMode",          CFG_S32, mouseaimmode,          0,    1),
	SEATOPT("MouseAimSpeedX",        CFG_F32, mouseaimspeedx,        0.0,  10.0),
	SEATOPT("MouseAimSpeedY",        CFG_F32, mouseaimspeedy,        0.0,  10.0),
	SEATOPT("RadialMenuSpeed",       CFG_F32, radialmenuspeed,       0.0,  10.0),
	SEATOPT("CrosshairSway",         CFG_F32, crosshairsway,         0.0,  10.0),
	SEATOPT("CameraTilt",            CFG_F32, cameratilt,            0.0,  4.0),
	SEATOPT("CameraBob",             CFG_F32, camerabob,             0.0,  4.0),
	SEATOPT("GunSwayWithBob",        CFG_S32, gunswaywithbob,        0,    1),
	SEATOPT("TiltIntoRun",           CFG_S32, tiltforward,           0,    1),
	SEATOPT("InvertTilt",            CFG_S32, tiltinvert,            0,    1),
	SEATOPT("CodAiming",             CFG_S32, codaiming,             0,    1),
	SEATOPT("CodAimLock",            CFG_S32, codaimlock,            0,    1),
	SEATOPT("CrosshairEdgeBoundary", CFG_F32, crosshairedgeboundary, 0.0,  1.0),
	SEATOPT("CrouchMode",            CFG_S32, crouchmode,            0,    CROUCHMODE_TOGGLE_ANALOG),
	SEATOPT("ExtendedControls",      CFG_S32, extcontrols,           0,    1),
	SEATOPT("CrosshairColour",       CFG_U32, crosshaircolour,       0,    4294967295.0),
	SEATOPT("CrosshairSize",         CFG_U32, crosshairsize,         0,    4),
	SEATOPT("CrosshairHealth",       CFG_S32, crosshairhealth,       0,    CROSSHAIR_HEALTH_ON_WHITE),
	SEATOPT("UseKeyReloads",         CFG_S32, usereloads,            0,    0),
};

// what a seat starts from before a file's values land on it; taken before
// anything has loaded, so it is the compiled-in default
static struct extplayerconfig g_SeatExtCfgDefault;

static char g_SeatSlug[MAX_PLAYERS][CONFIG_MAX_SECNAME];

static void seatStandInSlug(s32 seat, char *out, size_t outlen)
{
	// the name iniBindProfileProperties gives a fileless player
	snprintf(out, outlen, "MpPlayer.Player%x", seat);
}

static void seatSlugFor(s32 seat, const struct fileguid *guid, char *out, size_t outlen)
{
	if (!guid || (!guid->fileid && !guid->deviceserial) || guid->deviceserial == 0xFFFF) {
		seatStandInSlug(seat, out, outlen);
	} else {
		snprintf(out, outlen, "MpPlayer.%x-%x", guid->deviceserial, guid->fileid);
	}
}

static void seatGameKey(char *out, size_t outlen, const char *slug, const struct seatopt *opt)
{
	snprintf(out, outlen, "%s.Game.%s", slug, opt->name);
}

static void seatBindKey(char *out, size_t outlen, const char *slug, u32 ck)
{
	snprintf(out, outlen, "%s.Binds.%s", slug, inputGetContKeyName(ck));
}

// Point every key under slug at the seat's storage. A key the file carries
// is applied as it binds (configApplyPending); one it does not leaves the
// storage as it was.
static void seatRegister(s32 seat, const char *slug)
{
	char key[CONFIG_MAX_KEYNAME];
	u8 *base = (u8 *)&g_PlayerExtCfg[seat];

	for (s32 i = 0; i < ARRAYCOUNT(g_SeatGameOpts); i++) {
		const struct seatopt *opt = &g_SeatGameOpts[i];
		void *ptr = base + opt->offset;

		seatGameKey(key, sizeof(key), slug, opt);

		switch (opt->type) {
		case CFG_F32:
			configRegisterFloat(key, (f32 *)ptr, (f32)opt->min, (f32)opt->max);
			break;
		case CFG_U32:
			configRegisterUInt(key, (u32 *)ptr, (u32)opt->min, (u32)opt->max);
			break;
		case CFG_S32:
		default:
			configRegisterInt(key, (s32 *)ptr, (s32)opt->min, (s32)opt->max);
			break;
		}
	}

	for (u32 ck = 0; ck < CK_TOTAL_COUNT; ck++) {
		seatBindKey(key, sizeof(key), slug, ck);
		configRegisterString(key, inputSeatBindStr(seat, ck), inputSeatBindStrMax());
	}
}

// Detach a seat from its section. The values stay on the entries, so the
// next save still writes them and the next bind of that file reads them.
static void seatDetach(s32 seat)
{
	char key[CONFIG_MAX_KEYNAME];
	const char *slug = g_SeatSlug[seat];

	if (!slug[0]) {
		return;
	}

	// the strings are what gets kept, and they lag the live binds
	inputSeatBindsCapture(seat);

	for (s32 i = 0; i < ARRAYCOUNT(g_SeatGameOpts); i++) {
		seatGameKey(key, sizeof(key), slug, &g_SeatGameOpts[i]);
		configUnbindKey(key);
	}

	for (u32 ck = 0; ck < CK_TOTAL_COUNT; ck++) {
		seatBindKey(key, sizeof(key), slug, ck);
		configUnbindKey(key);
	}

	g_SeatSlug[seat][0] = '\0';
}

static void seatFinish(s32 seat, const char *slug)
{
	struct extplayerconfig *cfg = &g_PlayerExtCfg[seat];

	// gameInit derives this once at boot; a seat that changed hands needs it
	// derived again from the FovY it now has
	cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;

	snprintf(g_SeatSlug[seat], sizeof(g_SeatSlug[seat]), "%s", slug);
	sysLogPrintf(LOG_NOTE, "seat %d options: [%s]", seat + 1, slug);
}

static void seatAttach(s32 seat, const char *slug)
{
	g_PlayerExtCfg[seat] = g_SeatExtCfgDefault;
	inputSeatBindsReset(seat);

	seatRegister(seat, slug);

	inputSeatBindsApply(seat);
	seatFinish(seat, slug);
}

// One file, one seat. Two seats bound to the same keys would both write
// through them, last write wins; the seat that had it first goes back to its
// stand-in instead.
static void seatEvictOthers(s32 seat, const char *slug)
{
	char standin[CONFIG_MAX_SECNAME];

	for (s32 other = 0; other < MAX_PLAYERS; other++) {
		if (other == seat || strcmp(g_SeatSlug[other], slug) != 0) {
			continue;
		}

		seatStandInSlug(other, standin, sizeof(standin));
		sysLogPrintf(LOG_WARNING, "seat %d options: [%s] moved to seat %d", other + 1, slug, seat + 1);
		seatDetach(other);
		seatAttach(other, standin);
	}
}

void seatProfileBind(s32 seat, const struct fileguid *guid)
{
	char slug[CONFIG_MAX_SECNAME];

	if (seat < 0 || seat >= MAX_PLAYERS) {
		return;
	}

	seatSlugFor(seat, guid, slug, sizeof(slug));

	if (strcmp(slug, g_SeatSlug[seat]) == 0) {
		return;
	}

	seatEvictOthers(seat, slug);
	seatDetach(seat);
	seatAttach(seat, slug);
}

void seatProfileAdopt(s32 seat, const struct fileguid *guid)
{
	char slug[CONFIG_MAX_SECNAME];
	struct extplayerconfig live;

	if (seat < 0 || seat >= MAX_PLAYERS) {
		return;
	}

	seatSlugFor(seat, guid, slug, sizeof(slug));

	if (strcmp(slug, g_SeatSlug[seat]) == 0) {
		return;
	}

	seatEvictOthers(seat, slug);

	live = g_PlayerExtCfg[seat];
	seatDetach(seat);

	// binding applies whatever the file had; the live values go back over it
	seatRegister(seat, slug);
	g_PlayerExtCfg[seat] = live;

	// registering only touched the strings, never binds[], so rebuilding the
	// strings from binds[] puts the live binds back
	inputSeatBindsCapture(seat);

	seatFinish(seat, slug);
}

const char *seatProfileSlug(s32 seat)
{
	if (seat < 0 || seat >= MAX_PLAYERS) {
		return "";
	}

	return g_SeatSlug[seat];
}

void seatProfileMigrateLegacy(void)
{
	char oldkey[CONFIG_MAX_KEYNAME];
	char newkey[CONFIG_MAX_KEYNAME];
	char standin[CONFIG_MAX_SECNAME];
	s32 moved = 0;

	for (s32 seat = 0; seat < MAX_PLAYERS; seat++) {
		seatStandInSlug(seat, standin, sizeof(standin));

		// the old sections counted seats from 1, the stand-ins from 0
		for (s32 i = 0; i < ARRAYCOUNT(g_SeatGameOpts); i++) {
			snprintf(oldkey, sizeof(oldkey), "Game.Player%d.%s", seat + 1, g_SeatGameOpts[i].name);
			seatGameKey(newkey, sizeof(newkey), standin, &g_SeatGameOpts[i]);
			moved += configMigrateKey(oldkey, newkey);
		}

		for (u32 ck = 0; ck < CK_TOTAL_COUNT; ck++) {
			snprintf(oldkey, sizeof(oldkey), "Input.Player%d.Binds.%s", seat + 1, inputGetContKeyName(ck));
			seatBindKey(newkey, sizeof(newkey), standin, ck);
			moved += configMigrateKey(oldkey, newkey);
		}
	}

	if (moved) {
		sysLogPrintf(LOG_NOTE, "seat options: moved %d keys from [Game.Player<n>] / [Input.Player<n>.Binds] to [MpPlayer.Player<n>.*]", moved);
	}
}

PD_CONSTRUCTOR static void seatProfileConfigInit(void)
{
	char standin[CONFIG_MAX_SECNAME];

	g_SeatExtCfgDefault = g_PlayerExtCfg[0];

	// every seat starts on its stand-in; configInit then fills them
	for (s32 seat = 0; seat < MAX_PLAYERS; seat++) {
		seatStandInSlug(seat, standin, sizeof(standin));
		seatRegister(seat, standin);
		snprintf(g_SeatSlug[seat], sizeof(g_SeatSlug[seat]), "%s", standin);
	}
}

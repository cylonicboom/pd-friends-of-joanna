#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include "fs.h"
#include "config.h"
#include "system.h"
#include "utils.h"
#include "types.h"
#include "bss.h"

/*
 * Grows on demand. It used to be a fixed 2048 entries, and every key the file
 * holds takes one whether or not anything binds it - so the table fills with
 * the number of profiles ever saved into pd.ini, not with anything running.
 * Per-file options put ~70 keys under every profile, and the boot queue alone
 * can bring in 32 profiles.
 *
 * Nothing holds an entry pointer across an add: configAddEntry already
 * memmoves the tail to keep sections contiguous, so a realloc moving the
 * whole table breaks nothing that the memmove did not already break.
 */
struct configentry *settings = NULL;
static s32 numSettings = 0;
static s32 maxSettings = 0;
static u8 configMaxWarningLogged = 0;
static u8 configGuidQueueWarningLogged = 0;

static inline s32 configClampInt(s32 val, s32 min, s32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline u32 configClampUInt(u32 val, u32 min, u32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline f32 configClampFloat(f32 val, f32 min, f32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline struct configentry *configFindEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return NULL;
}

static s32 configGrow(void)
{
	const s32 newmax = maxSettings ? maxSettings * 2 : CONFIG_MAX_SETTINGS;
	struct configentry *grown = realloc(settings, (size_t)newmax * sizeof(*grown));

	if (!grown) {
		return 0;
	}

	settings = grown;
	maxSettings = newmax;
	return 1;
}

static inline struct configentry *configAddEntry(const char *key)
{
	if (numSettings < maxSettings || configGrow()) {
		const char *delim = strrchr(key, '.');
		s32 seclen = delim ? (delim - key) : 0;

		// Insert after the last entry in the same section to keep sections contiguous
		s32 insertAt = numSettings;
		for (s32 i = numSettings - 1; i >= 0; i--) {
			if (settings[i].seclen == seclen && !strncasecmp(settings[i].key, key, seclen)) {
				insertAt = i + 1;
				break;
			}
		}

		if (insertAt < numSettings) {
			memmove(&settings[insertAt + 1], &settings[insertAt],
				(numSettings - insertAt) * sizeof(struct configentry));
		}

		struct configentry *cfg = &settings[insertAt];
		memset(cfg, 0, sizeof(*cfg));
		snprintf(cfg->key, CONFIG_MAX_KEYNAME, "%s", key);
		cfg->seclen = seclen;
		numSettings++;
		return cfg;
	}
	if (!configMaxWarningLogged) {
		sysLogPrintf(LOG_WARNING, "config: out of memory growing the table past %d entries", maxSettings);
		configMaxWarningLogged = 1;
	}
	return NULL;
}

static inline struct configentry *configFindOrAddEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return configAddEntry(key);
}

static inline const char *configGetSection(char *sec, const struct configentry *cfg)
{
	if (!cfg->seclen || cfg->seclen > CONFIG_MAX_SECNAME) {
		strncpy(sec, cfg->key, CONFIG_MAX_SECNAME);
		sec[CONFIG_MAX_SECNAME] = '\0';
		return sec;
	}

	memcpy(sec, cfg->key, cfg->seclen);
	sec[cfg->seclen] = '\0';

	return sec;
}

/*
 * Keys nothing has bound.
 *
 * Every key configLoad parses becomes an entry, but until something calls
 * configRegister* on it the entry has no ptr. configSet used to throw the
 * value away and configSaveEntry skipped the entry, so any key whose owner
 * had not registered yet by the time of a save was DROPPED from the file:
 * the guest players' [MpPlayer.PlayerN] blocks on any early exit (30 lines
 * of a real pd.ini, measured), and anything a later feature keys by an
 * identity chosen at runtime - a reality, a saved setup - for a reality or
 * setup not touched this session.
 *
 * Now the value waits in `pending`: written back verbatim by configSave,
 * applied by whichever configRegister* eventually binds the key. Nothing
 * needs a queue-and-rebind dance to survive a save any more. The one thing
 * that changes is that forgetting a key becomes an act - configForgetKey -
 * where it used to be the default.
 */
void configSet(struct configentry *cfg, const char *val);

static void configApplyPending(struct configentry *cfg)
{
	if (cfg->pending && cfg->ptr) {
		char *val = cfg->pending;
		cfg->pending = NULL;
		configSet(cfg, val);
		free(val);
	}
}

void configRegisterInt(const char *key, s32 *var, s32 min, s32 max)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_S32;
		cfg->ptr = var;
		cfg->min_s32 = min;
		cfg->max_s32 = max;
		configApplyPending(cfg);
	}
}

void configRegisterUInt(const char* key, u32* var, u32 min, u32 max)
{
	struct configentry* cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_U32;
		cfg->ptr = var;
		cfg->min_u32 = min;
		cfg->max_u32 = max;
		configApplyPending(cfg);
	}
}

void configRegisterU8Int(const char* key, u8* var, u32 min, u32 max)
{
	struct configentry* cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_U8;
		cfg->ptr = var;
		cfg->min_u32 = min;
		cfg->max_u32 = max;
		configApplyPending(cfg);
	}
}

void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_F32;
		cfg->ptr = var;
		cfg->min_f32 = min;
		cfg->max_f32 = max;
		configApplyPending(cfg);
	}
}

void configRegisterString(const char *key, char *var, u32 maxstr)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_STR;
		cfg->ptr = var;
		cfg->max_str = maxstr;
		configApplyPending(cfg);
	}
}

// strdup is POSIX, not C99; the build asks for C99.
static char *configStrdup(const char *src)
{
	size_t len = strlen(src) + 1;
	char *dst = malloc(len);
	if (dst) {
		memcpy(dst, src, len);
	}
	return dst;
}

void configForgetKey(const char *key)
{
	struct configentry *cfg = configFindEntry(key);
	if (cfg) {
		free(cfg->pending);
		cfg->pending = NULL;
		cfg->ptr = NULL;
	}
}

void configUnbindKey(const char *key)
{
	struct configentry *cfg = configFindEntry(key);
	char buf[64];

	if (!cfg || !cfg->ptr) {
		return;
	}

	switch (cfg->type) {
		case CFG_S32: snprintf(buf, sizeof(buf), "%d", *(s32 *)cfg->ptr); break;
		case CFG_U32: snprintf(buf, sizeof(buf), "%u", *(u32 *)cfg->ptr); break;
		case CFG_U8:  snprintf(buf, sizeof(buf), "%u", *(u8 *)cfg->ptr); break;
		case CFG_F32: snprintf(buf, sizeof(buf), "%f", *(f32 *)cfg->ptr); break;
		case CFG_STR:
			free(cfg->pending);
			cfg->pending = configStrdup((char *)cfg->ptr);
			cfg->ptr = NULL;
			return;
		default: buf[0] = '\0'; break;
	}

	free(cfg->pending);
	cfg->pending = configStrdup(buf);
	cfg->ptr = NULL;
}

void configSet(struct configentry *cfg, const char *val) {
	s32 tmp_s32;
	f32 tmp_f32;
	u32 tmp_u32;
	u8  tmp_u8;
	if (!cfg->ptr) {
		// Nothing owns this key yet; keep the value for whoever does.
		free(cfg->pending);
		cfg->pending = configStrdup(val);
		return;
	}
	switch (cfg->type) {
		case CFG_S32:
			tmp_s32 = strtol(val, NULL, 0);
			if (cfg->min_s32 < cfg->max_s32) {
				tmp_s32 = configClampInt(tmp_s32, cfg->min_s32, cfg->max_s32);
			}
			*(s32 *)cfg->ptr = tmp_s32;
			break;
		case CFG_F32:
			tmp_f32 = strtof(val, NULL);
			if (cfg->min_f32 < cfg->max_f32) {
				tmp_f32 = configClampFloat(tmp_f32, cfg->min_f32, cfg->max_f32);
			}
			*(f32 *)cfg->ptr = tmp_f32;
			break;
		case CFG_U32:
			tmp_u32 = strtoul(val, NULL, 0);
			if (cfg->min_u32 < cfg->max_u32) {
				tmp_u32 = configClampUInt(tmp_u32, cfg->min_u32, cfg->max_u32);
			}
			*(u32*)cfg->ptr = tmp_u32;
			break;
		case CFG_U8:
			tmp_u8 = strtoul(val, NULL, 0);
			if (cfg->min_u8 < cfg->max_u8) {
				tmp_u8 = configClampUInt(tmp_u8, cfg->min_u8, cfg->max_u8);
			}
			*(u8*)cfg->ptr = tmp_u8;
			break;
		case CFG_STR:
			strncpy(cfg->ptr, val, cfg->max_str ? cfg->max_str - 1 : 4096);
			break;
		default:
			break;
	}
}

static inline void configSetFromString(const char *key, const char *val)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (!cfg) {
		return;
	}

	configSet(cfg, val);
}

static void configSaveEntry(struct configentry *cfg, FILE *f)
{
	if (!cfg->ptr) {
		if (cfg->pending) {
			fprintf(f, "%s=%s\n", cfg->key + cfg->seclen + 1, cfg->pending);
		}
		return;
	}
	switch (cfg->type) {
		case CFG_S32:
			if (cfg->min_s32 < cfg->max_s32) {
				*(s32 *)cfg->ptr = configClampInt(*(s32 *)cfg->ptr, cfg->min_s32, cfg->max_s32);
			}
			fprintf(f, "%s=%d\n", cfg->key + cfg->seclen + 1, *(s32 *)cfg->ptr);
			break;
		case CFG_F32:
			if (cfg->min_f32 < cfg->max_f32) {
				*(f32 *)cfg->ptr = configClampFloat(*(f32 *)cfg->ptr, cfg->min_f32, cfg->max_f32);
			}
			fprintf(f, "%s=%f\n", cfg->key + cfg->seclen + 1, *(f32 *)cfg->ptr);
			break;
		case CFG_U32:
			if (cfg->min_u32 < cfg->max_u32) {
				*(u32*)cfg->ptr = configClampUInt(*(u32*)cfg->ptr, cfg->min_u32, cfg->max_u32);
			}
			fprintf(f, "%s=%u\n", cfg->key + cfg->seclen + 1, *(u32 *)cfg->ptr);
			break;
		case CFG_U8:
			if (cfg->min_u8 < cfg->max_u8) {
				*(u8*)cfg->ptr = configClampUInt(*(u8*)cfg->ptr, cfg->min_u8, cfg->max_u8);
			}
			fprintf(f, "%s=%u\n", cfg->key + cfg->seclen + 1, *(u8 *)cfg->ptr);
			break;
		case CFG_STR:
			fprintf(f, "%s=%s\n", cfg->key + cfg->seclen + 1, (char *)cfg->ptr);
			break;
		default:
			break;
	}
}

struct configentry *configFindEntryByPtr(void *ptr)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (settings[i].ptr == ptr) {
			return &settings[i];
		}
	}
	return NULL;
}

s32 configSave(const char *fname)
{
	FILE *f = fsFileOpenWriteAtomic(fname);
	if (!f) {
		return 0;
	}

	char tmpSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };

	if (numSettings == 0) {
		return fsFileCommitAtomic(f, fname);
	}

	configGetSection(curSec, &settings[0]);
	fprintf(f, "[%s]\n", curSec);

	for (s32 i = 0; i < numSettings; ++i) {
		struct configentry *cfg = &settings[i];
		configGetSection(tmpSec, cfg);
		if (strncmp(curSec, tmpSec, CONFIG_MAX_SECNAME) != 0) {
			fprintf(f, "\n[%s]\n", tmpSec);
			strncpy(curSec, tmpSec, CONFIG_MAX_SECNAME);
		}
		configSaveEntry(cfg, f);
	}

	return fsFileCommitAtomic(f, fname);
}

static inline s32 configLoadFileIdFromSection(const char *sec, u16 *deviceserial, s32 *fileid)
{
    u32 tmp_deviceserial = 0;
    s32 tmp_fileid = 0;

    // Ensure sscanf matches both fields
    if (sscanf(sec, "MpPlayer.%x-%x", &tmp_deviceserial, &tmp_fileid) == 2) {
        *deviceserial = tmp_deviceserial;
        *fileid = tmp_fileid;
        return 1;
    }

    // If sscanf did not match both fields, return 0
    return 0;
}

/*
 * One parser for the three ways the ini is read.
 *
 *   key == NULL, scan == NULL   full load: every key is applied and every
 *                               [MpPlayer.*] header is queued for binding
 *   key != NULL                 re-read exactly that one key, nothing else
 *   scan != NULL                do not apply anything; hand every key in that
 *                               one section to the callback, value included
 *
 * The third is for sections whose key names are not known until the file has
 * been read - the caller allocates storage per name and registers it, which
 * is what keeps the entry alive through the next configSave. Without that a
 * key nobody registered sits in the table with a NULL ptr, and both configSet
 * and configSaveEntry skip a NULL ptr, so it is silently dropped on save.
 */
/*
 * One pass over the ini. `key` applies exactly that key, `scan` hands every
 * key of one section to a callback, `loadsection` applies every key of one
 * section, and all three NULL loads the lot.
 *
 * loadsection exists because binding a profile used to call configLoadKey
 * once per property, and configLoadKey re-opens and re-parses the WHOLE file
 * every time - O(properties x filesize) per profile bind, on a file that
 * grows with the number of profiles.
 */
static s32 configParseFile(const char *fname, char *key, const struct configsectionscan *scan, const char *loadsection)
{
	FILE *f = fsFileOpenRead(fname);
	if (!f) {
		return 0;
	}

	char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	char keyBuf[CONFIG_MAX_SECNAME * 2 + 2] = { 0 }; // SECTION + . + KEY + \0
	char nameBuf[CONFIG_MAX_SECNAME + 1] = { 0 };   // KEY on its own, for scan
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char lineBuf[2048] = { 0 };
	char *line = lineBuf;
	s32 lineLen = 0;

	while (fgets(lineBuf, sizeof(lineBuf), f)) {
		line = lineBuf;

		line = strParseToken(line, token, NULL);

		if (token[0] == '[' && token[1] == '\0') {
			// section; get name
			line = strParseToken(line, token, NULL);
			if (!token[0]) {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
				continue;
			}
			strncpy(curSec, token, CONFIG_MAX_SECNAME);
			// detect if curSec has a fileguid in it
			u16 deviceserial = 0;
			s32 fileid = 0;
			s32 configindex = -1;
			if (!key && !scan && !loadsection && configLoadFileIdFromSection(curSec, &deviceserial, &fileid)) {
				if (g_NumGuidsToProcess < ARRAYCOUNT(g_GuidsToProcess)) {
					g_GuidsToProcess[g_NumGuidsToProcess++] = (struct fileguid) { fileid, deviceserial };
				} else if (!configGuidQueueWarningLogged) {
					configGuidQueueWarningLogged = 1;
					sysLogPrintf(LOG_WARNING, "configLoad: more than %d [MpPlayer.*] sections in %s; the rest will not keep their settings",
						ARRAYCOUNT(g_GuidsToProcess), fname);
				}
			}
			// eat ]
			line = strParseToken(line, token, NULL);
			if (token[0] != ']' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
			}
		} else if (token[0]) {
			// probably a key=value pair; append key name to section name
			snprintf(nameBuf, sizeof(nameBuf), "%s", token);
			snprintf(keyBuf, sizeof(keyBuf) - 1, "%s.%s", curSec, token);
			// eat =
			line = strParseToken(line, token, NULL);
			if (token[0] != '=' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed keyvalue line: %s", lineBuf);
				continue;
			}
			// the rest of the line is the value
			line = strTrim(line);
			if (line[0] == '"') {
				line = strUnquote(line);
			}
			if (scan) {
				if (!strcasecmp(curSec, scan->section)) {
					scan->fn(nameBuf, line, scan->ctx);
				}
			} else if (loadsection) {
				if (!strcasecmp(curSec, loadsection)) {
					configSetFromString(keyBuf, line);
				}
			} else if (!key || strcmp(keyBuf, key) == 0) {
				configSetFromString(keyBuf, line);
			}
		}
	}

	fsFileFree(f);

	return 1;

}

s32 configLoadKey(const char *fname, char *key)
{
	return configParseFile(fname, key, NULL, NULL);
}

s32 configLoadSection(const char *fname, const char *section)
{
	return configParseFile(fname, NULL, NULL, section);
}

s32 configLoad(const char *fname)
{
	return configParseFile(fname, 0, NULL, NULL);
}

s32 configScanSection(const char *fname, const char *section, configsectionfunc fn, void *ctx)
{
	struct configsectionscan scan = { section, fn, ctx };

	if (!section || !fn) {
		return 0;
	}

	return configParseFile(fname, 0, &scan, NULL);
}

void configInit(void)
{
	if (fsFileSize(CONFIG_PATH) > 0) {
		configLoad(CONFIG_PATH);
	}
}

s32 getPlayerConfigSlug(s32 playernum, char *out, char *key)
{
	struct fileguid *guid = &(g_PlayerConfigsArray[playernum].fileguid);
	if (guid && key) {
		sprintf(out, "MpPlayer.%x-%x.%s\0",  guid->deviceserial, guid->fileid, key);
		return 1;
	} else if (guid) {
		sprintf(out, "MpPlayer.%x-%x\0",  guid->deviceserial, guid->fileid);
		return 1;
	}
	return 0;
}

s32 getConfigIndexFromDB(u16 deviceserial, s32 fileid) {
	s32 retval = -1;
	for (s32 i = 0; i < numSettings; ++i) {
		if (strncmp(settings[i].key, "MpPlayer.", 9) != 0) {
			continue;
		}
	}
	return retval;
}

#pragma once

#include <PR/ultratypes.h>

// Same guard mod.h carries. Without it every declaration below gets C++
// linkage when a .cpp includes this, while config.c compiles as C - so any
// C++ caller fails to link. Nothing called it from C++ until the overlay's
// Saves panel did.
#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_FNAME "pd.ini"
#define CONFIG_PATH "$S/" CONFIG_FNAME


#define CONFIG_MAX_SECNAME 128
#define CONFIG_MAX_KEYNAME 256
// initial capacity; the table doubles past it
#define CONFIG_MAX_SETTINGS (256 * 8)

typedef enum {
	CFG_NONE,
	CFG_S32,
	CFG_F32,
	CFG_U32,
	CFG_STR,
	CFG_U8
} configtype;

struct configentry {
	char key[CONFIG_MAX_KEYNAME + 1];
	s32 seclen;
	configtype type;
	void *ptr;
	// A value parsed from the file for a key nothing has bound yet. Kept so
	// configSave writes it back verbatim instead of dropping it, and applied
	// the moment a configRegister* binds the key. See the note in config.c.
	char *pending;
	union {
		struct { f32 min_f32, max_f32; };
		struct { s32 min_s32, max_s32; };
		struct { u32 min_u32, max_u32; };
		struct { u8 min_u8, max_u8; };
		u32 max_str;
	};
};

extern struct configentry *settings;

void configInit(void);

// loads config from file (path extensions such as ! apply)
s32 configLoad(const char *fname);
s32 configLoadKey(const char *fname, char *key);
// Applies every key of one section in a single pass. Prefer this to calling
// configLoadKey in a loop - that re-parses the whole file per key.
s32 configLoadSection(const char *fname, const char *section);

// Reads one section out of the file without applying anything, calling fn for
// each key in it with the key name and the raw value. For sections whose key
// names are only known from the file itself; the callback is expected to
// allocate storage and configRegister* it, which is what keeps the key alive
// through the next configSave.
typedef void (*configsectionfunc)(const char *key, const char *value, void *ctx);
struct configsectionscan {
	const char *section;
	configsectionfunc fn;
	void *ctx;
};
s32 configScanSection(const char *fname, const char *section, configsectionfunc fn, void *ctx);

// saves config to file (path extensions such as ! apply)
s32 configSave(const char *fname);

// registers a variable in the config file
// this should be done before configInit() is called, preferably in a module constructor
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max);
void configRegisterUInt(const char* key, u32* var, u32 min, u32 max);
void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max);
void configRegisterString(const char *key, char *var, u32 maxstr);
// Drop a parsed-but-unbound key so the next configSave leaves it out. The
// only way an entry that was in the file goes away.
void configForgetKey(const char *key);
// Detach a bound key but keep its current value, so a later save still
// writes it and a later bind still reads it. For state that belongs to
// something not loaded any more - a reality that was switched away from.
void configUnbindKey(const char *key);

// player save stuff
struct configentry *configFindPlayerEntry(s32 player, const char *key);
struct configentry *configFindOrAddPlayerEntry(s32 player, const char *key);
void configSetPlayerEntry(s32 player, const char *key, const char *val);
s32 getPlayerConfigSlug(s32 playernum, char *out, char* key);
void configRegisterU8Int(const char *key, u8 *var, u32 min, u32 max);

struct configentry *configFindEntryByPtr(void *ptr);

s32 getConfigIndexFromDB(u16 deviceserial, s32 fileid);

#ifdef __cplusplus
}
#endif

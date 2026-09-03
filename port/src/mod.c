#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "utils.h"
#include "romdata.h"
#include "mod.h"
#include "data.h"
#include "bss.h"
#include "game/body.h"
#include "game/training.h"
#include "game/stagetable.h"

#define DEBUG_MODELS(fmt, ...) \
	do { if (g_DebugModels) sysLogPrintf(LOG_NOTE, fmt, ##__VA_ARGS__); } while (0)
#include "game/mplayer/mplayer.h"
#include "game/mplayer/setup.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"

s32 g_TexModNum = -1;
s32 g_TexCurrentModelFileNum = 0;

extern struct stagemusic g_StageTracks[];
extern struct stageallocation g_StageAllocations8Mb[];
extern s32 g_MainIsBooting;
extern s32 g_MainChangeToStageNum;

extern struct headorbody *g_HeadsAndBodies;
extern struct headorbody g_HeadsAndBodiesOriginal[];
extern s32 g_NumHeadsAndBodies;
extern const u32 g_NumHeadsAndBodies_Original;

extern struct mphead *g_MpHeads;
extern struct mphead g_MpHeadsOriginal[];
extern s32 g_NumMpHeads;
extern const u32 g_NumMpHeads_Original;

extern struct mpbody *g_MpBodies;
extern struct mpbody g_MpBodiesOriginal[];
extern s32 g_NumMpBodies;
extern const u32 g_NumMpBodies_Original;

extern u32 g_NumModDirs;
extern char modDirs[64][FS_MAXPATH + 1];

extern struct modelstate g_ModelStates[NUM_MODELS];
extern s8 g_PropExplosionTypes[];

struct texturesurfaceconfig g_VanillaTextures[NUM_TEXTURES];
struct modelstate g_ModelStatesOriginal[NUM_MODELS];
s8 g_PropExplosionTypesOriginal[NUM_MODELS];

static struct { char *name; s32 id; } *g_ModHeadNames = NULL;
static s32 g_NumModHeadNames = 0;

static struct { char *name; u32 filenum; } *g_ModHandFileNames = NULL;
static s32 g_NumModHandFileNames = 0;

// Per-mod cached config data (parsed once at boot, then just copied on modSwitch)
struct modelstate g_ModelStates_PerMod[64][NUM_MODELS];
s8 g_ExplosionTypes_PerMod[64][NUM_MODELS];
static bool g_ModConfigsCached = false;

s32 g_ModStageNums[STAGE_4MBMENU];
u8 g_StageModFlags[256];

char g_ModNames[64][64];
char g_ModVersions[64][32];

struct mparenagroup *g_MpArenaGroups = NULL;
s32 g_NumMpArenaGroups = 0;

#define MAX_IMPORTED_ASSETS 256
static char *g_ImportedAssets[MAX_IMPORTED_ASSETS];
static s32 g_NumImportedAssets = 0;

#define PARSE_STAGE_FLOAT(sec, name, v, min, max) \
	p = modConfigParseFloatValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

#define PARSE_STAGE_INT(sec, name, v, min, max) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

#define PARSE_STAGE_FILENAME(sec, name, v) \
	p = modConfigParseFileValue(p, token, &v, modnum); \
	if (!p) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

#define PARSE_STAGE_STRING(sec, name, v) \
	p = strParseToken(p, token, NULL); \
	if (!p) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	} \
	v = strUnquote(token);

#define PARSE_INT(sec, name, v, min, max, ret) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "mod: %s: invalid " name " value: %s", sec, token); \
		return ret; \
	}

static inline char *modConfigParseStringValue(char *p, char *token, char *value){
	p = strParseToken(p, token, NULL);
	if (!p) {
		return NULL;
	}
	char *t = strUnquote(token);
	strcpy(value, t);
	sysLogPrintf(LOG_NOTE, "modconfigParseStringValue %s", value);
	return p;
}
static inline char *modConfigParseFileValue(char *p, char *token, s32 *filenum, s32 modNum)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	// check if it is a number already
	s32 num = strtol(token, NULL, 0);
	if (num > 0 && romdataFileGetName(num)) {
		*filenum = num;
		return p;
	}
	// it's a filename
	char *unquoted = strUnquote(token);
	num = romdataFileGetNumForNameInMod(unquoted, modNum);
	if (num >= 0) {
		*filenum = num;
		return p;
	}
	sysLogPrintf(LOG_ERROR, "modConfigParseFileValue: failed to find '%s' in mod %d", unquoted, modNum);
	// the filename was invalid
	return NULL;
}

static inline char *modConfigParseIntValue(char *p, char *token, s32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	char *endp = token;
	const s32 num = strtol(token, &endp, 0);
	if (num == 0 && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

static inline char *modConfigParseFloatValue(char *p, char *token, f32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	char *endp = token;
	const f32 num = strtof(token, &endp);
	if (num == 0.f && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

static char *modConfigParseStageMusic(char *p, char *token, s32 stagenum)
{
	struct stagemusic *smus = NULL;
	for (struct stagemusic *p = g_StageTracks; p->stagenum; ++p) {
		if (p->stagenum == stagenum) {
			smus = p;
			break;
		}
	}

	if (!smus) {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: music can't be changed for this stage", stagenum);
		return NULL;
	}

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// parse keyvalues until } is reached
	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "primarytrack")) {
			PARSE_STAGE_INT("music:", "primarytrack", tmp, 0, 128);
			smus->primarytrack = tmp;
		} else if (!strcmp(token, "ambienttrack")) {
			PARSE_STAGE_INT("music:", "ambienttrack", tmp, 0, 128);
			smus->ambienttrack = tmp;
		} else if (!strcmp(token, "xtrack")) {
			PARSE_STAGE_INT("music:", "xtrack", tmp, 0, 128);
			smus->xtrack = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: music: invalid key: %s", stagenum, token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unterminated music block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseStageWeatherRooms(char *p, char *token, s32 stagenum, struct weathercfg *wcfg)
{
	// determine where we can start adding rooms
	s32 idx;
	for (idx = 0; idx < WEATHERCFG_MAX_SKIPROOMS && wcfg->skiprooms[idx]; ++idx);

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// check if user wants to clear the whole list
	p = strParseToken(p, token, NULL);
	if (!strcmp(token, "clear")) {
		memset(wcfg->skiprooms, 0, sizeof(wcfg->skiprooms));
		idx = 0;
		p = strParseToken(p, token, NULL);
	}

	s32 tmp = 0;
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (token[0] == ',' && !token[1]) {
			p = strParseToken(p, token, NULL);
			continue;
		}

		tmp = strtol(token, NULL, 0);
		if (tmp <= 0 || tmp > 32767) {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: weather: rooms: invalid room %s", stagenum, token);
			return NULL;
		}

		if (idx < WEATHERCFG_MAX_SKIPROOMS) {
			wcfg->skiprooms[idx++] = tmp;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: weather: unterminated rooms block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseStageWeather(char *p, char *token, s32 stagenum)
{
	s32 wi;
	struct weathercfg *wcfg = NULL;
	for (wi = 0; wi < ARRAYCOUNT(g_WeatherConfig) && g_WeatherConfig[wi].stagenum; ++wi) {
		if (g_WeatherConfig[wi].stagenum == stagenum) {
			break;
		}
	}

	if (wi >= WEATHERCFG_MAX_STAGES) {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: no more space for weather config", stagenum);
		return NULL;
	}

	wcfg = &g_WeatherConfig[wi];

	if (!wcfg->stagenum) {
		// new weather config; initialize with defaults
		*wcfg = g_DefaultWeatherConfig;
		wcfg->stagenum = stagenum;
	} else {
		// flags have to be re-specified
		wcfg->flags = 0;
	}

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// parse keyvalues until } is reached
	s32 tmpi = 0;
	f32 tmpf = 0.f;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "include_rooms") || !strcmp(token, "exclude_rooms")) {
			// include_rooms | exclude_rooms { ROOM_NUMBERS... }
			const s32 include = (token[0] == 'i');
			p = modConfigParseStageWeatherRooms(p, token, stagenum, wcfg);
			if (!p) {
				return NULL;
			}
			if (wcfg->skiprooms[0] && include) {
				wcfg->flags |= WEATHERFLAG_INCLUDE;
			}
		} else if (!strcmp(token, "cutscene_only")) {
			wcfg->flags |= WEATHERFLAG_CUTSCENE_ONLY;
		} else if (!strcmp(token, "constant_wind")) {
			PARSE_STAGE_FLOAT("weather:", "constant_wind (0)", wcfg->windanglerad, -M_TAU, M_TAU);
			PARSE_STAGE_FLOAT("weather:", "constant_wind (1)", wcfg->windspeedx, -1024.f, 1024.f);
			PARSE_STAGE_FLOAT("weather:", "constant_wind (2)", wcfg->windspeedz, -1024.f, 1024.f);
			wcfg->flags |= WEATHERFLAG_FORCE_WINDDIR;
		} else if (!strcmp(token, "windspeed")) {
			PARSE_STAGE_FLOAT("weather:", "windspeed", tmpf, -1024.f, 1024.f);
			wcfg->windspeed = tmpf;
		} else if (!strcmp(token, "ymin")) {
			PARSE_STAGE_FLOAT("weather:", "ymin", tmpf, -65536.f, 65536.f);
			wcfg->ymin = tmpf;
		} else if (!strcmp(token, "ymax")) {
			PARSE_STAGE_FLOAT("weather:", "ymax", tmpf, -65536.f, 65536.f);
			wcfg->ymax = tmpf;
		} else if (!strcmp(token, "zmax")) {
			PARSE_STAGE_FLOAT("weather:", "zmax", tmpf, -65536.f, 65536.f);
			wcfg->zmax = tmpf;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: weather: invalid key: %s", stagenum, token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unterminated weather block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseTexture(char *p, char *token, s32 modnum) {
	s32 texid = 0;
	sysLogPrintf(LOG_ERROR, "modconfigParseTexture: parsing texture block: %s, p: %s", token, p);
	if (sscanf(p, "%x", &texid) == 1) {
		sysLogPrintf(LOG_ERROR, "modconfigParseTexture: found texture id: %d", texid);
		while (1) {

			p = strParseToken(p, token, NULL);
			// p = modConfigNextToken(p, token);
			if (!p || strcmp(token, "}") == 0)
				break;

			if (strncmp(token, "surfacetype", 11) == 0) {
				s32 val = 0;
				sscanf(token + 11, "%d", &val);
				g_Textures[texid].surfacetype = val;
			} else if (strncmp(token, "soundsurfacetype", 16) == 0) {
				s32 val = 0;
				sscanf(token + 16, "%d", &val);
				g_Textures[texid].soundsurfacetype = val;
			}
		}
	}
	return p;
}

static char *modConfigSkipBlock(char *p, char *token)
{
	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{') return NULL;

	// skip until }
	int depth = 1;
	while (p && token[0] && depth > 0) {
		p = strParseToken(p, token, NULL);
		if (!strcmp(token, "{")) depth++;
		else if (!strcmp(token, "}")) depth--;
	}
	return p;
}

void modResetMplayerArrays(void)
{
	if (g_HeadsAndBodies != g_HeadsAndBodiesOriginal) {
		free(g_HeadsAndBodies);
		g_HeadsAndBodies = g_HeadsAndBodiesOriginal;
		g_NumHeadsAndBodies = g_NumHeadsAndBodies_Original;
	}

	if (g_MpHeads != g_MpHeadsOriginal) {
		free(g_MpHeads);
		g_MpHeads = g_MpHeadsOriginal;
		g_NumMpHeads = g_NumMpHeads_Original;
	}

	if (g_MpBodies != g_MpBodiesOriginal) {
		free(g_MpBodies);
		g_MpBodies = g_MpBodiesOriginal;
		g_NumMpBodies = g_NumMpBodies_Original;
	}

	if (g_MpArenas_AIO) {
		for (s32 i = 0; i < g_NumMpArenas_AIO; i++) {
			if (g_MpArenas_AIO[i].customname) {
				free(g_MpArenas_AIO[i].customname);
			}
		}
		free(g_MpArenas_AIO);
		g_MpArenas_AIO = NULL;
		g_NumMpArenas_AIO = 0;
	}

	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		free(g_ModHeadNames[i].name);
	}
	free(g_ModHeadNames);
	g_ModHeadNames = NULL;
	g_NumModHeadNames = 0;

	for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
		free(g_ModHandFileNames[i].name);
	}
	free(g_ModHandFileNames);
	g_ModHandFileNames = NULL;
	g_NumModHandFileNames = 0;

	for (s32 i = 0; i < g_NumImportedAssets; ++i) {
		if (g_ImportedAssets[i]) {
			free(g_ImportedAssets[i]);
		}
	}
	g_NumImportedAssets = 0;
}

struct modconfigslotinfo {
	s32 slotNum;
	s32 bodySlotNum;
	s32 bodyName;
	s32 bodyHeadNum;
	u8 requireFeature;
	char handName[64];
};

// Returns updated p. Sets *skipEntry=1 if the entry should be discarded
// (e.g. file not found). On hard parse failure (unknown key, malformed
// value), returns NULL with *skipEntry left as 0.
static char *modConfigParseHeadOrBodyEntry(char *p, char *token, struct headorbody *item, s32 modNum, char *nameOut, struct modconfigslotinfo *slotInfo, s32 *skipEntry)
{
	s32 tmp = 0;
	f32 tmpf = 0.0f;
	char tmps[64] = "";
	if (skipEntry) *skipEntry = 0;

	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "ismale")) {
			PARSE_INT("HeadsAndBodies", "ismale", tmp, 0, 1, NULL);
			item->ismale = tmp;
		}	else if (!strcmp(token, "requiresrom")) {
			p = modConfigParseStringValue(p, token, &tmps);
			if (p) {
				sysLogPrintf(LOG_NOTE, "requiresrom %s", tmps);
				if (!romsourceIsMounted(tmps)){
					if (skipEntry) *skipEntry = 1;
					return NULL;
				}
			}
		} else if (!strcmp(token, "slotnum")) {
			PARSE_INT("HeadsAndBodies", "slotnum", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->slotNum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("HeadsAndBodies", "requirefeature", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->requireFeature = tmp;
		} else if (!strcmp(token, "bodyslotnum")) {
			PARSE_INT("HeadsAndBodies", "bodyslotnum", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->bodySlotNum = tmp;
		} else if (!strcmp(token, "bodyname")) {
			PARSE_INT("HeadsAndBodies", "bodyname", tmp, 0, 0xFFFF, NULL);
			if (slotInfo) slotInfo->bodyName = tmp;
		} else if (!strcmp(token, "bodyheadnum")) {
			PARSE_INT("HeadsAndBodies", "bodyheadnum", tmp, 0, 0xFFFF, NULL);
			if (slotInfo) slotInfo->bodyHeadNum = tmp;
		} else if (!strcmp(token, "hasownhead") || !strcmp(token, "unk00_01")) {
			// `unk00_01` is the legacy field name kept for back-compat.
			PARSE_INT("HeadsAndBodies", "hasownhead", tmp, 0, 1, NULL);
			item->unk00_01 = tmp;
		} else if (!strcmp(token, "canvaryheight")) {
			PARSE_INT("HeadsAndBodies", "canvaryheight", tmp, 0, 1, NULL);
			item->canvaryheight = tmp;
		} else if (!strcmp(token, "type")) {
			PARSE_INT("HeadsAndBodies", "type", tmp, 0, 7, NULL);
			item->type = tmp;
		} else if (!strcmp(token, "height")) {
			PARSE_INT("HeadsAndBodies", "height", tmp, 0, 255, NULL);
			item->height = tmp;
		} else if (!strcmp(token, "filenum")) {
			p = modConfigParseFileValue(p, token, &tmp, modNum);
			if (!p) {
				sysLogPrintf(LOG_WARNING, "modconfig: HeadsAndBodies '%s': filenum unresolved, skipping entry",
				             nameOut && nameOut[0] ? nameOut : "?");
				if (skipEntry) *skipEntry = 1;
				return NULL;
			}
			item->filenum = tmp | (modNum << 16);
		} else if (!strcmp(token, "scale")) {
			p = modConfigParseFloatValue(p, token, &tmpf);
			if (!p) return NULL;
			item->scale = tmpf;
		} else if (!strcmp(token, "animscale")) {
			p = modConfigParseFloatValue(p, token, &tmpf);
			if (!p) return NULL;
			item->animscale = tmpf;
		} else if (!strcmp(token, "handfilenum")) {
			p = modConfigParseFileValue(p, token, &tmp, modNum);
			if (!p) {
				sysLogPrintf(LOG_WARNING, "modconfig: HeadsAndBodies '%s': handfilenum unresolved, skipping entry",
				             nameOut && nameOut[0] ? nameOut : "?");
				if (skipEntry) *skipEntry = 1;
				return NULL;
			}
			item->handfilenum = tmp | (modNum << 16);
			if (slotInfo) {
				strncpy(slotInfo->handName, strUnquote(token), 63);
				slotInfo->handName[63] = '\0';
			}
		} else if (!strcmp(token, "yoffset")) {
			PARSE_INT("HeadsAndBodies", "yoffset", tmp, -1000, 1000, NULL);
			item->yoffset = tmp;
		} else if (!strcmp(token, "name")) {
			p = strParseToken(p, token, NULL);
			if (nameOut) {
				strncpy(nameOut, strUnquote(token), 63);
				nameOut[63] = '\0';
			}
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: HeadsAndBodies: invalid key: %s", token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}
	return p;
}

static char *modConfigParseModelStates(char *p, char *token, s32 modNum)
{
	sysLogPrintf(LOG_NOTE, "modconfig: Parsing ModelStates block for mod %d", modNum);
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '{', got '%s'", token);
		return NULL;
	}

	s32 numParsed = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		// Parse model ID (hex number)
		s32 modelId = strtol(token, NULL, 0);
		sysLogPrintf(LOG_NOTE, "  Parsing model ID: %s -> 0x%04x", token, modelId);
		if (modelId < 0 || modelId >= NUM_MODELS) {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: invalid model ID: %s", token);
			return NULL;
		}

		// Expect '{'
		p = strParseToken(p, token, NULL);
		if (token[0] != '{' || token[1] != '\0') {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '{' after model ID, got '%s'", token);
			return NULL;
		}

		// Parse key-value pairs
		p = strParseToken(p, token, NULL);
		while (p && token[0] && strcmp(token, "}") != 0) {
			// Strip trailing colon from token if present
			s32 len = strlen(token);
			if (len > 0 && token[len - 1] == ':') {
				token[len - 1] = '\0';
			}

			if (!strcasecmp(token, "File")) {
				s32 fileNum = 0;
				p = modConfigParseFileValue(p, token, &fileNum, modNum);
				if (!p) return NULL;
				g_ModelStates[modelId].fileid = fileNum | (modNum << 16);
			} else if (!strcasecmp(token, "Scale")) {
				f32 scale = 0.0f;
				p = modConfigParseFloatValue(p, token, &scale);
				if (!p || scale <= 0.0f) {
					sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: invalid scale value: %s", token);
					return NULL;
				}
				// Convert float scale to fixed-point (scale * 4096)
				u16 fixedScale = (u16)(scale * 4096.0f);
				g_ModelStates[modelId].scale = fixedScale;
				DEBUG_MODELS("  Model 0x%04x: scale %.4f -> 0x%04x (was 0x%04x)",
					modelId, scale, fixedScale, g_ModelStatesOriginal[modelId].scale);
				numParsed++;
			} else {
				sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: unknown key: %s", token);
				return NULL;
			}
			p = strParseToken(p, token, NULL);
		}

		if (token[0] != '}') {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '}' after model properties");
			return NULL;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '}' at end of block");
		return NULL;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: ModelStates: parsed %d model overrides", numParsed);
	return p;
}

static char *modConfigParseExplosionTypes(char *p, char *token, s32 modNum)
{
	sysLogPrintf(LOG_NOTE, "modconfig: Parsing ExplosionTypes block for mod %d", modNum);
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '{', got '%s'", token);
		return NULL;
	}

	s32 numParsed = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		// Parse model ID (hex number)
		s32 modelId = strtol(token, NULL, 0);
		if (modelId < 0 || modelId >= NUM_MODELS) {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: invalid model ID: %s", token);
			return NULL;
		}

		// Expect '{'
		p = strParseToken(p, token, NULL);
		if (token[0] != '{' || token[1] != '\0') {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '{' after model ID, got '%s'", token);
			return NULL;
		}

		// Parse key-value pairs
		p = strParseToken(p, token, NULL);
		while (p && token[0] && strcmp(token, "}") != 0) {
			if (!strcasecmp(token, "Type")) {
				p = strParseToken(p, token, NULL);
				if (!p || token[0] != ':'  || token[1] != '\0') {
					sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected ':' after 'Type'");
					return NULL;
				}
				p = strParseToken(p, token, NULL);
				if (!p) return NULL;

				// Map explosion type name to value
				s8 expType = -1;
				if (!strcasecmp(token, "NONE")) expType = 0;
				else if (!strcasecmp(token, "BULLETHOLE")) expType = 1;
				else if (!strcasecmp(token, "EYESPY")) expType = 2;
				else if (!strcasecmp(token, "LAPTOP")) expType = 3;
				else if (!strcasecmp(token, "A51TABLE")) expType = 4;
				else if (!strcasecmp(token, "FRTARGET")) expType = 5;
				else if (!strcasecmp(token, "6")) expType = 6;
				else if (!strcasecmp(token, "7")) expType = 7;
				else if (!strcasecmp(token, "8")) expType = 8;
				else if (!strcasecmp(token, "9")) expType = 9;
				else if (!strcasecmp(token, "11")) expType = 11;
				else if (!strcasecmp(token, "12")) expType = 12;
				else if (!strcasecmp(token, "ROCKET")) expType = 13;
				else if (!strcasecmp(token, "GASBARREL")) expType = 14;
				else if (!strcasecmp(token, "16")) expType = 16;
				else if (!strcasecmp(token, "HUGE17")) expType = 17;
				else if (!strcasecmp(token, "BONDEXPLODE")) expType = 18;
				else if (!strcasecmp(token, "SDGRENADE")) expType = 21;
				else if (!strcasecmp(token, "PHOENIX")) expType = 22;
				else if (!strcasecmp(token, "DRAGONBOMBSPY")) expType = 23;
				else if (!strcasecmp(token, "24")) expType = 24;
				else if (!strcasecmp(token, "HUGE25")) expType = 25;
				else {
					sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: unknown explosion type: %s", token);
					return NULL;
				}

				g_PropExplosionTypes[modelId] = expType;
			} else {
				sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: unknown key: %s", token);
				return NULL;
			}
			p = strParseToken(p, token, NULL);
		}

		if (token[0] != '}') {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '}' after model properties");
			return NULL;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '}' at end of block");
		return NULL;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: ExplosionTypes: parsed %d explosion type overrides", numParsed);
	return p;
}

static struct { char *name; s32 id; } g_VanillaHeadNames[] = {
	{ "head_dark_combat", HEAD_DARK_COMBAT },
	{ "head_elvis", HEAD_ELVIS },
	{ "head_ross", HEAD_ROSS },
	{ "head_carrington", HEAD_CARRINGTON },
	{ "head_mrblonde", HEAD_MRBLONDE },
	{ "head_trent", HEAD_TRENT },
	{ "head_ddshock", HEAD_DDSHOCK },
	{ "head_graham", HEAD_GRAHAM },
	{ "head_dark_frock", HEAD_DARK_FROCK },
	{ "head_secretary", HEAD_SECRETARY },
	{ "head_cassandra", HEAD_CASSANDRA },
	{ "head_theking", HEAD_THEKING },
	{ "head_fem_guard", HEAD_FEM_GUARD },
	{ "head_jon", HEAD_JON },
	{ "head_mark2", HEAD_MARK2 },
	{ "head_christ", HEAD_CHRIST },
	{ "head_russ", HEAD_RUSS },
	{ "head_grey", HEAD_GREY },
	{ "head_darling", HEAD_DARLING },
	{ "head_robert", HEAD_ROBERT },
	{ "head_beau1", HEAD_BEAU1 },
	{ "head_fem_guard2", HEAD_FEM_GUARD2 },
	{ "head_brian", HEAD_BRIAN },
	{ "head_jamie", HEAD_JAMIE },
	{ "head_duncan2", HEAD_DUNCAN2 },
	{ "head_biotech", HEAD_BIOTECH },
	{ "head_neil2", HEAD_NEIL2 },
	{ "head_edmcg", HEAD_EDMCG },
	{ "head_anka", HEAD_ANKA },
	{ "head_leslie_s", HEAD_LESLIE_S },
	{ "head_matt_c", HEAD_MATT_C },
	{ "head_peer_s", HEAD_PEER_S },
	{ "head_eileen_t", HEAD_EILEEN_T },
	{ "head_andy_r", HEAD_ANDY_R },
	{ "head_ben_r", HEAD_BEN_R },
	{ "head_steve_k", HEAD_STEVE_K },
	{ "head_jonathan", HEAD_JONATHAN },
	{ "head_maian_s", HEAD_MAIAN_S },
	{ "head_shaun", HEAD_SHAUN },
	{ "head_beau2", HEAD_BEAU2 },
	{ "head_eileen_h", HEAD_EILEEN_H },
	{ "head_scott_h", HEAD_SCOTT_H },
	{ "head_sanchez", HEAD_SANCHEZ },
	{ "head_darkaqua", HEAD_DARKAQUA },
	{ "head_ddsniper", HEAD_DDSNIPER },
	{ "head_beau3", HEAD_BEAU3 },
	{ "head_beau4", HEAD_BEAU4 },
	{ "head_beau5", HEAD_BEAU5 },
	{ "head_beau6", HEAD_BEAU6 },
	{ "head_griffey", HEAD_GRIFFEY },
	{ "head_moto", HEAD_MOTO },
	{ "head_keith", HEAD_KEITH },
	{ "head_winner", HEAD_WINNER },
	{ "head_a51faceplate", HEAD_A51FACEPLATE },
	{ "head_elvis_gogs", HEAD_ELVIS_GOGS },
	{ "head_stevem", HEAD_STEVEM },
	{ "head_dark_snow", HEAD_DARK_SNOW },
	{ "head_president", HEAD_PRESIDENT },
	{ "head_vd", HEAD_VD },
	{ "head_ken", HEAD_KEN },
	{ "head_joel", HEAD_JOEL },
	{ "head_tim", HEAD_TIM },
	{ "head_grant", HEAD_GRANT },
	{ "head_penny", HEAD_PENNY },
	{ "head_robin", HEAD_ROBIN },
	{ "head_alex", HEAD_ALEX },
	{ "head_julianne", HEAD_JULIANNE },
	{ "head_laura", HEAD_LAURA },
	{ "head_davec", HEAD_DAVEC },
	{ "head_cook", HEAD_COOK },
	{ "head_pryce", HEAD_PRYCE },
	{ "head_silke", HEAD_SILKE },
	{ "head_smith", HEAD_SMITH },
	{ "head_gareth", HEAD_GARETH },
	{ "head_murchie", HEAD_MURCHIE },
	{ "head_wong", HEAD_WONG },
	{ "head_carter", HEAD_CARTER },
	{ "head_tintin", HEAD_TINTIN },
	{ "head_munton", HEAD_MUNTON },
	{ "head_stamper", HEAD_STAMPER },
	{ "head_jones", HEAD_JONES },
	{ "head_phelps", HEAD_PHELPS },
	{ NULL, -1 }
};

static struct { char *name; s32 id; } g_VanillaBodyNames[] = {
	{ "body_dark_combat", BODY_DARK_COMBAT },
	{ "body_elvis1", BODY_ELVIS1 },
	{ "body_area51guard", BODY_AREA51GUARD },
	{ "body_overall", BODY_OVERALL },
	{ "body_carrington", BODY_CARRINGTON },
	{ "body_mrblonde", BODY_MRBLONDE },
	{ "body_skedar", BODY_SKEDAR },
	{ "body_trent", BODY_TRENT },
	{ "body_ddshock", BODY_DDSHOCK },
	{ "body_labtech", BODY_LABTECH },
	{ "body_stripes", BODY_STRIPES },
	{ "body_dark_frock", BODY_DARK_FROCK },
	{ "body_dark_trench", BODY_DARK_TRENCH },
	{ "body_officeworker", BODY_OFFICEWORKER },
	{ "body_officeworker2", BODY_OFFICEWORKER2 },
	{ "body_secretary", BODY_SECRETARY },
	{ "body_cassandra", BODY_CASSANDRA },
	{ "body_theking", BODY_THEKING },
	{ "body_fem_guard", BODY_FEM_GUARD },
	{ "body_dd_labtech", BODY_DD_LABTECH },
	{ "body_dd_secguard", BODY_DD_SECGUARD },
	{ "body_drcaroll", BODY_DRCAROLL },
	{ "body_eyespy", BODY_EYESPY },
	{ "body_dark_ripped", BODY_DARK_RIPPED },
	{ "body_dd_guard", BODY_DD_GUARD },
	{ "body_dd_shock_inf", BODY_DD_SHOCK_INF },
	{ "body_testchr", BODY_TESTCHR },
	{ "body_biotech", BODY_BIOTECH },
	{ "body_fbiguy", BODY_FBIGUY },
	{ "body_ciaguy", BODY_CIAGUY },
	{ "body_a51trooper", BODY_A51TROOPER },
	{ "body_a51airman", BODY_A51AIRMAN },
	{ "body_chicrob", BODY_CHICROB },
	{ "body_steward", BODY_STEWARD },
	{ "body_stewardess", BODY_STEWARDESS },
	{ "body_president", BODY_PRESIDENT },
	{ "body_stewardess_coat", BODY_STEWARDESS_COAT },
	{ "body_miniskedar", BODY_MINISKEDAR },
	{ "body_nsa_lackey", BODY_NSA_LACKEY },
	{ "body_pres_security", BODY_PRES_SECURITY },
	{ "body_negotiator", BODY_NEGOTIATOR },
	{ "body_g5_guard", BODY_G5_GUARD },
	{ "body_pelagic_guard", BODY_PELAGIC_GUARD },
	{ "body_g5_swat_guard", BODY_G5_SWAT_GUARD },
	{ "body_alaskan_guard", BODY_ALASKAN_GUARD },
	{ "body_maian_soldier", BODY_MAIAN_SOLDIER },
	{ "body_president_clone", BODY_PRESIDENT_CLONE },
	{ "body_president_clone2", BODY_PRESIDENT_CLONE2 },
	{ "body_dark_af1", BODY_DARK_AF1 },
	{ "body_darkwet", BODY_DARKWET },
	{ "body_darkaqualung", BODY_DARKAQUALUNG },
	{ "body_darksnow", BODY_DARKSNOW },
	{ "body_darklab", BODY_DARKLAB },
	{ "body_femlabtech", BODY_FEMLABTECH },
	{ "body_ddsniper", BODY_DDSNIPER },
	{ "body_pilotaf1", BODY_PILOTAF1 },
	{ "body_cilabtech", BODY_CILABTECH },
	{ "body_cifemtech", BODY_CIFEMTECH },
	{ "body_carreveningsuit", BODY_CARREVENINGSUIT },
	{ "body_jonathan", BODY_JONATHAN },
	{ "body_cisoldier", BODY_CISOLDIER },
	{ "body_skedarking", BODY_SKEDARKING },
	{ "body_elviswaistcoat", BODY_ELVISWAISTCOAT },
	{ "body_dark_leather", BODY_DARK_LEATHER },
	{ "body_dark_negotiator", BODY_DARK_NEGOTIATOR },
	{ NULL, -1 }
};

s32 modLookupHeadByName(const char *name)
{
	if (!name || !name[0]) return -1;

	// Check vanilla names
	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaHeadNames[i].name)) {
			// Find which MpHead slot points to this HeadsAndBodies index
			for (s32 j = 0; j < g_NumMpHeads; ++j) {
				if (g_MpHeads[j].headnum == g_VanillaHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	// Check dynamic mod names
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			for (s32 j = 0; j < g_NumMpHeads; ++j) {
				if (g_MpHeads[j].headnum == g_ModHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	return -1;
}

s32 modLookupHandFileByName(const char *name)
{
	if (!name || !name[0]) return -1;

	for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
		if (!strcmp(name, g_ModHandFileNames[i].name)) {
			return (s32)g_ModHandFileNames[i].filenum;
		}
	}

	return -1;
}

s32 modLookupHeadnumByName(const char *name)
{
	if (!name || !name[0]) return -1;

	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaHeadNames[i].name)) {
			return g_VanillaHeadNames[i].id;
		}
	}

	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			return g_ModHeadNames[i].id;
		}
	}

	return -1;
}

s32 modLookupBodyByName(const char *name)
{
	if (!name || !name[0]) return -1;

	// Check vanilla names
	for (s32 i = 0; g_VanillaBodyNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaBodyNames[i].name)) {
			for (s32 j = 0; j < g_NumMpBodies; ++j) {
				if (g_MpBodies[j].bodynum == g_VanillaBodyNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	// Check dynamic mod names
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			for (s32 j = 0; j < g_NumMpBodies; ++j) {
				if (g_MpBodies[j].bodynum == g_ModHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	return -1;
}

// Reverse lookup: given a HeadsAndBodies array index (the value stored in
// g_MpHeads[i].headnum or g_MpBodies[i].bodynum), return the matching
// name string from the vanilla or mod-registered name tables. Returns NULL
// if no name is registered for that slot.
const char *modGetNameForHeadBodyIndex(s32 headBodyIndex)
{
	if (headBodyIndex < 0) {
		return NULL;
	}
	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (g_VanillaHeadNames[i].id == headBodyIndex) {
			return g_VanillaHeadNames[i].name;
		}
	}
	for (s32 i = 0; g_VanillaBodyNames[i].name; ++i) {
		if (g_VanillaBodyNames[i].id == headBodyIndex) {
			return g_VanillaBodyNames[i].name;
		}
	}
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (g_ModHeadNames[i].id == headBodyIndex) {
			return g_ModHeadNames[i].name;
		}
	}
	return NULL;
}

static char *modConfigParseHeadsAndBodies(char *p, char *token, s32 modNum)
{
	struct headorbody tempItem;
	memset(&tempItem, 0, sizeof(tempItem));
	char name[64] = "";

	struct modconfigslotinfo slotInfo = { -1, -1, -1, -1, 0, "" };

	// Save the position right before the opening '{' so we can recover by
	// skipping the entire block if a non-fatal parse error occurs.
	char *blockStart = p;

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	p = strParseToken(p, token, NULL);
	s32 skipEntry = 0;
	p = modConfigParseHeadOrBodyEntry(p, token, &tempItem, modNum, name, &slotInfo, &skipEntry);

	if (skipEntry) {
		// Soft failure: skip to end of block and continue parsing the modconfig.
		sysLogPrintf(LOG_WARNING, "modconfig: skipping HeadsAndBodies '%s' (mod %d) due to unresolved file reference",
		             name[0] ? name : "?", modNum);
		return modConfigSkipBlock(blockStart, token);
	}

	if (!p || token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: unterminated HeadsAndBodies block");
		return NULL;
	}

	// Ensure array is writable
	if (g_HeadsAndBodies == g_HeadsAndBodiesOriginal) {
		struct headorbody *new_array = malloc((g_NumHeadsAndBodies + 1) * sizeof(struct headorbody));
		if (!new_array) return NULL;
		memcpy(new_array, g_HeadsAndBodiesOriginal, g_NumHeadsAndBodies * sizeof(struct headorbody));
		g_HeadsAndBodies = new_array;
	}

	s32 replaceIndex = -1;
	if (name[0]) {
		// Check vanilla names first
		for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
			if (!strcmp(name, g_VanillaHeadNames[i].name)) {
				replaceIndex = g_VanillaHeadNames[i].id;
				break;
			}
		}
		// Then check dynamic mod names
		if (replaceIndex < 0) {
			for (s32 i = 0; i < g_NumModHeadNames; ++i) {
				if (!strcmp(name, g_ModHeadNames[i].name)) {
					replaceIndex = g_ModHeadNames[i].id;
					break;
				}
			}
		}
	}

	if (replaceIndex >= 0) {
		// Overwrite existing head/body entry
		if (g_NumHeadsAndBodies > replaceIndex) {
			g_HeadsAndBodies[replaceIndex] = tempItem;
			sysLogPrintf(LOG_NOTE, "modconfig: replaced %s (index %d) with imported asset", name, replaceIndex);
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: cannot replace %s, array too small", name);
		}
	} else {
		// Append
		struct headorbody *new_array = realloc(g_HeadsAndBodies, (g_NumHeadsAndBodies + 1) * sizeof(struct headorbody));
		if (!new_array) {
			sysLogPrintf(LOG_ERROR, "modconfig: failed to allocate memory for HeadsAndBodies");
			return NULL;
		}
		g_HeadsAndBodies = new_array;
		g_HeadsAndBodies[g_NumHeadsAndBodies] = tempItem;
		g_NumHeadsAndBodies++;

		// Register name for future lookups (modLookupHeadByName etc.)
		if (name[0]) {
			void *tmp = realloc(g_ModHeadNames, (g_NumModHeadNames + 1) * sizeof(*g_ModHeadNames));
			if (tmp) {
				g_ModHeadNames = tmp;
				g_ModHeadNames[g_NumModHeadNames].name = strDuplicate(name);
				g_ModHeadNames[g_NumModHeadNames].id = g_NumHeadsAndBodies - 1;
				g_NumModHeadNames++;
				sysLogPrintf(LOG_NOTE, "modconfig: registered head name '%s' -> HeadsAndBodies[%d]",
				             name, g_NumHeadsAndBodies - 1);
			}
		}
	}

	// Register hand file name for future lookups (modLookupHandFileByName)
	if (slotInfo.handName[0]) {
		s32 replaced = 0;
		for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
			if (!strcmp(slotInfo.handName, g_ModHandFileNames[i].name)) {
				g_ModHandFileNames[i].filenum = tempItem.handfilenum;
				replaced = 1;
				break;
			}
		}
		if (!replaced) {
			void *tmp = realloc(g_ModHandFileNames, (g_NumModHandFileNames + 1) * sizeof(*g_ModHandFileNames));
			if (tmp) {
				g_ModHandFileNames = tmp;
				g_ModHandFileNames[g_NumModHandFileNames].name = strDuplicate(slotInfo.handName);
				g_ModHandFileNames[g_NumModHandFileNames].filenum = tempItem.handfilenum;
				g_NumModHandFileNames++;
			}
		}
	}

	s32 headBodyIndex = (replaceIndex >= 0) ? replaceIndex : (g_NumHeadsAndBodies - 1);

	// Head-slot linking
	if (slotInfo.slotNum >= 0) {
		if (g_MpHeads == g_MpHeadsOriginal) {
			struct mphead *new_array = malloc(g_NumMpHeads * sizeof(struct mphead));
			if (new_array) {
				memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
				g_MpHeads = new_array;
			}
		}

		if (g_MpHeads != g_MpHeadsOriginal) {
			if (slotInfo.slotNum >= g_NumMpHeads) {
				s32 oldNum = g_NumMpHeads;
				struct mphead *new_array = realloc(g_MpHeads, (slotInfo.slotNum + 1) * sizeof(struct mphead));
				if (new_array) {
					g_MpHeads = new_array;
					g_NumMpHeads = slotInfo.slotNum + 1;
					memset(&g_MpHeads[oldNum], 0, (g_NumMpHeads - oldNum) * sizeof(struct mphead));
				}
			}

			if (slotInfo.slotNum < g_NumMpHeads) {
				g_MpHeads[slotInfo.slotNum].headnum = headBodyIndex;
				g_MpHeads[slotInfo.slotNum].requirefeature = slotInfo.requireFeature;
			}
		}
	} else if (replaceIndex < 0 && slotInfo.bodySlotNum < 0) {
		// New entry with no explicit slot and no body slot: auto-append to MpHeads.
		if (g_MpHeads == g_MpHeadsOriginal) {
			struct mphead *new_array = malloc(g_NumMpHeads * sizeof(struct mphead));
			if (new_array) {
				memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
				g_MpHeads = new_array;
			}
		}

		if (g_MpHeads != g_MpHeadsOriginal) {
			struct mphead *new_array = realloc(g_MpHeads, (g_NumMpHeads + 1) * sizeof(struct mphead));
			if (new_array) {
				g_MpHeads = new_array;
				g_MpHeads[g_NumMpHeads].headnum = headBodyIndex;
				g_MpHeads[g_NumMpHeads].requirefeature = slotInfo.requireFeature;
				g_NumMpHeads++;
				sysLogPrintf(LOG_NOTE, "modconfig: auto-appended head '%s' to MpHeads[%d]",
				             name, g_NumMpHeads - 1);
			}
		}
	}

	// Body-slot linking
	if (slotInfo.bodySlotNum >= 0) {
		// bodyslotnum 1 is a flag meaning "auto-assign next available slot"
		s32 bodySlot = slotInfo.bodySlotNum;
		if (bodySlot == 1) {
			// If a body slot already references this headBodyIndex (e.g. the
			// modconfig has been re-parsed), update that slot in place instead
			// of allocating a duplicate.
			s32 existingSlot = -1;
			for (s32 i = 0; i < g_NumMpBodies; ++i) {
				if (g_MpBodies[i].bodynum == headBodyIndex) {
					existingSlot = i;
					break;
				}
			}
			bodySlot = (existingSlot >= 0) ? existingSlot : g_NumMpBodies;
		}

		if (g_MpBodies == g_MpBodiesOriginal) {
			struct mpbody *new_array = malloc(g_NumMpBodies * sizeof(struct mpbody));
			if (new_array) {
				memcpy(new_array, g_MpBodiesOriginal, g_NumMpBodies * sizeof(struct mpbody));
				g_MpBodies = new_array;
			}
		}

		if (g_MpBodies != g_MpBodiesOriginal) {
			if (bodySlot >= g_NumMpBodies) {
				s32 oldNum = g_NumMpBodies;
				struct mpbody *new_array = realloc(g_MpBodies, (bodySlot + 1) * sizeof(struct mpbody));
				if (new_array) {
					g_MpBodies = new_array;
					g_NumMpBodies = bodySlot + 1;
					memset(&g_MpBodies[oldNum], 0, (g_NumMpBodies - oldNum) * sizeof(struct mpbody));
				}
			}

			if (bodySlot < g_NumMpBodies) {
				g_MpBodies[bodySlot].bodynum = headBodyIndex;
				g_MpBodies[bodySlot].requirefeature = slotInfo.requireFeature;
				if (slotInfo.bodyName >= 0) {
					g_MpBodies[bodySlot].name = slotInfo.bodyName;
				}
				if (slotInfo.bodyHeadNum >= 0) {
					g_MpBodies[bodySlot].headnum = slotInfo.bodyHeadNum;
				}
			}
		}
	}

	return p;
}

static char *modConfigParseMpHeads(char *p, char *token)
{
	struct mphead *new_array;

	if (g_MpHeads == g_MpHeadsOriginal) {
		new_array = malloc((g_NumMpHeads + 1) * sizeof(struct mphead));
		if (!new_array) return NULL;
		memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
	} else {
		new_array = realloc(g_MpHeads, (g_NumMpHeads + 1) * sizeof(struct mphead));
		if (!new_array) return NULL;
	}

	g_MpHeads = new_array;
	struct mphead *item = &g_MpHeads[g_NumMpHeads];
	memset(item, 0, sizeof(struct mphead));

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "headnum")) {
			PARSE_INT("MpHeads", "headnum", tmp, 0, 0xFFFF, NULL);
			item->headnum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpHeads", "requirefeature", tmp, 0, 255, NULL);
			item->requirefeature = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: MpHeads: invalid key: %s", token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpHeads++;
	return p;
}

static char *modConfigParseMpBodies(char *p, char *token)
{
	struct mpbody *new_array;

	if (g_MpBodies == g_MpBodiesOriginal) {
		new_array = malloc((g_NumMpBodies + 1) * sizeof(struct mpbody));
		if (!new_array) return NULL;
		memcpy(new_array, g_MpBodiesOriginal, g_NumMpBodies * sizeof(struct mpbody));
	} else {
		new_array = realloc(g_MpBodies, (g_NumMpBodies + 1) * sizeof(struct mpbody));
		if (!new_array) return NULL;
	}

	g_MpBodies = new_array;
	struct mpbody *item = &g_MpBodies[g_NumMpBodies];
	memset(item, 0, sizeof(struct mpbody));

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "bodynum")) {
			PARSE_INT("MpBodies", "bodynum", tmp, 0, 0xFFFF, NULL);
			item->bodynum = tmp;
		} else if (!strcmp(token, "name")) {
			PARSE_INT("MpBodies", "name", tmp, 0, 0xFFFF, NULL);
			item->name = tmp;
		} else if (!strcmp(token, "headnum")) {
			PARSE_INT("MpBodies", "headnum", tmp, 0, 0xFFFF, NULL);
			item->headnum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpBodies", "requirefeature", tmp, 0, 255, NULL);
			item->requirefeature = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: MpBodies: invalid key: %s", token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpBodies++;
	return p;
}

static char *modConfigParseMpArena(char *p, char *token)
{
	struct mparena *new_array;

	sysLogPrintf(LOG_NOTE, "modconfig: parsing MpArena block");

	new_array = realloc(g_MpArenas_AIO, (g_NumMpArenas_AIO + 1) * sizeof(struct mparena));
	if (!new_array) {
		sysLogPrintf(LOG_ERROR, "modconfig: failed to allocate memory for MpArena");
		return NULL;
	}

	g_MpArenas_AIO = new_array;
	struct mparena *item = &g_MpArenas_AIO[g_NumMpArenas_AIO];
	memset(item, 0, sizeof(struct mparena));

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "stagenum")) {
			PARSE_INT("MpArena", "stagenum", tmp, 0, 0xFFFF, NULL);
			item->stagenum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpArena", "requirefeature", tmp, 0, 255, NULL);
			item->requirefeature = tmp;
		} else if (!strcmp(token, "name")) {
			PARSE_INT("MpArena", "name", tmp, 0, 0xFFFF, NULL);
			item->name = tmp;
		} else if (!strcmp(token, "label") || !strcmp(token, "literalname")) {
			p = strParseToken(p, token, NULL);
			if (!p) return NULL;
			item->customname = strDuplicate(strUnquote(token));
		} else if (!strcmp(token, "group")) {
			p = strParseToken(p, token, NULL);
			if (!p) return NULL;
			item->group = strDuplicate(strUnquote(token));
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: MpArena: invalid key: %s", token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpArenas_AIO++;
	return p;
}

static char *modConfigParseMpArenaGroup(char *p, char *token)
{
	struct mparenagroup *new_array;

	new_array = realloc(g_MpArenaGroups, (g_NumMpArenaGroups + 1) * sizeof(struct mparenagroup));
	if (!new_array) {
		sysLogPrintf(LOG_ERROR, "modconfig: failed to allocate memory for MpArenaGroup");
		return NULL;
	}

	g_MpArenaGroups = new_array;
	struct mparenagroup *item = &g_MpArenaGroups[g_NumMpArenaGroups];
	memset(item, 0, sizeof(struct mparenagroup));
	item->startindex = -1;

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "name") || !strcmp(token, "literalname")) {
			p = strParseToken(p, token, NULL);
			if (!p) return NULL;
			item->name = strDuplicate(strUnquote(token));
		} else if (!strcmp(token, "langid")) {
			PARSE_INT("MpArenaGroup", "langid", tmp, 0, 0xFFFF, NULL);
			item->langid = tmp;
		} else if (!strcmp(token, "startindex")) {
			PARSE_INT("MpArenaGroup", "startindex", tmp, 0, 10000, NULL);
			item->startindex = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: MpArenaGroup: invalid key: %s", token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpArenaGroups++;
	sysLogPrintf(LOG_NOTE, "modconfig: added MpArenaGroup '%s' (langid=%d, startindex=%d)",
		item->name ? item->name : "(null)", item->langid, item->startindex);
	return p;
}

static char *modConfigParseStage(char *p, char *token, s32 modnum)
{
	// stage number
	p = strParseToken(p, token, NULL);
	const s32 stagenum = strtol(token, NULL, 0);
	if (stagenum <= 0x01 || stagenum > 0xff) {
		sysLogPrintf(LOG_ERROR, "modconfig: invalid stage number: %x", stagenum);
		return NULL;
	}

	g_ModStageNums[stagenum] = modnum;
	sysLogPrintf(LOG_NOTE, "modconfig: mapped stage 0x%02x to mod %d", stagenum, modnum);

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// find the stage table pointers this corresponds to
	struct stagetableentry *stab = NULL;
	struct stageallocation *salloc = NULL;
	const s32 sidx = stageGetIndex(stagenum);
	if (sidx >= 0) {
		stab = &g_Stages[sidx];
	} else {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unknown stage number", stagenum);
		// Skip this block instead of failing completely
		return modConfigSkipBlock(p, token);
	}
	for (struct stageallocation *p = g_StageAllocations8Mb; p->stagenum; ++p) {
		if (p->stagenum == stagenum) {
			salloc = p;
			break;
		}
	}

	// parse keyvalues until } is reached
	s32 tmp = 0;
	char *tmps = NULL;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "bgfile")) {
			// bg FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "bgfile", tmp);
			stab->bgfileid = tmp;
		} else if (!strcmp(token, "tilesfile")) {
			// tilesfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "tilesfile", tmp);
			stab->tilefileid = tmp;
		} else if (!strcmp(token, "padsfile")) {
			// padsfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "padsfile", tmp);
			stab->padsfileid = tmp;
		} else if (!strcmp(token, "setupfile") || !strcmp(token, "setupFile")) {
			// setupfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "setupfile", tmp);
			stab->setupfileid = tmp;
		} else if (!strcmp(token, "mpsetupfile")) {
			// mpsetupfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "mpsetupfile", tmp);
			stab->mpsetupfileid = tmp;
		} else if (!strcmp(token, "alarm")) {
			PARSE_STAGE_INT("", "alarm", tmp, 1, 0xFFFF);
			stab->alarm = tmp;
		} else if (!strcmp(token, "extragunmem")) {
			PARSE_STAGE_INT("", "extragunmem", tmp, 0, 0xFFFF);
			stab->extragunmem = tmp;
		}  else if (!strcmp(token, "allocation")) {
			// allocation "ALLOCSTRING"
			PARSE_STAGE_STRING("", "allocation", tmps);
			// FIXME: this leaks
			tmps = strDuplicate(tmps);
			if (tmps) {
				salloc->string = tmps;
			}
		}	else if (!strcmp(token, "music")) {
			// music { KEYVALUES... }
			p = modConfigParseStageMusic(p, token, stagenum);
			if (!p) {
				sysLogPrintf(LOG_NOTE, "modConfigParseStage: returning NULL (music parse failed for stage 0x%02x)", stagenum);
				return NULL;
			}
		} else if (!strcmp(token, "weather")) {
			// weather { KEYVALUES... }
			p = modConfigParseStageWeather(p, token, stagenum);
			if (!p) {
				sysLogPrintf(LOG_NOTE, "modConfigParseStage: returning NULL (weather parse failed for stage 0x%02x)", stagenum);
				return NULL;
			}
		} else if (!strcmp(token, "use_mod_files")) {
			PARSE_STAGE_INT("", "use_mod_files", tmp, 0, 1);
			if (tmp) g_StageModFlags[stagenum] |= MOD_FLAG_FORCE_LOAD;
		} else if (!strcmp(token, "force_vanilla")) {
			PARSE_STAGE_INT("", "force_vanilla", tmp, 0, 1);
			if (tmp) g_StageModFlags[stagenum] |= MOD_FLAG_FORCE_VANILLA;
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: invalid key: %s", stagenum, token);
			return NULL;
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: unterminated stage 0x%02x block", stagenum);
		return NULL;
	}

	return p;
}

static s32 g_ModsScanned = 0;

void modScanAllMods(void)
{
	if (g_ModsScanned) return;

	for (s32 i = 0; i < g_NumModDirs; ++i) {
		char path[FS_MAXPATH];
		snprintf(path, sizeof(path), "%s/modconfig.txt", modDirs[i]);

		u32 len = 0;
		char *data = fsFileLoad(path, &len);
		if (!data) continue;

		char token[UTIL_MAX_TOKEN + 1];
		char *p = strParseToken(data, token, NULL);
		while (p && token[0]) {
			if (!strcmp(token, "modname")) {
				p = strParseToken(p, token, NULL);
				if (token[0]) {
					strncpy(g_ModNames[i], strUnquote(token), sizeof(g_ModNames[i]) - 1);
				}
			} else if (!strcmp(token, "modversion")) {
				p = strParseToken(p, token, NULL);
				if (token[0]) {
					strncpy(g_ModVersions[i], strUnquote(token), sizeof(g_ModVersions[i]) - 1);
				}
			}
			p = strParseToken(p, token, NULL);
		}
		sysMemFree(data);
	}
	g_ModsScanned = 1;
}


s32 modLoadAIO(void)
{
	s32 loaded = 0;
	modScanAllMods();

	for (s32 i = 0; i < g_NumModDirs; ++i) {
		char path[FS_MAXPATH];
		snprintf(path, sizeof(path), "%s/modconfig.txt", modDirs[i]);

		u32 len = 0;
		char *data = fsFileLoad(path, &len);
		if (!data) continue;

		char token[UTIL_MAX_TOKEN + 1];
		char *p = strParseToken(data, token, NULL);

		while (p && token[0]) {
			if (!strcmp(token, "MpArena")) {
				// Skip arenas - using vanilla list
				p = modConfigSkipBlock(p, token);
				continue;
			} else if (!strcmp(token, "MpArenaGroup")) {
				// Skip arena groups - using vanilla list
				p = modConfigSkipBlock(p, token);
				continue;
			} else if (!strcmp(token, "MpHeads")) {
				p = modConfigParseMpHeads(p, token);
				continue;
			} else if (!strcmp(token, "MpBodies")) {
				p = modConfigParseMpBodies(p, token);
				continue;
			} else if (!strcmp(token, "HeadsAndBodies")) {
				p = modConfigParseHeadsAndBodies(p, token, i);
				continue;
			} else if (!strcmp(token, "stage")) {
				// Skip stage number, then skip the block
				p = strParseToken(p, token, NULL); // skip stage number
				p = modConfigSkipBlock(p, token);
				continue;
			}
			else if (!strcmp(token, "texture")) {
				p = modConfigSkipBlock(p, token);
				continue;
			}

			p = strParseToken(p, token, NULL);
		}


		sysMemFree(data);
	}

	if (!loaded) {
		sysLogPrintf(LOG_ERROR, "modLoadAIO: failed to find AIO mod in any of %d dirs", g_NumModDirs);
	}

	return loaded;
}

void modInit(void)
{
	// Reset stage flags
	memset(g_StageModFlags, 0, sizeof(g_StageModFlags));

	// Reset mod stage mapping
	for (s32 i = 0; i < STAGE_4MBMENU; i++) {
		g_ModStageNums[i] = -1;
	}
}

// Cache all mod configs at boot (parse once, then just copy on modSwitch)
void modCacheAllConfigs(void)
{
	if (g_ModConfigsCached) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Caching configs for %d mods", g_NumModDirs);

	// First, backup the original vanilla states
	memcpy(g_ModelStatesOriginal, g_ModelStates, sizeof(g_ModelStates));
	memcpy(g_PropExplosionTypesOriginal, g_PropExplosionTypes, NUM_MODELS);
	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Backed up vanilla states");

	// For each mod, load its config and cache the results
	for (u32 i = 0; i < g_NumModDirs; i++) {
		// Start with vanilla defaults for this mod
		memcpy(g_ModelStates_PerMod[i], g_ModelStatesOriginal, sizeof(g_ModelStates));
		memcpy(g_ExplosionTypes_PerMod[i], g_PropExplosionTypesOriginal, NUM_MODELS);

		// Temporarily set g_ModNum to this mod for config parsing
		s32 oldModNum = g_ModNum;
		g_ModNum = i;

		// Load the modconfig (which will modify g_ModelStates and g_PropExplosionTypes)
		modConfigLoad(MOD_CONFIG_FNAME);

		// Cache the modified states for this mod
		memcpy(g_ModelStates_PerMod[i], g_ModelStates, sizeof(g_ModelStates));
		memcpy(g_ExplosionTypes_PerMod[i], g_PropExplosionTypes, NUM_MODELS);

		sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Cached config for mod %d (%s)", i, g_ModNames[i][0] ? g_ModNames[i] : "unnamed");

		// Restore g_ModNum
		g_ModNum = oldModNum;
	}

	g_ModConfigsCached = true;
	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: All configs cached");
}

s32 modConfigLoad(const char *fname)
{
	u32 dataLen = 0;

	// Resolve where this call will actually look. fsFullPath consults
	// fsModFullPath, which probes modDirs[g_ModNum] first and falls
	// back to other mod dirs — so a "not found" here is meaningful
	// only when paired with the directory that was searched.
	const char *resolvedPath = fsFullPath(fname);
	const char *activeModDir = (g_ModNum >= 0 && g_ModNum < (s32)g_NumModDirs) ? modDirs[g_ModNum] : "(none)";
	const char *activeModName = (g_ModNum >= 0 && g_ModNum < 64 && g_ModNames[g_ModNum][0]) ? g_ModNames[g_ModNum] : "(unnamed)";

	char *data = fsFileLoad(fname, &dataLen);
	if (!data) {
		/* sysLogPrintf(LOG_NOTE,
				"modconfig: probe miss for '%s' (g_ModNum=%d '%s' modDir='%s' resolved='%s') — "
				"caller may try another mod dir",
				fname, g_ModNum, activeModName, activeModDir, resolvedPath); */
		return false;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: loaded '%s' (%u bytes) for mod %d '%s' from '%s'",
			fname, dataLen, g_ModNum, activeModName, resolvedPath);

	s32 modnum = g_ModNum;

	// Restore original model states before applying mod overrides
	if (!g_MainIsBooting) {
		sysLogPrintf(LOG_NOTE, "modconfig: Restoring original ModelStates and ExplosionTypes before loading mod %d", modnum);
		memcpy(g_ModelStates, g_ModelStatesOriginal, sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypes, g_PropExplosionTypesOriginal, sizeof(g_PropExplosionTypesOriginal));
	}

	s32 success = true;
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *end = data + dataLen;
	char *p = strParseToken(data, token, NULL);
	while (p && token[0]) {
		if (!strcmp(token, "modname")) {
			p = strParseToken(p, token, NULL);
			if (token[0]) {
				char *name = strUnquote(token);
				if (modnum >= 0 && modnum < 64) {
					strncpy(g_ModNames[modnum], name, sizeof(g_ModNames[modnum]) - 1);
				}
			}
		} else if (!strcmp(token, "modversion")) {
			p = strParseToken(p, token, NULL);
			if (token[0]) {
				char *ver = strUnquote(token);
				if (modnum >= 0 && modnum < 64) {
					strncpy(g_ModVersions[modnum], ver, sizeof(g_ModVersions[modnum]) - 1);
				}
			}
		} else if (!strcmp(token, "stage")) {
			// stage NUMBER { KEYVALUES... }
			char *prev = p;
			p = modConfigParseStage(p, token, modnum);
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed stage block at offset %d", prev - data);
				sysLogPrintf(LOG_ERROR, "modconfig: stage block skipped: %s", token);
				success = false;
				break;
			}

		} else if (!strcmp(token, "texture")) {
			// process texture surcfacetype and soundsurfacetype

				sysLogPrintf(LOG_ERROR, "modconfig: processing texture block at offset %d", p - data);
				sysLogPrintf(LOG_ERROR, "modconfig: texture block: %s", token);
				char *prev = p;

				p = modConfigParseTexture(p, token, modnum);
				if (!p) {
					sysLogPrintf(LOG_ERROR, "modconfig: malformed texture block at offset %d", prev - data);
					sysLogPrintf(LOG_ERROR, "modconfig: texture block skipped: %s", token);
					success = false;
					break;
				}

		} else if (!strcmp(token, "HeadsAndBodies")) {
			if (!g_MainIsBooting) {
				p = modConfigParseHeadsAndBodies(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed HeadsAndBodies block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpHeads")) {
			if (!g_MainIsBooting) {
				p = modConfigParseMpHeads(p, token);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpHeads block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpBodies")) {
			if (!g_MainIsBooting) {
				p = modConfigParseMpBodies(p, token);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpBodies block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpArena")) {
			if (!g_MainIsBooting) {
				p = modConfigParseMpArena(p, token);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpArena block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "ModelStates")) {
			if (!g_MainIsBooting) {
				p = modConfigParseModelStates(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed ModelStates block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "ExplosionTypes")) {
			if (!g_MainIsBooting) {
				p = modConfigParseExplosionTypes(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed ExplosionTypes block");
				success = false;
				break;
			}
		} else {
			// garbage
			sysLogPrintf(LOG_ERROR, "modconfig: unexpected %s at offset %d", token[0] ? token : "end of file", p - data);
			success = false;
			break;
		}
		p = strParseToken(p, token, NULL);
	}

	sysMemFree(data);
	return success;
}

// Derive a short texture-name prefix from the mod directory name.
// E.g. "$H/mods/mod_gex_characters" -> "gex". Returns NULL if not derivable.
static const char *modGetTexPrefix(s32 modNum, char *buf, size_t bufSize)
{
	if (modNum < 0 || (u32)modNum >= g_NumModDirs || !modDirs[modNum][0]) {
		return NULL;
	}
	const char *slash = strrchr(modDirs[modNum], '/');
	const char *base = slash ? slash + 1 : modDirs[modNum];
	if (strncmp(base, "mod_", 4) != 0) {
		return NULL;
	}
	base += 4;
	size_t i = 0;
	while (base[i] && base[i] != '_' && i + 1 < bufSize) {
		buf[i] = base[i];
		++i;
	}
	if (i == 0) {
		return NULL;
	}
	buf[i] = '\0';
	return buf;
}

s32 modTextureResolveFile(s32 modNum, s32 modelFileNum, u16 textureId,
		u16 *resolvedLocalId, char *resolvedName, u32 resolvedNameSize)
{
	char candidate[128];
	char prefixBuf[32];
	const char *prefix;
	u16 localId = textureId;
	s32 fileNum = 0;

	if (resolvedLocalId) *resolvedLocalId = textureId;
	if (resolvedName && resolvedNameSize > 0) resolvedName[0] = '\0';
	if (modNum < 0 || (u32)modNum >= g_NumModDirs) return 0;

	if (textureId >= NUM_TEXTURES) {
		u16 reverseId = modTexMapReverseLookup(modNum, textureId);
		if (reverseId != 0xffff) localId = reverseId;
	}

	if (modelFileNum > 0) {
		const char *modelName = romdataFileGetSlotName(modNum, modelFileNum);
		if (modelName) {
			const char *nameStart = strstr(modelName, "::");
			nameStart = nameStart ? nameStart + 2 : modelName;
			if (strncmp(nameStart, "files/", 6) == 0) nameStart += 6;
			snprintf(candidate, sizeof(candidate), "%s/%04x.bin", nameStart, localId);
			fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
		}
	}

	prefix = modGetTexPrefix(modNum, prefixBuf, sizeof(prefixBuf));
	if (fileNum <= 0) {
		localId = textureId;
		snprintf(candidate, sizeof(candidate), "%04x.bin", textureId);
		fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
	}
	if (fileNum <= 0 && prefix) {
		snprintf(candidate, sizeof(candidate), "%s_%04x.bin", prefix, textureId);
		fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
	}
	if (fileNum <= 0 && textureId >= NUM_TEXTURES) {
		u16 reverseId = modTexMapReverseLookup(modNum, textureId);
		if (reverseId != 0xffff && reverseId != textureId) {
			localId = reverseId;
			snprintf(candidate, sizeof(candidate), "%04x.bin", reverseId);
			fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
			if (fileNum <= 0 && prefix) {
				snprintf(candidate, sizeof(candidate), "%s_%04x.bin", prefix, reverseId);
				fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
			}
		}
	}

	if (fileNum > 0) {
		if (resolvedLocalId) *resolvedLocalId = localId;
		if (resolvedName && resolvedNameSize > 0) {
			snprintf(resolvedName, resolvedNameSize, "%s", candidate);
		}
	}
	return fileNum;
}

s32 modTextureLoad(u16 num, void *dst, u32 dstSize)
{
	// Only attempt mod texture loading when we have an explicit model-level mod context
	// (g_TexModNum is set by modeldef during a mod-owned model's load/process). Without
	// this gate, vanilla model loads would probe mod filetables and accidentally pick up
	// mod overrides for unrelated texture IDs.
	if (g_TexModNum < 0) {
		return 0;
	}

	s32 modNum = g_TexModNum;
	u16 lookup = num;
	const char *modelNameForLog = NULL;

	// If we know which model is loading (set by modeldef), try a per-model
	// dir first: `<ModelName>/<local-texid>.bin`. This mirrors how PNG
	// overrides live under `ext_tex/<ModelName>/<texid>.png` and prevents
	// two models in the same mod that reference the same source texid from
	// pulling each other's bytes.
	char name[128] = { 0 };
	if (g_TexCurrentModelFileNum > 0) {
		const char *modelName = romdataFileGetSlotName(modNum, g_TexCurrentModelFileNum);
		if (modelName) {
			// Strip any leading "mod:...::" prefix produced by merge-filetables.
			const char *nameStart = strstr(modelName, "::");
			nameStart = nameStart ? nameStart + 2 : modelName;
			// Also strip a leading files/ directory if present.
			if (strncmp(nameStart, "files/", 6) == 0) nameStart += 6;
			modelNameForLog = nameStart;
		}
	}
	s32 fileNum = modTextureResolveFile(modNum, g_TexCurrentModelFileNum, num,
		&lookup, name, sizeof(name));

	if (modNum == 2 && (g_TexCurrentModelFileNum == 2025 || g_TexCurrentModelFileNum == 2028)) {
		static u8 s_seen[2][512];
		u32 slot = (g_TexCurrentModelFileNum == 2025) ? 0 : 1;
		if (num < 4096) {
			u32 byte = num >> 3;
			u32 bit = 1u << (num & 7);
			if ((s_seen[slot][byte] & bit) == 0) {
				s_seen[slot][byte] |= bit;
				sysLogPrintf(LOG_NOTE,
					"AIO texture trace: modelFile=%d model='%s' port=0x%04x local=0x%04x fileNum=%d query='%s'",
					g_TexCurrentModelFileNum,
					modelNameForLog ? modelNameForLog : "(none)",
					num,
					lookup,
					fileNum,
					name[0] ? name : "(none)");
			}
		}
	}

	// DIAG: probe trace (disabled — re-enable to see which tex IDs are missed)
	{
		static u8 s_modTexSeen[64][512]; // 64 mods * 4096 tex / 8
		if ((u32)modNum < 64 && num < 4096) {
			u32 byte = num >> 3;
			u32 bit = 1u << (num & 7);
			if ((s_modTexSeen[modNum][byte] & bit) == 0) {
				s_modTexSeen[modNum][byte] |= bit;
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PROBE: tex=0x%04x modNum=%d fileNum=0x%x", num, modNum, fileNum); */
			}
		}
	}

	if (fileNum > 0) {
		DEBUG_MODELS("modTextureLoad: checking texture %04x (file %d) in mod %d", num, fileNum, modNum);
		u32 size = 0;
		s32 encodedFileNum = fileNum | (modNum << 16);
		u8 *data = romdataFileLoad(encodedFileNum, &size);


		if (data) {
			// If the data is pointing to the ROM, we can let the game's default DMA handler
			// take care of it (return 0). This avoids unnecessary memcpy and keeps vanilla behavior.
			if (data >= g_RomFile && data < g_RomFile + g_RomFileSize) {
				if (num >= 0x1010 && num <= 0x1023) {
					/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x ROM-pointer, returning 0 (DMA fallback)", num); */
				}
				return 0;
			}

			if (num >= 0x1010 && num <= 0x1023) {
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x size=%u dstSize=%u data=%p", num, size, dstSize, data); */
			}

			// It's external (or alt-rom) data
			if (size <= dstSize) {
				memcpy(dst, data, size);
				romdataFileFree(encodedFileNum);
				return size;
			} else {
				sysLogPrintf(LOG_ERROR, "mod: texture %04x (file %d) too large for buffer (%d > %d)", num, fileNum, size, dstSize);
				romdataFileFree(encodedFileNum);
				return 0;
			}
		} else {
			if (num >= 0x1010 && num <= 0x1023) {
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x fileNum=0x%x romdataFileLoad returned NULL", num, fileNum); */
			}
			return 0;
		}
	}

	if (num >= 0x1010 && num <= 0x1023) {
		/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE MISS: tex=0x%04x no fileNum found", num); */
	}

	// Fallback to a loose file on disk under the active mod's textures dir.
	// Scoped to g_TexModNum so we don't pull from unrelated mods.
	if ((u32)modNum < g_NumModDirs && modDirs[modNum][0]) {
		char path[FS_MAXPATH + 1];
		snprintf(path, sizeof(path), "%s/" MOD_TEXTURES_DIR "/%04x.bin", modDirs[modNum], num);
		const s32 ret = fsFileLoadTo(path, dst, dstSize);
		if (ret > 0) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external texture %04x from mod %d", num, modNum);
			return ret;
		}
	}

	return 0;
}void *modSequenceLoad(u16 num, u32 *outSize)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_SEQUENCES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return NULL;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_SEQUENCES_DIR "/%04x.bin", num);
	if (fsFileSize(path) > 0) {
		void *ret = fsFileLoad(path, outSize);
		if (ret) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external sequence %04x", num);
			return ret;
		}
	}

	return NULL;
}

void *modAnimationLoadData(u16 num)
{
	char path[FS_MAXPATH + 1];
	// load the animation data
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.bin", num);
	void *data = fsFileLoad(path, NULL);
	if (!data) {
		sysFatalError("External animation %04x has no data file.\nEnsure that it is placed at %s or delete the descriptor.", num, path);
	}
	return data;
}

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_ANIMATIONS_DIR "/") >= 0);
	}

	if (!dirExists) {
		return false;
	}

	char path[FS_MAXPATH + 1];

	// load the descriptor, if any
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.txt", num);
	if (fsFileSize(path) <= 0) {
		return false;
	}

	char *desc = fsFileLoad(path, NULL);
	if (!desc) {
		return false;
	}

	// parse the descriptor
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *p = strParseToken(desc, token, NULL);
	s32 tmp = 0;
	while (p && token[0]) {
		if (!strcmp(token, "numframes")) {
			PARSE_INT(path, "numframes", tmp, 0, 0xFFFF, false);
			anim->numframes = tmp;
		} else if (!strcmp(token, "bytesperframe")) {
			PARSE_INT(path, "bytesperframe", tmp, 0, 0xFFFF, false);
			anim->bytesperframe = tmp;
		} else if (!strcmp(token, "headerlen")) {
			PARSE_INT(path, "headerlen", tmp, 0, 0xFFFF, false);
			anim->headerlen = tmp;
		} else if (!strcmp(token, "framelen")) {
			PARSE_INT(path, "framelen", tmp, 0, 0xFF, false);
			anim->framelen = tmp;
		} else if (!strcmp(token, "flags")) {
			PARSE_INT(path, "flags", tmp, 0, 0xFF, false);
			anim->flags = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "mod: %s: invalid key: %s", path, token);
			return false;
		}
		p = strParseToken(p, token, NULL);
	}

	sysMemFree(desc);

	sysLogPrintf(LOG_NOTE, "mod: loaded external animation %04x", num);

	return true;
}

// mplayer
void modUnloadTextureSurfaceType(void) {
	for (s32 i = 0; i < NUM_TEXTURES; i++) {
		g_Textures[i].surfacetype = g_VanillaTextures[i].surfacetype;
		g_Textures[i].soundsurfacetype = g_VanillaTextures[i].soundsurfacetype;
	}
}


s32 modNumFromStage(s32 stagenum) {
	s32 modnum = -1;
	// this needs to be initialized at boot
	// by attempting to load all mods
	// and discovering which stages are used
	if (stagenum >= 0 && stagenum < ARRAYCOUNT(g_ModStageNums)) {
		if (g_ModStageNums[stagenum] > -1) {
			modnum = g_ModStageNums[stagenum];
		}
	}

	sysLogPrintf(LOG_NOTE, "modNumFromStage: stage 0x%02x -> mod %d", stagenum, modnum);
	return modnum;
}


void modSwitch(s32 modnum, s32 stagenum) {
	sysLogPrintf(LOG_NOTE, "modSwitch(mod=%d, stage=0x%02x) called. Current g_ModNum=%d", modnum, stagenum, g_ModNum);

	// Fix for race condition where menu logic resets mod during stage transition
	if (stagenum == -1 && g_MainChangeToStageNum >= 0) {
		s32 pendingMod = modNumFromStage(g_MainChangeToStageNum);
		if (pendingMod > -1) {
			modnum = pendingMod;
			stagenum = g_MainChangeToStageNum;
			sysLogPrintf(LOG_NOTE, "modSwitch: overriding mod switch to %d for pending stage 0x%02x", modnum, stagenum);
		}
	}

	// this essentially reloads you back to the boot mod
	modUnloadTextureSurfaceType();
	// NOTE: Do NOT reset multiplayer arrays (heads/bodies) on modSwitch!
	// These are now global/exported across all mods and should only be reset at game startup.

	// Initialize backup of original model states on first run
	static bool modelStatesBackedUp = false;
	if (!modelStatesBackedUp) {
		DEBUG_MODELS("modSwitch: Creating backup of original ModelStates and ExplosionTypes");
		memcpy(g_ModelStatesOriginal, g_ModelStates, sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypesOriginal, g_PropExplosionTypes, NUM_MODELS);
		modelStatesBackedUp = true;
		DEBUG_MODELS("modSwitch: Backup complete - sample model 0x0001 scale: 0x%04x, explosion type: %d",
			g_ModelStatesOriginal[0x0001].scale, g_PropExplosionTypesOriginal[0x0001]);
	}

	if (modNumFromStage(stagenum) > -1 && modnum < 0) {
		g_ModNum = modNumFromStage(stagenum);
		sysLogPrintf(LOG_NOTE, "modSwitch: switching to mod %d for stage 0x%02x", g_ModNum, stagenum);
	} else {
		if (modnum >= 0) {
			sysLogPrintf(LOG_NOTE, "modSwitch: manual switch to mod %d (stage 0x%02x)", modnum, stagenum);
		}
		g_ModNum = modnum;
	}

	// Safety check: if g_ModNum is invalid (e.g. -1), keep current mod or default to 0
	if (g_ModNum < 0) {
		// If we already have a valid mod loaded, keep it (don't switch away when loading menu stages)
		static s32 lastValidMod = 0;
		if (lastValidMod > 0) {
			sysLogPrintf(LOG_NOTE, "modSwitch: keeping current mod %d for stage 0x%02x (no mod assignment)", lastValidMod, stagenum);
			g_ModNum = lastValidMod;
		} else {
			sysLogPrintf(LOG_NOTE, "modSwitch: defaulting to mod 0 (boot) for stage 0x%02x", stagenum);
			g_ModNum = 0;
		}
	}

	// Track the last valid mod we switched to (for persistence)
	static s32 lastValidMod = 0;
	if (g_ModNum >= 0) {
		lastValidMod = g_ModNum;
	}

	romdataResetMod(g_ModNum);

	sysLogPrintf(LOG_NOTE, "g_ModNum: %d", g_ModNum);

	// Use cached config data instead of re-parsing (fast, atomic, no overwrites)
	if (g_ModConfigsCached && g_ModNum >= 0 && g_ModNum < 64) {
		sysLogPrintf(LOG_NOTE, "modSwitch: Loading cached config for mod %d", g_ModNum);
		memcpy(g_ModelStates, g_ModelStates_PerMod[g_ModNum], sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypes, g_ExplosionTypes_PerMod[g_ModNum], NUM_MODELS);
		DEBUG_MODELS("modSwitch: Applied cached config (model 0x0020 scale: 0x%04x)", g_ModelStates[0x0020].scale);
	} else {
		// Fallback: parse config on-the-fly (only during boot before cache is ready)
		modConfigLoad(MOD_CONFIG_FNAME);
	}

	// Only enable AIO arena mode if AIO is present AND we are in the boot mod (menus)
	// or if the current mod IS the AIO mod.

  bodiesInit();          // recount guard-head arrays now mods are loaded
  fojoPatchGuardHeads(); // splice FoJo Calico/Poplin into female guard pool
  fojoInitChrBioCharacters(); // resolve FoJo bio chr ids to runtime mpheadnums
	// Load AIO assets (heads, bodies, character models) but keep vanilla arenas
	modLoadAIO();
	// Always use vanilla arena list (don't switch to AIO arenas)
	// mpSetArenaMode(true);
}

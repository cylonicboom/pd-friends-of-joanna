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
#include "game/stagetable.h"

#define DEBUG_MODELS(fmt, ...) \
	do { if (g_DebugModels) sysLogPrintf(LOG_NOTE, fmt, ##__VA_ARGS__); } while (0)
#include "game/mplayer/mplayer.h"
#include "game/mplayer/setup.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"

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

	for (s32 i = 0; i < g_NumImportedAssets; ++i) {
		if (g_ImportedAssets[i]) {
			free(g_ImportedAssets[i]);
		}
	}
	g_NumImportedAssets = 0;
}

static char *modConfigParseHeadOrBodyEntry(char *p, char *token, struct headorbody *item, s32 modNum, char *nameOut, s32 *slotNumOut)
{
	s32 tmp = 0;
	f32 tmpf = 0.0f;

	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "ismale")) {
			PARSE_INT("HeadsAndBodies", "ismale", tmp, 0, 1, NULL);
			item->ismale = tmp;
		} else if (!strcmp(token, "slotnum")) {
			PARSE_INT("HeadsAndBodies", "slotnum", tmp, 0, 255, NULL);
			if (slotNumOut) *slotNumOut = tmp;
		} else if (!strcmp(token, "unk00_01")) {
			PARSE_INT("HeadsAndBodies", "unk00_01", tmp, 0, 1, NULL);
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
			if (!p) return NULL;
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
			if (!p) return NULL;
			item->handfilenum = tmp | (modNum << 16);
		} else if (!strcmp(token, "name")) {
			// Parse name
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

static char *modConfigParseHeadsAndBodies(char *p, char *token, s32 modNum)
{
	struct headorbody tempItem;
	memset(&tempItem, 0, sizeof(tempItem));
	char name[64] = "";

	s32 slotNum = -1;

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	p = strParseToken(p, token, NULL);
	p = modConfigParseHeadOrBodyEntry(p, token, &tempItem, modNum, name, &slotNum);

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
		for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
			if (!strcmp(name, g_VanillaHeadNames[i].name)) {
				replaceIndex = g_VanillaHeadNames[i].id;
				break;
			}
		}
	}

	if (replaceIndex >= 0) {
		// Overwrite existing head
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
	}

	if (slotNum >= 0) {
		if (g_MpHeads == g_MpHeadsOriginal) {
			struct mphead *new_array = malloc(g_NumMpHeads * sizeof(struct mphead));
			if (new_array) {
				memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
				g_MpHeads = new_array;
			}
		}

		if (g_MpHeads != g_MpHeadsOriginal) {
			if (slotNum >= g_NumMpHeads) {
				s32 oldNum = g_NumMpHeads;
				struct mphead *new_array = realloc(g_MpHeads, (slotNum + 1) * sizeof(struct mphead));
				if (new_array) {
					g_MpHeads = new_array;
					g_NumMpHeads = slotNum + 1;
					memset(&g_MpHeads[oldNum], 0, (g_NumMpHeads - oldNum) * sizeof(struct mphead));
					sysLogPrintf(LOG_NOTE, "DEBUG: Expanded g_MpHeads from %d to %d for slotNum=%d", oldNum, g_NumMpHeads, slotNum);
				} else {
					sysLogPrintf(LOG_ERROR, "DEBUG: Failed to expand g_MpHeads for slotNum=%d", slotNum);
				}
			}

			if (slotNum < g_NumMpHeads) {
				s32 headBodyIndex = (replaceIndex >= 0) ? replaceIndex : (g_NumHeadsAndBodies - 1);
				g_MpHeads[slotNum].headnum = headBodyIndex;
				g_MpHeads[slotNum].requirefeature = 0;
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

s32 modImport(char *modName, char *assetName)
{
	// Check if already imported
	char importKey[256];
	snprintf(importKey, sizeof(importKey), "%s.%s", modName, assetName);

	for (s32 i = 0; i < g_NumImportedAssets; ++i) {
		if (!strcmp(g_ImportedAssets[i], importKey)) {
			return 1; // Already imported
		}
	}

	modScanAllMods();

	s32 srcModNum = -1;
	for (s32 i = 0; i < g_NumModDirs; ++i) {
		if (!strcmp(g_ModNames[i], modName)) {
			srcModNum = i;
			break;
		}
	}

	if (srcModNum < 0) {
		// Silent fail
		return 0;
	}

	char path[FS_MAXPATH];
	snprintf(path, sizeof(path), "%s/modconfig.txt", modDirs[srcModNum]);

	u32 len = 0;
	char *data = fsFileLoad(path, &len);
	if (!data) return 0;

	char token[UTIL_MAX_TOKEN + 1];
	char *p = strParseToken(data, token, NULL);
	s32 success = 0;

	while (p && token[0]) {
		if (!strcmp(token, "HeadsAndBodies")) {
			char *blockStart = p;

			// Skip {
			p = strParseToken(p, token, NULL);
			if (token[0] != '{') continue;

			// Scan for name
			char *innerP = p;
			char innerToken[UTIL_MAX_TOKEN + 1];
			s32 found = 0;

			innerP = strParseToken(innerP, innerToken, NULL);
			while (innerP && innerToken[0] && strcmp(innerToken, "}") != 0) {
				if (!strcmp(innerToken, "name")) {
					innerP = strParseToken(innerP, innerToken, NULL);
					if (!strcmp(strUnquote(innerToken), assetName)) {
						found = 1;
					}
				}
				innerP = strParseToken(innerP, innerToken, NULL);
			}

			if (found) {
				modConfigParseHeadsAndBodies(blockStart, token, srcModNum);
				sysLogPrintf(LOG_NOTE, "modconfig: imported %s.%s", modName, assetName);

				// Add to imported list
				if (g_NumImportedAssets < MAX_IMPORTED_ASSETS) {
					g_ImportedAssets[g_NumImportedAssets++] = strDuplicate(importKey);
				}

				success = 1;
				break;
			} else {
				p = modConfigSkipBlock(blockStart, token);
			}
		} else if (!strcmp(token, "MpHeads")) {
			if (!strcmp(assetName, "MpHeads")) {
				modConfigParseMpHeads(p, token);
				sysLogPrintf(LOG_NOTE, "modconfig: imported %s.%s", modName, assetName);
				success = 1; // Found at least one
			} else {
				p = modConfigSkipBlock(p, token);
			}
		} else if (!strcmp(token, "MpBodies")) {
			if (!strcmp(assetName, "MpBodies")) {
				modConfigParseMpBodies(p, token);
				sysLogPrintf(LOG_NOTE, "modconfig: imported %s.%s", modName, assetName);
				success = 1; // Found at least one
			} else {
				p = modConfigSkipBlock(p, token);
			}
		} else if (!strcmp(token, "MpArena")) {
			if (!strcmp(assetName, "MpArena")) {
				modConfigParseMpArena(p, token);
				sysLogPrintf(LOG_NOTE, "modconfig: imported %s.%s", modName, assetName);
				success = 1; // Found at least one
			} else {
				p = modConfigSkipBlock(p, token);
			}
		} else if (!strcmp(token, "stage") || !strcmp(token, "texture")) {
			p = modConfigSkipBlock(p, token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (success && (!strcmp(assetName, "MpHeads") || !strcmp(assetName, "MpBodies") || !strcmp(assetName, "MpArena"))) {
		// Add to imported list for bulk imports
		if (g_NumImportedAssets < MAX_IMPORTED_ASSETS) {
			g_ImportedAssets[g_NumImportedAssets++] = strDuplicate(importKey);
		}
	}

	sysMemFree(data);
	return success;
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
		s32 foundInThisMod = 0;

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
				foundInThisMod = 1;
				continue;
			} else if (!strcmp(token, "MpBodies")) {
				p = modConfigParseMpBodies(p, token);
				foundInThisMod = 1;
				continue;
			} else if (!strcmp(token, "HeadsAndBodies")) {
				p = modConfigParseHeadsAndBodies(p, token, i);
				foundInThisMod = 1;
				continue;
			} else if (!strcmp(token, "stage")) {
				// Skip stage number, then skip the block
				p = strParseToken(p, token, NULL); // skip stage number
				p = modConfigSkipBlock(p, token);
				continue;
			} else if (!strcmp(token, "texture")) {
				p = modConfigSkipBlock(p, token);
				continue;
			}

			p = strParseToken(p, token, NULL);
		}

		if (foundInThisMod) {
			loaded = 1;
			sysLogPrintf(LOG_NOTE, "modLoadAIO: loaded AIO assets from '%s' (modname: '%s')", modDirs[i], g_ModNames[i]);
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
	char *data = fsFileLoad(fname, &dataLen);
	if (!data) {
		sysLogPrintf(LOG_NOTE, "modconfig: Failed to load '%s' for mod %d", fname, g_ModNum);
		return false;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: Successfully loaded '%s' (%u bytes) for mod %d", fname, dataLen, g_ModNum);

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

s32 modTextureLoad(u16 num, void *dst, u32 dstSize)
{
	// Try to load via romdata (filetable) first to respect context.
	// We need to look up the file ID by name because texture IDs don't match file IDs directly.
	char name[64];
	snprintf(name, sizeof(name), "%04x.bin", num);

	s32 fileNum = romdataFileGetNumForNameInMod(name, g_ModNum);

	if (fileNum > 0) {
		DEBUG_MODELS("modTextureLoad: checking texture %04x (file %d) in mod %d", num, fileNum, g_ModNum);
		u32 size = 0;
		u8 *data = romdataFileLoad(fileNum, &size);

		if (data) {
			// If the data is pointing to the ROM, we can let the game's default DMA handler
			// take care of it (return 0). This avoids unnecessary memcpy and keeps vanilla behavior.
			if (data >= g_RomFile && data < g_RomFile + g_RomFileSize) {
				// It's a ROM pointer, so no external replacement was found/loaded.
				// Return 0 to let the caller (texdecompress) handle it via DMA.
				return 0;
			}

			// It's external data
			if (size <= dstSize) {
				memcpy(dst, data, size);
				// Free the data from romdata cache.
				// If it was external, it frees memory and resets to SRC_UNLOADED.
				romdataFileFree(fileNum);
				return size;
			} else {
				sysLogPrintf(LOG_ERROR, "mod: texture %04x (file %d) too large for buffer (%d > %d)", num, fileNum, size, dstSize);
				romdataFileFree(fileNum);
				return 0;
			}
		} else {
			// File is in filetable but romdataFileLoad returned NULL.
			// This means it was rejected by context (e.g. wrong stage/mod).
			// We MUST return 0 here to let the game use the vanilla asset (or fail gracefully),
			// instead of falling back to a blind search which would bypass the context check.
			return 0;
		}
	}

	// Fallback to manual path lookup for IDs not in the filetable
	// (e.g. custom textures with high IDs that were not added to filetable.json)
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_TEXTURES_DIR "/%04x.bin", num);

	// fsFileLoadTo calls fsFullPath, which calls fsModFullPath
	// fsModFullPath now checks all mods if not found in current mod
	const s32 ret = fsFileLoadTo(path, dst, dstSize);

	if (ret > 0) {
		sysLogPrintf(LOG_NOTE, "mod: loaded external texture %04x, path: %s", num, path);
	}

	return ret;
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

	// Load AIO assets (heads, bodies, character models) but keep vanilla arenas
	extern s32 g_AIOPresent;
	if (g_AIOPresent && g_ModNum == 0) {
		// Load AIO heads/bodies for character models
		if (g_NumMpArenas_AIO == 0) {
			modLoadAIO();
		}
		// Disabled: keep using vanilla arenas with langbanks
		// mpSetArenaMode(true);
	}
	// Always use vanilla arena list (don't switch to AIO arenas)
}

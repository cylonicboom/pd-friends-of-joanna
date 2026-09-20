#ifndef IN_GAME_ENV_H
#define IN_GAME_ENV_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

struct distfadesettings {
    f32 opaperc;
    f32 xluperc;
    f32 refdist;
};

struct environment *envGetCurrent(void);
f32 envGetSquaredFogMax(void);
void envTick(void);
void envApplyFogEnvironment(struct fogenvironment *sky);
void envApplyNoFogEnvironment(struct nofogenvironment *sky);
void envSetStageNum(s32 stagenum);
void envChooseAndApply(s32 stagenum, bool allowoverride);
void envApplyTransitionFrac(f32 arg0);
Gfx *envStartFog(Gfx *gdl, bool xlupass);
Gfx *envStopFog(Gfx *gdl);
bool envIsPosInFogMaxDistance(struct coord *pos, f32 tolerance);
struct distfadesettings *envGetDistFadeSettings(void);
s32 envGetObjShadeMode(struct prop *prop, f32 arg1[4]);
#ifndef PLATFORM_N64
void envChaosFog(s32 stagenum, s32 fogmin, s32 fogmax, u8 r, u8 g, u8 b); /* pd.fog */

/*
 * A stage's environment as a modconfig declares it - the union of the two
 * table row shapes, so one struct can express any row in either table. `fog`
 * picks which semantics apply: set, it is a g_FogEnvironments row (fogmin and
 * fogmax matter, transparency does not); clear, it is a g_NoFogEnvironments row.
 * Filled by the modconfig parser and handed to envSetStageEnv, which keeps a
 * copy per stagenum and consults it before either static table.
 */
struct modstageenv {
	u8 fog;
	s16 near;
	s16 far;
	s16 opaperc;
	s16 xluperc;
	s16 refdist;
	s16 fogmin;
	s16 fogmax;
	u8 sky_r, sky_g, sky_b;
	u8 numsuns;
	struct sun *suns;
	u8 clouds_enabled;
	s16 clouds_scale;
	u8 clouds_type;
	u8 clouds_r, clouds_g, clouds_b;
	u8 water_enabled;
	s16 water_scale;
	u8 water_type;
	u8 water_r, water_g, water_b;
	u8 clouds_height;
	u8 transparency;
};

void envModStageEnvDefaults(struct modstageenv *e);
bool envLookupSuns(const char *name, u8 *numsuns, struct sun **suns);
void envSetStageEnv(s32 stagenum, const struct modstageenv *e);
void envClearStageEnv(s32 stagenum);
#endif

#endif

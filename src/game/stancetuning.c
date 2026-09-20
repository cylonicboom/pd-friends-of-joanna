#include <ultra64.h>
#include "constants.h"
#include "game/stancetuning.h"
#include "bss.h"
#include "data.h"
#include "types.h"

#ifndef PLATFORM_N64

s32 g_FojoMovement = FOJO_MOVEMENT_ENABLED;

f32 g_AimStanceSpeed = AIMSTANCE_SPEED;

f32 g_FlinchSpeed = FLINCH_SPEED;
s32 g_FlinchBusy = FLINCH_BUSY;
s32 g_FlinchBusyMax = FLINCH_BUSY_MAX;

f32 g_MeleeBodyReach = MELEE_BODY_REACH;
f32 g_MeleeConeCos = MELEE_CONE_COS;

f32 g_ThirdPersonCamDist = THIRDPERSON_CAMDIST;
f32 g_ThirdPersonCamClearance = THIRDPERSON_CAMCLEARANCE;
f32 g_ThirdPersonCamMinDist = THIRDPERSON_CAMMINDIST;
f32 g_ThirdPersonCamSide = THIRDPERSON_CAMSIDE;
f32 g_ThirdPersonCamForward = THIRDPERSON_CAMFWD;
f32 g_ThirdPersonCamHeight = THIRDPERSON_CAMHEIGHT;
s32 g_ThirdPersonCamTether = THIRDPERSON_TETHER;
f32 g_TetherBodyTurnSpeed = TETHER_BODYTURN;

f32 g_BodyFadeStart = THIRDPERSON_BODYFADE_START;
f32 g_BodyFadeFloor = THIRDPERSON_BODYFADE_FLOOR;

s32 g_AnimSplitLowerMask = ANIMSPLIT_LOWERBODY;

f32 g_ReloadSpeed = RELOAD_SPEED;
s32 g_ReloadAnimEnabled = RELOAD_ANIM_ENABLED;
f32 g_ReloadAnimSpeed = RELOAD_ANIMSPEED;

f32 g_RollImpulse = ROLL_IMPULSE;

s32 g_BlurDoseEnabled = BLUR_DOSE_ENABLED;
f32 g_BlurDoseFullSecs = BLUR_DOSE_FULL_SECS;
f32 g_BlurDoseK = BLUR_DOSE_K;

s32 g_BuildSpeedEnabled = true;
f32 g_BuildSpeedRef = BUILD_SPEED_REF;
f32 g_BuildCrouchMix = BUILD_CROUCH_MIX;

/**
 * Whether fojo movement is on.
 *
 * A global today, and deliberately a function: an arena rule is the shape this
 * wants eventually - a Combat Sim match either runs fojo's movement or the
 * port's - and when that lands, this is the only place that has to start asking
 * g_MpSetup as well. Every stance site already goes through here, so none of
 * them will need touching for it.
 */
bool fojoMovementEnabled(void)
{
	return g_FojoMovement != 0;
}

/**
 * Put every knob back where it shipped.
 *
 * The defines are the defaults, so this is the whole of it - there is no second
 * table to keep in step, and adding a knob cannot forget to add its default.
 */
void stanceTuningReset(void)
{
	g_FojoMovement = FOJO_MOVEMENT_ENABLED;

	g_AimStanceSpeed = AIMSTANCE_SPEED;

	g_FlinchSpeed = FLINCH_SPEED;
	g_FlinchBusy = FLINCH_BUSY;
	g_FlinchBusyMax = FLINCH_BUSY_MAX;

	g_MeleeBodyReach = MELEE_BODY_REACH;
	g_MeleeConeCos = MELEE_CONE_COS;

	g_ThirdPersonCamDist = THIRDPERSON_CAMDIST;
	g_ThirdPersonCamClearance = THIRDPERSON_CAMCLEARANCE;
	g_ThirdPersonCamMinDist = THIRDPERSON_CAMMINDIST;
	g_ThirdPersonCamSide = THIRDPERSON_CAMSIDE;
	g_ThirdPersonCamForward = THIRDPERSON_CAMFWD;
	g_ThirdPersonCamHeight = THIRDPERSON_CAMHEIGHT;
	g_ThirdPersonCamTether = THIRDPERSON_TETHER;
	g_TetherBodyTurnSpeed = TETHER_BODYTURN;

	g_BodyFadeStart = THIRDPERSON_BODYFADE_START;
	g_BodyFadeFloor = THIRDPERSON_BODYFADE_FLOOR;

	g_AnimSplitLowerMask = ANIMSPLIT_LOWERBODY;
	g_ReloadSpeed = RELOAD_SPEED;
	g_ReloadAnimEnabled = RELOAD_ANIM_ENABLED;
	g_ReloadAnimSpeed = RELOAD_ANIMSPEED;
	g_RollImpulse = ROLL_IMPULSE;

	g_BlurDoseEnabled = BLUR_DOSE_ENABLED;
	g_BlurDoseFullSecs = BLUR_DOSE_FULL_SECS;
	g_BlurDoseK = BLUR_DOSE_K;

	g_BuildSpeedEnabled = true;
	g_BuildSpeedRef = BUILD_SPEED_REF;
	g_BuildCrouchMix = BUILD_CROUCH_MIX;
}

#endif

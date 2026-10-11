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

f32 g_HipSwaySide = HIPSWAY_SIDE;
f32 g_HipSwayRoll = HIPSWAY_ROLL;
f32 g_HipSwayShape = HIPSWAY_SHAPE;
f32 g_HipSwayPhase = HIPSWAY_PHASE;
f32 g_HipSwaySpeedCurve = HIPSWAY_SPEEDCURVE;
f32 g_WanderRest = WANDER_REST;
f32 g_WanderMove = WANDER_MOVE;
f32 g_WanderRate = WANDER_RATE;
f32 g_WanderSpeedCurve = WANDER_SPEEDCURVE;
f32 g_WanderStride = WANDER_STRIDE;

f32 g_GunFollowNear = GUNFOLLOW_NEAR;
f32 g_GunFollowFar = GUNFOLLOW_FAR;
f32 g_GunFollowGap = GUNFOLLOW_GAP;
f32 g_GunFollowCurve = GUNFOLLOW_CURVE;
f32 g_CodSlide = COD_SLIDE;
f32 g_CodLeanRoll = COD_LEANROLL;

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

/**
 * The Fojo Sway panel's reset: the curve knobs back where they shipped. The
 * per-player gains are Game options and are left alone.
 */
void swayTuningReset(void)
{
	g_HipSwaySide = HIPSWAY_SIDE;
	g_HipSwayRoll = HIPSWAY_ROLL;
	g_HipSwayShape = HIPSWAY_SHAPE;
	g_HipSwayPhase = HIPSWAY_PHASE;
	g_HipSwaySpeedCurve = HIPSWAY_SPEEDCURVE;
	g_WanderRest = WANDER_REST;
	g_WanderMove = WANDER_MOVE;
	g_WanderRate = WANDER_RATE;
	g_WanderSpeedCurve = WANDER_SPEEDCURVE;
	g_WanderStride = WANDER_STRIDE;
	g_GunFollowNear = GUNFOLLOW_NEAR;
	g_GunFollowFar = GUNFOLLOW_FAR;
	g_GunFollowGap = GUNFOLLOW_GAP;
	g_GunFollowCurve = GUNFOLLOW_CURVE;
	g_CodSlide = COD_SLIDE;
	g_CodLeanRoll = COD_LEANROLL;
}

/**
 * Gun follow: how much faster than stock the view gun closes on its target,
 * for a gap of this many half-screens. 1 everywhere at the defaults.
 */
f32 gunFollowSpeed(f32 gap)
{
	f32 t = g_GunFollowGap > 0.0001f ? gap / g_GunFollowGap : 1.0f;

	if (t > 1.0f) {
		t = 1.0f;
	} else if (t < 0.0f) {
		t = 0.0f;
	}

	if (g_GunFollowCurve != 1.0f) {
		t = __builtin_powf(t, g_GunFollowCurve);
	}

	return g_GunFollowNear + (g_GunFollowFar - g_GunFollowNear) * t;
}

#endif

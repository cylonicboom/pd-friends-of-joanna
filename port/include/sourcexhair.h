#ifndef _IN_SOURCEXHAIR_H
#define _IN_SOURCEXHAIR_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Perfect Dark Source crosshairs: the tuning and a live readout, for the
 * Fojo Crosshair window. The drawing is in sight.c.
 *
 * Distances are world units from the eye to where the aim line hits.
 * Beyond normaldist the flat crosshair is drawn exactly as stock. Inside it,
 * the cross moves onto the surface, weighted by sourceXhairWeight(): 0 at
 * normaldist, rising to 1 across the blend band (a fraction of
 * normaldist - neardist) on a smoothstep, so it eases in rather than snapping.
 * The weight scales how much it grows (grow), how far it turns to lie on the
 * surface (skew) and how far it slides back toward the eye (pull).
 *
 * Grow and pull are separate: grow is its size on screen, pull is where it
 * sits in depth. Pulled toward the eye it is drawn nearer - in front of the
 * wall, closer than the gun if it gets that far - at the same screen size.
 */
struct sourcexhairtuning {
	f32 normaldist; // farther than this: the normal flat crosshair
	f32 neardist;   // closer than this: stops growing
	f32 blend;      // 0 = snap at normaldist, 1 = ease across the whole range
	f32 grow;       // 0 = same size on screen at every distance, 1 = full perspective
	f32 skew;       // 0 = always faces the eye, 1 = lies flat on the surface
	f32 size;       // on-screen size at normaldist, times the flat cross
	f32 alphamul;   // the flat cross overdraws itself; one set of quads does not
	f32 pull;       // 0 = on the surface, 1 = all the way back to closest
	f32 closest;    // nearest to the eye it is ever drawn, world units
	s32 trace;      // "sourcexhair:" log lines
};

extern struct sourcexhairtuning g_SourceXhair;

void sourceXhairReset(void);
void sourceXhairChanged(void); // after editing g_SourceXhair: clamps and queues a pd.ini save

// 0..1: how much of the surface behaviour applies at this distance
f32 sourceXhairWeight(f32 dist);

// on-screen size at this distance, as a multiple of the flat cross
f32 sourceXhairScreenScale(f32 dist);

// how far from the eye it is drawn for a hit this far away, after pull
f32 sourceXhairDrawDist(f32 dist);

struct sourcexhairlive {
	s32 drawn;      // the world cross was drawn last frame
	f32 dist;       // eye to the hit
	f32 drawdist;   // eye to where it is drawn, after pull
	f32 weight;
	f32 scale;
	const char *why;
};

const struct sourcexhairlive *sourceXhairLive(void);

#ifdef __cplusplus
}
#endif

#endif

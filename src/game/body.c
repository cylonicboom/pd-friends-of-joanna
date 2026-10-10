#include <ultra64.h>
#include "constants.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/chr.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/atan2f.h"
#include "game/modelmgr.h"
#include "game/lv.h"
#include "game/modeldef.h"
#include "game/mplayer/mplayer.h"
#include "game/pad.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/mema.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/ailist.h"
#include "system.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "data.h"
#include "mod.h"
#ifndef PLATFORM_N64
#include "romdata.h"
#include "game/texdecompress.h"
#endif
#include "types.h"

s32 g_NumActiveHeadsPerGender;
u32 var8009cd24;
s32 g_ActiveMaleHeads[8];
s32 g_ActiveFemaleHeads[8];

s32 g_NumBondBodies = 0;
s32 g_NumMaleGuardHeads = 0;
s32 g_NumFemaleGuardHeads = 0;
s32 g_NumMaleGuardTeamHeads = 0;
s32 g_NumFemaleGuardTeamHeads = 0;
s32 var80062b14 = 0;
s32 var80062b18 = 0;

s32 g_BondBodies[] = {
	BODY_DJBOND,
	BODY_CONNERY,
	BODY_DALTON,
	BODY_MOORE,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

s32 g_MaleGuardHeads[] = {
	HEAD_BEAU1,
	HEAD_CHRIST,
	HEAD_DARLING,
	HEAD_JON,
	HEAD_ROSS,
	HEAD_RUSS,
	HEAD_MARK2,
	HEAD_JAMIE,
	HEAD_DUNCAN2,
	HEAD_BRIAN,
	HEAD_STEVEM,
	HEAD_KEITH,
	HEAD_GRANT,
	HEAD_PENNY,
	HEAD_DAVEC,
	HEAD_JONES,
	HEAD_GRAHAM,
	HEAD_SHAUN,
	HEAD_NEIL2,
	HEAD_EDMCG,
	HEAD_MATT_C,
	HEAD_PEER_S,
	HEAD_ANDY_R,
	HEAD_BEN_R,
	HEAD_STEVE_K,
	HEAD_SCOTT_H,
	HEAD_SANCHEZ,
	HEAD_COOK,
	HEAD_PRYCE,
	HEAD_SILKE,
	HEAD_SMITH,
	HEAD_GARETH,
	HEAD_MURCHIE,
	HEAD_WONG,
	HEAD_CARTER,
	HEAD_TINTIN,
	HEAD_MUNTON,
	HEAD_PHELPS,
	HEAD_KEN,
	HEAD_JOEL,
	HEAD_TIM,
	HEAD_ROBIN,
	-1,
};

s32 g_MaleGuardTeamHeads[] = {
	HEAD_BEAU1,
	HEAD_CHRIST,
	HEAD_DARLING,
	HEAD_JON,
	HEAD_ROSS,
	HEAD_RUSS,
	HEAD_MARK2,
	HEAD_JAMIE,
	HEAD_DUNCAN2,
	HEAD_BRIAN,
	HEAD_STEVEM,
	HEAD_KEITH,
	HEAD_GRANT,
	HEAD_PENNY,
	HEAD_DAVEC,
	HEAD_JONES,
	-1,
};

// FoJo additions (Calico/Poplin) are appended at runtime by
// fojoPatchGuardHeads() after modconfig.txt has registered them, so this
// list only carries the vanilla heads at startup.
s32 g_FemaleGuardHeads[] = {
	HEAD_LESLIE_S,
	HEAD_ANKA,
	HEAD_EILEEN_T,
	HEAD_EILEEN_H,
	-1, // placeholder slot for FoJo Calico
	-1, // placeholder slot for FoJo Poplin
	-1,
};

s32 g_FemaleGuardTeamHeads[] = {
	HEAD_LESLIE_S,
	HEAD_ANKA,
	HEAD_EILEEN_T,
	HEAD_EILEEN_H,
	-1,
};

s32 var80062c80 = 0;
s32 g_ActiveMaleHeadsIndex = 0;
s32 g_ActiveFemaleHeadsIndex = 0;

s32 g_FemGuardHeads[3] = {
	HEAD_ALEX,
	HEAD_JULIANNE,
	HEAD_LAURA,
};

// Fill the FoJo placeholder slots in g_FemaleGuardHeads with whatever
// HeadsAndBodies indices the active modconfig actually registered for
// Calico (head_catherine) and Poplin (head_foslerfer). Slots whose names
// don't resolve get compacted out so the random-pick logic never lands on
// a -1 sentinel.
void fojoPatchGuardHeads(void)
{
	static const struct { const char *name; s32 placeholderSlot; } slots[] = {
		{ "head_catherine",  4 }, // Calico
		{ "head_foslerfer",  5 }, // Poplin
	};

	for (s32 i = 0; i < (s32)(sizeof(slots) / sizeof(slots[0])); ++i) {
		s32 headnum = modLookupHeadnumByName(slots[i].name);
		if (headnum >= 0) {
			g_FemaleGuardHeads[slots[i].placeholderSlot] = headnum;
		}
	}

	// Compact: shift entries down so leading slots are valid and the array
	// terminates at the first -1 (matches bodiesInit's count loop).
	s32 dst = 0;
	for (s32 src = 0; g_FemaleGuardHeads[src] != -1; ++src) {
		if (g_FemaleGuardHeads[src] >= 0) {
			g_FemaleGuardHeads[dst++] = g_FemaleGuardHeads[src];
		}
	}
	g_FemaleGuardHeads[dst] = -1;
	g_NumFemaleGuardHeads = dst;
}

u32 bodyGetRace(s32 bodynum)
{
	switch (bodynum) {
	case BODY_SKEDAR:
	case BODY_MINISKEDAR:
	case BODY_SKEDARKING:
		return RACE_SKEDAR;
	case BODY_DRCAROLL:
		return RACE_DRCAROLL;
	case BODY_EYESPY:
		return RACE_EYESPY;
	case BODY_CHICROB:
		return RACE_ROBOT;
	}

	return RACE_HUMAN;
}

bool bodyLoad(s32 bodynum)
{
	if (!g_HeadsAndBodies[bodynum].modeldef) {
		g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNew(g_HeadsAndBodies[bodynum].filenum);
		return true;
	}

	return false;
}

#ifndef PLATFORM_N64
// Character-model swap (pd.model_swap), from Kai (be46717). The overlay ROM
// and the file redirect live in port/src/romdata.c; this side decides which
// files are redirected, swaps the loaded modeldefs and rebuilds the live chrs.
//
// Only vanilla (untagged) body/head files are swapped: the overlay is a stock
// PD ROM layout, so a mod-owned character file has nothing to swap to.

// Private texture pool for swapped models: overlay textures load here (once,
// shared across all swapped models) so they don't alias the base game's cached
// textures in g_TexSharedPool. Referenced by modeldefLoad. Allocated from the
// port heap on the first swap-on (Kai used MEMPOOL_PERMANENT, which is too
// small for 6 MB here), so it survives stage changes and acts as a persistent
// overlay-texture cache.
struct texpool g_ModelSwapTexPool;
static bool g_ModelSwapTexPoolReady = false;
#define MODELSWAP_TEXPOOL_BYTES (6 * 1024 * 1024)

// # of live chrs whose overlay body/head produced no modeldef this rebuild
// (they keep the base model). Reported by the model-swap toggle log.
static s32 g_ModelSwapNullLoads = 0;

// Per-stage modeldef cache for the two model sources: [0] = base ROM,
// [1] = overlay ROM. g_HeadsAndBodies[].modeldef holds whichever source is
// live and the other one parks here, so a toggle SWAPS pointers instead of
// reloading every body/head. Every modeldef load comes out of MEMPOOL_STAGE,
// which has no free, so reloading on each toggle leaked a full set of
// body+head modeldefs until mempAlloc failed mid-stage. Now each source loads
// at most once per stage. Cleared by modelSwapResetDefCache() from
// bodiesReset: MEMPOOL_STAGE is wiped at stage load, so every cached pointer
// dangles across a stage change.
#define MODELSWAP_MAX_HEADSBODIES 2048
static struct modeldef *g_ModelSwapDefCache[2][MODELSWAP_MAX_HEADSBODIES];

static bool modelSwapFileIsSwappable(u32 filenum)
{
	return filenum != 0 && MOD_FILEID_MOD((s32)filenum) < 0
		&& filenum < ROMDATA_MODELSWAP_MAX_FILES;
}

void modelSwapResetDefCache(void)
{
	s32 i;

	for (i = 0; i < MODELSWAP_MAX_HEADSBODIES; i++) {
		g_ModelSwapDefCache[0][i] = NULL;
		g_ModelSwapDefCache[1][i] = NULL;
	}
}

// A chr's child props can have their model ATTACHED to the chr's body model:
// attachedtomodel points at chr->model and attachedtonode at a node inside
// that model's definition. When the body is swapped to a fresh model with a
// DIFFERENT definition, those two pointers dangle into the old (freed) model.
// Re-point every affected child at the new body model and re-resolve its
// attach node against the new definition (same bodynum, so the skeleton is
// identical). Called after the body re-link but before the old model is freed.
//
// Walk the chr's ACTUAL child list rather than weapons_held[]: props get
// attached to a body model from more places than the gun/hat slots (objEmbed
// hangs a stuck knife/dart off whichever body node was hit).
static void modelSwapReattachHeld(struct chrdata *chr, struct model *old)
{
	struct model *body = chr->model;
	struct prop *child;
	bool skedar;

	if (body == NULL || body->definition == NULL || chr->prop == NULL) {
		return;
	}

	skedar = (body->definition->skel == &g_SkelSkedar);

	for (child = chr->prop->child; child != NULL; child = child->next) {
		struct defaultobj *obj;
		struct modelnode *node = NULL;
		struct model *childmodel;

		// prop->obj is a union member: only read it for the types that
		// actually have one.
		if (child->type != PROPTYPE_OBJ && child->type != PROPTYPE_WEAPON
				&& child->type != PROPTYPE_DOOR) {
			continue;
		}

		obj = child->obj;

		if (obj == NULL || obj->model == NULL) {
			continue;
		}

		childmodel = obj->model;

		if (childmodel->attachedtomodel != old && childmodel->attachedtomodel != NULL) {
			continue; // hangs off something else entirely — leave it alone
		}

		if (child == chr->weapons_held[HAND_RIGHT]) {
			node = modelGetPart(body->definition,
					skedar ? MODELPART_SKEDAR_RIGHTHAND : MODELPART_CHR_RIGHTHAND);
		} else if (child == chr->weapons_held[HAND_LEFT]) {
			node = modelGetPart(body->definition,
					skedar ? MODELPART_SKEDAR_LEFTHAND : MODELPART_CHR_LEFTHAND);
		} else if (child == chr->weapons_held[2] && !skedar) {
			// Hat slot — skedar has no hats.
			node = modelGetPart(body->definition, MODELPART_CHR_0006);
		} else if (childmodel->attachedtonode) {
			// Anything else (embedded projectiles) hangs off an arbitrary body
			// node: both definitions are the same bodynum and share a
			// skeleton, so map the old attach node onto the new one by matrix
			// index.
			s32 mtxindex = modelFindNodeMtxIndex(childmodel->attachedtonode, 0);

			if (mtxindex >= 0) {
				node = modelFindNodeByMtxIndex(body, mtxindex);
			}
		}

		if (node) {
			childmodel->attachedtomodel = body;
			childmodel->attachedtonode = node;
		} else {
			// No equivalent node in the new definition: detach, so the item
			// stops being drawn on the body instead of walking the freed model.
			childmodel->attachedtomodel = NULL;
			childmodel->attachedtonode = NULL;
		}
	}
}

// Rebuild every live non-player chr's body+head model IN PLACE, so the swap
// takes effect immediately instead of only on respawn. chr0f020b14 SKIPS
// chrInit for an existing chr, so health/AI/inventory are preserved. Corpses
// and players are skipped. Called from the Lua tick (after tick, before
// render), which is a safe boundary.
static s32 modelSwapRebuildLiveChrs(void)
{
	s32 i;
	s32 rebuilt = 0;

	if (g_ChrSlots == NULL) {
		return 0;
	}

	for (i = 0; i < g_NumChrSlots; i++) {
		struct chrdata *chr = &g_ChrSlots[i];
		struct model *old;
		struct model *neu;
		struct coord pos;
		RoomNum rooms[8];
		f32 faceangle;

		if (chr->prop == NULL || chr->model == NULL) {
			continue; // no live model
		}
		if (chr->prop->type != PROPTYPE_CHR) {
			continue; // players / non-chr props handled elsewhere
		}
		if (chrIsDead(chr)) {
			continue; // leave corpses as they are
		}
		if (chr->bodynum < 0 || chr->bodynum >= g_NumHeadsAndBodies
				|| !modelSwapFileIsSwappable(g_HeadsAndBodies[chr->bodynum].filenum)) {
			continue; // mod-owned body: nothing to swap to
		}

		// chr0f020b14 re-grounds what it re-links, and cdFindGroundInfoAtCyl
		// reports "no floor" as a huge negative sentinel, which would drop the
		// chr far under the map. Skip anyone standing where no floor can be
		// found (airborne, on a lift); they get swapped on their next respawn.
		{
			struct coord testpos;
			f32 ground;

			testpos.x = chr->prop->pos.x;
			testpos.y = chr->prop->pos.y + 100.0f;
			testpos.z = chr->prop->pos.z;

			ground = cdFindGroundInfoAtCyl(&testpos, chr->radius, chr->prop->rooms,
					NULL, NULL, NULL, NULL, NULL, NULL);

			if (ground <= -100000.0f) {
				continue;
			}
		}

		old = chr->model;
		faceangle = chrGetInverseTheta(chr);
		pos.x = chr->prop->pos.x;
		pos.y = chr->prop->pos.y;
		pos.z = chr->prop->pos.z;
		roomsCopy(chr->prop->rooms, rooms);

		// bodyAllocateModel brackets the overlay-texture redirect itself (so
		// respawns get it too).
		neu = bodyAllocateModel(chr->bodynum, chr->headnum, 0);
		if (neu) {
			chr0f020b14(chr->prop, neu, &pos, rooms, faceangle, NULL);
			// chr0f020b14 set chr->model = neu; re-hang the attached children
			// on it before the old model is freed.
			modelSwapReattachHeld(chr, old);

			// chr0f020b14 is the spawn-time linker: it leaves the fresh model
			// with a zeroed anim (T-pose) and a default chrinfo rwdata. Both
			// models are the same bodynum, so the anim struct and the root
			// chrinfo rwdata are layout-identical: copy them across.
			modelCopyAnimData(old, neu);

			if ((old->definition->rootnode->type & 0xff) == MODELNODETYPE_CHRINFO
					&& (neu->definition->rootnode->type & 0xff) == MODELNODETYPE_CHRINFO) {
				union modelrwdata *oldrw = modelGetNodeRwData(old, old->definition->rootnode);
				union modelrwdata *newrw = modelGetNodeRwData(neu, neu->definition->rootnode);

				if (oldrw && newrw) {
					newrw->chrinfo = oldrw->chrinfo;
				}
			}

			modelmgrFreeModel(old);
			rebuilt++;
		} else {
			// The overlay file produced no usable modeldef; the chr keeps its
			// old model.
			g_ModelSwapNullLoads++;
		}
	}

	return rebuilt;
}

// Turn the character-model swap on/off. Flags every vanilla character
// body/head (and first-person hands) file for redirection to the overlay ROM,
// swaps the loaded modeldefs with the parked set for the other source, drops
// the file cache so any load that does happen re-reads, then rebuilds every
// live chr in place so the swap is visible immediately.
void modelSwapSetActive(bool on)
{
	s32 i;
	s32 count;
	bool changed = (on != 0) != (g_ModelSwapActive != 0);

	if (!g_ModelRomActive) {
		return; // no overlay loaded; nothing to swap
	}

	count = g_NumHeadsAndBodies;
	if (count > MODELSWAP_MAX_HEADSBODIES) {
		count = MODELSWAP_MAX_HEADSBODIES;
	}

	// Rebuild the redirect set from the live body/head table.
	for (i = 0; i < ROMDATA_MODELSWAP_MAX_FILES; i++) {
		g_ModelSwapFiles[i] = 0;
	}
	if (on) {
		for (i = 0; i < count && g_HeadsAndBodies[i].filenum; i++) {
			if (modelSwapFileIsSwappable(g_HeadsAndBodies[i].filenum)) {
				g_ModelSwapFiles[g_HeadsAndBodies[i].filenum] = 1;
			}
			if (modelSwapFileIsSwappable(g_HeadsAndBodies[i].handfilenum)) {
				g_ModelSwapFiles[g_HeadsAndBodies[i].handfilenum] = 1;
			}
		}
	}

	g_ModelSwapActive = on ? 1 : 0;

	if (changed) {
		s32 rebuilt;
		s32 probefile;
		s32 probecn;
		s32 leaving = on ? 0 : 1;   // model source being switched away from
		s32 entering = on ? 1 : 0;  // model source being switched to

		// Ensure the private overlay-texture pool exists. If it can't be had,
		// texturing stays base and only geometry swaps.
		if (on && g_ModelSwapTexList != NULL && !g_ModelSwapTexPoolReady) {
			u8 *poolmem = sysMemAlloc(ALIGN16(MODELSWAP_TEXPOOL_BYTES));
			if (poolmem) {
				texInitPool(&g_ModelSwapTexPool, poolmem, MODELSWAP_TEXPOOL_BYTES);
				g_ModelSwapTexPoolReady = true;
			}
		}

		// Park the modeldefs of the source being left and adopt the ones
		// already loaded for the source being entered (NULL the first time,
		// so they load on demand).
		for (i = 0; i < count && g_HeadsAndBodies[i].filenum; i++) {
			if (!modelSwapFileIsSwappable(g_HeadsAndBodies[i].filenum)) {
				continue;
			}
			g_ModelSwapDefCache[leaving][i] = g_HeadsAndBodies[i].modeldef;
			g_HeadsAndBodies[i].modeldef = g_ModelSwapDefCache[entering][i];
			g_FileInfo[g_HeadsAndBodies[i].filenum & 0xffff].loadedsize = 0;
			if (modelSwapFileIsSwappable(g_HeadsAndBodies[i].handfilenum)) {
				g_FileInfo[g_HeadsAndBodies[i].handfilenum & 0xffff].loadedsize = 0;
			}
		}

		// Diagnostics: zero the redirect counters so they reflect just this
		// toggle's rebuild loads, then probe whether the first body file
		// resolves to a same-named file in the overlay ROM.
		g_ModelSwapRedirects = 0;
		g_ModelSwapMisses = 0;
		g_ModelSwapNullLoads = 0;
		probefile = (s32)g_HeadsAndBodies[0].filenum;
		probecn = romdataModelRomFileGetNumForName(romdataFileGetName(probefile));

		rebuilt = modelSwapRebuildLiveChrs();

		sysLogPrintf(LOG_NOTE,
				"modelswap: %s chrs=%d rebuilt=%d nullloads=%d redirects=%d misses=%d probe(file=%d '%s' -> overlay=%d)",
				on ? "ON" : "OFF", g_NumChrSlots, rebuilt,
				g_ModelSwapNullLoads, g_ModelSwapRedirects, g_ModelSwapMisses,
				probefile, romdataFileGetName(probefile) ? romdataFileGetName(probefile) : "?", probecn);
	}
}

bool modelSwapRomLoaded(void)
{
	return g_ModelRomActive != 0;
}
#endif

struct model *body0f02ce8c(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, struct model *model, bool isplayer, u8 varyheight)
{
	f32 scale = g_HeadsAndBodies[bodynum].scale * 0.10000001f;
	f32 animscale = g_HeadsAndBodies[bodynum].animscale;
	struct modelnode *node = NULL;
	u32 stack[2];

	// sysLogPrintf(LOG_NOTE, "DEBUG body0f02ce8c ENTER bodynum=%d headnum=%d bodymodeldef=%p headmodeldef=%p isplayer=%d",
	//	bodynum, headnum, (void *)bodymodeldef, (void *)headmodeldef, isplayer);

	if (cheatIsActive(CHEAT_DKMODE)) {
		scale *= 0.8f;
	}

	if (bodymodeldef == NULL) {
		if (g_HeadsAndBodies[bodynum].modeldef == NULL) {
			g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNew(g_HeadsAndBodies[bodynum].filenum);
		}

		bodymodeldef = g_HeadsAndBodies[bodynum].modeldef;
	}

	modelAllocateRwData(bodymodeldef);

	if (!g_HeadsAndBodies[bodynum].unk00_01) {
		if (bodymodeldef->skel == &g_SkelChr) {
			node = modelGetPart(bodymodeldef, MODELPART_CHR_HEADSPOT);

			if (node != NULL) {
				if (headnum < 0) {
					headmodeldef = func0f18e57c(-1 - headnum, &headnum);
					bodymodeldef->rwdatalen += headmodeldef->rwdatalen;
				} else if (headnum > 0) {
					if (headmodeldef == NULL) {
						// sysLogPrintf(LOG_NOTE, "DEBUG body0f02ce8c: headmodeldef is NULL, loading fresh for headnum=%d normmplay=%d", headnum, g_Vars.normmplayerisrunning);
						if (g_Vars.normmplayerisrunning && !IS4MB()) {
							headmodeldef = modeldefLoadToNew(g_HeadsAndBodies[headnum].filenum);
							g_HeadsAndBodies[headnum].modeldef = headmodeldef;
							g_FileInfo[g_HeadsAndBodies[headnum].filenum & 0xFFFF].loadedsize = 0;
							bodyCalculateHeadOffset(headmodeldef, headnum, bodynum);
						} else {
							headmodeldef = modeldefLoadToNew(g_HeadsAndBodies[headnum].filenum);
							g_HeadsAndBodies[headnum].modeldef = headmodeldef;
							bodyCalculateHeadOffset(headmodeldef, headnum, bodynum);
						}
					} else {
						// sysLogPrintf(LOG_NOTE, "DEBUG body0f02ce8c: headmodeldef NOT NULL (%p), SKIPPING bodyCalculateHeadOffset for headnum=%d", (void *)headmodeldef, headnum);
					}

					modelAllocateRwData(headmodeldef);

					bodymodeldef->rwdatalen += headmodeldef->rwdatalen;

					if (g_HeadsAndBodies[bodynum].canvaryheight && varyheight) {
						// Set height to between 95% and 115%
						f32 frac = RANDOMFRAC() * 0.05f;
						scale *= 2.0f * frac - 0.05f + 1.0f;
					}
				}

				if (!isplayer) {
					if (cheatIsActive(CHEAT_SMALLCHARACTERS)) {
						scale *= 0.4f;
					}

					if (cheatIsActive(CHEAT_DKMODE)) {
						scale *= 1.25f;
					}
				} else {
					if (cheatIsActive(CHEAT_SMALLJO)) {
						scale *= 0.4f;
					}
				}
			}
		} else if (bodymodeldef->skel == &g_SkelSkedar) {
			if (g_HeadsAndBodies[bodynum].canvaryheight && varyheight && bodynum == BODY_SKEDAR) {
				// Set height to between 65% and 85%
				f32 frac = RANDOMFRAC();
				scale *= 2.0f * (0.1f * frac) - 0.1f + 0.75f;
			}

			if (1);
		}
	}

	if (model) {
		if (model->rwdatalen < bodymodeldef->rwdatalen);
	} else {
		model = modelmgrInstantiateModelWithAnim(bodymodeldef);
	}

	if (model) {
		modelSetScale(model, scale);
		modelSetAnimScale(model, animscale);

		if (headmodeldef && !g_HeadsAndBodies[bodynum].unk00_01 && node == NULL) {
			// A body whose skeleton is not g_SkelChr, or has no head spot,
			// cannot take a head: node is still NULL here and
			// modelmgrAttachHead would dereference it. A modconfig that
			// declares such a body (a full-body model like Skedar or
			// Dr Caroll) without `hasownhead 1` lands here from the
			// character previewer. Render the body bare and say why, once.
#ifndef PLATFORM_N64
			static s32 s_lastWarnedBody = -1;
			if (s_lastWarnedBody != bodynum) {
				s_lastWarnedBody = bodynum;
				sysLogPrintf(LOG_WARNING, "body %d has no head spot (skel %p, headspot %s) but hasownhead is 0; head %d not attached - declare `hasownhead 1` on it",
						bodynum, (void *)bodymodeldef->skel, bodymodeldef->skel == &g_SkelChr ? "missing" : "n/a", headnum);
			}
#endif
			headmodeldef = NULL;
		}

		if (headmodeldef && !g_HeadsAndBodies[bodynum].unk00_01) {
			bodymodeldef->rwdatalen -= headmodeldef->rwdatalen;

			modelmgrAttachHead(model, node, headmodeldef);

			if ((s16)*(s32 *)&headmodeldef->skel == SKEL_HEAD) {
				struct modelnode *node2;

				if (!sunglasses) {
					node2 = modelGetPart(headmodeldef, MODELPART_HEAD_SUNGLASSES);

					if (node2) {
						union modelrwdata *rwdata = modelGetNodeRwData(model, node2);
						rwdata->toggle.visible = false;
					}
				}

				node2 = modelGetPart(headmodeldef, MODELPART_HEAD_HUDPIECE);

				if (node2) {
					union modelrwdata *rwdata = modelGetNodeRwData(model, node2);
					rwdata->toggle.visible = false;
				}
			}
		}
	}

	return model;
}

struct model *body0f02d338(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, u8 varyheight)
{
	return body0f02ce8c(bodynum, headnum, bodymodeldef, headmodeldef, sunglasses, NULL, false, varyheight);
}

struct model *bodyAllocateModel(s32 bodynum, s32 headnum, u32 spawnflags)
{
	bool sunglasses = false;
	u8 varyheight = true;

	if (spawnflags & SPAWNFLAG_FORCESUNGLASSES) {
		sunglasses = true;
	} else if (spawnflags & SPAWNFLAG_MAYBESUNGLASSES) {
		sunglasses = rngRandom() % 2 == 0;
	}

	if (spawnflags & SPAWNFLAG_FIXEDHEIGHT) {
		varyheight = false;
	}

#ifndef PLATFORM_N64
	// Model swap: this is the single choke point every character body/head
	// load passes through (spawn, respawn and the live rebuild), so bracket
	// the overlay-texture redirect here. Swapped models draw the overlay's
	// textures from the private pool. World/gun textures don't pass through
	// here, so they're unaffected.
	if (g_ModelSwapActive && g_ModelSwapTexPoolReady && g_ModelSwapTexList != NULL) {
		struct model *model;

		g_ModelSwapTexActive = 1;
		model = body0f02d338(bodynum, headnum, NULL, NULL, sunglasses, varyheight);
		g_ModelSwapTexActive = 0;
		return model;
	}
#endif

	return body0f02d338(bodynum, headnum, NULL, NULL, sunglasses, varyheight);
}

s32 body0f02d3f8(void)
{
	return g_BondBodies[var80062c80];
}

s32 bodyChooseHead(s32 bodynum)
{
	s32 head;

	if (g_HeadsAndBodies[bodynum].ismale) {
		head = g_ActiveMaleHeads[g_ActiveMaleHeadsIndex++];

		if (g_ActiveMaleHeadsIndex == g_NumActiveHeadsPerGender) {
			g_ActiveMaleHeadsIndex = 0;
		}
	} else if (bodynum == BODY_FEM_GUARD) {
		head = g_FemGuardHeads[rngRandom() % 3];
	} else {
		head = g_ActiveFemaleHeads[g_ActiveFemaleHeadsIndex++];

		if (g_ActiveFemaleHeadsIndex == g_NumActiveHeadsPerGender) {
			g_ActiveFemaleHeadsIndex = 0;
		}
	}

	return head;
}

/**
 * Read a "packed" chr definition and create a runtime chr from it.
 *
 * Chr definitions are stored in a packed format in each stage's setup file.
 * The packed format is used for space saving reasons.
 */
void bodyAllocateChr(s32 stagenum, struct packedchr *packed, s32 cmdindex)
{
	struct pad pad;
	RoomNum rooms[2];
	struct chrdata *chr;
	struct modeldef *headmodeldef;
	struct model *model;
	struct prop *prop;
	s32 bodynum;
	s32 headnum;
	f32 angle;
	s32 index;

	padUnpack(packed->padnum, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_ROOM, &pad);

	rooms[0] = pad.room;
	rooms[1] = -1;

	if (cdTestVolume(&pad.pos, 20, rooms, CDTYPE_ALL, CHECKVERTICAL_YES, 200, -200) == CDRESULT_COLLISION
			&& packed->chair == -1
			&& (packed->spawnflags & SPAWNFLAG_IGNORECOLLISION) == 0) {
		return;
	}

	if (packed->spawnflags & (SPAWNFLAG_ONLYONA | SPAWNFLAG_ONLYONSA | SPAWNFLAG_ONLYONPA)) {
		if ((packed->spawnflags & (SPAWNFLAG_ONLYONA | SPAWNFLAG_ONLYONSA | SPAWNFLAG_ONLYONPA)) == 0) {
			return;
		}

		if (((packed->spawnflags & SPAWNFLAG_ONLYONA) && lvGetDifficulty() == DIFF_A)
				|| ((packed->spawnflags & SPAWNFLAG_ONLYONSA) && lvGetDifficulty() == DIFF_SA)
				|| ((packed->spawnflags & SPAWNFLAG_ONLYONPA) && lvGetDifficulty() == DIFF_PA)) {
			// ok
		} else {
			return;
		}
	}

	headnum = -55555;
	headmodeldef = NULL;

	if (packed->bodynum == 255) {
		bodynum = body0f02d3f8();
	} else {
		bodynum = packed->bodynum;
	}

	if (!g_HeadsAndBodies[bodynum].unk00_01) {
		if (packed->headnum >= 0) {
			headnum = packed->headnum;
		} else if (headnum == -55555) {
			headnum = bodyChooseHead(bodynum);
		}
	}

	if (headnum < 0) {
		index = -1 - headnum;

		if (index >= 0 && index < 22) {
			headmodeldef = func0f18e57c(index, &headnum);
		}

		model = body0f02ce8c(bodynum, headnum, NULL, headmodeldef, false, NULL, false, false);
	} else {
		model = bodyAllocateModel(bodynum, headnum, packed->spawnflags);
	}

	if (model != NULL) {
		angle = atan2f(pad.look.x, pad.look.z);
		prop = chrAllocate(model, &pad.pos, rooms, angle, ailistFindById(packed->ailistnum));

		if (prop != NULL) {
			propActivate(prop);
			propEnable(prop);

			chr = prop->chr;
			chrSetChrnum(chr, packed->chrnum);
			chr->hearingscale = packed->hearscale / 1000.0f;
			chr->visionrange = packed->viewdist;
			chr->padpreset1 = packed->padpreset;
			chr->chrpreset1 = packed->chrpreset;
			chr->headnum = headnum;
			chr->bodynum = bodynum;
			chr->race = bodyGetRace(chr->bodynum);

			chr->rtracked = false;

			if (bodynum == BODY_DRCAROLL) {
				chr->drcarollimage_left = 0;
				chr->drcarollimage_right = 0;
				chr->height = 185;
				chr->radius = 30;
			} else if (bodynum == BODY_CHICROB) {
				chr->unk348[0] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
				chr->unk348[1] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
				chr->unk348[0]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
				chr->unk348[1]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
				chr->unk348[0]->beam->age = -1;
				chr->unk348[1]->beam->age = -1;
				chr->height = 200;
				chr->radius = 42;
			}

			if (packed->spawnflags & SPAWNFLAG_INVINCIBLE) {
				chr->chrflags |= CHRCFLAG_INVINCIBLE;
			}

			if (packed->spawnflags & SPAWNFLAG_BASICGUARD) {
				chr->hidden |= CHRHFLAG_BASICGUARD;
			}

			if (packed->spawnflags & SPAWNFLAG_ANTINONINTERACTABLE) {
				chr->hidden |= CHRHFLAG_ANTINONINTERACTABLE;
			}

			if (packed->spawnflags & SPAWNFLAG_DONTSHOOTME) {
				chr->hidden |= CHRHFLAG_DONTSHOOTME;
			}

			if (packed->spawnflags & SPAWNFLAG_HIDDEN) {
				chr->chrflags |= CHRCFLAG_HIDDEN;
			}

			if (packed->spawnflags & SPAWNFLAG_RTRACKED) {
				chr->rtracked = true;
			}

			if (packed->spawnflags & SPAWNFLAG_NOBLOOD) {
				chr->noblood = true;
			}

			if (packed->spawnflags & SPAWNFLAG_BLUESIGHT) {
				chr->hidden2 |= CHRH2FLAG_BLUESIGHT;
			}

			chr->flags = packed->flags;
			chr->flags2 = packed->flags2;

			if (cheatIsActive(CHEAT_MARQUIS)) {
				chr->flags2 &= ~CHRFLAG1_NOHANDCOMBAT;
				chr->flags2 |= CHRFLAG1_HANDCOMBATONLY;
			}

			chr->team = packed->team;
			chr->squadron = packed->squadron;
			chr->aibot = NULL;

			if (packed->tude != 4) {
				chr->tude = packed->tude;
			} else {
				chr->tude = rngRandom() % 4;
			}

			chr->voicebox = rngRandom() % 3;

			if (!g_HeadsAndBodies[chr->bodynum].ismale) {
				chr->voicebox = VOICEBOX_FEMALE;
			}

			chr->naturalanim = packed->naturalanim;
			chr->myspecial = packed->chair;
			chr->yvisang = packed->yvisang;

			packed->chrindex = chr - g_ChrSlots;

			chr->teamscandist = packed->teamscandist;
			chr->convtalk = packed->convtalk;

			if (chr->flags & CHRFLAG0_CAN_HEARSPAWN) {
				chr->chrflags |= CHRCFLAG_CLONEABLE;
			}

			if (!g_Vars.normmplayerisrunning && g_MissionConfig.iscoop && g_Vars.numaibuddies > 0) {
				chr->flags |= CHRFLAG0_AIVSAI;
			}

			if (rngRandom() % 5 == 0) {
				// Make chr punch slower
				chr->flags2 |= CHRFLAG1_ADJUSTPUNCHSPEED;
			}

			if (CHRRACE(chr) == RACE_SKEDAR) {
				chr->chrflags |= CHRCFLAG_FORCEAUTOAIM;
			}
		}
	}
}

struct prop *bodyAllocateEyespy(struct pad *pad, RoomNum room)
{
	RoomNum rooms[2];
	struct prop *prop;
	struct chrdata *chr;
	struct model *model;
	s32 inlift;
	struct prop *lift;
	f32 ground;

	rooms[0] = room;
	rooms[1] = -1;

#if PIRACYCHECKS
	{
		u32 stack[2];
		u32 checksum = 0;
		s32 *ptr = (s32 *)&lvReset;
		s32 *end = (s32 *)&lvConfigureFade;

		while (ptr < end) {
			checksum <<= 1;
			checksum ^= *ptr;
			ptr++;
		}

		if (checksum != CHECKSUM_PLACEHOLDER) {
			s32 *ptr2 = (s32 *)_memaFree;
			s32 *end2 = (s32 *)memaInit;

			while (ptr2 < end2) {
				ptr2[0] = 0;
				ptr2++;
			}
		}
	}
#endif

	model = bodyAllocateModel(BODY_EYESPY, 0, 0);

	if (model) {
		prop = chrAllocate(model, &pad->pos, rooms, 0, ailistFindById(GAILIST_IDLE));

		if (prop) {
			propActivate(prop);
			propEnable(prop);
			chr = prop->chr;
			chrSetChrnum(chr, chrsGetNextUnusedChrnum());
			chr->bodynum = BODY_EYESPY;
			chr->padpreset1 = 0;
			chr->chrpreset1 = 0;
			chr->headnum = 0;
			chr->hearingscale = 0;
			chr->visionrange = 0;
			chr->race = bodyGetRace(chr->bodynum);

			ground = cdFindGroundInfoAtCyl(&pad->pos, 30, rooms, NULL, NULL, NULL, NULL, &inlift, &lift);
			chr->ground = ground;
			chr->manground = ground;

			chr->flags = 0;
			chr->flags2 = 0;
			chr->team = 0;
			chr->squadron = 0;
			chr->maxdamage = 2;
			chr->tude = rngRandom() & 3;
			chr->voicebox = rngRandom() % 3;
			chr->naturalanim = 0;
			chr->myspecial = 0;
			chr->yvisang = 0;
			chr->teamscandist = 0;
			chr->convtalk = 0;
			chr->radius = 26;
			chr->height = 200;
			func0f02e9a0(chr, 0);
			chr->chrflags |= CHRCFLAG_HIDDEN;

#if VERSION >= VERSION_NTSC_1_0
			chr->hidden2 |= CHRH2FLAG_CONSIDERPROXIES;
#else
			chr->hidden |= CHRHFLAG_CONSIDERPROXIES;
#endif

			return prop;
		}
	}

	return NULL;
}

void body0f02ddbf(void)
{
	// empty
}

/**
 * Tweak the head's Y offset to suit the body.
 *
 * By default, heads and their matching bodies align perfectly and don't need
 * any tweaking. This function is used in multiplayer where players can put any
 * heads on any bodies.
 */
void bodyCalculateHeadOffset(struct modeldef *headmodeldef, s32 headnum, s32 bodynum)
{
	struct modelnode *node;
	struct modelnode *prev;
	Gfx *gdl;
	s32 offset;
	struct modelrodata_bbox *bbox;
	s32 i;

	// sysLogPrintf(LOG_NOTE, "DEBUG bodyCalculateHeadOffset ENTER headnum=%d bodynum=%d headtype=%d bodytype=%d yoffset=%d",
	//	headnum, bodynum, g_HeadsAndBodies[headnum].type, g_HeadsAndBodies[bodynum].type, g_HeadsAndBodies[headnum].yoffset);

#if VERSION >= VERSION_JPN_FINAL
	offset = 0;

	switch (headnum) {
	case HEAD_DARK_COMBAT:
	case HEAD_DARK_FROCK:
	case HEAD_DARKAQUA:
	case HEAD_DARK_SNOW:
		switch (bodynum) {
		case BODY_DARK_COMBAT:
		case BODY_DARK_FROCK:
		case BODY_DARK_TRENCH:
		case BODY_DARK_RIPPED:
		case BODY_DARK_AF1:
		case BODY_DARKWET:
		case BODY_DARKAQUALUNG:
		case BODY_DARKSNOW:
		case BODY_DARKLAB:
		case BODY_DARK_LEATHER:
		case BODY_DARK_NEGOTIATOR:
			break;
		default:
			offset = -12;
			break;
		}
		break;
	}
#endif

	if ((s16)(*(s32 *)&headmodeldef->skel) == SKEL_HEAD) {
#if VERSION >= VERSION_JPN_FINAL
		if (g_HeadsAndBodies[headnum].type == g_HeadsAndBodies[bodynum].type && offset == 0 && g_HeadsAndBodies[headnum].yoffset == 0) {
			// sysLogPrintf(LOG_NOTE, "DEBUG bodyCalculateHeadOffset EARLY RETURN (JPN) same type & offset=0 & yoffset=0");
			return;
		}
#else
		if (g_HeadsAndBodies[headnum].type == g_HeadsAndBodies[bodynum].type && g_HeadsAndBodies[headnum].yoffset == 0) {
			// sysLogPrintf(LOG_NOTE, "DEBUG bodyCalculateHeadOffset EARLY RETURN same type & yoffset=0");
			return;
		}
#endif

#if VERSION >= VERSION_JPN_FINAL
		switch (g_HeadsAndBodies[headnum].type) {
		default:
		case HEADBODYTYPE_FEMALE:
			offset += 0;
			break;
		case HEADBODYTYPE_MAIAN:
			offset += 0;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset -= 35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			offset += 0;
			break;
		case HEADBODYTYPE_CASS:
			offset -= 20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset -= 40;
			break;
		}
#else
		// Same as JPN, but sets the value rather than adjusts
		switch (g_HeadsAndBodies[headnum].type) {
		default:
		case HEADBODYTYPE_FEMALE:
			offset = 0;
			break;
		case HEADBODYTYPE_MAIAN:
			offset = 0;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset = -35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			offset = 0;
			break;
		case HEADBODYTYPE_CASS:
			offset = -20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset = -40;
			break;
		}
#endif

		switch (g_HeadsAndBodies[bodynum].type) {
		case HEADBODYTYPE_FEMALE:
			break;
		case HEADBODYTYPE_MAIAN:
			offset -= 30;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset += 35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			break;
		case HEADBODYTYPE_CASS:
			offset += 20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset += 40;
			break;
		}

		if (g_HeadsAndBodies[bodynum].type == HEADBODYTYPE_FEMALE) {
			if (g_HeadsAndBodies[headnum].type == HEADBODYTYPE_DEFAULT
					|| g_HeadsAndBodies[headnum].type == HEADBODYTYPE_MRBLONDE) {
				offset -= 10;
			} else if (g_HeadsAndBodies[headnum].type == HEADBODYTYPE_CASS
					|| g_HeadsAndBodies[headnum].type == HEADBODYTYPE_FEMALEGUARD) {
				offset -= 5;
			}
		} else if (g_HeadsAndBodies[bodynum].type == HEADBODYTYPE_CASS
				&& (g_HeadsAndBodies[headnum].type == HEADBODYTYPE_DEFAULT
					|| g_HeadsAndBodies[headnum].type == HEADBODYTYPE_MRBLONDE)) {
			offset -= 5;
		}

		offset += g_HeadsAndBodies[headnum].yoffset;

		// sysLogPrintf(LOG_NOTE, "DEBUG bodyCalculateHeadOffset final offset=%d (yoffset=%d)", offset, g_HeadsAndBodies[headnum].yoffset);

		// Apply the offset
		if (offset != 0) {
			node = NULL;

			do {
				prev = node;

				modelIterateDisplayLists(headmodeldef, &node, &gdl);

				if (node && node != prev && node->type == MODELNODETYPE_DL) {
					struct modelrodata_dl *rodata = &node->rodata->dl;

					for (i = 0; i < rodata->numvertices; i++) {
						rodata->vertices[i].y += offset;
					}
				}
			} while (node);

			bbox = modeldefFindBboxRodata(headmodeldef);

			if (bbox != NULL) {
				bbox->ymin += offset;
				bbox->ymax += offset;
			}
		}
	}
}

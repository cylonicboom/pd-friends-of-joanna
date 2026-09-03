#include <ultra64.h>
#include "system.h"
#include "constants.h"
#include "game/chraction.h"
#include "game/ceil.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/tex.h"
#include "game/menugfx.h"
#include "game/menu.h"
#include "game/mainmenu.h"
#include "game/inv.h"
#include "game/game_1531a0.h"
#include "game/file.h"
#include "game/texdecompress.h"
#include "game/tex.h"
#include "game/modeldef.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/main.h"
#include "lib/model.h"
#include "data.h"
#include "types.h"

#ifndef PLATFORM_N64
#include "mod.h"
#include "romdata.h"
#include "ext_tex.h"
#endif

struct skeleton *g_Skeletons[] = {
	&g_SkelChr,
	&g_SkelClassicGun,
	&g_Skel06,
	&g_SkelUzi,
	&g_SkelBasic,
	&g_SkelCctv,
	&g_SkelWindowedDoor,
	&g_Skel11,
	&g_Skel12,
	&g_Skel13,
	&g_SkelTerminal,
	&g_SkelCiHub,
	&g_SkelAutogun,
	&g_Skel17,
	&g_Skel18,
	&g_Skel19,
	&g_Skel0A,
	&g_Skel0B,
	&g_SkelCasing,
	&g_SkelChrGun,
	&g_Skel0C,
	&g_SkelJoypad,
	&g_SkelLift,
	&g_SkelSkedar,
	&g_SkelLogo,
	&g_SkelPdLogo,
	&g_SkelHoverbike,
	&g_SkelJumpship,
	&g_Skel20,
	&g_Skel21,
	&g_Skel22,
	&g_SkelLaptopGun,
	&g_SkelK7Avenger,
	&g_SkelChopper,
	&g_SkelFalcon2,
	&g_SkelKnife,
	&g_SkelDrCaroll,
	&g_SkelRope,
	&g_SkelCmp150,
	&g_SkelBanner,
	&g_SkelDragon,
	&g_SkelSuperDragon,
	&g_SkelRocket,
	&g_Skel4A,
	&g_SkelShotgun,
	&g_SkelFarsight,
	&g_Skel4D,
	&g_SkelReaper,
	&g_SkelDropship,
	&g_SkelMauler,
	&g_SkelDevastator,
	&g_SkelRobot,
	&g_SkelPistol,
	&g_SkelAr34,
	&g_SkelMagnum,
	&g_SkelSlayerRocket,
	&g_SkelCyclone,
	&g_SkelSniperRifle,
	&g_SkelTranquilizer,
	&g_SkelCrossbow,
	&g_SkelHudPiece,
	&g_SkelTimedProxyMine,
	&g_SkelPhoenix,
	&g_SkelCallisto,
	&g_SkelHand,
	&g_SkelRcp120,
	&g_SkelSkShuttle,
	&g_SkelLaser,
	&g_SkelMaianUfo,
	&g_SkelGrenade,
	&g_SkelCableCar,
	&g_SkelSubmarine,
	&g_SkelTarget,
	&g_SkelEcmMine,
	&g_SkelUplink,
	&g_SkelRareLogo,
	&g_SkelWireFence,
	&g_SkelRemoteMine,
	&g_SkelBB,
#ifdef AVOID_UB
	NULL // terminate list for sure
#endif
};

#ifndef PLATFORM_N64
extern u16 modTexMapLookup(s32 modIdx, u16 localTexId);

// True if `portId` is a texture asset owned by `modIdx`: either a PNG
// override registered under G_TEXTYPE_GENERAL (ext_tex/ path) or the mod's
// own texmap maps some source ID to this port. Widened from the original
// PNG-only gate so gDL remaps can rewrite baked mod-range IDs for mods
// that ship textures as raw bytes (e.g. mod_gex_characters from gex.z64).
static bool modeldefPortIdIsModOwned(s32 modIdx, u16 portId)
{
	if (extTexExists(G_TEXTYPE_GENERAL, 0, portId)) {
		s8 owner = extTexGetOwnerMod(G_TEXTYPE_GENERAL, 0, portId);
		if (owner >= 0 && owner == modIdx) return true;
	}
	extern u16 modTexMapReverseLookup(s32 modIdx, u16 portTexId);
	if (modTexMapReverseLookup(modIdx, portId) != 0xffff) return true;
	return false;
}

// Per-modeldef pass statistics collected by the gDL walker. All counts are
// per-slot (a G_NOOP can contribute 1 or 2 slots when subcmd==1).
struct modeldefGdlStats {
	u32 noops;        // G_NOOP commands inspected
	u32 inRange;      // slots whose value is < NUM_TEXTURES (candidate sources)
	u32 mapped;       // candidates that produced a different port-range id
	u32 gateAccepted; // mapped ids whose owner matches modIdx (rewrite happened)
};

// Try to remap a single 12-bit texturenum field. Returns the value to write
// back into the gDL slot (same as `orig` when no remap was applied) and sets
// `*didRemap` accordingly. Shared between the two slot positions in a G_NOOP
// command so the source-id/port-id/ownership policy lives in exactly one spot.
// `stats` may be NULL.
static u32 modeldefRemapOneTexnumSlot(u32 orig, s32 modIdx, bool *didRemap, struct modeldefGdlStats *stats)
{
	*didRemap = false;
	// NOTE: intentionally no `orig >= NUM_TEXTURES` short-circuit here.
	// NUM_TEXTURES tracks NTSC-final's vanilla texture count, but JPN heads
	// (Mikado mounted from JPN ROM) reference vanilla texids in the range
	// [NUM_TEXTURES .. JPN's vanilla count]. modTexMapLookup safely returns
	// `orig` unchanged when the mod has no entry for a given texid, so it's
	// fine to consult it for every candidate slot.
	if (stats) ++stats->inRange;
	extern s32 g_TexCurrentModelFileNum;
	// Only mod-inserted files (id >= 2018) participate in the mod texMap;
	// a vanilla model rendering the same source texid must keep its vanilla
	// binding. Also skip when the currently-loading model owns a per-model
	// PNG override at this source texid — the ext_tex path will serve it.
	if (g_TexCurrentModelFileNum < 2018) {
		return orig;
	}
	if (extTexModelHasEntryForTexid((s16)(g_TexCurrentModelFileNum & 0xffff), (s32)orig)) {
		return orig;
	}
	u16 mapped = modTexMapLookup(modIdx, (u16)orig);
	if ((u32)mapped == orig) {
		return orig;
	}
	if (stats) ++stats->mapped;
	if (!modeldefPortIdIsModOwned(modIdx, mapped)) {
		return orig;
	}
	if (stats) ++stats->gateAccepted;
	*didRemap = true;
	return (u32)mapped;
}

// Rewrite a mod-owned model's texconfig texturenums via the mod's source->port
// texmap. Centralized so the texconfig pass and the gDL pass below cannot drift
// in their handling of "what's a remappable source ID".
static void modeldefRemapTexconfigsForMod(struct modeldef *modeldef, s32 modIdx)
{
	if (modIdx < 0 || !modeldef->texconfigs || modeldef->numtexconfigs <= 0) {
		return;
	}

	struct textureconfig *tc = modeldef->texconfigs;
	s32 numtc = modeldef->numtexconfigs;
	for (s32 i = 0; i < numtc; ++i) {
		uintptr_t v = (uintptr_t)tc[i].texturenum;
		bool didRemap = false;
		u32 next = modeldefRemapOneTexnumSlot((u32)v, modIdx, &didRemap, NULL);
		if (didRemap) {
			tc[i].texturenum = (texnum_t)(uintptr_t)next;
		}
	}
}

// Remap the two 12-bit texturenum slots inside a single G_NOOP texture-binding
// command. Returns the number of slots that were rewritten. Shared between the
// per-DL walker and any future caller to keep the bit-layout knowledge in one
// place. `stats` may be NULL.
static u32 modeldefRemapGdlCmd(Gfx *cmd, s32 modIdx, struct modeldefGdlStats *stats)
{
	if (cmd->texture.cmd != G_NOOP) {
		return 0;
	}

	if (stats) ++stats->noops;

	u32 remapped = 0;
	u32 w1 = cmd->words.w1;
	bool didRemap;

	u32 t0 = w1 & 0xfff;
	u32 n0 = modeldefRemapOneTexnumSlot(t0, modIdx, &didRemap, stats);
	if (didRemap) {
		w1 = (w1 & ~0xfffu) | (n0 & 0xfffu);
		++remapped;
	}

	if (cmd->unkc0.subcmd == 1) {
		u32 t1 = (w1 >> 12) & 0xfff;
		u32 n1 = modeldefRemapOneTexnumSlot(t1, modIdx, &didRemap, stats);
		if (didRemap) {
			w1 = (w1 & ~(0xfffu << 12)) | ((n1 & 0xfffu) << 12);
			++remapped;
		}
	}

	cmd->words.w1 = w1;
	return remapped;
}

// Walk every display list reachable through the model and apply
// modeldefRemapGdlCmd to its commands. The DL boundary math mirrors
// modeldef0f1a7560 exactly so this pass and the downstream texLoadFromGdl
// agree on what bytes belong to which DL.
static void modeldefRemapGdlTexnumsForMod(struct modeldef *modeldef, s32 modIdx, s32 filenum)
{
	if (modIdx < 0) {
		return;
	}

	s32 loadedsize = (s32)fileGetLoadedSize(filenum);
	struct modelnode *node = NULL;
	uintptr_t gdl = 0;

	modelIterateDisplayLists(modeldef, &node, (Gfx **)&gdl);
	if (!gdl) {
		return;
	}

	u32 totalRemapped = 0;
	struct modeldefGdlStats stats = {0};

	while (node) {
		uintptr_t s0 = gdl;
		modelIterateDisplayLists(modeldef, &node, (Gfx **)&gdl);

		s32 bytes;
		if (gdl) {
			bytes = (s32)(UNSEGADDR(gdl) - UNSEGADDR(s0));
		} else {
			bytes = loadedsize - (s32)(UNSEGADDR(s0) & 0xffffff);
		}

#ifdef PLATFORM_64BIT
		s32 numcmds = bytes >> 4;
#else
		s32 numcmds = bytes >> 3;
#endif

		Gfx *dl = (Gfx *)((uintptr_t)modeldef + (UNSEGADDR(s0) & 0xffffff));
		for (s32 i = 0; i < numcmds; ++i) {
			totalRemapped += modeldefRemapGdlCmd(&dl[i], modIdx, &stats);

			// One-shot dump of all aio-owned (modIdx==2) model G_NOOP
			// texturenums so we can rebuild texmap entries against the
			// model's actual gDL. Rate-limited per-filenum.
			if (dl[i].texture.cmd == G_NOOP && modIdx == 2) {
				static u32 s_lastDumpedFile = 0;
				static u32 s_dumpedForCurrent = 0;
				if ((u32)filenum != s_lastDumpedFile) {
					s_lastDumpedFile = (u32)filenum;
					s_dumpedForCurrent = 0;
				}
				if (s_dumpedForCurrent < 200) {
					u32 w1 = dl[i].words.w1;
					u32 t0 = w1 & 0xfff;
					u32 t1 = (dl[i].unkc0.subcmd == 1) ? (w1 >> 12) & 0xfff : 0xffff;
					sysLogPrintf(LOG_NOTE,
						"modeldefGdlDump: filenum=0x%08x cmd[%d] t0=0x%03x t1=0x%03x subcmd=%u",
						filenum, i, t0, t1, dl[i].unkc0.subcmd);
					++s_dumpedForCurrent;
				}
			}
		}
	}

	// Always log per-modeldef stats so we can tell apart: zero G_NOOPs,
	// G_NOOPs with no in-range source ids, ids that don't appear in the
	// texmap, and gate denials. Rate-limited to keep the log readable.
	static u32 s_logCount = 0;
	if (s_logCount < 128) {
		sysLogPrintf(LOG_NOTE,
			"modeldefRemapGdlTexnums: filenum=0x%08x modIdx=%d noops=%u inRange=%u mapped=%u gateAccepted=%u rewritten=%u",
			filenum, modIdx,
			stats.noops, stats.inRange, stats.mapped, stats.gateAccepted, totalRemapped);
		++s_logCount;
	}
}

static void modeldefGetNodeUvEnvelope(struct modeldef *modeldef, s32 loadedSize,
		struct modelnode *node, struct modeldefTextureUsage *entry)
{
	Vtx *vertices = NULL;
	s32 numvertices = 0;

	if ((node->type & 0xff) == MODELNODETYPE_DL) {
		vertices = node->rodata->dl.vertices;
		numvertices = node->rodata->dl.numvertices;
	} else if ((node->type & 0xff) == MODELNODETYPE_GUNDL) {
		vertices = node->rodata->gundl.vertices;
		numvertices = node->rodata->gundl.numvertices;
	}

	entry->numvertices = numvertices;
	entry->minS = entry->maxS = 0;
	entry->minT = entry->maxT = 0;
	if (!vertices || numvertices <= 0
			|| (uintptr_t)vertices < (uintptr_t)modeldef
			|| (uintptr_t)vertices + (u32)numvertices * sizeof(Vtx) > (uintptr_t)modeldef + loadedSize) {
		entry->numvertices = 0;
		return;
	}

	entry->minS = entry->maxS = vertices[0].s;
	entry->minT = entry->maxT = vertices[0].t;
	for (s32 i = 1; i < numvertices; ++i) {
		if (vertices[i].s < entry->minS) entry->minS = vertices[i].s;
		if (vertices[i].s > entry->maxS) entry->maxS = vertices[i].s;
		if (vertices[i].t < entry->minT) entry->minT = vertices[i].t;
		if (vertices[i].t > entry->maxT) entry->maxT = vertices[i].t;
	}
}

static bool modeldefRecordTextureTriangle(struct modeldef *modeldef, s32 loadedSize,
		struct modelnode *node, u8 listType, u32 commandIndex, u16 textureId,
		Vtx **vertexSlots, u8 v0, u8 v1, u8 v2,
		struct modeldefTextureTriangle *triangles, s32 maxtriangles, s32 *written)
{
	const u8 indexes[3] = {v0, v1, v2};
	if (!triangles || *written >= maxtriangles) return false;
	for (s32 i = 0; i < 3; ++i) {
		if (indexes[i] >= 16 || !vertexSlots[indexes[i]]
				|| (uintptr_t)vertexSlots[indexes[i]] < (uintptr_t)modeldef
				|| (uintptr_t)vertexSlots[indexes[i]] + sizeof(Vtx) > (uintptr_t)modeldef + loadedSize) {
			return false;
		}
	}

	struct modeldefTextureTriangle *triangle = &triangles[(*written)++];
	triangle->nodeoffset = (u32)((uintptr_t)node - (uintptr_t)modeldef);
	triangle->commandindex = commandIndex;
	triangle->textureid = textureId;
	triangle->listtype = listType;
	for (s32 i = 0; i < 3; ++i) {
		triangle->vertexindex[i] = indexes[i];
		triangle->s[i] = vertexSlots[indexes[i]]->s;
		triangle->t[i] = vertexSlots[indexes[i]]->t;
	}
	return true;
}

s32 modeldefInspectTextureUsage(s32 fileid, u16 textureid1, u16 textureid2,
		struct modeldefTextureUsage *entries, s32 maxentries, s32 *totalmatches,
		struct modeldefTextureTriangle *triangles, s32 maxtriangles,
		s32 *capturedtriangles, s32 *totaltriangles)
{
	const u32 allocationSize = fileGetAllocationSize(fileid);
	if (totalmatches) *totalmatches = 0;
	if (capturedtriangles) *capturedtriangles = 0;
	if (totaltriangles) *totaltriangles = 0;
	if (allocationSize == 0 || allocationSize > 16 * 1024 * 1024
			|| !entries || maxentries <= 0) return 0;

	u8 *buffer = sysMemAlloc(allocationSize);
	if (!buffer) return 0;
	u8 previousLoadType = g_LoadType;
	g_LoadType = LOADTYPE_MODEL;
	struct modeldef *modeldef = fileLoadToAddr(fileid, FILELOADMETHOD_EXTRAMEM, buffer, allocationSize);
	g_LoadType = previousLoadType;
	if (!modeldef) {
		sysMemFree(buffer);
		return 0;
	}

	modelPromoteTypeToPointer(modeldef);
	modelPromoteOffsetsToPointers(modeldef, 0x5000000, (uintptr_t)modeldef);
	const s32 loadedSize = fileGetLoadedSize(fileid);
	if (loadedSize <= 0 || (u32)loadedSize > allocationSize) {
		sysMemFree(buffer);
		return 0;
	}
	struct modelnode *node = NULL;
	Gfx *gdl = NULL;
	s32 written = 0;
	s32 total = 0;
	s32 trianglesWritten = 0;
	s32 trianglesTotal = 0;
	modelIterateDisplayLists(modeldef, &node, &gdl);

	while (node && gdl) {
		struct modelnode *currentNode = node;
		Gfx *currentGdl = gdl;
		modelIterateDisplayLists(modeldef, &node, &gdl);
		const u32 currentOffset = UNSEGADDR(currentGdl) & 0xffffff;
		const u32 nextOffset = gdl ? UNSEGADDR(gdl) & 0xffffff : loadedSize;
		if (currentOffset >= (u32)loadedSize || nextOffset > (u32)loadedSize || nextOffset <= currentOffset) {
			continue;
		}
		const s32 bytes = nextOffset - currentOffset;
#ifdef PLATFORM_64BIT
		const s32 commandCount = bytes >> 4;
#else
		const s32 commandCount = bytes >> 3;
#endif
		Gfx *commands = (Gfx *)((uintptr_t)modeldef + currentOffset);
		Vtx *vertexSlots[16] = {0};
		bool selectedTextureActive = false;
		u16 activeTextureId = 0;
		u8 listType = 2;
		if ((currentNode->type & 0xff) == MODELNODETYPE_DL) {
			listType = currentGdl == currentNode->rodata->dl.opagdl ? 0 : 1;
		} else if ((currentNode->type & 0xff) == MODELNODETYPE_GUNDL) {
			listType = currentGdl == currentNode->rodata->gundl.opagdl ? 0 : 1;
		}

		for (s32 commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
			Gfx *command = &commands[commandIndex];
			const u8 opcode = command->words.w0 >> 24;
			if (opcode == G_NOOP) {
				const u16 ids[2] = {
					(u16)(command->words.w1 & 0xfff),
					(u16)((command->words.w1 >> 12) & 0xfff),
				};
				const s32 slotCount = command->unkc0.subcmd == 1 ? 2 : 1;
				selectedTextureActive = false;
				for (s32 slot = 0; slot < slotCount; ++slot) {
					if (ids[slot] != textureid1 && ids[slot] != textureid2) continue;
					selectedTextureActive = true;
					activeTextureId = ids[slot];
					if (written < maxentries) {
						struct modeldefTextureUsage *entry = &entries[written++];
						entry->nodeoffset = (u32)((uintptr_t)currentNode - (uintptr_t)modeldef);
						entry->nodetype = currentNode->type;
						entry->listtype = listType;
						entry->textureslot = slot;
						entry->commandindex = commandIndex;
						entry->textureid = ids[slot];
						modeldefGetNodeUvEnvelope(modeldef, loadedSize, currentNode, entry);
					}
					++total;
				}
			} else if (opcode == G_VTX) {
				const u32 vertexOffset = command->words.w1 & 0xffffff;
				const u32 vertexCount = (command->words.w0 & 0xffff) / sizeof(Vtx);
				const u32 firstSlot = (command->words.w0 >> 16) & 0xf;
				for (u32 i = 0; i < vertexCount && firstSlot + i < 16; ++i) {
					vertexSlots[firstSlot + i] = vertexOffset + (i + 1) * sizeof(Vtx) <= (u32)loadedSize
						? (Vtx *)((uintptr_t)modeldef + vertexOffset + i * sizeof(Vtx)) : NULL;
				}
			} else if (selectedTextureActive && opcode == G_TRI1) {
				const u8 v0 = ((command->words.w1 >> 16) & 0xff) / 10;
				const u8 v1 = ((command->words.w1 >> 8) & 0xff) / 10;
				const u8 v2 = (command->words.w1 & 0xff) / 10;
				++trianglesTotal;
				modeldefRecordTextureTriangle(modeldef, loadedSize, currentNode, listType,
					commandIndex, activeTextureId, vertexSlots, v0, v1, v2,
					triangles, maxtriangles, &trianglesWritten);
			} else if (selectedTextureActive && opcode == G_TRI4) {
				for (s32 tri = 0; tri < 4; ++tri) {
					const u8 v0 = (command->words.w1 >> (tri * 8)) & 0xf;
					const u8 v1 = (command->words.w1 >> (tri * 8 + 4)) & 0xf;
					const u8 v2 = (command->words.w0 >> (tri * 4)) & 0xf;
					if (v0 == 0 && v1 == 0 && v2 == 0) continue;
					++trianglesTotal;
					modeldefRecordTextureTriangle(modeldef, loadedSize, currentNode, listType,
						commandIndex, activeTextureId, vertexSlots, v0, v1, v2,
						triangles, maxtriangles, &trianglesWritten);
				}
			}
		}
	}

	if (totalmatches) *totalmatches = total;
	if (capturedtriangles) *capturedtriangles = trianglesWritten;
	if (totaltriangles) *totaltriangles = trianglesTotal;
	sysMemFree(buffer);
	return written;
}
#endif

void modeldef0f1a7560(struct modeldef *modeldef, s32 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
{
	s32 allocsize;
	s32 loadedsize;
	s32 sp84;
	u32 s0;
	u32 s4;
	uintptr_t s5;
	struct modelnode *node;
	struct modelnode *prevnode;
	uintptr_t gdl;
	Vtx *vertices;

	allocsize = fileGetAllocationSize(filenum);
	loadedsize = fileGetLoadedSize(filenum);
	node = NULL;

#ifndef PLATFORM_N64
	s32 prevTexMod = g_TexModNum;
	g_TexModNum = filenum >> 16;
#endif

	modelIterateDisplayLists(modeldef, &node, (Gfx **)&gdl);

	s5 = gdl;

	if (gdl) {
		s32 v1 = allocsize - (loadedsize - (uintptr_t)(((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)) - (uintptr_t)modeldef));
		sp84 = (uintptr_t)v1 + (uintptr_t)((uintptr_t)modeldef - ((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)));

		texCopyGdls((Gfx *)((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)),
				(Gfx *)(v1 + (uintptr_t)modeldef),
				loadedsize - (uintptr_t)(((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)) - (uintptr_t)modeldef));
		texLoadFromConfigs(modeldef->texconfigs, modeldef->numtexconfigs, texpool, (uintptr_t)modeldef2 - (uintptr_t)arg2);

		while (node) {
			prevnode = node;
			s0 = gdl;

			modelIterateDisplayLists(modeldef, &node, (Gfx **) &gdl);

			if (gdl) {
				s4 = UNSEGADDR(gdl) - UNSEGADDR(s0);
			} else {
				s4 = loadedsize + (uintptr_t)modeldef - (uintptr_t)modeldef - (UNSEGADDR(s0) & 0xffffff);
			}

			modelNodeReplaceGdl(modeldef, prevnode, (Gfx *) s0, (Gfx *) s5);

			if (prevnode->type == MODELNODETYPE_DL) {
				struct modelrodata_dl *rodata = &prevnode->rodata->dl;
				vertices = rodata->vertices;
			} else {
				vertices = NULL;
			}

			s5 += texLoadFromGdl((Gfx *)((uintptr_t)modeldef + (UNSEGADDR(s0) & 0xffffff) + sp84), s4, (Gfx *)((uintptr_t)modeldef + (UNSEGADDR(s5) & 0xffffff)), texpool, (u8 *) vertices);
		}

		fileSetSize(filenum, modeldef, (((uintptr_t)modeldef + (UNSEGADDR(s5) & 0xffffff)) - (uintptr_t)modeldef + 0xf) & ~0xf, arg5);
	}

#ifndef PLATFORM_N64
	g_TexModNum = prevTexMod;
#endif
}

void modelPromoteTypeToPointer(struct modeldef *modeldef)
{
	s32 i;

	if ((u32)modeldef->skel < 0x10000) {
		for (i = 0; g_Skeletons[i] != NULL; i++) {
			if ((s16)modeldef->skel == g_Skeletons[i]->skel) {
				modeldef->skel = g_Skeletons[i];
				return;
			}
		}
	}
}

struct modeldef *modeldefLoad(s32 fileid, u8 *dst, s32 size, struct texpool *arg3)
{
	struct modeldef *modeldef;

	g_LoadType = LOADTYPE_MODEL;

	if (dst) {
		modeldef = fileLoadToAddr(fileid, FILELOADMETHOD_EXTRAMEM, dst, size);
	} else {
		modeldef = fileLoadToNew(fileid, FILELOADMETHOD_EXTRAMEM, LOADTYPE_MODEL);
	}

	if (!modeldef) {
		sysLogPrintf(LOG_ERROR, "modeldefLoad: fileLoad returned NULL for fileid=0x%08x", fileid);
		return NULL;
	}

	modelPromoteTypeToPointer(modeldef);
	modelPromoteOffsetsToPointers(modeldef, 0x5000000, (uintptr_t) modeldef);

#ifndef PLATFORM_N64
	{
		s32 modIdx = (fileid >> 16) & 0xff;
		s32 prevModelFN = g_TexCurrentModelFileNum;
		g_TexCurrentModelFileNum = fileid & 0xffff;
		modeldefRemapTexconfigsForMod(modeldef, modIdx);
		modeldefRemapGdlTexnumsForMod(modeldef, modIdx, fileid);
		modeldef0f1a7560(modeldef, fileid, 0x5000000, modeldef, arg3, dst == NULL);
		g_TexCurrentModelFileNum = prevModelFN;
	}
#else
	modeldef0f1a7560(modeldef, fileid, 0x5000000, modeldef, arg3, dst == NULL);
#endif

	return modeldef;
}

struct modeldef *modeldefLoadToNew(s32 fileid)
{
	return modeldefLoad(fileid, NULL, 0, NULL);
}

struct modeldef *modeldefLoadToAddr(s32 fileid, u8 *dst, s32 size)
{
	return modeldefLoad(fileid, dst, size, NULL);
}

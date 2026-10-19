#include <ultra64.h>
#include <string.h>
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
#include "gbiex.h"
#include "types.h"

#ifndef PLATFORM_N64
#include "mod.h"
#include "texscope.h"
#include "romdata.h"
#include "textrace.h"
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
static struct {
	s32 fileid;
	u8 *modeldata;
	u8 *texturedata;
	u32 modelcapacity;
	u32 texturecapacity;
	struct texpool texturepool;
	struct modeldef *modeldef;
} g_ModeldefEditorWorkspace;

void modeldefEditorWorkspaceUnload(void)
{
	if (g_ModeldefEditorWorkspace.modeldata) {
		sysMemFree(g_ModeldefEditorWorkspace.modeldata);
	}
	if (g_ModeldefEditorWorkspace.texturedata) {
		sysMemFree(g_ModeldefEditorWorkspace.texturedata);
	}
	memset(&g_ModeldefEditorWorkspace, 0, sizeof(g_ModeldefEditorWorkspace));
	g_ModeldefEditorWorkspace.fileid = -1;
}

static bool modeldefEditorWorkspaceLoadInner(s32 fileid, u32 texturecapacity);

// Same bookkeeping rule as modeldefInspectTextureUsage: the workspace copy
// is private, the file's loadedsize/allocsize belong to the live game.
bool modeldefEditorWorkspaceLoad(s32 fileid, u32 texturecapacity)
{
	const s32 raw = fileid & 0xffff;
	struct fileinfo saved = g_FileInfo[raw];
	const bool ok = modeldefEditorWorkspaceLoadInner(fileid, texturecapacity);
	g_FileInfo[raw] = saved;
	return ok;
}

static bool modeldefEditorWorkspaceLoadInner(s32 fileid, u32 texturecapacity)
{
	const u32 inflatedSize = fileGetInflatedSize(fileid, LOADTYPE_MODEL);
	const u32 modelcapacity = ALIGN64(inflatedSize) + 0x8000;
	if (inflatedSize == 0 || modelcapacity > 16 * 1024 * 1024
			|| texturecapacity < 0x10000 || texturecapacity > 16 * 1024 * 1024) {
		return false;
	}

	modeldefEditorWorkspaceUnload();
	g_ModeldefEditorWorkspace.modeldata = sysMemAlloc(modelcapacity);
	g_ModeldefEditorWorkspace.texturedata = sysMemAlloc(texturecapacity);
	if (!g_ModeldefEditorWorkspace.modeldata || !g_ModeldefEditorWorkspace.texturedata) {
		modeldefEditorWorkspaceUnload();
		return false;
	}

	g_ModeldefEditorWorkspace.fileid = fileid;
	g_ModeldefEditorWorkspace.modelcapacity = modelcapacity;
	g_ModeldefEditorWorkspace.texturecapacity = texturecapacity;
	texInitPool(&g_ModeldefEditorWorkspace.texturepool,
		g_ModeldefEditorWorkspace.texturedata, texturecapacity);
	g_ModeldefEditorWorkspace.modeldef = modeldefLoad(fileid,
		g_ModeldefEditorWorkspace.modeldata, modelcapacity,
		&g_ModeldefEditorWorkspace.texturepool);
	if (!g_ModeldefEditorWorkspace.modeldef) {
		modeldefEditorWorkspaceUnload();
		return false;
	}
	return true;
}

bool modeldefEditorWorkspaceGetInfo(struct modeldefEditorWorkspaceInfo *info)
{
	if (!info || !g_ModeldefEditorWorkspace.modeldef) return false;
	info->fileid = g_ModeldefEditorWorkspace.fileid;
	info->modeldef = g_ModeldefEditorWorkspace.modeldef;
	info->modelcapacity = g_ModeldefEditorWorkspace.modelcapacity;
	info->modelloadedsize = fileGetLoadedSize(g_ModeldefEditorWorkspace.fileid);
	info->texturecapacity = g_ModeldefEditorWorkspace.texturecapacity;
	info->texturebytesused = g_ModeldefEditorWorkspace.texturepool.leftpos
		- g_ModeldefEditorWorkspace.texturepool.start;
	info->texturecount = g_ModeldefEditorWorkspace.texturepool.end
		- g_ModeldefEditorWorkspace.texturepool.rightpos;
	return true;
}

struct tex *modeldefEditorWorkspaceFindTexture(u16 textureid)
{
	if (!g_ModeldefEditorWorkspace.modeldef) return NULL;
	texScopePush(g_ModeldefEditorWorkspace.fileid);
	struct tex *tex = texFindInPool(textureid, &g_ModeldefEditorWorkspace.texturepool);
	texScopePop();
	return tex;
}

static u32 modeldefEditorTextureLodDecodedSize(struct tex *tex, s32 lod)
{
	const u32 tmemUnits = texGetSizeInBytes(tex, lod);
	return tmemUnits * (tex->depth == G_IM_SIZ_32b ? 16 : 8);
}

bool modeldefEditorWorkspaceGetTextureInfo(s32 index, struct modeldefEditorTextureInfo *info)
{
	if (!info || !g_ModeldefEditorWorkspace.modeldef || index < 0) return false;
	const s32 textureCount = g_ModeldefEditorWorkspace.texturepool.end
		- g_ModeldefEditorWorkspace.texturepool.rightpos;
	if (index >= textureCount) return false;

	struct tex *tex = &g_ModeldefEditorWorkspace.texturepool.rightpos[index];
	info->textureid = tex->texturenum;
	info->width = tex->width;
	info->height = tex->height;
	info->gbiformat = tex->gbiformat;
	info->depth = tex->depth;
	info->lutmode = tex->lutmodeindex;
	info->palettecount = tex->lutmodeindex ? tex->numcolors + 1 : 0;
	info->lodcount = tex->numlods ? tex->numlods : 1;
	info->hasloddata = tex->hasloddata;
	info->decodedsize = modeldefEditorTextureLodDecodedSize(tex, 0);
	return true;
}

s32 modeldefEditorWorkspaceGetTextureLods(u16 textureid,
		struct modeldefEditorTextureLodInfo *entries, s32 maxentries)
{
	if (!entries || maxentries <= 0) return 0;
	struct tex *tex = modeldefEditorWorkspaceFindTexture(textureid);
	if (!tex) return 0;

	const s32 lodCount = tex->numlods ? tex->numlods : 1;
	const s32 count = lodCount < maxentries ? lodCount : maxentries;
	u32 tmemOffset = 0;
	u32 decodedOffset = 0;
	for (s32 lod = 0; lod < count; ++lod) {
		struct modeldefEditorTextureLodInfo *entry = &entries[lod];
		entry->lod = lod;
		entry->width = texGetWidthAtLod(tex, lod);
		entry->height = texGetHeightAtLod(tex, lod);
		entry->embedded = tex->hasloddata;
		entry->tmemoffset = tmemOffset;
		entry->tmemunits = texGetSizeInBytes(tex, lod);
		entry->decodedoffset = decodedOffset;
		entry->decodedsize = modeldefEditorTextureLodDecodedSize(tex, lod);
		tmemOffset += entry->tmemunits;
		decodedOffset += entry->decodedsize;
	}
	return count;
}

s32 modeldefEditorWorkspaceGetPalette(u16 textureid,
		struct modeldefEditorPaletteEntry *entries, s32 maxentries)
{
	if (!entries || maxentries <= 0) return 0;
	struct tex *tex = modeldefEditorWorkspaceFindTexture(textureid);
	if (!tex || tex->gbiformat != G_IM_FMT_CI || tex->lutmodeindex == 0) return 0;

	const s32 paletteCount = tex->numcolors + 1;
	const s32 count = paletteCount < maxentries ? paletteCount : maxentries;
	const u8 *palette = tex->data + tex->tlutoffset;
	if (tex->data < g_ModeldefEditorWorkspace.texturepool.start
			|| palette + paletteCount * 2 > g_ModeldefEditorWorkspace.texturepool.leftpos) {
		return 0;
	}
	for (s32 index = 0; index < count; ++index) {
		struct modeldefEditorPaletteEntry *entry = &entries[index];
		const u16 raw = palette[index * 2] << 8 | palette[index * 2 + 1];
		entry->rawvalue = raw;
		entry->usagecount = 0;
		entry->duplicateof = -1;
		if (tex->lutmodeindex == (G_TT_IA16 >> G_MDSFT_TEXTLUT)) {
			entry->red = entry->green = entry->blue = raw >> 8;
			entry->alpha = raw & 0xff;
		} else {
			entry->red = (((raw >> 11) & 0x1f) * 255) / 31;
			entry->green = (((raw >> 6) & 0x1f) * 255) / 31;
			entry->blue = (((raw >> 1) & 0x1f) * 255) / 31;
			entry->alpha = (raw & 1) ? 255 : 0;
		}
		for (s32 previous = 0; previous < index; ++previous) {
			if (entries[previous].rawvalue == raw) {
				entry->duplicateof = previous;
				break;
			}
		}
	}

	const u32 rowBytes = tex->depth == G_IM_SIZ_8b ? tex->width : (tex->width + 1) / 2;
	const u32 indexBytes = rowBytes * tex->height;
	if (indexBytes <= tex->tlutoffset) {
		for (u32 y = 0; y < tex->height; ++y) {
			for (u32 x = 0; x < tex->width; ++x) {
				const u8 value = tex->data[y * rowBytes + (tex->depth == G_IM_SIZ_8b ? x : x / 2)];
				const u8 paletteIndex = tex->depth == G_IM_SIZ_8b
					? value : (x & 1 ? value & 0xf : value >> 4);
				if (paletteIndex < count) entries[paletteIndex].usagecount++;
			}
		}
	}
	return count;
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

static s32 modeldefInspectTextureUsageInner(s32 fileid, u16 textureid1, u16 textureid2,
		struct modeldefTextureUsage *entries, s32 maxentries, s32 *totalmatches,
		struct modeldefTextureTriangle *triangles, s32 maxtriangles,
		s32 *capturedtriangles, s32 *totaltriangles);

/**
 * A private, read-only load of a model file must leave the file's
 * bookkeeping as it found it.
 *
 * fileLoadToAddr writes g_FileInfo[raw].loadedsize and allocsize, and
 * fileLoadToNew reuses a non-zero loadedsize instead of recomputing
 * "inflated + 0x8000" - the headroom modeldef0f1a7560 expands the model's
 * display lists into. A debugger scan of an OFF-SCREEN head (the skin match
 * panel, 2026-09-28) left loadedsize at the bare inflated size; the next
 * modeldefLoadToNew of that head - the third-person switch rebuilds the
 * body and reloads the head - allocated too small, the expanded lists ran
 * past the allocation, and the renderer walked a list with no ENDDL into
 * vertex data ('Unknown GBI opcode', lldb). Head on screen first, no reload,
 * no crash: exactly her repro. The model-swap code zeroes loadedsize before
 * its reloads for the same reason.
 */
s32 modeldefInspectTextureUsage(s32 fileid, u16 textureid1, u16 textureid2,
		struct modeldefTextureUsage *entries, s32 maxentries, s32 *totalmatches,
		struct modeldefTextureTriangle *triangles, s32 maxtriangles,
		s32 *capturedtriangles, s32 *totaltriangles)
{
	const s32 raw = fileid & 0xffff;
	struct fileinfo saved = g_FileInfo[raw];
	const s32 count = modeldefInspectTextureUsageInner(fileid, textureid1, textureid2,
			entries, maxentries, totalmatches, triangles, maxtriangles,
			capturedtriangles, totaltriangles);
	g_FileInfo[raw] = saved;
	return count;
}

static s32 modeldefInspectTextureUsageInner(s32 fileid, u16 textureid1, u16 textureid2,
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
					(u16)G_NOOP_TEXSLOT_READ(command->words.w0, command->words.w1, 0),
					(u16)G_NOOP_TEXSLOT_READ(command->words.w0, command->words.w1, 1),
				};
				const s32 slotCount = command->unkc0.subcmd == 1 ? 2 : 1;
				selectedTextureActive = false;
				for (s32 slot = 0; slot < slotCount; ++slot) {
					const bool matchAll = textureid1 == 0xffff && textureid2 == 0xffff;
					if (!matchAll && ids[slot] != textureid1 && ids[slot] != textureid2) continue;
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
	// Scope this model's texture loads to whoever owns it. The owner rule
	// (tag first, then the running stage's claim, then vanilla) and the
	// reasons for it live with texScopePush. This is the one bracket the
	// gun loader also passes through, so a gun or hand model gets its
	// scope here whether or not it came in through modeldefLoad.
	texScopePush(filenum);
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
	texScopePop();
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
	// Model swap: route a swapped character model's textures into the private
	// swap pool (body.c / texdecompress.c) so the overlay's textures load fresh
	// instead of hitting the base game's shared-pool cache.
	{
		extern struct texpool g_ModelSwapTexPool;

		if (arg3 == NULL && g_ModelSwapTexActive) {
			arg3 = &g_ModelSwapTexPool;
		}
	}
#endif

#ifndef PLATFORM_N64
	{
		// A mod model's texture ids are read as the model wrote them: the
		// load scope says which mod's filetable they name, and the pool
		// keeps one entry per (id, owner). Nothing is rewritten here any
		// more; the gun loader, which never came through this function,
		// gets the same treatment for free.
		texScopePush(fileid);
		TEXTRACE(TEXTRACE_MODELDEF, fileid, MOD_FILEID_MOD(fileid), g_TexCurrentModelFileNum, 0, 0);
		modeldef0f1a7560(modeldef, fileid, 0x5000000, modeldef, arg3, dst == NULL);
		TEXTRACE(TEXTRACE_MODELDEF_END, fileid, fileGetLoadedSize(fileid), 0, 0, 0);
		texScopePop();
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

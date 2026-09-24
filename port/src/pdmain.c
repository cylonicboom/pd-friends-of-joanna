#include <stdlib.h>
#include <ctype.h>

#include "bss.h"
#include "constants.h"
#include "data.h"
#include "fs.h"
#include "utils.h"
#include "game/body.h"
#include "game/camdraw.h"
#include "game/challenge.h"
#include "game/cheats.h"
#include "game/chrai.h"
#include "game/debug.h"
#include "game/endscreen.h"
#include "game/file.h"
#include "game/game_1531a0.h"
#include "game/game_175f90.h"
#include "game/game_1a78b0.h"
#include "game/gfxmemory.h"
#include "game/lang.h"
#include "game/luaai.h"
#include "game/lv.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/setup.h"
#include "game/mpstats.h"
#include "game/music.h"
#include "game/objectives.h"
#include "game/pak.h"
#include "asset.h"
#include "game/pdmode.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/race.h"
#include "game/smoke.h"
#include "game/splat.h"
#include "game/stubs/game_000840.h"
#include "game/stubs/game_000850.h"
#include "game/stubs/game_000860.h"
#include "game/stubs/game_000870.h"
#include "game/stubs/game_0008e0.h"
#include "game/stubs/game_0008f0.h"
#include "game/stubs/game_000900.h"
#include "game/stubs/game_000910.h"
#include "game/stubs/game_00b180.h"
#include "game/stubs/game_00b200.h"
#include "game/stubs/game_175f50.h"
#include "game/tex.h"
#include "game/timing.h"
#include "game/title.h"
#include "game/training.h"
#include "game/utils.h"
#include "game/zbuf.h"
#include "input.h"
#include "lib/anim.h"
#include "lib/args.h"
#include "lib/audiomgr.h"
#include "lib/boot.h"
#include "lib/crash.h"
#include "lib/debughud.h"
#include "lib/dma.h"
#include "lib/fault.h"
#include "lib/joy.h"
#include "lib/lib_2f490.h"
#include "lib/lib_34d0.h"
#include "lib/main.h"
#include "lib/mema.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/profile.h"
#include "lib/rdp.h"
#include "lib/rmon.h"
#include "lib/rng.h"
#include "lib/rzip.h"
#include "lib/sched.h"
#include "lib/snd.h"
#include "lib/str.h"
#include "lib/vars.h"
#include "lib/vi.h"
#include "lib/videbug.h"
#include "lib/vm.h"
#include "mod.h"
#include "mpsetups.h"
#include <unistd.h>
#include "system.h"
#include "savequeue.h"
#include "types.h"
#include "video.h"
#include <PR/ultrasched.h>
#include <string.h>
#include <ultra64.h>

extern u32 g_NumModDirs;
extern u8 *g_MempHeap;
extern u32 g_MempHeapSize;
extern bool gfx_external_textures_enabled;

void rngSetSeed(u32 seed);

bool var8005d9b0 = false;
s32 g_StageNum = STAGE_TITLE;
u32 g_MainMemaHeapSize = 1024 * 300;
bool var8005d9bc = false;
s32 var8005d9c0 = 0;
s32 var8005d9c4 = 0;
bool g_MainGameLogicEnabled = true;
u32 g_MainNumGfxTasks = 0;
bool g_MainIsEndscreen = false;
s32 g_DoBootPakMenu = 0;

u32 var8005dd3c = 0x00000000;
u32 var8005dd40 = 0x00000000;
u32 var8005dd44 = 0x00000000;
u32 var8005dd48 = 0x00000000;
u32 var8005dd4c = 0x00000000;
u32 var8005dd50 = 0x00000000;
s32 g_MainChangeToStageNum = -1;
bool g_MainIsDebugMenuOpen = false;

/*
 * Per-stagenum allocation overrides, from a modconfig's `allocation` key. The
 * string is duplicated once per stagenum and replaced, not leaked, on a
 * re-parse. See the lookup below for how it is consulted.
 */
static char *g_ModStageAllocation[NUM_STAGENUMS];

void stageSetModAllocation(s32 stagenum, const char *string)
{
	char *dup;

	if (stagenum < 0 || stagenum >= NUM_STAGENUMS || !string) {
		return;
	}

	if (g_ModStageAllocation[stagenum] && strcmp(g_ModStageAllocation[stagenum], string) == 0) {
		return;
	}

	dup = strDuplicate(string);

	if (!dup) {
		return;
	}

	free(g_ModStageAllocation[stagenum]);
	g_ModStageAllocation[stagenum] = dup;
}

const char *stageGetModAllocation(s32 stagenum)
{
	if (stagenum < 0 || stagenum >= NUM_STAGENUMS) {
		return NULL;
	}

	return g_ModStageAllocation[stagenum];
}

struct stageallocation g_StageAllocations8Mb[] = {
    {STAGE_CITRAINING, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_DEFECTION, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_INVESTIGATION, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_EXTRACTION, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_CHICAGO, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_G5BUILDING, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_VILLA, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma600"},
    {STAGE_INFILTRATION, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500"},
    {STAGE_RESCUE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500"},
    {STAGE_ESCAPE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma500"},
    {STAGE_AIRBASE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_AIRFORCEONE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_CRASHSITE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_PELAGIC, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_DEEPSEA, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_DEFENSE, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_ATTACKSHIP, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_SKEDARRUINS, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"},
    {STAGE_MP_SKEDAR, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_RAVINE, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_PIPES, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_G5BUILDING, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_SEWERS, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_WAREHOUSE, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_BASE, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_COMPLEX, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_TEMPLE, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_FELICITY, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_AREA52, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_GRID, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_CARPARK, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_RUINS, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_FORTRESS, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_MP_VILLA, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_RUN, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP2, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP6, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP7, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP8, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP14, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP16, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP17, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP18, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP19, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_MP20, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_ASH, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_28, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_MBR, "-ml0 -me0 -mgfx120 -mvtx100 -ma700"},
    {STAGE_TEST_SILO, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_24, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_MAIANSOS, "-ml0 -me0 -mgfx120 -mvtx100 -ma500"},
    {STAGE_RETAKING, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_TEST_DEST, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_2B, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_WAR, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_TEST_UFF, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_TEST_OLD, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_DUEL, "-ml0 -me0 -mgfx120 -mvtx100 -ma700"},
    {STAGE_TEST_LAM, "-ml0 -me0 -mgfx120 -mvtx98 -ma400"},
    {STAGE_TEST_ARCH, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},
    {STAGE_TEST_LEN, "-ml0 -me0 -mgfx120 -mvtx98 -ma300"},
    {STAGE_TITLE, "-ml0 -me0 -mgfx80 -mvtx20 -ma001"},
#ifndef PLATFORM_N64
    // GoldenEye X Mod
    {STAGE_EXTRA1, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Frigate
    {STAGE_EXTRA2, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Complex
    {STAGE_EXTRA3, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Train
    {STAGE_EXTRA4, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Archives
    {STAGE_EXTRA5, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Aztec
    {STAGE_EXTRA6, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Tample
    {STAGE_EXTRA7, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Icicle Pyramid
    {STAGE_EXTRA8, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Caves
    {STAGE_EXTRA9, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"},  // Library
    {STAGE_EXTRA10, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Facility
    {STAGE_EXTRA11, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Bunker
    {STAGE_EXTRA12, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Caverns
    {STAGE_EXTRA13, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Basement
    {STAGE_EXTRA14, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Egyptian
    {STAGE_EXTRA15, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Stack
    {STAGE_EXTRA16, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"}, // Runway
    {STAGE_EXTRA17, "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"}, // Control
    // Kakariko Village Mod
    {STAGE_EXTRA18, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Tawfret Ruins
    {STAGE_EXTRA19, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Targitzan's Temple
    // Goldfinger 64 Mod
    {STAGE_EXTRA20, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Junkyard
    {STAGE_EXTRA21, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Steel Mill
    {STAGE_EXTRA22, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Mall
    {STAGE_EXTRA23, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Tunnels
    // Additional
    {STAGE_EXTRA24, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // Rogue
    {STAGE_EXTRA25, "-ml0 -me0 -mgfx120 -mvtx200 -ma400"}, // Paradox
    {STAGE_EXTRA26, "-ml0 -me0 -mgfx200 -mvtx200 -ma400"}, // War Colors
#endif
    {0, "-ml0 -me0 -mgfx120 -mvtx98 -ma300"},
};

struct stageallocation g_StageAllocations4Mb[] = {
    {STAGE_MP_SKEDAR, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_PIPES, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_AREA52, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_RAVINE, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_G5BUILDING, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_SEWERS, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_WAREHOUSE, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_BASE, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_COMPLEX, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_TEMPLE, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_FELICITY, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_GRID, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_TEST_RUN, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_CARPARK, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_RUINS, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_MP_FORTRESS, "-ml0 -me0 -mgfx96 -mvtx96 -ma130"},
    {STAGE_MP_VILLA, "-ml0 -me0 -mgfx96 -mvtx96 -ma140"},
    {STAGE_TEST_MP2, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP6, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP7, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP8, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP14, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP16, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP17, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP18, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP19, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_MP20, "-ml0 -me0 -mgfx96 -mvtx96 -ma115"},
    {STAGE_TEST_LEN, "-ml0 -me0 -mgfx100 -mvtx96 -ma120"},
    {STAGE_4MBMENU, "-mgfx100 -mvtx50 -ma50"},
    {STAGE_TITLE, "-ml0 -me0 -mgfx80 -mvtx20 -ma001"},
    {0, "-ml0 -me0 -mgfx100 -mvtx96 -ma300"},
};

Gfx var8005dcc8[] = {
    gsSPSegment(0x00, 0x00000000), gsSPDisplayList(&var800613a0),
    gsSPDisplayList(&var80061380), gsDPFullSync(),
    gsSPEndDisplayList(),
};

s32 g_MainIsBooting = 1;

void mainInit(void) {
  s32 x;
  s32 i;
  s32 j;
  u32 addr;

  faultInit();
  dmaInit();
  amgrInit();
  varsInit();
  // stagenum init for mod loading
  for (s32 i = 0; i < ARRAYCOUNT(g_ModStageNums); ++i) {
    // Assume any mod-specific stages use the master mod's ssettings
    g_ModStageNums[i] = 0;
  }
  mempInit();
  memaInit();
  joyInit();
  joyReset();

  var8005d9b0 = rmonIsDisabled();

  g_Is4Mb = (osGetMemSize() <= 0x400000);
  g_VmShowStats = 0;

  // no copyright screen
  viSetMode(VIMODE_HI);
  viConfigureForLegal();
  viBlack(true);
  viUpdateMode();

  filesInit();

  if (var8005d9b0) {
    argSetString("          -ml0 -me0 -mgfx100 -mvtx50 -mt700 -ma400");
  }

  mempSetHeap(g_MempHeap, g_MempHeapSize);

  mempResetPool(MEMPOOL_8);
  mempResetPool(MEMPOOL_PERMANENT);
  crashReset();
  challengesInit();
  utilsInit();
  texInit();
  // capture vanilla surface types
  // so we can restore them later if needed
  extern struct texturesurfaceconfig g_VanillaTextures[NUM_TEXTURES];
  for (s32 i = 0; i < NUM_TEXTURES; i++) {
    g_VanillaTextures[i].surfacetype = g_Textures[i].surfacetype;
    g_VanillaTextures[i].soundsurfacetype = g_Textures[i].soundsurfacetype;
  }
  if (fsGetModDir()) {
    modInit();
    // load all mods, then load mod 0 again
    for (s32 i = 0; i < g_NumModDirs; ++i) {
      g_ModNum = i;
      modConfigLoad(MOD_CONFIG_FNAME);
    }
    g_ModNum = 0;
    modConfigLoad(MOD_CONFIG_FNAME);
  }

  // Build the arena list once every modconfig has been read. With no
  // mod-declared MP stage this leaves g_MpArenas on the vanilla table and
  // g_NumMpArenaGroups at 0, which is the unmodded game's exact state.
  mpArenasRebuild();
  modStageRegReport();
  modStageRegWarnUnlisted();

  langInit();
  lvInit();
  cheatsInit();
  textInit();
  dhudInit();
  playermgrInit();
  frametimeInit();
  profileInit();
  smokesInit();
  mpInit(true);
  pheadInit();
  paksInit();
  pheadInit2();
  animsInit();
  racesInit();
  bodiesInit();
  titleInit();

  modelSetDistanceChecksDisabled(true); // don't use LODs

  g_MainIsBooting = 0;
}

/*
 * The asset command line: the engine as a file tool.
 *
 *   pd --asset ls   <path>              one item per line: canonical path, size
 *   pd --asset test <path>[,<path>...]  exit 0 if every path exists, 1 if not
 *   pd --asset stat <path>[,<path>...]  one line per path: rc, path, owner, id, sub, exists, size
 *   pd --asset get  <path> [--asset-out <file>]   the bytes, to the file or stdout
 *   pd --asset get  <container> --asset-out <dir>  every item under it, one file each
 *   pd --asset link <path>              what the path links to (stage field, head...)
 *   pd --asset find <drive:[/owner/]glob>  names matching, * and ? wildcards, case-blind
 *   pd --asset count <drive:[/owner]>   how many, and for file: how many per leading letter
 *   pd --asset info                     the build, the ROM, the roster, the rom sources
 *   --asset-json                        stat, ls, find and info emit JSON
 *
 * The verbs are `pdt rom`'s (list, files, extract, info, search, count) over
 * every drive instead of one ROM image, so a Python ROM reader is not
 * needed for anything the engine already knows.
 *
 * Runs once every table the drives read is populated and before any stage
 * loads, then exits. Answers come off the live modloader - the roster, the
 * mounted rom sources, the per-mod filetables - so they are the game's
 * answers, not a ROM parser's. Logs go to stderr so stdout is the result.
 * This is what `pdt asset` calls; --asset-probe is the older spelling of
 * `stat` and stays for the scripts that use it.
 */
struct assetCliLsCtx {
  s32 json;
  s32 count;
};

static void assetCliJsonString(const char *s)
{
  putchar('"');
  for (; s && *s; ++s) {
    if (*s == '"' || *s == '\\') {
      putchar('\\');
    }
    putchar(*s);
  }
  putchar('"');
}

static void assetCliStatLine(const char *given, s32 rc, const struct assetref *ref, s32 json)
{
  char path[160] = "";
  u32 size = 0;
  s32 exists = 0;
  void *data = NULL;

  if (rc == ASSET_OK) {
    assetFormat(ref, path, sizeof(path));
    exists = assetExists(ref);
    data = assetLoad(ref, &size);
  }

  if (json) {
    printf("{\"given\":");
    assetCliJsonString(given);
    printf(",\"rc\":%d,\"path\":", rc);
    assetCliJsonString(path[0] ? path : NULL);
    printf(",\"owner\":%d,\"id\":%d,\"sub\":%d,\"via\":%d,\"exists\":%d,\"load\":%s,\"size\":%u}\n",
        ref->owner, ref->id, ref->sub, ref->via, exists, data ? "true" : "false", size);
  } else {
    printf("%s\trc=%d\tpath=%s\towner=%d\tid=%d\tsub=%d\texists=%d\tload=%s\tsize=%u\n",
        given, rc, path[0] ? path : "-", ref->owner, ref->id, ref->sub, exists,
        data ? "yes" : "no", size);
  }
}

static s32 assetCliLsVisit(const struct assetref *ref, const char *name, void *vctx)
{
  struct assetCliLsCtx *ctx = (struct assetCliLsCtx *)vctx;
  char path[160] = "";

  assetFormat(ref, path, sizeof(path));
  ctx->count++;

  if (ctx->json) {
    printf("{\"path\":");
    assetCliJsonString(path);
    printf(",\"name\":");
    assetCliJsonString(name);
    printf(",\"owner\":%d,\"id\":%d,\"sub\":%d}\n", ref->owner, ref->id, ref->sub);
  } else {
    printf("%s\n", path[0] ? path : name);
  }

  return 1;
}

/* walk a comma-separated list; fn returns 0 to make the whole run fail */
static s32 assetCliEach(const char *list, s32 (*fn)(const char *path, void *ctx), void *ctx)
{
  char buf[2048];
  char *cursor = buf;
  s32 ok = 1;

  snprintf(buf, sizeof(buf), "%s", list);

  while (cursor && *cursor) {
    char *comma = strchr(cursor, ',');

    if (comma) {
      *comma = '\0';
    }

    if (!fn(cursor, ctx)) {
      ok = 0;
    }

    cursor = comma ? comma + 1 : NULL;
  }

  return ok;
}

static s32 assetCliStatOne(const char *path, void *ctx)
{
  struct assetref ref;
  s32 rc = assetResolve(path, &ref);

  assetCliStatLine(path, rc, &ref, *(s32 *)ctx);
  return 1;
}

static s32 assetCliTestOne(const char *path, void *ctx)
{
  struct assetref ref;
  s32 ok = assetResolve(path, &ref) == ASSET_OK && assetExists(&ref) == 1;

  (void)ctx;
  printf("%s\t%s\n", ok ? "yes" : "no", path);
  return ok;
}

static s32 assetCliLinkOne(const char *path, void *ctx)
{
  struct assetref ref;
  struct assetref target;
  char out[160] = "";
  s32 rc = assetResolve(path, &ref);

  (void)ctx;

  if (rc == ASSET_OK && assetLink(&ref, &target) == ASSET_OK) {
    assetFormat(&target, out, sizeof(out));
  }

  printf("%s\t%s\n", path, out[0] ? out : "-");
  return out[0] != '\0';
}

/* * and ? only, case-blind; no character classes, no escapes. Enough for
 * "Chead*", "*setup*Z", "bg_mp?_*" and the like, and one function instead
 * of fnmatch on a platform set that includes MSVC. */
static s32 assetCliGlob(const char *pat, const char *s)
{
  while (*pat) {
    if (*pat == '*') {
      while (*pat == '*') {
        pat++;
      }

      if (!*pat) {
        return 1;
      }

      for (; *s; ++s) {
        if (assetCliGlob(pat, s)) {
          return 1;
        }
      }

      return 0;
    }

    if (!*s) {
      return 0;
    }

    if (*pat != '?' && tolower((unsigned char)*pat) != tolower((unsigned char)*s)) {
      return 0;
    }

    pat++;
    s++;
  }

  return *s == '\0';
}

struct assetCliFindCtx {
  const char *glob;
  s32 json;
  s32 count;
};

static s32 assetCliFindVisit(const struct assetref *ref, const char *name, void *vctx)
{
  struct assetCliFindCtx *ctx = (struct assetCliFindCtx *)vctx;
  const char *base = strrchr(name, '/');

  /* match the whole name and, separately, its basename: a vanilla file is
   * "bgdata/bg_mp8.seg" and a modder types "bg_mp8*" */
  if (!assetCliGlob(ctx->glob, name) && !(base && assetCliGlob(ctx->glob, base + 1))) {
    return 1;
  }

  {
    struct assetCliLsCtx ls = { ctx->json, 0 };
    assetCliLsVisit(ref, name, &ls);
  }

  ctx->count++;
  return 1;
}

struct assetCliCountCtx {
  s32 total;
  s32 byLetter[128];
};

static s32 assetCliCountVisit(const struct assetref *ref, const char *name, void *vctx)
{
  struct assetCliCountCtx *ctx = (struct assetCliCountCtx *)vctx;
  const char *base = strrchr(name, '/');
  unsigned char c;

  (void)ref;
  ctx->total++;
  c = (unsigned char)(base ? base[1] : name[0]);

  if (c < 128) {
    ctx->byLetter[c]++;
  }

  return 1;
}

struct assetCliGetAllCtx {
  const char *dir;
  s32 written;
  s32 failed;
};

/* An item name can contain '/', and the part before it can itself be an
 * item: mod_fojo has the head "CheadX" AND its textures "CheadX/0116.bin",
 * which no filesystem can hold as a file and a directory of one name. So
 * the export is flat and the '/' becomes "__": CheadX and CheadX__0116.bin
 * side by side, every head's 0116.bin distinct. The same rule as pdt rom's
 * extract, which is flat too. */
static void assetCliFlatten(const char *name, char *dst, u32 len)
{
  u32 w = 0;

  for (const char *r = name; *r && w + 3 < len; ++r) {
    if (*r == '/') {
      dst[w++] = '_';
      dst[w++] = '_';
    } else {
      dst[w++] = *r;
    }
  }

  dst[w] = '\0';
}

static s32 assetCliGetAllVisit(const struct assetref *ref, const char *name, void *vctx)
{
  struct assetCliGetAllCtx *ctx = (struct assetCliGetAllCtx *)vctx;
  char out[FS_MAXPATH];
  u32 size = 0;
  void *data = assetLoad(ref, &size);
  FILE *f;

  if (!data) {
    /* a stage row, a mod entry, a texture with nothing behind it: not
     * bytes, not a failure */
    return 1;
  }

  {
    char flat[FS_MAXPATH];

    assetCliFlatten(name, flat, sizeof(flat));
    snprintf(out, sizeof(out), "%s/%s", ctx->dir, flat);
  }

  f = fopen(out, "wb");

  if (!f || fwrite(data, 1, size, f) != size) {
    fprintf(stderr, "--asset get: could not write %s\n", out);
    ctx->failed++;
  } else {
    ctx->written++;
  }

  if (f) {
    fclose(f);
  }

  return 1;
}

static s32 assetCliInfoCount(const char *root)
{
  struct assetCliCountCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  return assetEnumerate(root, assetCliCountVisit, &ctx) >= 0 ? ctx.total : 0;
}

static void assetCliInfo(s32 json)
{
  static const char *const roots[] = { "file:/vanilla", "file:", "tex:", "stage:", "head:", "body:", "hand:", "seg:", "rom:", "mod:" };
  s32 counts[10];
  const char *version =
#ifdef VERSION_HASH
    VERSION_BRANCH " " VERSION_HASH " (" VERSION_TARGET ")";
#else
    "unknown";
#endif

  for (u32 i = 0; i < 10; ++i) {
    counts[i] = assetCliInfoCount(roots[i]);
  }

  if (json) {
    printf("{\"version\":");
    assetCliJsonString(version);
    printf(",\"basedir\":");
    assetCliJsonString(fsGetBaseDir());
    printf(",\"rom\":{\"present\":%s,\"size\":%u},\"mods\":[", g_RomFile ? "true" : "false", g_RomFileSize);

    for (u32 i = 0; i < g_NumModDirs; ++i) {
      printf("%s", i ? "," : "");
      assetCliJsonString(modDirs[i]);
    }

    printf("],\"romsources\":[");

    for (s32 i = 0; i < romsourceCount(); ++i) {
      const char *id = NULL;
      const char *file = NULL;
      u8 mounted = 0;

      romsourceInfo(i, &id, &file, &mounted);
      printf("%s{\"id\":", i ? "," : "");
      assetCliJsonString(id);
      printf(",\"file\":");
      assetCliJsonString(file);
      printf(",\"mounted\":%s}", mounted ? "true" : "false");
    }

    printf("],\"counts\":{");

    for (u32 i = 0; i < 10; ++i) {
      printf("%s", i ? "," : "");
      assetCliJsonString(roots[i]);
      printf(":%d", counts[i]);
    }

    printf("}}\n");
    return;
  }

  printf("version\t%s\n", version);
  printf("basedir\t%s\n", fsGetBaseDir());
  printf("rom\t%s\t%u bytes\n", g_RomFile ? "loaded" : "none", g_RomFileSize);

  for (u32 i = 0; i < g_NumModDirs; ++i) {
    printf("mod\t%u\t%s\n", i, modDirs[i]);
  }

  for (s32 i = 0; i < romsourceCount(); ++i) {
    const char *id = NULL;
    const char *file = NULL;
    u8 mounted = 0;

    romsourceInfo(i, &id, &file, &mounted);
    printf("romsource\t%s\t%s\t%s\n", id, mounted ? "mounted" : "missing", file);
  }

  for (u32 i = 0; i < 10; ++i) {
    printf("count\t%s\t%d\n", roots[i], counts[i]);
  }
}

static void assetCliFromArgs(void)
{
  const char *verb = sysArgGetString("--asset");
  const char *probe = sysArgGetString("--asset-probe");
  const char *out = sysArgGetString("--asset-out");
  const s32 json = sysArgCheck("--asset-json");
  const char *arg;
  s32 status = 0;

  if (probe && probe[0]) {
    /* the older spelling: stat, text, and the name it had */
    s32 j = json;
    assetCliEach(probe, assetCliStatOne, &j);
    fflush(stdout);
    exit(0);
  }

  if (!verb || !verb[0]) {
    return;
  }

  arg = sysArgGetString2("--asset");

  if (!strcmp(verb, "info")) {
    assetCliInfo(json);
    fflush(stdout);
    exit(0);
  }

  if (!arg) {
    fprintf(stderr, "--asset %s: a path is required\n", verb);
    exit(2);
  }

  if (!strcmp(verb, "ls")) {
    struct assetCliLsCtx ctx = { json, 0 };
    s32 n = assetEnumerate(arg, assetCliLsVisit, &ctx);

    if (n < 0) {
      fprintf(stderr, "--asset ls %s: rc=%d\n", arg, n);
      status = 1;
    }
  } else if (!strcmp(verb, "stat")) {
    s32 j = json;
    assetCliEach(arg, assetCliStatOne, &j);
  } else if (!strcmp(verb, "test")) {
    status = assetCliEach(arg, assetCliTestOne, NULL) ? 0 : 1;
  } else if (!strcmp(verb, "link")) {
    status = assetCliEach(arg, assetCliLinkOne, NULL) ? 0 : 1;
  } else if (!strcmp(verb, "find")) {
    /* drive:[/owner/]glob - the glob is the last segment unless the path
     * is a bare drive, and the container it is searched under is the rest */
    char root[256];
    const char *slash = strrchr(arg, '/');
    struct assetCliFindCtx ctx = { NULL, json, 0 };
    s32 n;

    if (slash && slash[1]) {
      u32 len = (u32)(slash - arg);

      if (len >= sizeof(root)) {
        fprintf(stderr, "--asset find: path too long\n");
        exit(2);
      }

      memcpy(root, arg, len);
      root[len] = '\0';
      ctx.glob = slash + 1;

      /* "file:/Chead*" - the segment before the glob was the drive, not an
       * owner; assetEnumerate wants "file:" or "file:/" for the root and
       * would otherwise read "Chead*" as an owner it cannot find */
    } else {
      snprintf(root, sizeof(root), "%s", arg);
      ctx.glob = "*";
    }

    n = assetEnumerate(root, assetCliFindVisit, &ctx);

    if (n < 0) {
      /* the segment before the glob may have been an owner the enumerate
       * could not split; try the whole thing as a root with no glob */
      fprintf(stderr, "--asset find %s: rc=%d under %s\n", arg, n, root);
      status = 1;
    } else if (!ctx.count) {
      status = 1;
    }
  } else if (!strcmp(verb, "count")) {
    struct assetCliCountCtx ctx;
    s32 n;

    memset(&ctx, 0, sizeof(ctx));
    n = assetEnumerate(arg, assetCliCountVisit, &ctx);

    if (n < 0) {
      fprintf(stderr, "--asset count %s: rc=%d\n", arg, n);
      status = 1;
    } else if (json) {
      s32 first = 1;

      printf("{\"path\":");
      assetCliJsonString(arg);
      printf(",\"total\":%d,\"byLetter\":{", ctx.total);

      for (s32 c = 0; c < 128; ++c) {
        if (ctx.byLetter[c]) {
          printf("%s\"%c\":%d", first ? "" : ",", (char)c, ctx.byLetter[c]);
          first = 0;
        }
      }

      printf("}}\n");
    } else {
      printf("total\t%d\n", ctx.total);

      for (s32 c = 0; c < 128; ++c) {
        if (ctx.byLetter[c]) {
          printf("%c\t%d\n", (char)c, ctx.byLetter[c]);
        }
      }
    }
  } else if (!strcmp(verb, "get") && out && out[0] && strcmp(out, "-") && assetEnumerate(arg, assetCliCountVisit, &(struct assetCliCountCtx){0}) > 0) {
    /* a container: every item under it, one file each, into the dir */
    struct assetCliGetAllCtx ctx = { out, 0, 0 };

    /* mkdir's EEXIST is fine; a dir that cannot be made shows up as write
     * failures below, one per file */
    fsCreateDir(out);
    assetEnumerate(arg, assetCliGetAllVisit, &ctx);
    fprintf(stderr, "%s: %d file(s) -> %s%s\n", arg, ctx.written, out,
        ctx.failed ? " (with failures)" : "");
    status = ctx.failed ? 1 : 0;
  } else if (!strcmp(verb, "get")) {
    struct assetref ref;
    u32 size = 0;
    void *data = NULL;
    s32 rc = assetResolve(arg, &ref);

    if (rc == ASSET_OK) {
      data = assetLoad(&ref, &size);
    }

    if (!data) {
      fprintf(stderr, "--asset get %s: rc=%d, nothing to load\n", arg, rc);
      status = 1;
    } else if (out && out[0] && strcmp(out, "-")) {
      FILE *f = fopen(out, "wb");

      if (!f || fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "--asset get %s: could not write %s\n", arg, out);
        status = 1;
      } else {
        fprintf(stderr, "%s: %u bytes -> %s\n", arg, size, out);
      }

      if (f) {
        fclose(f);
      }
    } else {
      fwrite(data, 1, size, stdout);
    }
  } else {
    fprintf(stderr, "--asset %s: unknown verb (ls, stat, test, link, get, find, count, info)\n", verb);
    status = 2;
  }

  fflush(stdout);
  exit(status);
}

void mainProc(void) {
  mainInit();
	modScanAllMods();
  modCacheAllConfigs();
  modStageDumpOwnership("scan");
  for (s32 i = 0; i < g_NumModDirs; i++) {
    sysLogPrintf(LOG_NOTE, "mainProc: initial modSwitch for mod %d", i);
    modSwitch(i, -1);
  }
  sysLogPrintf(LOG_NOTE, "mainProc: caching all mod configs");
  sysLogPrintf(LOG_NOTE, "mainProc: initial modSwitch for mod 0");
  modSwitch(0, -1);
  assetCliFromArgs();
  mpsetupProbeFromArgs();
  rdpInit();
  sndInit();

  while (true) {
    mainLoop();
  }
}

/**
 * It's suspected that this function would have allowed developers to override
 * the value of variables while the game is running in order to view their
 * effects immediately rather than having to recompile the game each time.
 *
 * The developers would have used rmon to create a table of name/value pairs,
 * then this function would have looked up the given variable name in the table
 * and written the new value to the variable's address.
 */
void mainOverrideVariable(char *name, void *value) {
  // empty
}

/**
 * This function enters an infinite loop which iterates once per stage load.
 * Within this loop is an inner loop which runs very frequently and decides
 * whether to run mainTick on each iteration.
 *
 * NTSC beta checks two shorts at an offset 64MB into the development board
 * and refuses to continue if they are not any of the allowed values.
 * Decomp patches these reads in its build system so it can be played
 * without the development board.
 */
void mainLoop(void) {
  s32 ending = false;
  s32 index;
  s32 numplayers;
  u32 stack;

  func0f175f98();

  var8005d9c4 = 0;
  argGetLevel(&g_StageNum);

  if (g_DoBootPakMenu) {
    g_Vars.pakstocheck = 0xfd;
    g_StageNum = STAGE_BOOTPAKMENU;
  }

  if (g_StageNum != STAGE_TITLE) {
    titleSetNextStage(g_StageNum);

    if (g_StageNum < STAGE_TITLE) {
      func0f01b148(0);

      if (argFindByPrefix(1, "-hard")) {
        lvSetDifficulty(argFindByPrefix(1, "-hard")[0] - '0');
      }
    }
  }

  if (g_StageNum == STAGE_CITRAINING && IS4MB()) {
    g_StageNum = STAGE_4MBMENU;
  }

  rngSetSeed(osGetCount());

  // Outer loop - this is infinite because ending is never changed
  while (!ending) {
    g_MainNumGfxTasks = 0;
    g_MainGameLogicEnabled = true;
    g_MainIsEndscreen = false;

    if (var8005d9b0 && var8005d9c4 == 0) {
      index = -1;

      if (IS4MB()) {
        if (g_StageNum < STAGE_TITLE && getNumPlayers() >= 2) {
          index = 0;
          while (g_StageAllocations4Mb[index].stagenum) {
            if (g_StageAllocations4Mb[index].stagenum == g_StageNum + 400) {
              break;
            }
            index++;
          }

          if (g_StageAllocations4Mb[index].stagenum == 0) {
            index = -1;
          }
        }

        if (index)
          ;

        if (index < 0) {
          index = 0;
          while (g_StageAllocations4Mb[index].stagenum) {
            if (g_StageNum == g_StageAllocations4Mb[index].stagenum) {
              break;
            }

            index++;
          }
        }

        argSetString(g_StageAllocations4Mb[index].string);
      } else {
        // 8MB
        if (g_StageNum < STAGE_TITLE && getNumPlayers() >= 2) {
          index = 0;
          while (g_StageAllocations8Mb[index].stagenum) {
            if (g_StageNum + 400 == g_StageAllocations8Mb[index].stagenum) {
              break;
            }
            index++;
          }

          if (g_StageAllocations8Mb[index].stagenum == 0) {
            index = -1;
          }
        }

        if (index < 0) {
          index = 0;

          while (g_StageAllocations8Mb[index].stagenum) {
            if (g_StageNum == g_StageAllocations8Mb[index].stagenum) {
              break;
            }

            index++;
          }
        }

        // A modconfig's `allocation` for this stage wins over the table. The
        // table has rows for STAGE_EXTRA1..26 only; a level row past that used
        // to run on the terminator's string (-ma300), and the key was refused
        // for it. Now the key stores here, and a level row with no entry and
        // no key gets the MP string every authored MP row carries.
        if (stageGetModAllocation(g_StageNum)) {
          sysLogPrintf(LOG_NOTE, "stage 0x%02x allocation from its modconfig: %s", g_StageNum, stageGetModAllocation(g_StageNum));
          argSetString((char *)stageGetModAllocation(g_StageNum));
        } else if (g_StageAllocations8Mb[index].stagenum == 0 && g_StageNum < STAGE_TITLE) {
          argSetString(STAGE_ALLOCATION_MP_DEFAULT);
        } else {
          argSetString(g_StageAllocations8Mb[index].string);
        }
      }
    }

    var8005d9c4 = 0;

    mempResetPool(MEMPOOL_7);
    mempResetPool(MEMPOOL_STAGE);
    filesStop(4);

    // g_Rooms lived in the pool that just went, and the stage about to load
    // may build no rooms at all - a menu stage builds none. The pointer is
    // left where it is, because the game reads it in plenty of places that
    // never ran outside a level, but the length is now zero and says so to
    // anything that asks before bgBuildTables() runs.
    //
    // Dab's fix puts this in src/lib/main.c's mainLoop; that file is not in
    // this build's SRC_LIB and is never compiled, so the port's own copy of
    // the stage-change path is where it goes.
    g_NumRoomsAllocated = 0;

    if (argFindByPrefix(1, "-ma")) {
      g_MainMemaHeapSize = strtol(argFindByPrefix(1, "-ma"), NULL, 0) * 1024;
    }

    memaReset(mempAlloc(g_MainMemaHeapSize, MEMPOOL_STAGE), g_MainMemaHeapSize);
    langReset(g_StageNum);
    playermgrReset();

    if (g_StageNum >= STAGE_TITLE) {
      numplayers = 0;
    } else {
      numplayers = getNumPlayers();
    }

    if (numplayers < 2) {
      playermgrDisableTeamPlayers(false);
    }

    if (g_MissionConfig.isteam) {
      numplayers = playermgrAllocatePlayers(-1);
    } else {
      playermgrAllocatePlayers(numplayers);
    }

    if (g_MissionConfig.isteam || g_Vars.perfectbuddynum) {
      mpReset();
    } else if (g_Vars.mplayerisrunning == false &&
               (numplayers >= 2 || g_Vars.lvmpbotlevel)) {
      g_MpSetup.chrslots = 1;

      if (numplayers >= 2) {
        g_MpSetup.chrslots |= 1 << 1;
      }

      if (numplayers >= 3) {
        g_MpSetup.chrslots |= 1 << 2;
      }

      if (numplayers >= 4) {
        g_MpSetup.chrslots |= 1 << 3;
      }

      g_MpSetup.stagenum = g_StageNum;
      mpReset();
    }

    gfxReset();
    joyReset();
    dhudReset();
    zbufReset(g_StageNum);
    langReset(g_StageNum);
    lvReset(g_StageNum);
    viReset(g_StageNum);
    frametimeCalculate();
    profileReset();

    while (g_MainChangeToStageNum < 0) {
      const s32 cycles = osGetCount() - g_Vars.thisframestartt;
      if (!g_Vars.mininc60 || (cycles >= g_Vars.mininc60 * CYCLES_PER_FRAME -
                                             CYCLES_PER_FRAME / 2)) {
        schedStartFrame(&g_Sched);
        mainTick();
        schedEndFrame(&g_Sched);
      }

      // The frame is presented and audio is queued; the loop is about to idle.
      // If anything is waiting to be written, this is where it goes.
      saveQueueTick();

      if (g_TickExtraSleep) {
        sysSleep(EXTRA_SLEEP_TIME);
      }
    }

    // saveQueueTick stops running for the duration of the teardown and load,
    // which is far longer than the deadline, so commit before leaving.
    saveQueueFlush();

    lvStop();
    mempDisablePool(MEMPOOL_STAGE);
    mempDisablePool(MEMPOOL_7);
    filesStop(4);
    viBlack(true);
    pak0f116994();

    if (g_MainChangeToStageNum == STAGE_TITLE) {
      // Switch to boot mod (mod_fojo)
      s32 bootMod = 0;
      for (s32 i = 0; i < g_NumModDirs; ++i) {
        if (strstr(modDirs[i], "mod_fojo")) {
          bootMod = i;
          break;
        }
      }
      modSwitch(bootMod, -1);
    } else {
      sysLogPrintf(LOG_NOTE, "mainLoop: switching to stage 0x%02x",
                   g_MainChangeToStageNum);
      // Switch to the mod that owns this stage
      modSwitch(-1, g_MainChangeToStageNum);
    }

    g_StageNum = g_MainChangeToStageNum;
    sysLogPrintf(LOG_NOTE,
                 "mainLoop: clearing g_MainChangeToStageNum (was 0x%02x)",
                 g_MainChangeToStageNum);
    g_MainChangeToStageNum = -1;
  }
}

void mainTick(void) {
  Gfx *gdl = NULL;
  Gfx *gdlstart = NULL;
  OSScMsg msg = {OS_SC_DONE_MSG};
  s32 i;

  if (g_MainChangeToStageNum < 0) {
    if (inputKeyJustPressed(VK_F2) && inputGetKeyModState() & KM_SHIFT) {
      bool enabled = videoGetExternalTextures();
      videoSetExternalTextures(!enabled);
    }

    frametimeCalculate();
	profile00009a98();
    profileReset();
    profileSetMarker(PROFILE_MAINTICK_START);
    joyDebugJoy();
    schedSetCrashEnable2(false);

    if (g_MainGameLogicEnabled) {
      gdl = gdlstart = gfxGetMasterDisplayList();

      gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE,
                 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                 G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
      gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, 6, 0,
                 G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                 G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);

      lvTick();
      playermgrShuffle();

      if (g_StageNum < STAGE_TITLE) {
        // Lua possession (controllable cube, pd.possess_spawn) freecam input —
        // once per frame; no-op unless possession is active. (Kai be46717.)
        luaPossessReadInput();

        for (i = 0; i < PLAYERCOUNT(); i++) {
          setCurrentPlayerNum(playermgrGetPlayerAtOrder(i));

          if (g_StageNum != STAGE_TEST_OLD || !titleIsKeepingMode()) {
            viSetViewPosition(g_Vars.currentplayer->viewleft,
                              g_Vars.currentplayer->viewtop);
            viSetFovAspectAndSize(g_Vars.currentplayer->fovy,
                                  g_Vars.currentplayer->aspect,
                                  g_Vars.currentplayer->viewwidth,
                                  g_Vars.currentplayer->viewheight);
          }

          lvTickPlayer();

          // Possession: after the body ticks, override this player's camera to
          // follow the controllable cube's fly pose. No-op unless possession is
          // active.
          luaPossessApplyCamera();
        }
      }

      gdl = lvRender(gdl);

      if (debugGetProfileMode() >= 2) {
        gdl = profileRender(gdl);
      }

      gdl = luaHudRender(gdl); // Lua overlays; returns at once when Lua is off

      gDPFullSync(gdl++);
      gSPEndDisplayList(gdl++);
    }

    if (g_MainGameLogicEnabled) {
      gfxSwapBuffers();
      viUpdateMode();
    }

    rdpCreateTask(gdlstart, gdl, 0, (uintptr_t)&msg);
    memaPrint();
    profileSetMarker(PROFILE_MAINTICK_END);
  }
}

void mainEndStage(void) {
  sndStopNosedive();

  if (!g_MainIsEndscreen) {
    pak0f11c6d0();
    joyDisableTemporarily();

    if (g_MissionConfig.isteam) {
      s32 prevplayernum = g_Vars.currentplayernum;
      s32 i;

      for (i = 0; i < PLAYERCOUNT(); i++) {
        setCurrentPlayerNum(i);

        teamCalculateAwards();
        printf("mainEndStage: before endscreenPushTeam (player %d)\n", i);
        endscreenPushTeam();
      }

      setCurrentPlayerNum(prevplayernum);
      musicStartMenu();
    } else if (g_Vars.normmplayerisrunning) {
      mpEndMatch();
    } else {
      endscreenPrepare();
      musicStartMenu();
    }
  }

  g_MainIsEndscreen = true;
}

/**
 * Change to the given stage at the end of the current frame.
 */
void mainChangeToStage(s32 stagenum) {
  pak0f11c6d0();

  // If returning to title screen, ensure we reload the boot mod (mod_fojo)
  if (stagenum == STAGE_TITLE) {
    // Find mod_fojo index
    s32 fojoIndex = -1;
    for (s32 i = 0; i < g_NumModDirs; ++i) {
      if (strstr(modDirs[i], "mod_fojo")) {
        fojoIndex = i;
        break;
      }
    }

    if (fojoIndex >= 0) {
      // Switch to mod_fojo, no specific level (will go to title)
      modSwitch(fojoIndex, -1);
    }
  }

  g_MainChangeToStageNum = stagenum;
}

s32 mainGetStageNum(void) { return g_StageNum; }

void func0000e990(void) {
  objectivesCheckAll();
  objectivesDisableChecking();
  mainEndStage();
}

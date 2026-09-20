/**
 * mkfiletable — build a mod's filetable.dat from its JSON manifest.
 *
 * Replaces `pdt build-mod-filetable`. Humans write and maintain the JSON; this
 * writes the binary, and the .dat is always generated, never edited.
 *
 *   mkfiletable <mod-name> [--workspace <dir>] [--output <dir>]
 *
 * Reads <workspace>/<mod-name>_filetable.json and, if present,
 * <workspace>/<mod-name>_texmap.json; writes <output>/filetable.dat and
 * rewrites the texmap.
 *
 * The encoding itself is pdft_write.c. This file owns everything that needs a
 * filesystem or a decision: reading the manifest, allocating ids, and keeping
 * texture port slots stable across rebuilds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <zlib.h>
#include <sys/stat.h>
#include "pdft_write.h"
#include "vendor/parson/parson.h"

#define VANILLA_MAX_ID   2017  /* the first mod-local id is the one after this */
#define FIRST_LOCAL_ID   2018

/* Composed paths are checked, not assumed: a truncated path would write the
 * filetable somewhere other than where it was asked to. */
#define PATHMAX 4096
#define JOIN(dst, ...) do { \
		if (snprintf((dst), sizeof(dst), __VA_ARGS__) >= (int)sizeof(dst)) { \
			die("path is longer than %d bytes", (int)sizeof(dst)); \
		} \
	} while (0)

struct texEntry {
	uint32_t localTexId;
	uint32_t slotIdx;
};

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "mkfiletable: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

/**
 * A manifest number may be a JSON number or a string like "0x0db6", because
 * the ids people write by hand are hex. Mirrors int(x, 0).
 */
static bool numberOf(const JSON_Value *v, long *out)
{
	if (!v) {
		return false;
	}

	if (json_value_get_type(v) == JSONNumber) {
		*out = (long)json_value_get_number(v);
		return true;
	}

	if (json_value_get_type(v) == JSONString) {
		const char *s = json_value_get_string(v);
		char *end = NULL;
		long n;

		if (!s || !s[0]) {
			return false;
		}

		n = strtol(s, &end, 0);

		if (end && *end) {
			return false;
		}

		*out = n;
		return true;
	}

	return false;
}


/* -- texmap persistence ---------------------------------------------------
 *
 * The texmap is what makes a texture's port stable across rebuilds: a local
 * texture id keeps whatever slot it was first given. Slots must stay dense —
 * the engine advances its global port base by the ENTRY COUNT while mapping
 * each entry by its slot index, so a hole makes the next mod's ports overlap
 * this one's. The Python never pruned, which is how two shipped mods ended up
 * with holes and duplicates; here, retired ids are dropped and the survivors
 * are compacted, in first-assigned order.
 */
static int cmpTexLocal(const void *a, const void *b)
{
	const struct texEntry *x = a, *y = b;
	return x->localTexId < y->localTexId ? -1 : x->localTexId > y->localTexId ? 1 : 0;
}

static int cmpTexSlot(const void *a, const void *b)
{
	const struct texEntry *x = a, *y = b;
	return x->slotIdx < y->slotIdx ? -1 : x->slotIdx > y->slotIdx ? 1 : 0;
}


/* -- the vanilla name -> id map -------------------------------------------
 *
 * What `replaces:` resolves against: a snapshot of one ROM's own file names,
 * taken by `pdt snapshot-vanilla-names` and checked in under
 * docker-caroll/share/pd-<romid>/files.json. It is a plain {name: id} object.
 */
struct vanillaMap {
	JSON_Value *val;
	JSON_Object *files;
};

static bool vanillaLoad(struct vanillaMap *vm, const char *path)
{
	JSON_Object *root;

	vm->val = json_parse_file(path);

	if (!vm->val) {
		return false;
	}

	root = json_value_get_object(vm->val);
	vm->files = root ? json_object_get_object(root, "files") : NULL;

	return vm->files != NULL;
}

static long vanillaLookup(const struct vanillaMap *vm, const char *name)
{
	JSON_Value *v;

	if (!vm->files) {
		return -1;
	}

	v = json_object_get_value(vm->files, name);

	if (!v || json_value_get_type(v) != JSONNumber) {
		return -1;
	}

	return (long)json_value_get_number(v);
}

/* -- alt ROMs -------------------------------------------------------------
 *
 * A romSource is another ROM this mod pulls bytes out of. The manifest says
 * which file and, for textures, where that ROM's texture list lives; the
 * entries say what to take. `byOffset` says it outright. `byId` names a
 * texture and we work the offset out from the ROM's own tlist. `byName` names
 * a file, and we read that ROM's own file table to find it.
 */
struct altFile;

struct altRom {
	char id[PDFT_ROMSOURCE_ID];
	char filename[PDFT_ROMSOURCE_FILE];
	uint32_t expectedSize;
	bool required;
	bool strict;
	uint8_t fallback;
	uint32_t tlistOffset;
	uint32_t tlistCount;
	uint32_t texdataOffset;
	FILE *fp;          /* opened lazily, only when something needs bytes */
	bool tried;
	char path[PATHMAX];

	/* byName only: the whole image, its inflated data segment and the file
	 * index read out of it. Built once, on the first name that needs it. */
	uint32_t dataOffset;   /* declared in the manifest, 0 = work it out */
	uint32_t tableOffset;  /* declared in the manifest, 0 = work it out */
	uint8_t *image;
	size_t imageLen;
	uint8_t *dataSeg;
	uint32_t dataSegLen;
	struct altFile *files;
	uint32_t numFiles;
	bool filesTried;
	const char *variant;
};

static bool altOpen(struct altRom *rom, const char *const *dirs, int numDirs)
{
	int i;

	if (rom->tried) {
		return rom->fp != NULL;
	}

	rom->tried = true;

	for (i = 0; i < numDirs; ++i) {
		char candidate[PATHMAX];

		if (!dirs[i]) {
			continue;
		}

		if (snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], rom->filename) >= (int)sizeof(candidate)) {
			continue;
		}

		rom->fp = fopen(candidate, "rb");

		if (rom->fp) {
			snprintf(rom->path, sizeof(rom->path), "%s", candidate);
			return true;
		}
	}

	return false;
}

/**
 * One record of a ROM's texture list is eight bytes, of which bytes 1..3 are
 * the big endian offset of that texture's data from texdataOffset. A texture
 * runs to wherever the next one starts.
 */
static bool altTextureExtent(struct altRom *rom, uint32_t texId, uint32_t *outOfs, uint32_t *outSize)
{
	uint8_t rec[16];
	uint32_t cur, next;

	/* A texture runs to wherever the next record starts, and the LAST texture
	 * is no exception: the list carries one more record than it has textures,
	 * whose offset is the end of the texture data. MEASURED in pd.jpn-final:
	 * tlistCount is 3511, record 3511 reads 0x29495e, and the 1397 bytes that
	 * gives for texture 0x0db6 are exactly what the Python writer emitted.
	 * Record 3512 is already the next segment's 1173 header, so the terminator
	 * is the real end of the list and not slack. Bounding on texId + 1 refused
	 * the last texture of every source ROM, which is four of mod_fojo's. */
	if (!rom->fp || texId >= rom->tlistCount) {
		return false;
	}

	if (fseek(rom->fp, (long)(rom->tlistOffset + texId * 8), SEEK_SET) != 0) {
		return false;
	}

	if (fread(rec, 1, sizeof(rec), rom->fp) != sizeof(rec)) {
		return false;
	}

	cur = ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | rec[3];
	next = ((uint32_t)rec[9] << 16) | ((uint32_t)rec[10] << 8) | rec[11];

	if (next <= cur || next - cur > 0x10000) {
		return false;
	}

	*outOfs = rom->texdataOffset + cur;
	*outSize = next - cur;
	return true;
}

/* -- a ROM's own file table ------------------------------------------------
 *
 * `byName` needs to turn a name into an offset and a size in another ROM,
 * which means reading that ROM's file table. The parse below is the engine's
 * own, not a second one invented here: `romdataInitFiles()`
 * (port/src/romdata.c) inflates the ROM's compressed data segment, reads a big
 * endian u32 offset table at the segment's file-table offset, and treats the
 * last non-zero entry as the ROM address of the NAME table, whose entries are
 * offsets relative to its own start. Index 0 is a file in neither table, so
 * both walks begin at 1 exactly as the engine's do.
 *
 * The per-variant segment offsets are Ryan Dwyer's, from the pd-extract
 * scripts in the decomp tooling, by way of leylinelib's pd_rom_driver.py.
 */
struct altFile {
	const char *name;
	uint32_t offset;
	uint32_t size;
	uint32_t id;   /* its slot in the ROM's file table, which IS its file id */
};

struct romVariant {
	const char *name;
	const char *cartId;   /* bytes 0x3b..0x3e of the header */
	uint32_t dataOffset;
	uint32_t tableOffset;
};

/* ntsc-1.0 and ntsc-final share both offsets, so one row covers them. */
static const struct romVariant g_RomVariants[] = {
	{ "ntsc-final", "NPDE", 0x39850, 0x28080 },
	{ "ntsc-beta",  "NPDE", 0x30850, 0x29160 },
	{ "pal-final",  "NPDP", 0x39850, 0x28910 },
	{ "pal-beta",   "NPDP", 0x39850, 0x29b90 },
	{ "jpn-final",  "NPDJ", 0x39850, 0x28800 },
};

#define NUM_ROM_VARIANTS ((int)(sizeof(g_RomVariants) / sizeof(g_RomVariants[0])))

/* A file table with fewer entries than this is not a file table; it is a wrong
 * offset that happened to decode. The three retail ROMs carry about 1800. */
#define ALT_MIN_FILES 256

static uint32_t be32at(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * Read the whole ROM once. A name lookup walks two tables in it and the name
 * strings live in a third place, so seeking around a FILE * for each would be
 * both slower and harder to bounds check than 32 MB of malloc.
 */
static bool altImage(struct altRom *rom)
{
	long len;

	if (rom->image) {
		return true;
	}

	if (!rom->fp || fseek(rom->fp, 0, SEEK_END) != 0) {
		return false;
	}

	len = ftell(rom->fp);

	if (len <= 0x1000 || fseek(rom->fp, 0, SEEK_SET) != 0) {
		return false;
	}

	rom->image = malloc((size_t)len);

	if (!rom->image) {
		die("could not allocate %ld bytes for %s", len, rom->path);
	}

	if (fread(rom->image, 1, (size_t)len, rom->fp) != (size_t)len) {
		free(rom->image);
		rom->image = NULL;
		return false;
	}

	rom->imageLen = (size_t)len;
	return true;
}

/**
 * Inflate a 1173 segment. The length is declared in the three bytes after the
 * magic, the same way the engine reads it, so a wrong offset usually fails on
 * the magic and an implausible length fails here rather than in zlib.
 */
static bool altInflate(struct altRom *rom, uint32_t ofs, uint8_t **outSeg, uint32_t *outLen)
{
	z_stream zs;
	uint8_t *seg;
	uint32_t len;
	int rc;

	if ((size_t)ofs + 5 > rom->imageLen) {
		return false;
	}

	if (rom->image[ofs] != 0x11 || rom->image[ofs + 1] != 0x73) {
		return false;
	}

	len = ((uint32_t)rom->image[ofs + 2] << 16) | ((uint32_t)rom->image[ofs + 3] << 8)
			| (uint32_t)rom->image[ofs + 4];

	if (len < 0x1000 || len > 0x800000) {
		return false;
	}

	seg = malloc(len);

	if (!seg) {
		die("could not allocate %u bytes for the data segment of %s", len, rom->path);
	}

	memset(&zs, 0, sizeof(zs));
	zs.next_in = rom->image + ofs + 5;
	zs.avail_in = (unsigned)(rom->imageLen - ofs - 5);
	zs.next_out = seg;
	zs.avail_out = len;

	/* -15: raw deflate, no zlib or gzip wrapper, which is what 1173 holds. */
	if (inflateInit2(&zs, -15) != Z_OK) {
		free(seg);
		return false;
	}

	rc = inflate(&zs, Z_FINISH);
	inflateEnd(&zs);

	if ((rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) || zs.total_out < len) {
		free(seg);
		return false;
	}

	*outSeg = seg;
	*outLen = len;
	return true;
}

static bool plainName(const char *s, size_t max)
{
	size_t i;

	for (i = 0; i < max; ++i) {
		if (!s[i]) {
			return i > 0;
		}

		if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7e) {
			return false;
		}
	}

	return false;
}

/**
 * Decode the offset and name tables at a candidate table offset. Returns false
 * rather than dying, because this doubles as the test that a candidate offset
 * is the right one: a wrong offset decodes to offsets outside the ROM or to
 * names that are not text, and it fails here.
 */
static bool altDecodeTable(struct altRom *rom, const uint8_t *seg, uint32_t segLen,
		uint32_t tableOffset, struct altFile **outFiles, uint32_t *outNum)
{
	struct altFile *files;
	const uint8_t *nameTable;
	uint32_t nameTableOfs = 0;
	uint32_t count = 0, named = 0, i;

	if (tableOffset + 8 > segLen) {
		return false;
	}

	/* How many offsets there are, and that they are ordered and inside the ROM. */
	for (i = 1; (tableOffset + (i + 1) * 4) <= segLen; ++i) {
		uint32_t ofs = be32at(seg + tableOffset + i * 4);
		uint32_t prev = be32at(seg + tableOffset + (i - 1) * 4);

		if (!ofs) {
			break;
		}

		if (ofs >= rom->imageLen || (i > 1 && ofs < prev)) {
			return false;
		}

		nameTableOfs = ofs;
		++count;
	}

	/* The last entry is the name table, so it is not a file. */
	if (count < ALT_MIN_FILES || !nameTableOfs) {
		return false;
	}

	nameTable = rom->image + nameTableOfs;
	files = calloc(count, sizeof(*files));

	if (!files) {
		die("could not allocate the file index for %s", rom->path);
	}

	for (i = 1; i < count; ++i) {
		uint32_t ofs = be32at(seg + tableOffset + i * 4);
		uint32_t next = be32at(seg + tableOffset + (i + 1) * 4);
		size_t nameSlot = nameTableOfs + (size_t)i * 4;
		uint32_t nameOfs;

		if (nameSlot + 4 > rom->imageLen) {
			break;
		}

		nameOfs = be32at(rom->image + nameSlot);

		if (!nameOfs) {
			break;
		}

		if (nameTableOfs + (size_t)nameOfs >= rom->imageLen) {
			free(files);
			return false;
		}

		if (!plainName((const char *)nameTable + nameOfs, rom->imageLen - nameTableOfs - nameOfs)) {
			free(files);
			return false;
		}

		files[named].name = (const char *)nameTable + nameOfs;
		files[named].offset = ofs;
		files[named].size = next > ofs ? next - ofs : 0;
		files[named].id = i;
		++named;
	}

	if (named < ALT_MIN_FILES) {
		free(files);
		return false;
	}

	*outFiles = files;
	*outNum = named;
	return true;
}

/**
 * Build the ROM's file index, once. The manifest may declare the two offsets;
 * otherwise every known variant is tried, header match first, and the one
 * whose table actually decodes wins. Guessing is not involved: a candidate is
 * accepted only by decoding to an ordered table of plausibly named files.
 */
static bool altFileIndex(struct altRom *rom)
{
	int order[NUM_ROM_VARIANTS];
	int n = 0, i;

	if (rom->filesTried) {
		return rom->files != NULL;
	}

	rom->filesTried = true;

	if (!altImage(rom)) {
		return false;
	}

	if (rom->dataOffset && rom->tableOffset) {
		uint8_t *seg = NULL;
		uint32_t segLen = 0;

		if (!altInflate(rom, rom->dataOffset, &seg, &segLen)) {
			die("romSource '%s': no 1173 data segment at the declared dataOffset 0x%x in %s",
					rom->id, rom->dataOffset, rom->path);
		}

		if (!altDecodeTable(rom, seg, segLen, rom->tableOffset, &rom->files, &rom->numFiles)) {
			free(seg);
			die("romSource '%s': no file table at the declared tableOffset 0x%x in %s",
					rom->id, rom->tableOffset, rom->path);
		}

		rom->dataSeg = seg;
		rom->dataSegLen = segLen;
		rom->variant = "declared";
		return true;
	}

	/* Variants whose cartridge id matches the header go first. */
	for (i = 0; i < NUM_ROM_VARIANTS; ++i) {
		if (rom->imageLen > 0x3f && !memcmp(rom->image + 0x3b, g_RomVariants[i].cartId, 4)) {
			order[n++] = i;
		}
	}

	for (i = 0; i < NUM_ROM_VARIANTS; ++i) {
		int k, seen = 0;

		for (k = 0; k < n; ++k) {
			if (order[k] == i) {
				seen = 1;
			}
		}

		if (!seen) {
			order[n++] = i;
		}
	}

	for (i = 0; i < n; ++i) {
		const struct romVariant *rv = &g_RomVariants[order[i]];
		uint8_t *seg = NULL;
		uint32_t segLen = 0;

		if (!altInflate(rom, rv->dataOffset, &seg, &segLen)) {
			continue;
		}

		if (altDecodeTable(rom, seg, segLen, rv->tableOffset, &rom->files, &rom->numFiles)) {
			rom->dataSeg = seg;
			rom->dataSegLen = segLen;
			rom->variant = rv->name;

			/* Said out loud, because which variant's offsets fit is the one
			 * thing about a byName resolution that was worked out rather than
			 * declared - and for a modded ROM it is worth seeing. */
			printf("romSource '%s': %s reads as %s, %u files\n",
					rom->id, rom->path, rom->variant, rom->numFiles);
			return true;
		}

		free(seg);
	}

	return false;
}

/**
 * How long a file in a ROM really is, as against how much room it was given.
 *
 * The offset table says where the next file starts, not where this one ends,
 * and a 1173 file is followed by slack up to that boundary. The deflate stream
 * itself knows where it stops, so inflating it and counting the input consumed
 * gives the true length: five header bytes plus the compressed data. This is
 * what the Python writer emitted, by way of zlib's unused_data, and matching it
 * is how a rebuilt table can be compared against the one that shipped.
 *
 * An uncompressed file, or one whose stream will not inflate, keeps the whole
 * span - the same fallback the Python takes.
 */
static uint32_t altTrueSize(struct altRom *rom, uint32_t ofs, uint32_t span)
{
	z_stream zs;
	uint8_t *scratch;
	uint32_t declared;
	int rc;

	if (span < 6 || (size_t)ofs + span > rom->imageLen) {
		return span;
	}

	if (rom->image[ofs] != 0x11 || rom->image[ofs + 1] != 0x73) {
		return span;
	}

	declared = ((uint32_t)rom->image[ofs + 2] << 16) | ((uint32_t)rom->image[ofs + 3] << 8)
			| (uint32_t)rom->image[ofs + 4];

	if (!declared || declared > 0x800000) {
		return span;
	}

	scratch = malloc(declared);

	if (!scratch) {
		return span;
	}

	memset(&zs, 0, sizeof(zs));
	zs.next_in = rom->image + ofs + 5;
	zs.avail_in = span - 5;
	zs.next_out = scratch;
	zs.avail_out = declared;

	if (inflateInit2(&zs, -15) != Z_OK) {
		free(scratch);
		return span;
	}

	rc = inflate(&zs, Z_FINISH);

	if (rc == Z_STREAM_END && zs.total_in + 5 <= span) {
		span = (uint32_t)zs.total_in + 5;
	}

	inflateEnd(&zs);
	free(scratch);
	return span;
}

static bool altFileExtent(struct altRom *rom, const char *name, uint32_t *outOfs, uint32_t *outSize)
{
	uint32_t i;

	if (!altFileIndex(rom)) {
		return false;
	}

	for (i = 0; i < rom->numFiles; ++i) {
		if (!strcmp(rom->files[i].name, name)) {
			if (!rom->files[i].size) {
				return false;
			}

			*outOfs = rom->files[i].offset;
			*outSize = altTrueSize(rom, rom->files[i].offset, rom->files[i].size);
			return true;
		}
	}

	return false;
}

static void usage(FILE *out)
{
	fprintf(out,
		"usage: mkfiletable <mod-name> [--workspace <dir>] [--output <dir>]\n"
		"                   [--rom-dir <dir>]... [--base-rom <file>]\n"
		"                   [--vanilla <files.json>] [--allow-orphans] [--help]\n"
		"\n"
		"Builds a mod's filetable.dat from its JSON manifest.\n"
		"\n"
		"  --workspace   where <mod-name>_filetable.json lives (default: .)\n"
		"  --output      where to write filetable.dat (default: the workspace)\n"
		"  --rom-dir     a directory to look for ROMs in; repeatable. Both the\n"
		"                manifest's romSources and the base ROM are found here.\n"
		"  --base-rom    the base game ROM, for resolving 'replaces' by name.\n"
		"                Normally found in a --rom-dir without being named.\n"
		"  --vanilla     a snapshotted {name: id} map, as an alternative to\n"
		"                reading the base ROM. Wins over it when both are given.\n"
		"  --allow-orphans  drop manifest rows whose 'replaces' resolves to\n"
		"                nothing, instead of failing\n"
		"\n"
		"A file entry may declare where its bytes come from:\n"
		"\n"
		"  \"source\": { \"rom\": \"jpn\", \"lookup\": \"byName\",\n"
		"               \"alias\": \"Cheaddark_combatZ\" }\n"
		"      bytes out of a mounted ROM image, found by name, id or offset.\n"
		"      Costs one of the engine's eight global romSource slots.\n"
		"\n"
		"  \"source\": { \"self\": true, \"alias\": \"bg_mp20.seg\" }\n"
		"      bytes out of this mod's OWN directory, at the entry's path. Costs\n"
		"      no romSource slot and carries no offset - the engine knows the\n"
		"      owning mod's directory and the path says where in it. The path is\n"
		"      checked against the output directory here, so a declaration that\n"
		"      resolves to nothing fails the build instead of the load. `alias`\n"
		"      records the name the file answers to outside the mod and must\n"
		"      agree with `replaces` when both are given.\n");
}

/**
 * Where a loose `path` actually lands inside a mod directory, by the same rule
 * romdataFileLoad() uses: a path already under files/ is joined straight on,
 * a textures/ path is tried under files/ first and then bare, and anything
 * else gets files/ prepended. Composing it here is what lets the tool fail a
 * self source whose file is not there, instead of shipping a table whose entry
 * resolves to nothing at runtime and falls back to the ROM without a word.
 */
static bool selfSourceFile(const char *modDir, const char *path, char *dst, size_t dstLen)
{
	struct stat st;

	if (!strncmp(path, "files/", 6)) {
		if (snprintf(dst, dstLen, "%s/%s", modDir, path) >= (int)dstLen) {
			return false;
		}

		return stat(dst, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
	}

	if (snprintf(dst, dstLen, "%s/files/%s", modDir, path) < (int)dstLen
			&& stat(dst, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		return true;
	}

	if (strncmp(path, "textures/", 9)) {
		return false;
	}

	if (snprintf(dst, dstLen, "%s/%s", modDir, path) >= (int)dstLen) {
		return false;
	}

	return stat(dst, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}


/**
 * A file's id is its slot in the ROM's own file table. That is the same number
 * a snapshotted name map carries, so a ROM can stand in for one.
 */
static long altFileId(struct altRom *rom, const char *name)
{
	uint32_t i;

	if (!altFileIndex(rom)) {
		return -1;
	}

	for (i = 0; i < rom->numFiles; ++i) {
		if (!strcmp(rom->files[i].name, name)) {
			return (long)rom->files[i].id;
		}
	}

	return -1;
}

/**
 * Non-textures before textures; replacers before the rest; then by name, and
 * textures among themselves by texture id. Ids are handed out in this order,
 * so this is what keeps a rebuild from renumbering the world.
 */
static int cmpEntry(const void *a, const void *b)
{
	const struct entry { JSON_Object *obj; const char *name; const char *path;
			long texId; bool isTexture; bool replaces; long fixedId;
			int altRom; uint32_t altOfs, altSize; bool drop; } *x = a, *y = b;

	if (x->isTexture != y->isTexture) {
		return x->isTexture ? 1 : -1;
	}

	if (x->isTexture) {
		return x->texId < y->texId ? -1 : x->texId > y->texId ? 1 : 0;
	}

	if (x->replaces != y->replaces) {
		return x->replaces ? -1 : 1;
	}

	return strcmp(x->name, y->name);
}

int main(int argc, char **argv)
{
	const char *modName = NULL, *workspace = ".", *output = NULL, *vanillaPath = NULL;
	const char *baseRomName = NULL;
	struct altRom baseRom;
	const char *romDirs[8] = { 0 };
	bool allowOrphans = false;
	int numRomDirs = 0;
	struct vanillaMap vanilla = { NULL, NULL };
	struct altRom *roms = NULL;
	struct pdftRomSource *sources = NULL;
	uint32_t numRoms = 0;
	char manifestPath[PATHMAX], texmapPath[PATHMAX], outPath[PATHMAX];
	char outTexmapPath[PATHMAX];
	JSON_Value *manifestVal, *texmapVal = NULL;
	JSON_Object *manifest;
	JSON_Array *files;
	struct pdftFile *out = NULL;
	struct pdftTexMap *texmap = NULL;
	struct texEntry *tex = NULL;
	uint32_t numOut = 0, numTex = 0;
	uint32_t nextLocalId = FIRST_LOCAL_ID;
	size_t i, n;
	uint8_t *blob;
	uint32_t blobLen = 0;
	char err[256];
	FILE *f;
	int arg;

	for (arg = 1; arg < argc; ++arg) {
		if (!strcmp(argv[arg], "--workspace") && arg + 1 < argc) {
			workspace = argv[++arg];
		} else if (!strcmp(argv[arg], "--output") && arg + 1 < argc) {
			output = argv[++arg];
		} else if (!strcmp(argv[arg], "--allow-orphans")) {
			allowOrphans = true;
		} else if (!strcmp(argv[arg], "--vanilla") && arg + 1 < argc) {
			vanillaPath = argv[++arg];
		} else if (!strcmp(argv[arg], "--base-rom") && arg + 1 < argc) {
			baseRomName = argv[++arg];
		} else if (!strcmp(argv[arg], "--help") || !strcmp(argv[arg], "-h")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(argv[arg], "--rom-dir") && arg + 1 < argc) {
			if (numRomDirs < (int)(sizeof(romDirs) / sizeof(romDirs[0])) - 2) {
				romDirs[numRomDirs++] = argv[++arg];
			} else {
				die("too many --rom-dir");
			}
		} else if (argv[arg][0] == '-') {
			die("unknown argument %s", argv[arg]);
		} else if (!modName) {
			modName = argv[arg];
		} else {
			die("unexpected argument %s", argv[arg]);
		}
	}

	if (!modName) {
		usage(stderr);
		return 2;
	}

	JOIN(manifestPath, "%s/%s_filetable.json", workspace, modName);
	JOIN(texmapPath, "%s/%s_texmap.json", workspace, modName);

	if (!output) {
		static char def[PATHMAX];
		JOIN(def, "%s/%s", workspace, modName);
		output = def;
	}

	JOIN(outPath, "%s/filetable.dat", output);
	JOIN(outTexmapPath, "%s", texmapPath);

	manifestVal = json_parse_file_with_comments(manifestPath);

	if (!manifestVal) {
		die("could not read or parse %s", manifestPath);
	}

	manifest = json_value_get_object(manifestVal);

	if (!manifest) {
		die("%s: top level is not an object", manifestPath);
	}

	{
		JSON_Array *rs = json_object_get_array(manifest, "romSources");
		size_t count = rs ? json_array_get_count(rs) : 0;

		if (count > PDFT_MAX_ROMSOURCES) {
			die("%s declares %zu romSources; the reader keeps %d",
					manifestPath, count, PDFT_MAX_ROMSOURCES);
		}

		roms = calloc(count ? count : 1, sizeof(*roms));
		sources = calloc(count ? count : 1, sizeof(*sources));

		for (i = 0; i < count; ++i) {
			JSON_Object *o = json_array_get_object(rs, i);
			const char *id = o ? json_object_get_string(o, "id") : NULL;
			const char *fn = o ? json_object_get_string(o, "filename") : NULL;
			const char *fb = o ? json_object_get_string(o, "fallbackBehavior") : NULL;
			JSON_Object *tx = o ? json_object_get_object(o, "textures") : NULL;
			JSON_Object *fl = o ? json_object_get_object(o, "files") : NULL;
			long v = 0;

			if (!id || !fn) {
				die("%s: romSource %zu needs an id and a filename", manifestPath, i);
			}

			snprintf(roms[i].id, sizeof(roms[i].id), "%s", id);
			snprintf(roms[i].filename, sizeof(roms[i].filename), "%s", fn);

			if (strcmp(roms[i].id, id) || strcmp(roms[i].filename, fn)) {
				die("%s: romSource '%s': id or filename is longer than the reader's buffer", manifestPath, id);
			}

			roms[i].expectedSize = numberOf(json_object_get_value(o, "expectedSize"), &v) ? (uint32_t)v : 0;
			roms[i].required = json_object_get_boolean(o, "required") == 1;
			roms[i].strict = json_object_get_boolean(o, "strict") == 1;
			roms[i].fallback = fb && !strcmp(fb, "vanilla") ? PDFT_FALLBACK_VANILLA
					: fb && !strcmp(fb, "error") ? PDFT_FALLBACK_ERROR : PDFT_FALLBACK_SKIP;

			if (tx) {
				roms[i].tlistOffset = numberOf(json_object_get_value(tx, "tlistOffset"), &v) ? (uint32_t)v : 0;
				roms[i].tlistCount = numberOf(json_object_get_value(tx, "tlistCount"), &v) ? (uint32_t)v : 0;
				roms[i].texdataOffset = numberOf(json_object_get_value(tx, "texdataOffset"), &v) ? (uint32_t)v : 0;
			}

			/* Optional, and only for a ROM whose segments are not where any
			 * known variant keeps them. Both or neither: half of the pair
			 * would send the detector looking for a table in the wrong
			 * segment, which is worse than declaring nothing. */
			if (fl) {
				roms[i].dataOffset = numberOf(json_object_get_value(fl, "dataOffset"), &v) ? (uint32_t)v : 0;
				roms[i].tableOffset = numberOf(json_object_get_value(fl, "tableOffset"), &v) ? (uint32_t)v : 0;

				if (!roms[i].dataOffset != !roms[i].tableOffset) {
					die("%s: romSource '%s': a files block needs both dataOffset and tableOffset",
							manifestPath, id);
				}
			}

			sources[i].id = roms[i].id;
			sources[i].filename = roms[i].filename;
			sources[i].expectedSize = roms[i].expectedSize;
			sources[i].required = roms[i].required;
			sources[i].strict = roms[i].strict;
			sources[i].fallback = roms[i].fallback;
			++numRoms;
		}

		romDirs[numRomDirs++] = output;
		romDirs[numRomDirs++] = workspace;
	}

	if (vanillaPath && !vanillaLoad(&vanilla, vanillaPath)) {
		die("could not read the vanilla name map at %s", vanillaPath);
	}

	/* The base ROM carries its own file names, and a file's slot in that table
	 * IS its file id - the same number a snapshotted map holds. So 'replaces'
	 * can resolve against the ROM the player already has to supply, and this
	 * tool stops needing a name map checked in somewhere else.
	 *
	 * --vanilla still wins when given, so nothing that passes it changes.
	 * ntsc-final is tried first because that is what the port builds. */
	memset(&baseRom, 0, sizeof(baseRom));

	if (!vanilla.files) {
		static const char *candidates[] = {
			"pd.ntsc-final.z64", "pd.pal-final.z64", "pd.jpn-final.z64",
			"pd.ntsc-beta.z64", "pd.pal-beta.z64",
		};
		size_t c;

		snprintf(baseRom.id, sizeof(baseRom.id), "%s", "base");

		if (baseRomName) {
			snprintf(baseRom.filename, sizeof(baseRom.filename), "%s", baseRomName);
			altOpen(&baseRom, romDirs, numRomDirs);
		} else {
			for (c = 0; c < sizeof(candidates) / sizeof(candidates[0]); ++c) {
				memset(&baseRom, 0, sizeof(baseRom));
				snprintf(baseRom.id, sizeof(baseRom.id), "%s", "base");
				snprintf(baseRom.filename, sizeof(baseRom.filename), "%s", candidates[c]);

				if (altOpen(&baseRom, romDirs, numRomDirs) && altFileIndex(&baseRom)) {
					break;
				}
			}
		}
	}

	files = json_object_get_array(manifest, "files");
	n = files ? json_array_get_count(files) : 0;

	if (!n) {
		die("%s has no files", manifestPath);
	}

	/* the texmap as it stands, so ports stay put */
	texmapVal = json_parse_file(texmapPath);

	if (texmapVal) {
		JSON_Object *root = json_value_get_object(texmapVal);
		JSON_Object *entries = root ? json_object_get_object(root, "entries") : NULL;
		size_t count = entries ? json_object_get_count(entries) : 0;

		tex = calloc(count + n, sizeof(*tex));

		for (i = 0; i < count; ++i) {
			const char *key = json_object_get_name(entries, i);
			long local = strtol(key, NULL, 0);
			long slot = 0;

			if (!numberOf(json_object_get_value_at(entries, i), &slot)) {
				die("%s: entry '%s' is not a number", texmapPath, key);
			}

			tex[numTex].localTexId = (uint32_t)local;
			tex[numTex].slotIdx = (uint32_t)slot;
			++numTex;
		}
	} else {
		tex = calloc(n, sizeof(*tex));
	}

	out = calloc(n, sizeof(*out));

	/* The order the entries go out in decides the ids, so it is part of the
	 * format as far as anyone downstream is concerned: replacers first by
	 * name, then the rest by name, then textures by their texture id. */
	{
		struct entry {
			JSON_Object *obj;
			const char *name;
			const char *path;
			long texId;
			bool isTexture;
			bool replaces;
			long fixedId;      /* from the vanilla map, for a replacer */
			int altRom;        /* index into roms[], or -1 */
			bool selfSrc;      /* source: { self: true } */
			const char *alias;  /* second name for this id; forces table v4 */
			uint32_t altOfs, altSize;
			bool drop;         /* an orphan, kept out of the output */
		};
		struct entry *ents = calloc(n, sizeof(*ents));
		size_t numPlain = 0, numTexEnt = 0;

		for (i = 0; i < n; ++i) {
			JSON_Object *e = json_array_get_object(files, i);
			const char *name = e ? json_object_get_string(e, "name") : NULL;
			const char *type = e ? json_object_get_string(e, "type") : NULL;

			if (!name || !name[0]) {
				die("%s: file entry %zu has no name", manifestPath, i);
			}

			const char *replaces = json_object_get_string(e, "replaces");
			JSON_Object *src = json_object_get_object(e, "source");

			ents[i].obj = e;
			ents[i].name = name;
			ents[i].path = json_object_get_string(e, "path");
			ents[i].isTexture = type && !strcmp(type, "texture");

			/* Every per-file name rule, checked with the same predicate the
			 * loader compiles - see port/include/pdftrules.h. This is fatal
			 * rather than a warning because each verdict it returns is a file
			 * that builds clean and is then never found at runtime, and a
			 * warning in a build that still writes filetable.dat is a warning
			 * nobody reads. The whole shipped roster passes: 5581 entries,
			 * 4940 of them textures, across all eight mods, zero findings. */
			{
				char texPrefixBuf[64];
				const char *texPrefix = pdftTexPrefixFromModDir(modName,
						texPrefixBuf, sizeof(texPrefixBuf));
				enum pdftNameVerdict verdict = pdftCheckName(name,
						ents[i].isTexture, texPrefix);

				if (verdict != PDFT_NAME_OK) {
					die("%s: file entry %zu, '%s': %s", manifestPath, i, name,
							pdftNameVerdictText(verdict));
				}
			}
			ents[i].replaces = replaces != NULL;
			ents[i].fixedId = -1;
			ents[i].altRom = -1;
			ents[i].selfSrc = false;

			if (replaces) {
				const char *nameSource;

				if (vanilla.files) {
					ents[i].fixedId = vanillaLookup(&vanilla, replaces);
					nameSource = vanillaPath;
				} else if (baseRom.fp) {
					ents[i].fixedId = altFileId(&baseRom, replaces);
					nameSource = baseRom.path;
				} else {
					die("%s: '%s' replaces '%s', and there is nothing to resolve it against. "
							"Put the base ROM in a --rom-dir, name it with --base-rom, or pass "
							"--vanilla <files.json>", manifestPath, name, replaces);
				}

				if (ents[i].fixedId < 0) {
					if (!allowOrphans) {
						die("%s: '%s' replaces '%s', which %s does not have. "
								"Fix the manifest, or pass --allow-orphans to drop the entry",
								manifestPath, name, replaces, nameSource);
					}

					/* The Python dropped these silently under the same flag, which
					 * is how a manifest row can do nothing for a year without
					 * anyone noticing. Dropped here too, but said out loud. */
					fprintf(stderr, "mkfiletable: dropping '%s': it replaces '%s', which %s "
							"does not have\n", name, replaces, nameSource);
					ents[i].drop = true;
				}
			}

			if (src && json_object_get_boolean(src, "self") == 1) {
				/* A source in this mod's OWN directory. The file-based twin of
				 * a romSource, and the one a mod that ships loose files can
				 * actually use: it names no image, so it costs no romSource
				 * slot, and g_RomSources[] is global across every mounted mod
				 * and eight deep.
				 *
				 * The alias is the name this file answers to outside the mod -
				 * the vanilla name it was slugged away from. It is authoring
				 * and provenance, not wire data: nothing is emitted for it, and
				 * 'replaces' is what actually pins the id. Checked against
				 * 'replaces' when both are given, because two different answers
				 * to what this file used to be called is a manifest bug that
				 * would otherwise ship silently. */
				const char *alias = json_object_get_string(src, "alias");
				char resolved[PATHMAX];

				if (json_object_get_string(src, "rom")) {
					die("%s: '%s' declares both self and rom in one source; "
							"the bytes are in one place or the other", manifestPath, name);
				}

				if (!ents[i].path || !ents[i].path[0]) {
					die("%s: '%s' is self-sourced and has no path; the path is the whole "
							"of the declaration", manifestPath, name);
				}

				/* The pipe/:: grammar exists so the walk can CHOOSE between
				 * candidate paths. A self source is the statement that there
				 * is nothing to choose, and a variant list here would mean the
				 * tool could not say which file it just promised was there. */
				if (strchr(ents[i].path, '|') || strstr(ents[i].path, "::")) {
					die("%s: '%s' is self-sourced with a path-variant list; a self source "
							"names exactly one file", manifestPath, name);
				}

				if (alias && replaces && strcmp(alias, replaces)) {
					die("%s: '%s' aliases '%s' but replaces '%s'; a file has one name "
							"outside the mod", manifestPath, name, alias, replaces);
				}

				/* It is no longer authoring-only. It goes on the wire as a
				 * second name for this id, so a modconfig can reference the
				 * file by the vanilla name it replaces. */
				ents[i].alias = alias;

				if (!selfSourceFile(output, ents[i].path, resolved, sizeof(resolved))) {
					die("%s: '%s' is self-sourced at '%s', which is not a readable file "
							"under %s. A declared source that resolves to nothing is worse "
							"than a loose path: it fails silently at load and falls back to "
							"the ROM", manifestPath, name, ents[i].path, output);
				}

				ents[i].selfSrc = true;
			} else if (src) {
				const char *romId = json_object_get_string(src, "rom");
				const char *lookup = json_object_get_string(src, "lookup");
				uint32_t k;
				long v = 0;

				if (!romId || !lookup) {
					die("%s: '%s' has a source with no rom or no lookup", manifestPath, name);
				}

				for (k = 0; k < numRoms; ++k) {
					if (!strcmp(roms[k].id, romId)) {
						ents[i].altRom = (int)k;
						break;
					}
				}

				if (ents[i].altRom < 0) {
					die("%s: '%s' names romSource '%s', which the manifest does not declare",
							manifestPath, name, romId);
				}

				if (!strcmp(lookup, "byOffset")) {
					if (!numberOf(json_object_get_value(src, "offset"), &v)) {
						die("%s: '%s' is byOffset with no usable offset", manifestPath, name);
					}

					ents[i].altOfs = (uint32_t)v;

					if (!numberOf(json_object_get_value(src, "size"), &v)) {
						die("%s: '%s' is byOffset with no usable size", manifestPath, name);
					}

					ents[i].altSize = (uint32_t)v;
				} else if (!strcmp(lookup, "byId")) {
					struct altRom *rom = &roms[ents[i].altRom];

					if (!numberOf(json_object_get_value(src, "id"), &v)) {
						die("%s: '%s' is byId with no usable id", manifestPath, name);
					}

					if (!altOpen(rom, romDirs, numRomDirs)) {
						die("could not find '%s' for romSource '%s'; pass --rom-dir",
								rom->filename, rom->id);
					}

					if (!altTextureExtent(rom, (uint32_t)v, &ents[i].altOfs, &ents[i].altSize)) {
						die("%s: '%s': texture 0x%lx is not readable from %s",
								manifestPath, name, v, rom->path);
					}
				} else if (!strcmp(lookup, "byName")) {
					struct altRom *rom = &roms[ents[i].altRom];
					const char *alias = json_object_get_string(src, "alias");

					/* The alias is the name in THAT ROM; our own name is the
					 * name in the mod. They differ whenever a mod mounts a
					 * file under a name of its own, which is most of the time. */
					if (!alias) {
						alias = name;
					}

					if (!altOpen(rom, romDirs, numRomDirs)) {
						die("could not find '%s' for romSource '%s'; pass --rom-dir",
								rom->filename, rom->id);
					}

					if (!altFileIndex(rom)) {
						die("%s: '%s' is byName, but no file table could be read out of %s",
								manifestPath, name, rom->path);
					}

					if (!altFileExtent(rom, alias, &ents[i].altOfs, &ents[i].altSize)) {
						die("%s: '%s': no file named '%s' in %s (%s, %u files)",
								manifestPath, name, alias, rom->path, rom->variant, rom->numFiles);
					}
				} else {
					die("%s: '%s' has lookup '%s', which is not one of byOffset, byId, byName",
							manifestPath, name, lookup);
				}
			}

			if (ents[i].isTexture) {
				if (!numberOf(json_object_get_value(e, "textureId"), &ents[i].texId)) {
					die("%s: texture '%s' has no usable textureId", manifestPath, name);
				}

				++numTexEnt;
			} else {
				++numPlain;
			}
		}

		(void)numPlain;
		(void)numTexEnt;
		qsort(ents, n, sizeof(*ents), cmpEntry);

		for (i = 0; i < n; ++i) {
			if (ents[i].drop) {
				continue;
			}

			out[numOut].id = ents[i].fixedId >= 0 ? (uint32_t)ents[i].fixedId : nextLocalId++;
			out[numOut].name = ents[i].name;
			out[numOut].path = ents[i].path;
			out[numOut].selfSource = ents[i].selfSrc;
			out[numOut].alias = ents[i].alias;
			out[numOut].alt.romIdx = ents[i].altRom;
			out[numOut].alt.offset = ents[i].altOfs;
			out[numOut].alt.size = ents[i].altSize;
			out[numOut].alt.compression = 0;
			++numOut;

			if (ents[i].isTexture) {
				bool known = false;
				uint32_t k;

				for (k = 0; k < numTex; ++k) {
					if (tex[k].localTexId == (uint32_t)ents[i].texId) {
						known = true;
						break;
					}
				}

				if (!known) {
					tex[numTex].localTexId = (uint32_t)ents[i].texId;
					tex[numTex].slotIdx = UINT_MAX;
					++numTex;
				}
			}
		}

		free(ents);
	}

	/* Drop texmap entries whose texture is gone, then compact the slots. */
	{
		uint32_t k, live = 0;

		for (k = 0; k < numTex; ++k) {
			bool stillThere = false;
			size_t j;

			for (j = 0; j < n; ++j) {
				JSON_Object *e = json_array_get_object(files, j);
				const char *type = json_object_get_string(e, "type");
				long texId = 0;

				if (!type || strcmp(type, "texture")) {
					continue;
				}

				if (numberOf(json_object_get_value(e, "textureId"), &texId)
						&& (uint32_t)texId == tex[k].localTexId) {
					stillThere = true;
					break;
				}
			}

			if (stillThere) {
				tex[live++] = tex[k];
			} else {
				printf("  retiring texture 0x%04x, its slot is reclaimed\n", tex[k].localTexId);
			}
		}

		numTex = live;

		/* Keep the order the slots were first handed out in, so a texture that
		 * already had a port keeps it wherever it can. */
		qsort(tex, numTex, sizeof(*tex), cmpTexSlot);

		for (k = 0; k < numTex; ++k) {
			tex[k].slotIdx = k;
		}
	}

	texmap = calloc(numTex ? numTex : 1, sizeof(*texmap));

	{
		struct texEntry *sorted = calloc(numTex ? numTex : 1, sizeof(*sorted));
		uint32_t k;

		memcpy(sorted, tex, numTex * sizeof(*tex));
		qsort(sorted, numTex, sizeof(*sorted), cmpTexLocal);

		for (k = 0; k < numTex; ++k) {
			texmap[k].localTexId = (uint16_t)sorted[k].localTexId;
			texmap[k].slotIdx = (uint16_t)sorted[k].slotIdx;
		}

		free(sorted);
	}

	{
		struct pdftInput in;

		in.files = out;
		in.numFiles = numOut;
		in.sources = sources;
		in.numSources = numRoms;
		in.texmap = texmap;
		in.numTexMap = numTex;

		blob = pdftWrite(&in, &blobLen, err, sizeof(err));

		if (!blob) {
			die("%s", err);
		}

		f = fopen(outPath, "wb");

		if (!f) {
			die("could not write %s", outPath);
		}

		fwrite(blob, 1, blobLen, f);
		fclose(f);
		{
			uint32_t k, numSelf = 0;

			for (k = 0; k < numOut; ++k) {
				if (out[k].selfSource) {
					++numSelf;
				}
			}

			printf("wrote %s: v%u, %u files, %u romSources, %u self-sourced, %u textures, %u bytes\n",
					outPath, pdftVersionFor(&in), numOut, numRoms, numSelf, numTex, blobLen);
		}
	}

	/* Write the texmap back so the next build keeps these ports. */
	{
		JSON_Value *rootVal = json_value_init_object();
		JSON_Object *root = json_value_get_object(rootVal);
		JSON_Value *entriesVal = json_value_init_object();
		JSON_Object *entries = json_value_get_object(entriesVal);
		uint32_t k;

		json_object_set_string(root, "modName", modName);

		for (k = 0; k < numTex; ++k) {
			char key[16], val[16];

			snprintf(key, sizeof(key), "0x%04x", texmap[k].localTexId);
			snprintf(val, sizeof(val), "0x%04x", texmap[k].slotIdx);
			json_object_set_string(entries, key, val);
		}

		json_object_set_value(root, "entries", entriesVal);

		if (json_serialize_to_file_pretty(rootVal, outTexmapPath) != JSONSuccess) {
			die("could not write %s", outTexmapPath);
		}

		json_value_free(rootVal);
	}

	json_value_free(manifestVal);

	if (texmapVal) {
		json_value_free(texmapVal);
	}

	return 0;
}

/**
 * pdftrules — the filetable rules the builder and the loader have to agree on.
 *
 * WHY THIS FILE IS HERE AND NOT IN tools/. Precedent is tools/modsetcheck,
 * which includes src/include/gbiex.h rather than restating the texture slot
 * ceiling, "so this tool cannot tell a mod author the set fits when the engine
 * will disagree". Same argument, same direction: the engine owns the rule and
 * the tool includes it. A rule restated in the builder is a rule that can drift
 * away from the loader, and every drift of that kind is silent — the builder
 * says the mod is fine and the file is simply never found at runtime.
 *
 * WHY IT IS FREESTANDING. Every other header in port/include pulls
 * PR/ultratypes.h. This one deliberately does not, because tools/mkfiletable
 * and tools/modsetcheck compile it with a plain `cc -std=c99` that has no PD
 * types and no include path into the engine's headers. Keep it to the C
 * library. If this ever needs u32, it has stopped being shareable.
 *
 * WHAT BELONGS HERE. Only invariants that are properties of ONE entry, because
 * only those can be checked in both places. A per-file rule is checkable by the
 * builder that writes the entry and by the loader that reads it back. A
 * whole-set rule - name uniqueness across a roster, texture slot exhaustion,
 * romSource id truncation - is not: the builder sees one mod, and the loader
 * sees the set only after the damage is done and can do nothing but report it.
 * Those live in tools/modsetcheck and must stay there.
 *
 * WHAT IS DELIBERATELY NOT HERE. The bg_/Ump_setup/_tilesZ/_padsZ family that
 * a stage is made of looks like a rule and is not one: nothing in the engine
 * requires those four names, and fs.c names them only to record which paths two
 * mods happen to contest. Writing that quadruple in here would be inventing a
 * constraint and then enforcing it on mods that are correct today.
 */
#ifndef _IN_PDFTRULES_H
#define _IN_PDFTRULES_H

#include <stddef.h>

/* Per-entry flags, as romdataParseFileTable() reads them. The field is a u32
 * and the reader tests individual bits, so an unknown bit is ignored rather
 * than mis-parsed: a bit added here does not move any byte and does not desync
 * an older reader, which is why SELFSOURCE needs no new version.
 *
 * romdata.c tested these as bare literals - `flags & 4`, `flags & 8`,
 * `flags & 0x10` - in fourteen places, against names that only the builder
 * could see. That is the drift this header exists to stop, so the literals are
 * gone and both sides now spell the same constant.
 */
#define PDFT_F_ROMRESIDENT 0x1  /* bind g_RomFile + offset */
#define PDFT_F_PATH        0x2  /* the path field is present and non-empty */
#define PDFT_F_ALT         0x4  /* the alt-ROM tail follows the path */
#define PDFT_F_SELFSOURCE  0x8  /* resolve `path` in the OWNING mod's own
                                 * directory, and nowhere else. */
#define PDFT_F_ALIAS       0x10 /* an alias tail follows: u16 len + string.
                                 * UNLIKE the bits above this one MOVES BYTES,
                                 * so it needs v4. An older reader refuses v4
                                 * outright (pdft_read.c bounds the version),
                                 * which is a clean failure rather than the
                                 * silent desync a tail behind an ignored bit
                                 * would have produced. */
#define PDFT_F_PATCH       0x20 /* a patch tail follows the alias tail: u16 len
                                 * + path, inside the owning mod's directory,
                                 * of an xdelta applied to this file's bytes
                                 * AFTER inflate on first load (and the result
                                 * re-deflated, so the 1173 header carries the
                                 * patched size). Orthogonal to where the
                                 * bytes come from - the vanilla rom, a rom
                                 * source or a self source. MOVES BYTES, so it
                                 * needs v5. */
#define PDFT_PATCH_MAX 128      /* the reader's patch path buffer */

/* A romSource record's own flags byte (bit0 required, bit1 strict). These are
 * NOT the PDFT_F_ file-entry bits above; the two bytes share nothing but the
 * word. */
#define PDFT_RS_REQUIRED   0x1
#define PDFT_RS_STRICT     0x2
#define PDFT_RS_PATCHED    0x4  /* the source is a BASE rom plus a PATCH, not
                                 * a file on disk: a tail follows the two
                                 * reserved bytes - u8 len + base id, u8 len +
                                 * patch path (inside the owning mod's dir),
                                 * u32 crc32 of the patched image. The base is
                                 * another declared source's id, or "base" for
                                 * the rom the engine booted from. MOVES BYTES,
                                 * so it needs v5; the writer refuses to emit
                                 * it below that. */
#define PDFT_ROMSOURCE_BASE "base"

/**
 * The longest name that survives the whole round trip.
 *
 * Not a guess and not the wire limit - the wire counts a name with a u16 and
 * would carry far more. This is the smallest buffer a name has to pass through
 * on the way to being matched: `char candidate[128]` in
 * modTextureResolveFileDetailed(), and `char name[128]` in struct
 * modTextureResolveAttempt beside it. snprintf truncates into those without a
 * word, and the truncated string is then looked up and not found, so a name
 * over this length fails as "missing file" rather than as "name too long".
 */
#define PDFT_NAME_MAX 128

/** Why a name would not be found at runtime. PDFT_NAME_OK is zero. */
enum pdftNameVerdict {
	PDFT_NAME_OK = 0,
	PDFT_NAME_EMPTY,
	PDFT_NAME_TOO_LONG,
	PDFT_NAME_TEXTURE_UNSYNTHESISABLE,
};

/** One line of English for a verdict, for a builder error or a loader warning. */
static inline const char *pdftNameVerdictText(enum pdftNameVerdict v)
{
	switch (v) {
	case PDFT_NAME_OK:
		return "ok";
	case PDFT_NAME_EMPTY:
		return "the name is empty, and an entry is reachable only by name";
	case PDFT_NAME_TOO_LONG:
		return "the name is longer than 127 bytes, so the texture resolver "
				"truncates it into its 128-byte buffer and then fails to "
				"find the truncated string";
	case PDFT_NAME_TEXTURE_UNSYNTHESISABLE:
		return "a texture entry's name is never read from the table - it is "
				"BUILT from the texture id - so a name outside the shapes "
				"modTextureResolveFileDetailed() composes can never be matched";
	}

	return "unknown";
}

/**
 * The texture prefix a mod directory yields, exactly as modGetTexPrefix() in
 * port/src/mod.c derives it: the directory basename, minus a required "mod_",
 * truncated at the next '_'. Returns NULL when the directory yields none, in
 * which case the prefixed shape below is simply unavailable to that mod.
 *
 * Shared rather than restated because the builder has to predict the string the
 * loader will compose, and a prefix the two disagree about produces a texture
 * that builds clean and never loads.
 */
static inline const char *pdftTexPrefixFromModDir(const char *modDir, char *buf, size_t bufSize)
{
	const char *slash;
	const char *base;
	size_t i = 0;

	if (!modDir || !modDir[0] || !buf || bufSize < 2) {
		return NULL;
	}

	slash = modDir;
	for (const char *p = modDir; *p; ++p) {
		if (*p == '/' || *p == '\\') {
			slash = p + 1;
		}
	}

	base = slash;
	if (base[0] != 'm' || base[1] != 'o' || base[2] != 'd' || base[3] != '_') {
		return NULL;
	}

	base += 4;
	while (base[i] && base[i] != '_' && i + 1 < bufSize) {
		buf[i] = base[i];
		++i;
	}

	if (i == 0) {
		return NULL;
	}

	buf[i] = '\0';

	return buf;
}

/** Four lowercase hex digits followed by ".bin", and nothing else. */
static inline int pdftIsTexBasename(const char *s)
{
	int i;

	if (!s) {
		return 0;
	}

	/* Lowercase only, because the loader composes the name with "%04x" and
	 * matches it with an FNV-1a hash over the raw bytes. Nothing folds case
	 * anywhere on that path, so "00AF.bin" is a different string from
	 * "00af.bin" and is never found. */
	for (i = 0; i < 4; ++i) {
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
			return 0;
		}
	}

	return s[4] == '.' && s[5] == 'b' && s[6] == 'i' && s[7] == 'n' && s[8] == '\0';
}

/**
 * Whether a texture entry's name is one the loader can actually compose.
 *
 * modTextureResolveFileDetailed() never reads a texture name out of the table.
 * It BUILDS one from the texture id and looks that up, trying, in order:
 *
 *   <model path>/%04x.bin   scoped to the model that referenced the texture
 *   %04x.bin                bare
 *   <prefix>_%04x.bin       prefix from the mod directory, see above
 *
 * so a texture entry named anything else is dead weight in the table: it
 * occupies an id, it passes every structural check, and no lookup will ever
 * reach it. This is the failure slugmod avoids by leaving texture names alone
 * while it renames everything around them, and it is the one a human renaming
 * a file by hand walks straight into.
 *
 * `prefix` may be NULL, which only removes the third shape from consideration.
 */
static inline int pdftTexNameIsSynthesisable(const char *name, const char *prefix)
{
	const char *slash;
	size_t plen;

	if (!name || !name[0]) {
		return 0;
	}

	/* The model-scoped shape: anything, a '/', then the bare basename. The
	 * path half is the model's own and is not ours to constrain. */
	slash = NULL;
	for (const char *p = name; *p; ++p) {
		if (*p == '/') {
			slash = p;
		}
	}

	if (slash) {
		return pdftIsTexBasename(slash + 1);
	}

	if (pdftIsTexBasename(name)) {
		return 1;
	}

	if (prefix && prefix[0]) {
		plen = 0;
		while (prefix[plen]) {
			++plen;
		}

		for (size_t i = 0; i < plen; ++i) {
			if (name[i] != prefix[i]) {
				return 0;
			}
		}

		return name[plen] == '_' && pdftIsTexBasename(name + plen + 1);
	}

	return 0;
}

/**
 * Every per-file name rule, in one call, so the builder and the loader cannot
 * check different subsets of them.
 *
 * `isTexture` is the manifest's `type: texture`, which is what decides whether
 * the name is authored or synthesised. `prefix` is this mod's, or NULL.
 */
static inline enum pdftNameVerdict pdftCheckName(const char *name, int isTexture, const char *prefix)
{
	size_t n = 0;

	if (!name || !name[0]) {
		return PDFT_NAME_EMPTY;
	}

	while (name[n]) {
		++n;
	}

	if (n >= PDFT_NAME_MAX) {
		return PDFT_NAME_TOO_LONG;
	}

	if (isTexture && !pdftTexNameIsSynthesisable(name, prefix)) {
		return PDFT_NAME_TEXTURE_UNSYNTHESISABLE;
	}

	return PDFT_NAME_OK;
}

#endif

/**
 * pdftrules_test — prove port/include/pdftrules.h rejects what it claims to.
 *
 * A predicate that accepts every mod on disk has demonstrated nothing: it could
 * be `return 1`. The shipped roster is the positive half of the evidence and
 * these are the negative half, one case per way a name is known to fail at
 * runtime, each written as the mistake a person actually makes rather than as
 * a fuzzed string.
 *
 *   cc -O2 -Wall -Wextra -std=c99 -I../../port/include -o pdftrules_test \
 *      pdftrules_test.c && ./pdftrules_test
 */
#include <stdio.h>
#include <string.h>
#include "pdftrules.h"

static int g_Fails;

static void expect(const char *what, const char *name, int isTexture,
		const char *prefix, enum pdftNameVerdict want)
{
	enum pdftNameVerdict got = pdftCheckName(name, isTexture, prefix);

	if (got != want) {
		printf("FAIL  %-46s wanted %d, got %d\n", what, (int)want, (int)got);
		++g_Fails;
	} else {
		printf("ok    %-46s\n", what);
	}
}

static void expectPrefix(const char *modDir, const char *want)
{
	char buf[64];
	const char *got = pdftTexPrefixFromModDir(modDir, buf, sizeof(buf));
	int bad = want ? (!got || strcmp(got, want)) : (got != NULL);

	if (bad) {
		printf("FAIL  prefix %-39s wanted %s, got %s\n", modDir,
				want ? want : "(none)", got ? got : "(none)");
		++g_Fails;
	} else {
		printf("ok    prefix %-39s\n", modDir);
	}
}

int main(void)
{
	char longName[PDFT_NAME_MAX + 8];

	/* The three shapes modTextureResolveFileDetailed() composes. All must pass,
	 * because every one of them is reachable and 4940 entries on the shipped
	 * roster use them. */
	expect("bare texture name", "00af.bin", 1, "fojo", PDFT_NAME_OK);
	expect("prefixed texture name", "fojo_00af.bin", 1, "fojo", PDFT_NAME_OK);
	expect("model-scoped texture name", "files/Pchr_x.bin/00af.bin", 1, "fojo", PDFT_NAME_OK);

	/* Uppercase hex. The loader composes the name with "%04x" and matches it
	 * with an FNV-1a hash over raw bytes, so nothing folds case anywhere on
	 * that path and this is a different string from the one it looks for. It
	 * is the failure a person hits renaming a file in a GUI. */
	expect("uppercase hex is a different string", "00AF.bin", 1, "fojo",
			PDFT_NAME_TEXTURE_UNSYNTHESISABLE);

	/* A texture given a human name. Reads fine, occupies an id, is never
	 * reached - the name in the table is not what the resolver asks for. This
	 * is exactly what slugmod refuses to do and says so in its usage. */
	expect("authored name on a texture entry", "Cheaddark_combatZ", 1, "fojo",
			PDFT_NAME_TEXTURE_UNSYNTHESISABLE);

	/* Slugging a texture the way every other file gets slugged. The tempting
	 * mistake, and the one the tool would make if slugmod's exception were
	 * ever dropped: it looks slugged and correct and matches nothing. */
	expect("slug in the wrong half of the name", "00af_fojo.bin", 1, "fojo",
			PDFT_NAME_TEXTURE_UNSYNTHESISABLE);

	/* The right prefix is this mod's, not another's. A file copied between
	 * mods keeps the donor's prefix and stops resolving in its new home. */
	expect("another mod's prefix", "gex_00af.bin", 1, "fojo",
			PDFT_NAME_TEXTURE_UNSYNTHESISABLE);
	expect("same name in the mod it came from", "gex_00af.bin", 1, "gex", PDFT_NAME_OK);

	/* Short of four digits, or long of them. */
	expect("three hex digits", "0af.bin", 1, "fojo", PDFT_NAME_TEXTURE_UNSYNTHESISABLE);
	expect("five hex digits", "000af.bin", 1, "fojo", PDFT_NAME_TEXTURE_UNSYNTHESISABLE);
	expect("wrong extension", "00af.png", 1, "fojo", PDFT_NAME_TEXTURE_UNSYNTHESISABLE);
	expect("trailing junk", "00af.bin ", 1, "fojo", PDFT_NAME_TEXTURE_UNSYNTHESISABLE);

	/* A mod with no derivable prefix simply loses the third shape; the other
	 * two still resolve. Nothing about that is an error. */
	expect("no prefix, bare shape still fine", "00af.bin", 1, NULL, PDFT_NAME_OK);
	expect("no prefix, prefixed shape unreachable", "fojo_00af.bin", 1, NULL,
			PDFT_NAME_TEXTURE_UNSYNTHESISABLE);

	/* Non-texture entries carry authored names and are matched literally, so
	 * none of the above applies to them. The trailing Z that marks a
	 * 1173-compressed ROM file must survive untouched. */
	expect("authored name, not a texture", "Cheaddark_combatZ", 0, "fojo", PDFT_NAME_OK);
	expect("a name that looks like a texture", "00af.bin", 0, "fojo", PDFT_NAME_OK);
	expect("bg segment", "bg_mp20_gex.seg", 0, "gex", PDFT_NAME_OK);
	expect("slugged tiles keep their Z", "bg_mp20_gex_tilesZ", 0, "gex", PDFT_NAME_OK);

	/* Empty and over-long. 127 is the last length that survives the resolver's
	 * 128-byte buffer with its terminator. */
	expect("empty name", "", 0, "fojo", PDFT_NAME_EMPTY);
	expect("null name", NULL, 0, "fojo", PDFT_NAME_EMPTY);

	memset(longName, 'a', sizeof(longName));
	longName[PDFT_NAME_MAX - 1] = '\0';
	expect("127 bytes fits", longName, 0, "fojo", PDFT_NAME_OK);

	/* Re-fill before lengthening: the terminator written above sits at 127, so
	 * writing another at 128 leaves the string 127 long and the case silently
	 * tests nothing. It did, on the first run of this file. */
	memset(longName, 'a', sizeof(longName));
	longName[PDFT_NAME_MAX] = '\0';
	expect("128 bytes truncates", longName, 0, "fojo", PDFT_NAME_TOO_LONG);

	/* The prefix derivation, against the roster fs.c actually mounts. It has to
	 * agree with modGetTexPrefix() in port/src/mod.c character for character. */
	expectPrefix("mod_fojo", "fojo");
	expectPrefix("$B/mods/mod_aio_characters", "aio");
	expectPrefix("$B/mods/mod_goldfinger_stages", "goldfinger");
	expectPrefix("C:\\pd\\data\\mods\\mod_kakariko_stages", "kakariko");
	expectPrefix("mods/notamod", NULL);
	expectPrefix("mod_", NULL);
	expectPrefix("", NULL);

	printf("\n%s\n", g_Fails ? "FAILED" : "all good");

	return g_Fails ? 1 : 0;
}

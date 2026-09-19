/**
 * slugmod — give a mod's files names and paths nobody else can claim, and make
 * every entry say where its bytes come from.
 *
 *   slugmod <mod-name> [--workspace <dir>] [--output <dir>] [--slug <s>]
 *           [--apply] [--help]
 *
 * Three stage mods ship 3134 fragment entries between them and not one declares
 * a source: every entry is a bare loose path. romdataFileLoad takes a file's
 * NAME from the owning mod's row but hunts the BYTES through every mounted mod
 * directory in reverse order, so two mods shipping the same relative path
 * resolve by roster position. 783 of those paths are claimed by more than one
 * mod today.
 *
 * This rewrites a mod so that stops being possible:
 *
 *   - the file on disk keeps its DIRECTORY and gets the mod's slug injected
 *     into its BASENAME, so the path is the mod's alone;
 *   - the manifest entry declares `source: { self: true }`, so the engine
 *     resolves that path in the owning mod's own directory instead of walking;
 *   - where the name was the file's own authored identity, the name is slugged
 *     too and the old one is recorded as `alias`.
 *
 * The two halves fix different things and both are wanted. The declaration
 * fixes which bytes a correctly-owned id gets. The rename fixes which mod a
 * bare name or path belongs to - for romdataFileGetNumForNameAnyMod, for a
 * human reading `ls`, and for an engine built before the flag existed, which
 * ignores it and walks.
 *
 * Nothing is written without --apply, and nothing is ever deleted: a rename
 * whose destination exists is refused rather than performed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "vendor/parson/parson.h"

#define PATHMAX 4096
#define SLUGMAX 64

#define JOIN(dst, ...) do { \
		if (snprintf((dst), sizeof(dst), __VA_ARGS__) >= (int)sizeof(dst)) { \
			die("path is longer than %d bytes", (int)sizeof(dst)); \
		} \
	} while (0)

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "slugmod: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void usage(FILE *out)
{
	fprintf(out,
		"usage: slugmod <mod-name> [--workspace <dir>] [--output <dir>]\n"
		"               [--slug <s>] [--apply] [--help]\n"
		"\n"
		"Rewrites a mod so every file entry declares where its bytes come from\n"
		"and no two mods can claim the same path.\n"
		"\n"
		"  --workspace  where <mod-name>_filetable.json lives (default: .)\n"
		"  --output     the mod directory the files live in (default: the\n"
		"               workspace, as mkfiletable defaults it)\n"
		"  --slug       the marker injected into each basename. Defaults to the\n"
		"               mod name without its leading 'mod_', which cannot\n"
		"               collide because mod directory names cannot. Shorter is\n"
		"               nicer to read - 'kakariko' for mod_kakariko_stages -\n"
		"               but a short slug is yours to keep unique.\n"
		"  --apply      perform the plan. Without it nothing is written.\n"
		"\n"
		"Where the slug goes:\n"
		"\n"
		"  name.ext  ->  name_<slug>.ext    before the last extension\n"
		"  nameZ     ->  name_<slug>Z       before the trailing Z, which marks\n"
		"                                   a 1173-compressed ROM file and is\n"
		"                                   what tools/extract and pdt classify\n"
		"                                   on (endswith('tilesZ'), and so on).\n"
		"                                   mod_fojo already did this by hand:\n"
		"                                   Cheaddark_combatZ -> CheadMikado_combatZ\n"
		"  name      ->  name_<slug>\n"
		"\n"
		"Names that are SYNTHESISED rather than authored keep theirs: a\n"
		"`type: texture` entry is found by modTextureResolveFileDetailed()\n"
		"building '%%04x.bin' from a texture id, so renaming it to 0048_x.bin\n"
		"would make it unfindable. Those entries get the path rename and the\n"
		"declaration, and their name is left alone. An entry whose name carries\n"
		"a '/' is a model-scoped texture name tied to a model file's path, and\n"
		"is left alone entirely.\n");
}

/**
 * The slug goes INSIDE the basename, never after it, because the last
 * character of a PD file name carries meaning: a trailing Z says the ROM file
 * is 1173-compressed, and tools/extract, tools/bin/pdt and tools/bin/rom-diff
 * all classify with endswith('Z'), endswith('tilesZ'), endswith('padsZ').
 * Appending would silently reclassify every one of them.
 */
static bool slugBasename(const char *base, const char *slug, char *dst, size_t dstLen)
{
	const char *dot = strrchr(base, '.');
	size_t n = strlen(base);
	int w;

	if (dot && dot != base) {
		w = snprintf(dst, dstLen, "%.*s_%s%s", (int)(dot - base), base, slug, dot);
	} else if (n && base[n - 1] == 'Z') {
		w = snprintf(dst, dstLen, "%.*s_%sZ", (int)(n - 1), base, slug);
	} else {
		w = snprintf(dst, dstLen, "%s_%s", base, slug);
	}

	return w > 0 && (size_t)w < dstLen;
}

/** Splits at the last '/', which may be absent. dir gets no trailing slash. */
static void splitPath(const char *path, char *dir, size_t dirLen, const char **base)
{
	const char *slash = strrchr(path, '/');

	if (!slash) {
		dir[0] = '\0';
		*base = path;
		return;
	}

	if ((size_t)(slash - path) >= dirLen) {
		die("path '%s' has a directory longer than %zu bytes", path, dirLen);
	}

	memcpy(dir, path, (size_t)(slash - path));
	dir[slash - path] = '\0';
	*base = slash + 1;
}

/** Where a manifest path lands on disk, by romdataFileLoad()'s own rule. */
static void modFilePath(const char *modDir, const char *path, char *dst, size_t dstLen)
{
	struct stat st;

	if (!strncmp(path, "files/", 6)) {
		snprintf(dst, dstLen, "%s/%s", modDir, path);
		return;
	}

	snprintf(dst, dstLen, "%s/files/%s", modDir, path);

	if (stat(dst, &st) == 0) {
		return;
	}

	if (!strncmp(path, "textures/", 9)) {
		snprintf(dst, dstLen, "%s/%s", modDir, path);
	}
}

static bool isFile(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

struct move {
	char from[PATHMAX];
	char to[PATHMAX];
	/* Copied, not borrowed. json_object_set_string() frees the value it
	 * replaces, so a pointer returned by json_object_get_string() dangles the
	 * moment the entry's name is rewritten - and the modconfig pass below runs
	 * after every entry has been. */
	char oldName[512];
	char newName[512];     /* empty when the name is left alone */
};

int main(int argc, char **argv)
{
	const char *modName = NULL, *workspace = ".", *output = NULL, *slug = NULL;
	char slugBuf[SLUGMAX];
	char manifestPath[PATHMAX], modconfigPath[PATHMAX];
	bool apply = false;
	JSON_Value *manifestVal;
	JSON_Object *manifest;
	JSON_Array *files;
	struct move *moves;
	size_t i, n, numMoves = 0, numNamed = 0, numKept = 0, numSkipped = 0;
	int arg;

	for (arg = 1; arg < argc; ++arg) {
		if (!strcmp(argv[arg], "--workspace") && arg + 1 < argc) {
			workspace = argv[++arg];
		} else if (!strcmp(argv[arg], "--output") && arg + 1 < argc) {
			output = argv[++arg];
		} else if (!strcmp(argv[arg], "--slug") && arg + 1 < argc) {
			slug = argv[++arg];
		} else if (!strcmp(argv[arg], "--apply")) {
			apply = true;
		} else if (!strcmp(argv[arg], "--help") || !strcmp(argv[arg], "-h")) {
			usage(stdout);
			return 0;
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

	if (!output) {
		static char def[PATHMAX];
		JOIN(def, "%s/%s", workspace, modName);
		output = def;
	}

	JOIN(modconfigPath, "%s/modconfig.txt", output);

	if (!slug) {
		const char *s = !strncmp(modName, "mod_", 4) ? modName + 4 : modName;

		if (snprintf(slugBuf, sizeof(slugBuf), "%s", s) >= (int)sizeof(slugBuf)) {
			die("the default slug '%s' is longer than %d bytes; pass --slug", s, SLUGMAX);
		}

		slug = slugBuf;
	}

	if (!slug[0] || strchr(slug, '/') || strchr(slug, '.')) {
		die("slug '%s' must be non-empty and carry no '/' or '.'", slug);
	}

	manifestVal = json_parse_file_with_comments(manifestPath);

	if (!manifestVal) {
		die("could not read or parse %s", manifestPath);
	}

	manifest = json_value_get_object(manifestVal);

	if (!manifest) {
		die("%s: top level is not an object", manifestPath);
	}

	files = json_object_get_array(manifest, "files");
	n = files ? json_array_get_count(files) : 0;

	if (!n) {
		die("%s has no files", manifestPath);
	}

	moves = calloc(n, sizeof(*moves));

	for (i = 0; i < n; ++i) {
		JSON_Object *e = json_array_get_object(files, i);
		const char *name = e ? json_object_get_string(e, "name") : NULL;
		const char *path = e ? json_object_get_string(e, "path") : NULL;
		const char *type = e ? json_object_get_string(e, "type") : NULL;
		const bool isTexture = type && !strcmp(type, "texture");
		char dir[PATHMAX], newPath[PATHMAX], newBase[512];
		const char *base;
		struct move *mv;

		if (!name || !name[0]) {
			die("%s: file entry %zu has no name", manifestPath, i);
		}

		/* Already declared. Leave it: a ROM source is a statement about
		 * another image, and re-pointing it at this directory would be a
		 * different mod. */
		if (json_object_get_object(e, "source")) {
			++numSkipped;
			continue;
		}

		/* No path means no loose file - the bytes are in a ROM or in the
		 * base dir, and there is nothing here to rename or to point at. */
		if (!path || !path[0]) {
			++numSkipped;
			continue;
		}

		if (strchr(path, '|') || strstr(path, "::")) {
			printf("  skip  %-34s path is a variant list, not one file\n", name);
			++numSkipped;
			continue;
		}

		/* A model-scoped texture name: 'CheadMikado_combatZ/0116.bin' is
		 * matched against a candidate built from the MODEL file's path by
		 * modTextureResolveFileDetailed(). Renaming either half without the
		 * other breaks the pair, and which half is which is not visible from
		 * one entry. */
		if (strchr(name, '/')) {
			printf("  skip  %-34s model-scoped texture name\n", name);
			++numSkipped;
			continue;
		}

		splitPath(path, dir, sizeof(dir), &base);

		if (!slugBasename(base, slug, newBase, sizeof(newBase))) {
			die("%s: slugged basename of '%s' does not fit", manifestPath, base);
		}

		if (dir[0]) {
			if (snprintf(newPath, sizeof(newPath), "%s/%s", dir, newBase) >= (int)sizeof(newPath)) {
				die("%s: slugged path of '%s' is longer than %d bytes", manifestPath, path, PATHMAX);
			}
		} else {
			snprintf(newPath, sizeof(newPath), "%s", newBase);
		}

		mv = &moves[numMoves++];
		snprintf(mv->oldName, sizeof(mv->oldName), "%s", name);
		modFilePath(output, path, mv->from, sizeof(mv->from));
		modFilePath(output, newPath, mv->to, sizeof(mv->to));

		/* The name is slugged only where it is the file's own authored
		 * identity. A texture entry's name is SYNTHESISED at lookup:
		 * modTextureResolveFileDetailed() builds '%04x.bin' from a texture id
		 * and hands it to romdataFileGetNumForNameInMod(), so a texture named
		 * 0048_kakariko.bin can never be found. Its path still moves, which is
		 * what the collision is actually in - 745 of the 783 paths two mods
		 * both claim are textures/XXXX.bin. */
		if (!isTexture && !strcmp(name, base)) {
			snprintf(mv->newName, sizeof(mv->newName), "%s", newBase);
			++numNamed;
		} else {
			++numKept;
		}

		if (!isFile(mv->from)) {
			die("%s: '%s' has path '%s', and %s is not a file. Refusing to plan a rename "
					"of something that is not there", manifestPath, name, path, mv->from);
		}

		if (isFile(mv->to)) {
			die("%s: '%s' would move to %s, which already exists. Nothing here overwrites "
					"or deletes", manifestPath, name, mv->to);
		}

		json_object_set_string(e, "path", newPath);

		{
			JSON_Value *srcVal = json_value_init_object();
			JSON_Object *src = json_value_get_object(srcVal);

			json_object_set_boolean(src, "self", 1);

			if (mv->newName[0]) {
				json_object_set_string(e, "name", mv->newName);
				json_object_set_string(src, "alias", mv->oldName);
			}

			json_object_set_value(e, "source", srcVal);
		}
	}

	/* Two entries landing on one name, or on a name the mod already uses, is a
	 * worse collision than the one being fixed, because it is inside one mod
	 * and the file ids would fight over a single row. */
	for (i = 0; i < n; ++i) {
		JSON_Object *a = json_array_get_object(files, i);
		const char *an = json_object_get_string(a, "name");
		const char *ap = json_object_get_string(a, "path");
		size_t j;

		for (j = i + 1; j < n; ++j) {
			JSON_Object *b = json_array_get_object(files, j);
			const char *bn = json_object_get_string(b, "name");
			const char *bp = json_object_get_string(b, "path");

			if (an && bn && !strcmp(an, bn)) {
				die("after slugging, entries %zu and %zu are both named '%s'", i, j, an);
			}

			if (ap && bp && !strcmp(ap, bp)) {
				die("after slugging, entries %zu and %zu are both at '%s'", i, j, ap);
			}
		}
	}

	json_object_set_string(manifest, "nameSlug", slug);

	printf("%s: slug '%s'\n", modName, slug);
	printf("  %zu entries: %zu renamed and re-named, %zu renamed with the name kept, "
			"%zu left alone\n", n, numNamed, numKept, numSkipped);

	{
		size_t longest = 0;
		const char *which = "";

		for (i = 0; i < numMoves; ++i) {
			const char *b = strrchr(moves[i].to, '/');
			b = b ? b + 1 : moves[i].to;

			if (strlen(b) > longest) {
				longest = strlen(b);
				which = b;
			}
		}

		if (longest) {
			printf("  longest resulting basename: %zu bytes, '%s'\n", longest, which);
		}
	}

	if (!apply) {
		for (i = 0; i < numMoves && i < 6; ++i) {
			printf("  move  %s\n     -> %s%s%s\n", moves[i].from, moves[i].to,
					moves[i].newName[0] ? "   name -> " : "", moves[i].newName);
		}

		if (numMoves > 6) {
			printf("  ... and %zu more\n", numMoves - 6);
		}

		printf("  nothing written; pass --apply\n");
		return 0;
	}

	for (i = 0; i < numMoves; ++i) {
		if (rename(moves[i].from, moves[i].to)) {
			die("could not move %s to %s (%zu of %zu done; the manifest is untouched)",
					moves[i].from, moves[i].to, i, numMoves);
		}
	}

	if (json_serialize_to_file_pretty(manifestVal, manifestPath) != JSONSuccess) {
		die("moved %zu files but could not write %s", numMoves, manifestPath);
	}

	/* modconfig.txt names files by NAME, not by id: bgfile, tilesfile,
	 * padsfile, setupfile, mpsetupfile, filenum, handfilenum and ModelStates'
	 * File all go through modConfigParseFileValue() ->
	 * romdataFileGetNumForNameInMod(). A renamed entry that is still referred
	 * to by its old name resolves to nothing and the stage declaration is
	 * dropped, so the rewrite is not optional.
	 *
	 * Only the contents of double-quoted strings are touched, and only when
	 * the basename matches a name this run changed - so the credit blocks,
	 * the comments and modname itself are left as they are. A '#' runs to the
	 * end of its line and is copied out whole: the three stage modconfigs
	 * reproduce their upstream revision histories verbatim, and those quote
	 * model names the same mod ships - mod_gex_stages' own
	 * `# Added the model "Pdd_grateZ"` would otherwise be edited into a line
	 * the upstream author never wrote. */
	{
		FILE *f = fopen(modconfigPath, "rb");
		char *buf;
		long len;
		size_t hits = 0;

		if (!f) {
			printf("  no modconfig.txt at %s; nothing to rewrite\n", modconfigPath);
			return 0;
		}

		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = malloc((size_t)len + 1);
		if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
			die("could not read %s", modconfigPath);
		}
		buf[len] = '\0';
		fclose(f);

		f = fopen(modconfigPath, "wb");

		if (!f) {
			die("moved %zu files and wrote the manifest, but could not rewrite %s",
					numMoves, modconfigPath);
		}

		{
			char *p = buf;

			while (*p) {
				char *open = strchr(p, '"');
				char *hash = strchr(p, '#');
				char *close;
				char val[PATHMAX];
				const char *vbase;
				char vdir[PATHMAX];
				size_t vlen, k;
				bool done = false;

				/* A comment before the next quote: copy the rest of that line
				 * out untouched. A '#' inside a quoted value is not reached,
				 * because the quote pair is consumed first. */
				if (hash && (!open || hash < open)) {
					char *nl = strchr(hash, '\n');
					size_t n = nl ? (size_t)(nl - p) + 1 : strlen(p);

					fwrite(p, 1, n, f);
					p += n;
					continue;
				}

				close = open ? strchr(open + 1, '"') : NULL;

				if (!open || !close) {
					fwrite(p, 1, strlen(p), f);
					break;
				}

				fwrite(p, 1, (size_t)(close - p) + 1, f);
				vlen = (size_t)(close - open) - 1;

				if (vlen && vlen < sizeof(val)) {
					memcpy(val, open + 1, vlen);
					val[vlen] = '\0';
					splitPath(val, vdir, sizeof(vdir), &vbase);

					for (k = 0; k < numMoves; ++k) {
						if (!moves[k].newName[0] || strcmp(moves[k].oldName, vbase)) {
							continue;
						}

						/* Back up over the value we just wrote and put the new
						 * one in its place, keeping any directory prefix. */
						fseek(f, -(long)vlen - 1, SEEK_CUR);

						if (vdir[0]) {
							fprintf(f, "%s/%s\"", vdir, moves[k].newName);
						} else {
							fprintf(f, "%s\"", moves[k].newName);
						}

						++hits;
						done = true;
						break;
					}
				}

				(void)done;
				p = close + 1;
			}
		}

		fclose(f);
		free(buf);
		printf("  rewrote %zu file reference%s in modconfig.txt\n", hits, hits == 1 ? "" : "s");
	}

	printf("  moved %zu files and rewrote %s\n", numMoves, manifestPath);
	printf("  now rebuild: mkfiletable %s --workspace %s --output %s\n",
			modName, workspace, output);
	return 0;
}

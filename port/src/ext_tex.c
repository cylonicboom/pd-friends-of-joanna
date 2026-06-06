#include <dirent.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#include "gbiex.h"
#include "types.h"

#include "system.h"
#include "fs.h"
#include "data.h"
#include "romdata.h"
#include "ext_tex.h"

#define EXT_TEX_DIRNAME "ext_tex"
#define FONT_OUTLINES_DIR "outlines"

static char extTexPath[FS_MAXPATH + 1];

#define MAX_EXT_TEX 8192
#define NUM_FONTS 5
const u16 IDMASK_FONT_OUTLINE = MASK_FONT_OUTLINE << 8;


struct ExtTexture
{
	u8 *texdata;
	s32 texnum;
	u16 width;
	u16 height;
	char extension[5];
	s8 ownerMod;
};

struct ModelTextures
{
	s16 fileNum;
	s16 numTextures;
	struct ExtTexture *textures;
	char basePath[FS_MAXPATH + 1];
	char modelName[64];
};

static struct ExtTexture extTextures[MAX_EXT_TEX];
static s32 g_ExtTexCurrentModIndex = -1; // set during extTexScanDir for readModelTextures

static struct ModelTextures *modelTextures;
static s32 numModels;

#if VERSION == VERSION_PAL_FINAL
#define NCHARS 135
#else
#define NCHARS 94
#endif

static struct ExtTexture fontExtTextures[NUM_FONTS][NCHARS];
static struct ExtTexture fontOutlineExtTextures[NUM_FONTS][NCHARS];

#define FONT_HANDELGOTHICSM 0
#define FONT_HANDELGOTHICMD 1
#define FONT_HANDELGOTHICXS 2
#define FONT_HANDELGOTHICLG 3
#define FONT_NUMERIC 4

s32 fileInfo(const char *filename, s32 *texNum, char extension[5])
{
	char *ext = strrchr(filename, '.');

	// no extension
	if (!ext) return 1;

	++ext;
	strncpy(extension, ext, 5);

	// get the filename without extension
	char basename[16] = { 0 };
	memcpy(basename, filename, strlen(filename) - strlen(ext) - 1);

	*texNum = strtol(basename, NULL, 16);

	return 0;
}

struct ExtTexture *lookupModelTex(u16 fileNum, s32 texNum)
{
	if (fileNum > NUM_FILES) {
		sysLogPrintf(LOG_WARNING, "lookupModelTex: INVALID fileNum %04x > NUM_FILES %d, texNum: %04x", fileNum, NUM_FILES, texNum);
		return 0;
	}

	struct ModelTextures *modelTex = NULL;
	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum == fileNum) {
			modelTex = &modelTextures[i];
			break;
		}
	}

	if (modelTex == NULL) {
		// sysLogPrintf(LOG_NOTE, "lookupModelTex: NO model entry for fileNum=%04x (texNum=%04x), numModels=%d", fileNum, texNum, numModels);
		if (numModels > 0) {
			for (int i = 0; i < numModels; ++i) {
				// sysLogPrintf(LOG_NOTE, "  modelTextures[%d]: fileNum=%04x modelName=%s basePath=%s numTex=%d",
				//	i, modelTextures[i].fileNum, modelTextures[i].modelName, modelTextures[i].basePath, modelTextures[i].numTextures);
			}
		}
		return NULL;
	}

	for (int i = 0; i < modelTex->numTextures; ++i) {
		if (modelTex->textures[i].texnum == texNum)
			return &modelTex->textures[i];
	}

	// sysLogPrintf(LOG_NOTE, "lookupModelTex: model fileNum=%04x (%s) found but texNum=%04x not in %d textures",
	//	fileNum, modelTex->modelName, texNum, modelTex->numTextures);
	return NULL;
}

struct ExtTexture *getExtTexture(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *texlist;
	switch (type) {
		case G_TEXTYPE_NONE:
			return NULL;
		case G_TEXTYPE_GENERAL:
			return &extTextures[texnum];
		case G_TEXTYPE_MODEL:
			return lookupModelTex(id, texnum);
		case G_TEXTYPE_FONT: {
			if (id & IDMASK_FONT_OUTLINE)
				return &fontOutlineExtTextures[id & ~IDMASK_FONT_OUTLINE][texnum];

			return &fontExtTextures[id][texnum];
		}
		default:
			sysLogPrintf(LOG_WARNING, "Invalid Texture type: %d, texnum: %04x", type, texnum);
			return NULL;
	}
}

u8 extTexExists(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	u8 exists = tex && tex->texnum >= 0;
	if (type == G_TEXTYPE_MODEL) {
		// sysLogPrintf(LOG_NOTE, "extTexExists: type=MODEL id=%04x texnum=%04x => %s", id, texnum, exists ? "YES" : "NO");
	}
	return exists;
}

s8 extTexGetOwnerMod(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	if (tex && tex->texnum >= 0) {
		return tex->ownerMod;
	}
	return -1;
}

u8 extTexGetDimensions(u8 type, u16 id, s32 texnum, u16 *width, u16 *height)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	if (tex && tex->texnum >= 0 && tex->width > 0 && tex->height > 0) {
		*width = tex->width;
		*height = tex->height;
		return 1;
	}
	return 0;
}

char *resolveFontname(const u8 fontId)
{
	switch (fontId) {
		case FONT_HANDELGOTHICSM: return "fonthandelgothicsm";
		case FONT_HANDELGOTHICMD: return "fonthandelgothicmd";
		case FONT_HANDELGOTHICXS: return "fonthandelgothicxs";
		case FONT_HANDELGOTHICLG: return "fonthandelgothiclg";
		case FONT_NUMERIC: return "fontnumeric";
		default: return "";
	}
}

u8 getTexPath(char *dst, u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex;
	const char *name;

	switch (type) {
		case G_TEXTYPE_GENERAL: {
			tex = &extTextures[texnum];
			// Check if this texture exists in a model subdirectory
			// (head models use GENERAL type but textures may be in model dirs)
			for (int i = 0; i < numModels; ++i) {
				for (int j = 0; j < modelTextures[i].numTextures; ++j) {
					if (modelTextures[i].textures[j].texnum == texnum) {
						snprintf(dst, FS_MAXPATH, "%s/%s/%04x.%s",
							modelTextures[i].basePath, modelTextures[i].modelName,
							texnum, modelTextures[i].textures[j].extension);
						return 0;
					}
				}
			}
			snprintf(dst, FS_MAXPATH, "%s/%04x.%s", extTexPath, texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_FONT: {
			name = resolveFontname(id & ~IDMASK_FONT_OUTLINE);

			if (id & IDMASK_FONT_OUTLINE) {
				tex = &fontOutlineExtTextures[id & ~IDMASK_FONT_OUTLINE][texnum];
				snprintf(dst, FS_MAXPATH, "%s/%s/" FONT_OUTLINES_DIR "/%02x.%s", extTexPath, name, texnum, tex->extension);
				return 0;
			}

			tex = &fontExtTextures[id][texnum];
			snprintf(dst, FS_MAXPATH, "%s/%s/%02x.%s", extTexPath, name, texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_MODEL: {
			tex = lookupModelTex(id, texnum);
			if (!tex) return 1;
			// Find the ModelTextures entry to get the basePath
			for (int i = 0; i < numModels; ++i) {
				if (modelTextures[i].fileNum == id) {
					snprintf(dst, FS_MAXPATH, "%s/%s/%05x.%s", modelTextures[i].basePath, modelTextures[i].modelName, texnum, tex->extension);
					return 0;
				}
			}
			return 1;
		}
		default: return 1;
	}
}

u8 *extTexLoad(u8 type, u16 id, s32 texnum, u32 *width, u32 *height)
{
	char path[FS_MAXPATH];
	u8 err = getTexPath(path, type, id, texnum);
	if (err) {
		sysLogPrintf(LOG_WARNING, "extTexLoad: getTexPath FAILED type=%d id=%04x texnum=%04x", type, id, texnum);
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "extTexLoad: loading type=%d id=%04x texnum=%04x path='%s'", type, id, texnum, path);

	struct ExtTexture *tex = getExtTexture(type, id, texnum);

	if (!tex) {
		sysLogPrintf(LOG_WARNING, "Unable to load texture: %05x", texnum);
		return NULL;
	}

	u32 channels;
	stbi_set_flip_vertically_on_load(1);
	tex->texdata = stbi_load(path, width, height, &channels, 4);
	stbi_set_flip_vertically_on_load(0);
	return tex->texdata;
}

u8 extTexFontID(struct font *font) {
	if (font == g_FontHandelGothicSm)
		return FONT_HANDELGOTHICSM;
	else if (font == g_FontHandelGothicMd)
		return FONT_HANDELGOTHICMD;
	else if (font == g_FontHandelGothicXs)
		return FONT_HANDELGOTHICXS;
	else if (font == g_FontHandelGothicLg)
		return FONT_HANDELGOTHICLG;
	else if (font == g_FontNumeric)
		return FONT_NUMERIC;

	return 0xff;
}

u8 resolveFontID(const char *fontname)
{
	if (strcmp(fontname, "fonthandelgothicsm") == 0)
		return FONT_HANDELGOTHICSM;
	else if (strcmp(fontname, "fonthandelgothicmd") == 0)
		return FONT_HANDELGOTHICMD;
	else if (strcmp(fontname, "fonthandelgothicxs") == 0)
		return FONT_HANDELGOTHICXS;
	else if (strcmp(fontname, "fonthandelgothiclg") == 0)
		return FONT_HANDELGOTHICLG;
	else if (strcmp(fontname, "fontnumeric") == 0)
		return FONT_NUMERIC;

	return 0xff;
}

void setTex(struct ExtTexture *texlist, s32 index, s32 texNum, char extension[5])
{
	struct ExtTexture *tex = &texlist[index];
	tex->texnum = texNum;
	strcpy(tex->extension, extension);
}

void setTexDimensions(struct ExtTexture *tex, const char *filepath)
{
	int w = 0, h = 0, comp = 0;
	if (stbi_info(filepath, &w, &h, &comp)) {
		tex->width = (u16)w;
		tex->height = (u16)h;
	}
}

void readModelTextures(const char *path, s16 fileNum, s32 *modelOffset, struct ModelTextures *modelTex)
{
	sysLogPrintf(LOG_NOTE, "readModelTextures: path=%s fileNum=%04x", path, (u16)fileNum);
	DIR *dr = opendir(path);
	struct dirent *de;

	s32 MAX_TEX = 16;
	modelTex->textures = sysMemAlloc(MAX_TEX * sizeof(struct ExtTexture));
	modelTex->numTextures = 0;
	modelTex->fileNum = fileNum;

	// Store the parent directory path for loading textures later
	char *lastSlash = strrchr(path, '/');
	if (lastSlash) {
		size_t dirLen = lastSlash - path;
		memcpy(modelTex->basePath, path, dirLen);
		modelTex->basePath[dirLen] = '\0';
		strncpy(modelTex->modelName, lastSlash + 1, sizeof(modelTex->modelName) - 1);
		modelTex->modelName[sizeof(modelTex->modelName) - 1] = '\0';
	} else {
		strncpy(modelTex->basePath, path, FS_MAXPATH);
		modelTex->modelName[0] = '\0';
	}

	char extension[5] = { 0 };

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		setTex(modelTex->textures, modelTex->numTextures, texNum, extension);

		// Read PNG dimensions from file header
		char texFilePath[FS_MAXPATH + 1];
		snprintf(texFilePath, sizeof(texFilePath), "%s/%s", path, name);
		setTexDimensions(&modelTex->textures[modelTex->numTextures], texFilePath);

		modelTex->numTextures++;

		// Also register as a general texture so head models (which use
		// G_TEXTYPE_GENERAL via texWriteLoadToTmemAddr) can find them
		if (texNum >= 0 && texNum < MAX_EXT_TEX) {
			setTex(extTextures, texNum, texNum, extension);
			extTextures[texNum].width = modelTex->textures[modelTex->numTextures - 1].width;
			extTextures[texNum].height = modelTex->textures[modelTex->numTextures - 1].height;
			sysLogPrintf(LOG_NOTE, "readModelTextures: also registered texnum=%04x as GENERAL (%dx%d)", texNum,
				extTextures[texNum].width, extTextures[texNum].height);
		}

		// allocate more memory for model textures if needed
		if (modelTex->numTextures > MAX_TEX) {
			MAX_TEX *= 2;
			modelTex->textures = sysMemRealloc(modelTex->textures, MAX_TEX * sizeof(struct ExtTexture));
		}
	}
	closedir(dr);

	// shrink the textures array to the actual number of textures found
	s32 numTex = modelTex->numTextures;

	if (numTex > 0)
		modelTex->textures = sysMemRealloc(modelTex->textures, numTex * sizeof(struct ExtTexture));

	for (int i = 0; i < modelTex->numTextures; ++i) {
		modelTex->textures[i].texdata = 0;
	}

	sysLogPrintf(LOG_NOTE, "readModelTextures: DONE path=%s fileNum=%04x basePath=%s modelName=%s numTextures=%d",
		path, (u16)fileNum, modelTex->basePath, modelTex->modelName, modelTex->numTextures);
	for (int i = 0; i < modelTex->numTextures; ++i) {
		sysLogPrintf(LOG_NOTE, "  tex[%d]: texnum=%04x ext=%s", i, modelTex->textures[i].texnum, modelTex->textures[i].extension);
	}
}

void readFontTextures(const char *path, const char *fontName)
{
	DIR *dr = opendir(path);
	struct dirent *de;

	u8 fontID = resolveFontID(fontName);
	char extension[5] = { 0 };

	char outlinesPath[FS_MAXPATH];
	sprintf(outlinesPath , "%s/" FONT_OUTLINES_DIR, path);
	u8 outlines = false;

	while (true) {
		de = readdir(dr);
		// after done processing the font folder, do the same for the outlines folder if any
		if (de == NULL) {
			if (outlines) break;

			outlines = true;
			closedir(dr);
			dr = opendir(outlinesPath);
			de = readdir(dr);

			if (de == NULL) break;
		}

		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		if (outlines)
			setTex(fontOutlineExtTextures[fontID], texNum, texNum, extension);
		else
			setTex(fontExtTextures[fontID], texNum, texNum, extension);
	}

	closedir(dr);
}

void extTexFree()
{
	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		if (extTextures[i].texdata)
			stbi_image_free(extTextures[i].texdata);

		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			if (fontExtTextures[i][j].texdata)
				stbi_image_free(fontExtTextures[i][j].texdata);

			if (fontOutlineExtTextures[i][j].texdata)
				stbi_image_free(fontOutlineExtTextures[i][j].texdata);

			fontExtTextures[i][j].texdata = 0;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	for (int i = 0; i < numModels; ++i) {
		struct ModelTextures *modelTex = &modelTextures[i];
		for (int j = 0; j < modelTex->numTextures; ++j) {
			if (modelTex->textures[j].texdata)
				stbi_image_free(modelTex->textures[j].texdata);

			modelTex->textures[j].texdata = 0;
		}
	}
}

static void extTexScanDir(const char *dirPath, s32 *maxModels)
{
	sysLogPrintf(LOG_NOTE, "extTexScanDir: scanning '%s'", dirPath);
	char buf[FS_MAXPATH];
	strncpy(buf, fsFullPath(dirPath), FS_MAXPATH);
	DIR *dr = opendir(buf);
	if (!dr) {
		sysLogPrintf(LOG_NOTE, "extTexScanDir: FAILED to open '%s'", dirPath);
		return;
	}

	char filepath[FS_MAXPATH];
	struct dirent *de;
	s32 entryCount = 0;

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		entryCount++;
		struct stat stbuf;
		snprintf(filepath, sizeof(filepath), "%s/%s", dirPath, name);
		char *buf = sysMemAlloc(FS_MAXPATH);
		strncpy(buf, fsFullPath(filepath), FS_MAXPATH);
		strncpy(filepath, buf, FS_MAXPATH);
		if (stat(buf, &stbuf) == -1) {
			sysLogPrintf(LOG_WARNING, "extTexScanDir: stat failed: %s", filepath);
			continue;
		}

		if (S_ISDIR(stbuf.st_mode)) {
			char s = name[0];
			sysLogPrintf(LOG_NOTE, "extTexScanDir: found dir '%s' (first char='%c')", name, s);
			if (s == 'P' || s == 'C' || s == 'G') {
				s16 fileNum = (s16)romdataFileGetNumForNameAnyMod(name);
				sysLogPrintf(LOG_NOTE, "extTexScanDir: model dir '%s' => fileNum=%d (0x%04x)", name, fileNum, (u16)fileNum);
				if (fileNum < 0) {
					sysLogPrintf(LOG_WARNING, "extTexScanDir: REJECTED '%s' — not in any mod's file table", name);
					continue;
				}

				struct ModelTextures *modelTex = &modelTextures[numModels++];
				readModelTextures(filepath, fileNum, NULL, modelTex);

				if (numModels > *maxModels) {
					*maxModels *= 2;
					modelTextures = sysMemRealloc(modelTextures, *maxModels * sizeof(struct ModelTextures));
				}
			} else if (s == 'f') {
				sysLogPrintf(LOG_NOTE, "extTexScanDir: font dir '%s'", name);
				readFontTextures(filepath, name);
			} else {
				sysLogPrintf(LOG_NOTE, "extTexScanDir: skipping unknown dir '%s'", name);
			}
		} else {
			s32 texNum = 0;
			char extension[5] = { 0 };
			s32 err = fileInfo(name, &texNum, extension);
			if (err) continue;
			setTex(extTextures, texNum, texNum, extension);
			setTexDimensions(&extTextures[texNum], filepath);
			sysLogPrintf(LOG_NOTE, "extTexScanDir: general texture '%s' => texNum=%04x (%dx%d)",
				name, texNum, extTextures[texNum].width, extTextures[texNum].height);
		}
	}

	closedir(dr);
	sysLogPrintf(LOG_NOTE, "extTexScanDir: DONE '%s' — %d entries processed, numModels now %d", dirPath, entryCount, numModels);
}

s32 extTexInit()
{
	const char *path = fsFullPath(EXT_TEX_DIRNAME);
	strcpy(extTexPath, path);

	sysLogPrintf(LOG_NOTE, "extTexInit: global extTexPath='%s'", extTexPath);
	sysLogPrintf(LOG_NOTE, "extTexInit: g_NumModDirs=%d", g_NumModDirs);
	for (u32 i = 0; i <= g_NumModDirs; ++i) {
		sysLogPrintf(LOG_NOTE, "extTexInit: modDirs[%d]='%s'", i, modDirs[i]);
	}

	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		extTextures[i].texnum = -1;
		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			fontExtTextures[i][j].texnum = -1;
			fontExtTextures[i][j].texdata = 0;

			fontOutlineExtTextures[i][j].texnum = -1;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	s32 MAX_MODELS = 16;
	numModels = 0;
	modelTextures = sysMemAlloc(MAX_MODELS * sizeof(struct ModelTextures));

	// Scan the global ext_tex directory (basedir/ext_tex/)
	sysLogPrintf(LOG_NOTE, "extTexInit: scanning global ext_tex dir...");
	extTexScanDir(extTexPath, &MAX_MODELS);

	// Scan each mod's ext_tex directory (mods/mod_xxx/ext_tex/)
	sysLogPrintf(LOG_NOTE, "extTexInit: scanning mod ext_tex dirs...");
	for (u32 i = 0; i <= g_NumModDirs; ++i) {
		if (modDirs[i][0]) {
			char modExtTexPath[FS_MAXPATH + 1];
			snprintf(modExtTexPath, FS_MAXPATH, "%s/" EXT_TEX_DIRNAME, modDirs[i]);
			sysLogPrintf(LOG_NOTE, "extTexInit: mod[%d] ext_tex path='%s'", i, modExtTexPath);
			extTexScanDir(modExtTexPath, &MAX_MODELS);
		}
	}

	sysLogPrintf(LOG_NOTE, "extTexInit: FINAL numModels=%d", numModels);
	s32 totalTextures = 0;
	for (int i = 0; i < numModels; ++i) {
		sysLogPrintf(LOG_NOTE, "  model[%d]: fileNum=%04x name=%s basePath=%s numTex=%d",
			i, (u16)modelTextures[i].fileNum, modelTextures[i].modelName, modelTextures[i].basePath, modelTextures[i].numTextures);
		totalTextures += modelTextures[i].numTextures;
	}

	// Count general textures
	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		if (extTextures[i].texnum >= 0) {
			totalTextures++;
		}
	}

	// shrink this array to the actual number of model folders found
	if (numModels > 0)
		modelTextures = sysMemRealloc(modelTextures, numModels * sizeof(struct ModelTextures));

	sysLogPrintf(LOG_NOTE, "extTexInit: total ext textures found: %d", totalTextures);
	return totalTextures;
}

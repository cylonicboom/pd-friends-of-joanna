// Skin match panel: the in-engine editor for the companion masks and head tag
// points that port/src/skinmatch.c measures and fast3d's SHADER_OPT_SKINMATCH
// consumes. The chr standing in the world is the preview - painting writes
// into the same mask the shader samples and marks it dirty, so the next frame
// shows it. Nothing here bakes a texture; save writes the sidecars next to
// the model's overrides in the owning ext_tex directory.
//
// Brushes: the two built-in discs, plus any PNG in <basedir>/brushes/ -
// luminance (or alpha, when the image has one) is the stamp weight, so a
// black-on-transparent or white-on-black shape both work. The brush is
// scaled to the size slider in mask texels and stamped with the strength.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <dirent.h>

#include "../fast3d/glad/glad.h"
#include "../fast3d/gfx_pc.h"
#include "bss.h"
#include "data.h"
// types.h defines bool as s32 for the C side; ImGui wants the real thing
#undef bool
#undef true
#undef false
#include "ext_tex.h"
#include "fs.h"
#include "romdata.h"
#include "system.h"
#include "skinmatch.h"
#include "external/stb_image.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_skinmatch.h"

enum {
	TOOL_PAINT,
	TOOL_ERASE,
	TOOL_FILL,
	TOOL_UNFILL,
};

struct SkinBrush {
	std::string name;
	s32 width;
	s32 height;
	std::vector<float> weight; // 0..1 per brush texel
};

static int g_SkinTool = TOOL_PAINT;
static int g_SkinClass = 0; // 0 skin, 1..4 garment layer
static int g_SkinBrushSize = 3; // diameter in mask texels
static float g_SkinBrushStrength = 1.0f;
static int g_SkinBrushChoice = 0;
static int g_SkinFillTol = 14; // percent, Oklab distance
static int g_SkinZoom = 6;
static int g_SkinNewMaskScale = 1; // 0: 1x, 1: 2x, 2: 4x
static float g_SkinOverlayAlpha = 0.55f;
static int g_SkinTagRadius = 1;
static bool g_SkinShowOverlay = true;
static s32 g_SkinBodyTexChoice = 0;
static s32 g_SkinHeadTexChoice = 0;
static bool g_SkinPainting = false;
static char g_SkinStatus[256] = "";
static std::vector<SkinBrush> g_SkinBrushes;
static bool g_SkinBrushesScanned = false;

// A texture the renderer submitted this frame, with its dimensions read back
// from GL. The debug texture list is rebuilt each frame by gfx_start_frame,
// so this is always this frame's binding.
struct SkinTexRef {
	GfxTextureDebugInfo info;
	s32 width;
	s32 height;
};

static void skinTexDims(GLuint textureId, s32 *width, s32 *height)
{
	GLint previous = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, height);
	glBindTexture(GL_TEXTURE_2D, previous);
}

static void skinCollectModelTextures(u16 fileNum, std::vector<SkinTexRef> &out)
{
	out.clear();
	const u32 count = gfx_get_debug_texture_count();

	for (u32 i = 0; i < count; i++) {
		SkinTexRef ref;

		if (!gfx_get_debug_texture(i, &ref.info)) {
			continue;
		}

		// tex.c stamps model texture loads as G_TEXTYPE_GENERAL with the
		// model's raw fileNum in id; match on the id, whatever the type
		if (ref.info.type == G_TEXTYPE_NONE || ref.info.id != fileNum) {
			continue;
		}

		skinTexDims(ref.info.texture_id, &ref.width, &ref.height);

		if (ref.width <= 0 || ref.height <= 0 || ref.width > 512 || ref.height > 512) {
			continue;
		}

		bool seen = false;
		for (const SkinTexRef &have : out) {
			if (have.info.texnum == ref.info.texnum) {
				seen = true;
				break;
			}
		}

		if (!seen) {
			out.push_back(ref);
		}
	}
}

// Read the texture back so the entry can be measured without waiting for the
// renderer's next import - the only path a freshly created blank entry has.
static bool skinReadbackPixels(const SkinTexRef &ref, std::vector<u8> &pixels)
{
	GLint previous = 0, pack = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
	glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
	pixels.resize((size_t)ref.width * ref.height * 4);
	glBindTexture(GL_TEXTURE_2D, ref.info.texture_id);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glPixelStorei(GL_PACK_ALIGNMENT, pack);
	glBindTexture(GL_TEXTURE_2D, previous);
	return true;
}

static void skinEnsureBodyPixels(struct skinmatchbody *body, const SkinTexRef &ref)
{
	if (body->rgba) {
		return;
	}

	std::vector<u8> px;
	skinReadbackPixels(ref, px);
	skinmatchOnTexturePixels(body->type, body->id, body->texnum, px.data(), ref.width, ref.height);
}

static void skinEnsureHeadPixels(struct skinmatchhead *head, const SkinTexRef &ref)
{
	if (head->rgba) {
		return;
	}

	std::vector<u8> px;
	skinReadbackPixels(ref, px);
	skinmatchOnTexturePixels(G_TEXTYPE_GENERAL, head->id, head->texnum, px.data(), ref.width, ref.height);
}

// ------------------------------------------------------------------ brushes

static void skinAddDiscBrush(const char *name, bool soft)
{
	SkinBrush b;
	b.name = name;
	b.width = b.height = 33;
	b.weight.resize(33 * 33);
	for (s32 y = 0; y < 33; y++) {
		for (s32 x = 0; x < 33; x++) {
			const float d = sqrtf((float)((x - 16) * (x - 16) + (y - 16) * (y - 16))) / 16.0f;
			float w = d >= 1.0f ? 0.0f : soft ? (1.0f - d * d) : 1.0f;
			b.weight[y * 33 + x] = w;
		}
	}
	g_SkinBrushes.push_back(b);
}

static void skinScanBrushes(void)
{
	g_SkinBrushes.clear();
	skinAddDiscBrush("disc, soft", true);
	skinAddDiscBrush("disc, hard", false);

	char dirpath[FS_MAXPATH + 1];
	snprintf(dirpath, sizeof(dirpath), "%s", fsFullPath("brushes"));
	DIR *dr = opendir(dirpath);
	if (!dr) {
		g_SkinBrushesScanned = true;
		return;
	}

	struct dirent *de;
	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		const size_t len = strlen(name);
		if (name[0] == '.' || len < 5 || strcmp(name + len - 4, ".png") != 0) continue;

		char path[FS_MAXPATH + 1];
		snprintf(path, sizeof(path), "%s/%s", dirpath, name);
		int w = 0, h = 0, ch = 0;
		stbi_set_flip_vertically_on_load(0);
		u8 *px = stbi_load(path, &w, &h, &ch, 4);
		if (!px) {
			sysLogPrintf(LOG_WARNING, "skinmatch: cannot read brush %s", path);
			continue;
		}
		if (w > 256 || h > 256) {
			sysLogPrintf(LOG_WARNING, "skinmatch: brush %s is %dx%d, max 256 - skipped", path, w, h);
			stbi_image_free(px);
			continue;
		}

		// alpha is the weight when the file carries one; otherwise luminance
		bool hasAlpha = false;
		for (int i = 0; i < w * h && !hasAlpha; i++) hasAlpha = px[i * 4 + 3] != 255;

		SkinBrush b;
		b.name = std::string(name, len - 4);
		b.width = w;
		b.height = h;
		b.weight.resize((size_t)w * h);
		for (int i = 0; i < w * h; i++) {
			b.weight[i] = hasAlpha ? px[i * 4 + 3] / 255.0f
				: (0.2126f * px[i * 4] + 0.7152f * px[i * 4 + 1] + 0.0722f * px[i * 4 + 2]) / 255.0f;
		}
		stbi_image_free(px);
		g_SkinBrushes.push_back(b);
	}
	closedir(dr);
	g_SkinBrushesScanned = true;
}

// ------------------------------------------------------------------ painting

static ImU32 skinLayerColour(s32 layer, float alpha)
{
	switch (layer) {
	case 1: return IM_COL32(79, 216, 255, (int)(alpha * 255));
	case 2: return IM_COL32(255, 210, 79, (int)(alpha * 255));
	case 3: return IM_COL32(140, 255, 79, (int)(alpha * 255));
	case 4: return IM_COL32(255, 140, 79, (int)(alpha * 255));
	}

	return IM_COL32(255, 79, 216, (int)(alpha * 255));
}

// Apply a weighted stamp to one mask texel. Skin weights accumulate (max, so
// re-stroking never darkens); garment ids are binary and take the texel once
// the weight passes half.
static void skinStampTexel(struct skinmatchbody *body, u32 i, float w, bool on)
{
	u8 *r = &body->mask[i * 2];
	u8 *g = &body->mask[i * 2 + 1];
	const u8 v = (u8)(w * 255.0f + 0.5f);

	if (g_SkinClass == 0) {
		if (on) {
			if (v > *r) *r = v;
			if (v > 127) *g = 0;
		} else {
			const s32 nv = (s32)*r - v;
			*r = nv < 0 ? 0 : (u8)nv;
		}
	} else if (w >= 0.5f) {
		if (on) {
			*g = (u8)g_SkinClass;
			*r = 0;
		} else if (*g == g_SkinClass) {
			*g = 0;
		}
	}
}

static void skinBrush(struct skinmatchbody *body, s32 cx, s32 cy, bool on)
{
	if (g_SkinBrushChoice < 0 || g_SkinBrushChoice >= (s32)g_SkinBrushes.size()) g_SkinBrushChoice = 0;
	const SkinBrush &b = g_SkinBrushes[g_SkinBrushChoice];
	const s32 size = g_SkinBrushSize < 1 ? 1 : g_SkinBrushSize;
	// scale the brush's longer side to `size` mask texels, nearest sampled
	const s32 longest = b.width > b.height ? b.width : b.height;
	const s32 sw = (b.width * size + longest / 2) / longest > 0 ? (b.width * size + longest / 2) / longest : 1;
	const s32 sh = (b.height * size + longest / 2) / longest > 0 ? (b.height * size + longest / 2) / longest : 1;

	for (s32 y = 0; y < sh; y++) {
		for (s32 x = 0; x < sw; x++) {
			const s32 mx = cx - sw / 2 + x, my = cy - sh / 2 + y;
			if (mx < 0 || my < 0 || mx >= body->width || my >= body->height) continue;
			// sample the centre of each scaled cell: at size 1 the old corner
			// sample landed on the disc's empty corner and stamped nothing
			const s32 bx = ((2 * x + 1) * b.width) / (2 * sw), by = ((2 * y + 1) * b.height) / (2 * sh);
			const float w = b.weight[by * b.width + bx] * g_SkinBrushStrength;
			if (w <= 0.0f) continue;
			skinStampTexel(body, (u32)my * body->width + mx, w, on);
		}
	}

	body->maskdirty = 1;
}

static void skinSetTexBlock(struct skinmatchbody *body, u32 tx, u32 ty, bool on)
{
	const u32 k = body->maskscale ? body->maskscale : 1;
	for (u32 y = ty * k; y < ty * k + k; y++) {
		for (u32 x = tx * k; x < tx * k + k; x++) {
			skinStampTexel(body, y * body->width + x, 1.0f, on);
		}
	}
}

// Flood fill on Oklab distance over texture texels, hue-weighted so it
// follows skin across its shading but stops at cloth. Never crosses into a
// texel another class owns - the skin fill leaked from the hands into the
// tights otherwise; the brush can. Each admitted texture texel takes its
// whole block of mask texels.
static void skinFill(struct skinmatchbody *body, s32 mx, s32 my, bool on)
{
	if (!body->rgba) return;

	const u32 k = body->maskscale ? body->maskscale : 1;
	const u32 W = body->width / k, H = body->height / k;
	const float tol = g_SkinFillTol / 100.0f;
	std::vector<u8> seen(W * H, 0);
	std::vector<u32> stack;
	f32 seed[3];
	const u32 s = (u32)(my / k) * W + (mx / k);
	skinmatchSrgbToOklab(body->rgba[s * 4], body->rgba[s * 4 + 1], body->rgba[s * 4 + 2], seed);
	stack.push_back(s);

	while (!stack.empty()) {
		u32 i = stack.back();
		stack.pop_back();
		if (seen[i]) continue;
		seen[i] = 1;

		f32 lab[3];
		skinmatchSrgbToOklab(body->rgba[i * 4], body->rgba[i * 4 + 1], body->rgba[i * 4 + 2], lab);
		const float dL = lab[0] - seed[0], da = lab[1] - seed[1], db = lab[2] - seed[2];
		if (sqrtf(dL * dL * 0.6f + da * da * 4.0f + db * db * 4.0f) > tol) continue;

		const u32 x = i % W, y = i / W;
		const u32 mi = ((y * k + k / 2) * body->width + (x * k + k / 2));
		const u8 r = body->mask[mi * 2], g = body->mask[mi * 2 + 1];
		if (on && ((g_SkinClass == 0 && g) || (g_SkinClass > 0 && r > 0 && g != g_SkinClass))) continue;

		skinSetTexBlock(body, x, y, on);
		if (x > 0) stack.push_back(i - 1);
		if (x < W - 1) stack.push_back(i + 1);
		if (y > 0) stack.push_back(i - W);
		if (y < H - 1) stack.push_back(i + W);
	}

	body->maskdirty = 1;
}

static void skinSwatch(const char *label, const f32 lab[3])
{
	u8 rgb[3];
	skinmatchOklabToSrgb(lab, rgb);
	ImGui::ColorButton(label, ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f),
		ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(26.0f, 16.0f));
	ImGui::SameLine();
	ImGui::Text("%s L %.3f %.3f %.3f", label, lab[0], lab[1], lab[2]);
}

// Where this model's sidecars go: beside its overrides when it has an ext_tex
// entry, otherwise a fresh <basedir>/ext_tex/<ModelName>/ directory.
static bool skinModelDir(u16 fileNum, char *dst, u32 len)
{
	if (extTexModelDir((s16)fileNum, dst, len) == 0) {
		return true;
	}

	const char *name = romdataFileGetName(fileNum);

	if (!name || !name[0]) {
		return false;
	}

	char rel[FS_MAXPATH + 1];
	snprintf(rel, sizeof(rel), "ext_tex/%s", name);
	fsCreateDir("ext_tex");
	fsCreateDir(rel);
	snprintf(dst, len, "%s", fsFullPath(rel));
	return true;
}

// The texture is drawn under an InvisibleButton rather than as an Image: an
// Image is not an interactive item, so click-dragging over it dragged the
// window instead of painting. The button owns the mouse for the whole stroke,
// so IsItemActive() stays true even when the cursor runs off the texture.
// Displayed upright: GL row 0 is the bottom of the picture, so screen rows
// count down from height - 1 (same convention as the textures panel's flip).
// Coordinates come back in GRID space - `grid` cells per texture texel - so
// a 2x/4x mask paints at its own resolution.
static s32 g_SkinLastX = -1, g_SkinLastY = -1;

static void skinDrawTexture(const char *id, const SkinTexRef &ref, s32 grid, s32 *hoverX, s32 *hoverY, bool *hovered, bool *clicked, bool *held)
{
	const ImVec2 size((float)ref.width * g_SkinZoom, (float)ref.height * g_SkinZoom);
	const ImVec2 minp = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft);
	ImGui::GetWindowDrawList()->AddImage(ImTextureRef((ImTextureID)ref.info.texture_id), minp,
		ImVec2(minp.x + size.x, minp.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
	*hovered = ImGui::IsItemHovered();
	*clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	*held = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

	if (*hovered || *held) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const float cell = (float)g_SkinZoom / grid;
		const s32 gw = ref.width * grid, gh = ref.height * grid;
		s32 col = (s32)floorf((mouse.x - minp.x) / cell);
		s32 row = (s32)floorf((mouse.y - minp.y) / cell);
		if (col < 0) col = 0;
		if (row < 0) row = 0;
		if (col >= gw) col = gw - 1;
		if (row >= gh) row = gh - 1;
		*hoverX = col;
		*hoverY = gh - 1 - row;
	}
}

static ImVec2 skinCellScreenMin(u32 x, u32 y, u32 gridHeight, float cell)
{
	const ImVec2 minp = ImGui::GetItemRectMin();
	return ImVec2(minp.x + x * cell, minp.y + (gridHeight - 1 - y) * cell);
}

static void skinDrawMaskOverlay(const struct skinmatchbody *body)
{
	if (!g_SkinShowOverlay || !body || !body->mask) return;

	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float cell = (float)g_SkinZoom / (body->maskscale ? body->maskscale : 1);

	for (u32 y = 0; y < body->height; y++) {
		for (u32 x = 0; x < body->width; x++) {
			const u32 i = y * body->width + x;
			const u8 r = body->mask[i * 2], g = body->mask[i * 2 + 1];
			if (!r && !g) continue;
			const ImU32 col = g ? skinLayerColour(g, g_SkinOverlayAlpha)
				: skinLayerColour(0, g_SkinOverlayAlpha * r / 255.0f);
			const ImVec2 a = skinCellScreenMin(x, y, body->height, cell);
			dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), col);
		}
	}
}

static void skinDrawTagOverlay(const struct skinmatchhead *head)
{
	if (!head) return;

	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float z = (float)g_SkinZoom;

	for (s32 i = 0; i < head->ntags; i++) {
		const struct skinmatchtag *t = &head->tags[i];
		const ImVec2 a = skinCellScreenMin(t->x, t->y, head->height ? head->height : 1, z);
		const ImVec2 c(a.x + 0.5f * z, a.y + 0.5f * z);
		dl->AddCircle(c, (t->r + 0.5f) * z, IM_COL32(255, 255, 255, 255), 0, 1.5f);
		char label[8];
		snprintf(label, sizeof(label), "%d", i + 1);
		dl->AddText(ImVec2(c.x + 4.0f, c.y - 14.0f), IM_COL32(255, 255, 255, 255), label);
	}
}

static void skinDrawBrushPreview(void)
{
	if (g_SkinBrushChoice < 0 || g_SkinBrushChoice >= (s32)g_SkinBrushes.size()) return;
	const SkinBrush &b = g_SkinBrushes[g_SkinBrushChoice];
	const float px = 48.0f / (b.width > b.height ? b.width : b.height);
	const ImVec2 minp = ImGui::GetCursorScreenPos();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(minp, ImVec2(minp.x + b.width * px, minp.y + b.height * px), IM_COL32(20, 20, 24, 255));
	for (s32 y = 0; y < b.height; y++) {
		for (s32 x = 0; x < b.width; x++) {
			const float w = b.weight[y * b.width + x];
			if (w <= 0.0f) continue;
			dl->AddRectFilled(ImVec2(minp.x + x * px, minp.y + y * px), ImVec2(minp.x + (x + 1) * px, minp.y + (y + 1) * px),
				IM_COL32(255, 255, 255, (int)(w * 255)));
		}
	}
	ImGui::Dummy(ImVec2(b.width * px, b.height * px));
}

void imguiSkinMatchDrawPanel(struct chrdata *chr)
{
	if (!g_SkinBrushesScanned) {
		skinScanBrushes();
	}

	bool enabled = g_SkinMatchEnabled != 0;
	if (ImGui::Checkbox("Video.SkinMatch", &enabled)) {
		g_SkinMatchEnabled = enabled ? 1 : 0;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("sidecars registered: %d", skinmatchNumSidecars());

	if (!chr || chr->chrnum < 0) {
		ImGui::TextDisabled("right-click a chr in Entities -> \"Skin match this chr\" to edit its body and head");
		return;
	}

	const s32 bodynum = chr->bodynum;
	const s32 headnum = chr->headnum;
	const bool bodyOk = skinmatchBodyEligible(bodynum);
	const bool headOk = headnum >= 0 && skinmatchHeadEligible(headnum);
	const u16 bodyFile = bodynum >= 0 ? (u16)(g_HeadsAndBodies[bodynum].filenum & 0xffff) : 0;
	const u16 headFile = headnum >= 0 ? (u16)(g_HeadsAndBodies[headnum].filenum & 0xffff) : 0;
	const char *bodyName = bodyFile ? romdataFileGetName(bodyFile) : NULL;
	const char *headName = headFile ? romdataFileGetName(headFile) : NULL;

	ImGui::Text("chr %d  body 0x%02x %s  head 0x%02x %s", chr->chrnum,
		bodynum, bodyName ? bodyName : "?", headnum, headName ? headName : "?");

	if (!bodyOk || !headOk) {
		ImGui::TextColored(ImVec4(0.88f, 0.35f, 0.35f, 1.0f), "denied: %s%s%s - on the engine denylist, no sidecar is loaded and none can be saved",
			bodyOk ? "" : "body", (!bodyOk && !headOk) ? " and " : "", headOk ? "" : "head");
		return;
	}

	std::vector<SkinTexRef> bodyTex, headTex;
	skinCollectModelTextures(bodyFile, bodyTex);
	skinCollectModelTextures(headFile, headTex);

	if (bodyTex.empty()) {
		ImGui::TextDisabled("body model 0x%04x drew no textures this frame - it has to be on screen", bodyFile);
		return;
	}

	if (g_SkinBodyTexChoice >= (s32)bodyTex.size()) g_SkinBodyTexChoice = 0;
	if (g_SkinHeadTexChoice >= (s32)headTex.size()) g_SkinHeadTexChoice = 0;

	ImGui::SetNextItemWidth(90.0f);
	ImGui::SliderInt("zoom", &g_SkinZoom, 1, 16, "%dx");
	ImGui::SameLine();
	ImGui::Checkbox("overlay", &g_SkinShowOverlay);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::SliderFloat("##ovalpha", &g_SkinOverlayAlpha, 0.0f, 1.0f, "%.2f");

	ImGui::Separator();

	struct skinmatchbody *body = NULL;
	struct skinmatchhead *head = NULL;

	if (ImGui::BeginTable("skinmatch cols", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableNextRow();

		// ------------------------------------------------------------ body
		ImGui::TableSetColumnIndex(0);
		ImGui::SeparatorText("body");

		if (bodyTex.size() > 1) {
			char preview[32];
			snprintf(preview, sizeof(preview), "tex %04x", bodyTex[g_SkinBodyTexChoice].info.texnum);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::BeginCombo("##bodytex", preview)) {
				for (s32 i = 0; i < (s32)bodyTex.size(); i++) {
					char label[48];
					const struct skinmatchbody *b = skinmatchBodyFor(G_TEXTYPE_GENERAL, bodyFile, bodyTex[i].info.texnum, false);
					snprintf(label, sizeof(label), "tex %04x  %dx%d%s", bodyTex[i].info.texnum, bodyTex[i].width, bodyTex[i].height, b ? "  masked" : "");
					if (ImGui::Selectable(label, i == g_SkinBodyTexChoice)) g_SkinBodyTexChoice = i;
				}
				ImGui::EndCombo();
			}
		}

		const SkinTexRef &bref = bodyTex[g_SkinBodyTexChoice];
		body = skinmatchBodyFor(G_TEXTYPE_GENERAL, bodyFile, bref.info.texnum, true);

		if (!body) {
			ImGui::TextDisabled("tex %04x  %dx%d  no mask yet", bref.info.texnum, bref.width, bref.height);
			ImGui::SetNextItemWidth(70.0f);
			const char *scales[] = { "1x", "2x", "4x" };
			ImGui::Combo("mask subdivision", &g_SkinNewMaskScale, scales, 3);
			ImGui::SameLine();
			if (ImGui::Button("start a mask for this texture")) {
				const u8 scale = g_SkinNewMaskScale == 2 ? 4 : g_SkinNewMaskScale == 1 ? 2 : 1;
				body = skinmatchBodyCreateBlank(G_TEXTYPE_GENERAL, bodyFile, bref.info.texnum, (u16)bref.width, (u16)bref.height, scale);
			}
		}

		if (body) {
			skinEnsureBodyPixels(body, bref);
			const s32 grid = body->maskscale ? body->maskscale : 1;

			ImGui::SetNextItemWidth(110.0f);
			const char *classes[] = { "skin", "tights L1", "tights L2", "tights L3", "tights L4" };
			ImGui::Combo("class", &g_SkinClass, classes, 5);
			ImGui::SameLine();
			ImGui::RadioButton("paint", &g_SkinTool, TOOL_PAINT); ImGui::SameLine();
			ImGui::RadioButton("erase", &g_SkinTool, TOOL_ERASE); ImGui::SameLine();
			ImGui::RadioButton("fill", &g_SkinTool, TOOL_FILL); ImGui::SameLine();
			ImGui::RadioButton("unfill", &g_SkinTool, TOOL_UNFILL);

			// brush row
			if (g_SkinBrushChoice >= (s32)g_SkinBrushes.size()) g_SkinBrushChoice = 0;
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::BeginCombo("brush", g_SkinBrushes[g_SkinBrushChoice].name.c_str())) {
				for (s32 i = 0; i < (s32)g_SkinBrushes.size(); i++) {
					if (ImGui::Selectable(g_SkinBrushes[i].name.c_str(), i == g_SkinBrushChoice)) g_SkinBrushChoice = i;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("rescan brushes/")) skinScanBrushes();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("PNGs in %s\nalpha is the weight when the file has one, otherwise luminance", fsFullPath("brushes"));
			ImGui::SameLine();
			skinDrawBrushPreview();
			ImGui::SetNextItemWidth(100.0f);
			ImGui::SliderInt("size", &g_SkinBrushSize, 1, 64, "%d");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			ImGui::SliderFloat("strength##brush", &g_SkinBrushStrength, 0.05f, 1.0f, "%.2f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			ImGui::SliderInt("fill tol", &g_SkinFillTol, 1, 40);

			if (ImGui::SmallButton("feather")) {
				skinmatchBodyFeather(body);
				skinmatchBodyRemeasure(body);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("3x3 blur of the skin weight - run it a few times to soften a hard edge");
			ImGui::SameLine();
			if (ImGui::SmallButton("clear class")) {
				for (u32 i = 0; i < (u32)body->width * body->height; i++) {
					if (g_SkinClass == 0) body->mask[i * 2] = 0;
					else if (body->mask[i * 2 + 1] == g_SkinClass) body->mask[i * 2 + 1] = 0;
				}
				body->maskdirty = 1;
				skinmatchBodyRemeasure(body);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("mask %ux%u (%dx)", body->width, body->height, grid);

			s32 hx = 0, hy = 0;
			bool hovered, clicked, held;
			if (ImGui::BeginChild("body tex", ImVec2(0.0f, ImMin((float)bref.height * g_SkinZoom + 12.0f, 520.0f)),
					ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
				skinDrawTexture("body", bref, grid, &hx, &hy, &hovered, &clicked, &held);
				skinDrawMaskOverlay(body);

				if (hovered || held) {
					const u32 ti = skinmatchBodyTexIndex(body, hx, hy);
					const bool on = g_SkinTool == TOOL_PAINT || g_SkinTool == TOOL_FILL;
					if ((g_SkinTool == TOOL_FILL || g_SkinTool == TOOL_UNFILL) && clicked) {
						skinFill(body, hx, hy, on);
						skinmatchBodyRemeasure(body);
					} else if ((g_SkinTool == TOOL_PAINT || g_SkinTool == TOOL_ERASE) && held) {
						// stamp every texel between the last stamp and this one
						if (g_SkinLastX < 0) {
							g_SkinLastX = hx;
							g_SkinLastY = hy;
						}
						const s32 dx = hx - g_SkinLastX, dy = hy - g_SkinLastY;
						const s32 steps = ImMax(1, ImMax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy));
						for (s32 k = 1; k <= steps; k++) {
							skinBrush(body, g_SkinLastX + dx * k / steps, g_SkinLastY + dy * k / steps, on);
						}
						g_SkinLastX = hx;
						g_SkinLastY = hy;
						g_SkinPainting = true;
					}
					if (hovered && body->rgba) {
						f32 lab[3];
						const u32 mi = (u32)hy * body->width + hx;
						skinmatchSrgbToOklab(body->rgba[ti * 4], body->rgba[ti * 4 + 1], body->rgba[ti * 4 + 2], lab);
						ImGui::SetTooltip("texel (%d,%d) #%02x%02x%02x  L %.3f  mask (%d,%d) R %d G %d", hx / grid, hy / grid,
							body->rgba[ti * 4], body->rgba[ti * 4 + 1], body->rgba[ti * 4 + 2], lab[0],
							hx, hy, body->mask[mi * 2], body->mask[mi * 2 + 1]);
					}
				}
			}
			ImGui::EndChild();

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				g_SkinLastX = g_SkinLastY = -1;
				if (g_SkinPainting) {
					// remeasure once per stroke, not per texel
					g_SkinPainting = false;
					skinmatchBodyRemeasure(body);
				}
			}

			u32 garmentCount = 0;
			for (u32 i = 0; i < (u32)body->width * body->height; i++) if (body->mask[i * 2 + 1]) garmentCount++;
			ImGui::Text("bare skin %d texels · under garment %u · %s", body->nbare, garmentCount / (grid * grid),
				body->ready ? "measured" : body->pending ? "waiting for pixels" : "need >= 4 bare texels");
			if (body->ready) {
				skinSwatch("dark", body->dark);
				skinSwatch("light", body->light);
			}
			ImGui::TextDisabled("companion GL #%u %s", body->maskgl, body->maskdirty ? "(dirty, re-uploads next draw)" : "");

			// the two knobs on the effect itself; a change re-sends the uniforms
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::SliderFloat("strength##effect", &body->strength, 0.0f, 1.0f, "%.2f")) body->maskdirty = 1;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("how far masked texels move toward the head tone");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::SliderFloat("detail", &body->detail, 0.0f, 1.0f, "%.2f")) body->maskdirty = 1;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("how much of each texel's own deviation from the skin axis survives\n0 flattens to the axis, 1 keeps freckles, marks and shading exactly");

			ImGui::SeparatorText("garments · fitted from texel0");
			bool any = false;
			for (s32 L = 1; L <= SKINMATCH_MAX_LAYERS; L++) {
				const struct skinmatchgarment *g = &body->garment[L - 1];
				if (!g->ntexels) continue;
				any = true;
				ImGui::PushStyleColor(ImGuiCol_Text, skinLayerColour(L, 1.0f));
				ImGui::Text("L%d", L);
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (g->valid) {
					u8 tint[3] = { 0, 0, 0 };
					for (int c = 0; c < 3; c++) {
						// tint is linear; show it through the same sRGB curve the shader uses
						const f32 v = g->tint[c] <= 0.0031308f ? g->tint[c] * 12.92f : 1.055f * powf(g->tint[c], 1.0f / 2.4f) - 0.055f;
						tint[c] = (u8)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f);
					}
					ImGui::ColorButton("tint", ImVec4(tint[0] / 255.0f, tint[1] / 255.0f, tint[2] / 255.0f, 1.0f),
						ImGuiColorEditFlags_NoTooltip, ImVec2(26.0f, 16.0f));
					ImGui::SameLine();
					ImGui::Text("alpha %.0f%%  gain %.2f %.2f %.2f  %d texels%s%s", g->alpha * 100.0f,
						g->gain[0], g->gain[1], g->gain[2], g->ntexels,
						g->opaque ? "  OPAQUE - this is cloth, unclass it" : "",
						g->flat ? "  flat region, multiplicative fallback" : "");
				} else {
					ImGui::Text("%d texels - need >= 8 here and >= 8 bare", g->ntexels);
				}
				char id[32];
				snprintf(id, sizeof(id), "nudge opacity L%d", L);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::SliderFloat(id, &body->nudge[L - 1], -30.0f, 30.0f, "%.0f")) {
					skinmatchBodyRemeasure(body);
					body->maskdirty = 1;
				}
			}
			if (!any) {
				ImGui::TextDisabled("none - pick a tights class and fill a region of skin seen through something sheer");
			}
		}

		// ------------------------------------------------------------ head
		ImGui::TableSetColumnIndex(1);
		ImGui::SeparatorText("head");

		if (headTex.empty()) {
			ImGui::TextDisabled("head model 0x%04x drew no textures this frame", headFile);
		} else {
			if (headTex.size() > 1) {
				char preview[32];
				snprintf(preview, sizeof(preview), "tex %04x", headTex[g_SkinHeadTexChoice].info.texnum);
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::BeginCombo("##headtex", preview)) {
					for (s32 i = 0; i < (s32)headTex.size(); i++) {
						char label[48];
						snprintf(label, sizeof(label), "tex %04x  %dx%d%s", headTex[i].info.texnum, headTex[i].width, headTex[i].height,
							skinmatchHeadForTex(headFile, (s32)headTex[i].info.texnum) ? "  tagged" : "");
						if (ImGui::Selectable(label, i == g_SkinHeadTexChoice)) g_SkinHeadTexChoice = i;
					}
					ImGui::EndCombo();
				}
			}

			const SkinTexRef &href = headTex[g_SkinHeadTexChoice];
			head = skinmatchHeadFor(headFile, true);
			if (head && head->texnum != (s32)href.info.texnum) {
				ImGui::TextDisabled("tags live on tex %04x of this head; showing %04x", head->texnum, href.info.texnum);
				head = skinmatchHeadForTex(headFile, (s32)href.info.texnum);
			}
			if (!head) {
				ImGui::TextDisabled("tex %04x  %dx%d  no tags yet - click the face", href.info.texnum, href.width, href.height);
			}

			ImGui::SetNextItemWidth(100.0f);
			ImGui::SliderInt("tag radius", &g_SkinTagRadius, 0, 3);
			ImGui::SameLine();
			ImGui::TextDisabled("one tap = mid tone, two = dark/light");

			s32 hx = 0, hy = 0;
			bool hovered, clicked, held;
			if (ImGui::BeginChild("head tex", ImVec2(0.0f, ImMin((float)href.height * g_SkinZoom + 12.0f, 520.0f)),
					ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
				skinDrawTexture("head", href, 1, &hx, &hy, &hovered, &clicked, &held);
				skinDrawTagOverlay(head);

				if (hovered && clicked) {
					if (!head) {
						head = skinmatchHeadCreateBlank(headFile, (s32)href.info.texnum);
					}
					if (head && head->ntags < SKINMATCH_MAX_TAGS) {
						head->tags[head->ntags].x = (s16)hx;
						head->tags[head->ntags].y = (s16)hy;
						head->tags[head->ntags].r = (u8)g_SkinTagRadius;
						head->ntags++;
						skinEnsureHeadPixels(head, href);
						skinmatchHeadRemeasure(head);
					}
				}
			}
			ImGui::EndChild();

			if (head) {
				skinEnsureHeadPixels(head, href);
				for (s32 i = 0; i < head->ntags; i++) {
					ImGui::PushID(i);
					if (ImGui::SmallButton("x")) {
						memmove(&head->tags[i], &head->tags[i + 1], (head->ntags - i - 1) * sizeof(head->tags[0]));
						head->ntags--;
						skinmatchHeadRemeasure(head);
						ImGui::PopID();
						break;
					}
					ImGui::SameLine();
					ImGui::Text("%d  (%d,%d) r%d", i + 1, head->tags[i].x, head->tags[i].y, head->tags[i].r);
					ImGui::PopID();
				}
				if (head->ready) {
					skinSwatch("dark", head->dark);
					skinSwatch("light", head->light);
				}
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::SliderFloat("nudge dark L", &head->nudgedark, -20.0f, 20.0f, "%.0f")) skinmatchHeadRemeasure(head);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::SliderFloat("nudge light L", &head->nudgelight, -20.0f, 20.0f, "%.0f")) skinmatchHeadRemeasure(head);
				ImGui::TextDisabled("descriptor is re-sampled from the loaded head texture at these\npoints on every load; nudges are saved as offsets");
			}
		}

		ImGui::EndTable();
	}

	// ------------------------------------------------------------ save
	ImGui::Separator();
	if (body || head) {
		ImGui::Text("shader: %s", (body && body->ready && head && head->ready && g_SkinMatchEnabled)
			? "SHADER_OPT_SKINMATCH set for this pair"
			: !g_SkinMatchEnabled ? "setting off" : (!body || !body->ready) ? "waiting on a measured body mask" : "waiting on head tags");
	}
	if (body && ImGui::Button("save body sidecars")) {
		char dir[FS_MAXPATH + 1];
		if (skinModelDir(bodyFile, dir, sizeof(dir)) && skinmatchBodySave(body, dir) == 0) {
			snprintf(g_SkinStatus, sizeof(g_SkinStatus), "wrote %s/%04x.skin.png + .skin.json", dir, body->texnum);
		} else {
			snprintf(g_SkinStatus, sizeof(g_SkinStatus), "could not write the body sidecars (see log)");
		}
	}
	if (head) {
		if (body) ImGui::SameLine();
		if (ImGui::Button("save head tags")) {
			char dir[FS_MAXPATH + 1];
			if (skinModelDir(headFile, dir, sizeof(dir)) && skinmatchHeadSave(head, dir) == 0) {
				snprintf(g_SkinStatus, sizeof(g_SkinStatus), "wrote %s/%04x.skin.json", dir, head->texnum);
			} else {
				snprintf(g_SkinStatus, sizeof(g_SkinStatus), "could not write the head tags (see log)");
			}
		}
	}
	if (g_SkinStatus[0]) {
		ImGui::TextDisabled("%s", g_SkinStatus);
	}
}

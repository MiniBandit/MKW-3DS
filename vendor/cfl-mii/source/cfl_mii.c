
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "cfl_mii.h"
#include "vshader_shbin.h"

static FILE* s_debugLog = NULL;

void CFL_EnableSDDebug(bool enable)
{
	if (enable) {
		if (!s_debugLog) s_debugLog = fopen("sdmc:/3ds/cfl_test.txt", "w");
	} else if (s_debugLog) {
		fclose(s_debugLog);
		s_debugLog = NULL;
	}
}

void CFL_FlushSDDebug(void)
{
	if (s_debugLog) fflush(s_debugLog);
}

void dbglog(const char* fmt, ...)
{
	if (!s_debugLog) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(s_debugLog, fmt, ap);
	va_end(ap);
	fflush(s_debugLog);
}

void dbglogErr(const char* fmt, ...)
{
	va_list ap;
	if (s_debugLog) {
		va_start(ap, fmt);
		vfprintf(s_debugLog, fmt, ap);
		va_end(ap);
		fflush(s_debugLog);
	}
	va_start(ap, fmt);
	/* MKW: no console output over the lower LCD. */
	va_end(ap);
}

void dbglogVramStats(const char* context, bool onScreen)
{
	u32 free = vramSpaceFree();
	if (onScreen)
		dbglogErr("%s: VRAM free = %lu bytes (%.1f KB)\n", context, (unsigned long)free, free / 1024.0f);
	else
		dbglog("%s: VRAM free = %lu bytes (%.1f KB)\n", context, (unsigned long)free, free / 1024.0f);
}

#define CFL_SECTION_GOATEE      0
#define CFL_SECTION_CAP         1
#define CFL_SECTION_FACE        2
#define CFL_SECTION_FOREHEAD    3
#define CFL_SECTION_GLASSES     4
#define CFL_SECTION_HAIR        5
#define CFL_SECTION_MASK        6
#define CFL_SECTION_NLINE       7
#define CFL_SECTION_NOSE        8
#define CFL_SECTION_CAPTEX      9
#define CFL_SECTION_EYE         10
#define CFL_SECTION_EYEBROW     11
#define CFL_SECTION_FACET_BEARD 12
#define CFL_SECTION_FACET_LINE  13
#define CFL_SECTION_FACET_MAKE  14
#define CFL_SECTION_GLASSES_TEX 15
#define CFL_SECTION_MOLE        16
#define CFL_SECTION_MOUTH       17
#define CFL_SECTION_MUSTACHE    18
#define CFL_SECTION_NLINETEX    19

typedef struct {
	float* positions;
	float* normals;
	float* texcoords;
	u32 vertexCount;

	u8* indices;
	                     
	u32 indexCount;
} CFLModel;

typedef struct {
	u16 width, height;
	u8 format;
	u8 uWrap, vWrap;
	const u8* data;
	u32 dataSize;
} CFLTexture;

#define CFL_TEXFMT_I4     0
#define CFL_TEXFMT_I8     1
#define CFL_TEXFMT_A4     2
#define CFL_TEXFMT_A8     3
#define CFL_TEXFMT_IA4    4
#define CFL_TEXFMT_IA8    5
#define CFL_TEXFMT_RG8    6
#define CFL_TEXFMT_RGB565 7
#define CFL_TEXFMT_RGB8   8
#define CFL_TEXFMT_RGB5A1 9
#define CFL_TEXFMT_RGBA4  10
#define CFL_TEXFMT_RGBA8  11
#define CFL_TEXFMT_ETC1   12
#define CFL_TEXFMT_ETC1A4 13

typedef struct {
	float hair[3];
	float noseGlasses[3];
	float goatee[3];
} CFLFaceAnchors;

static u16 rd16(const u8* p) { u16 v; memcpy(&v, p, 2); return v; }
static u32 rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
static s16 rds16(const u8* p) { s16 v; memcpy(&v, p, 2); return v; }
static float rdf32(const u8* p) { float v; memcpy(&v, p, 4); return v; }

#define CFL_RES_MAX_SIZE 1083400
static u8* g_cflResBuffer;

static bool loadMiiResource(u32* outSize)
{
	// The test package supplies the same native resource as a loose file.
	// Keep the read-only system-title path as a hardware fallback.
	bool mounted = false;
	FILE* f = fopen("mii/CFL_Res.dat", "rb");
	if (!f) {
		Result rc = romfsMountFromTitle(0x0004009B00010202ULL, MEDIATYPE_NAND, "$CFLRES");
		if (R_FAILED(rc)) return false;
		mounted = true;
		f = fopen("$CFLRES:/CFL_Res.dat", "rb");
		if (!f) { romfsUnmount("$CFLRES"); return false; }
	}

	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	rewind(f);

	if (fileSize <= 0 || fileSize > CFL_RES_MAX_SIZE) {
		dbglogErr("CFL_Res.dat size (%ld) is invalid or exceeds the expected maximum (%u).\n",
			fileSize, (unsigned)CFL_RES_MAX_SIZE);
		fclose(f);
		if (mounted) romfsUnmount("$CFLRES");
		return false;
	}

	u8* buf = (u8*)malloc((size_t)fileSize);
	if (!buf) {
		dbglogErr("malloc(%ld) failed for the Mii resource buffer.\n", fileSize);
		fclose(f);
		if (mounted) romfsUnmount("$CFLRES");
		return false;
	}

	size_t got = fread(buf, 1, (size_t)fileSize, f);
	fclose(f);
	if (mounted) romfsUnmount("$CFLRES");

	if (got != (size_t)fileSize) {
		dbglogErr("Short read on CFL_Res.dat: got %zu of %ld bytes\n", got, fileSize);
		free(buf);
		return false;
	}

	dbglog("Loaded CFL_Res.dat: %ld bytes\n", fileSize);
	g_cflResBuffer = buf;
	*outSize = (u32)fileSize;
	return true;
}

static bool findItem(const u8* data, u32 size, u32 sectionIndex, u32 itemIndex,
	const u8** outItem, u32* outItemRemaining)
{
	if (size < 4 + 20 * 4 || sectionIndex >= 20) return false;
	u32 secOff = rd32(data + 0x04 + 4 * sectionIndex);
	if (secOff + 4 > size) {
		dbglog("CFL_Res.dat: section %lu offset out of bounds\n", (unsigned long)sectionIndex);
		return false;
	}

	const u8* sec = data + secOff;
	u16 itemCount = rd16(sec + 0x00);
	if (itemCount == 0) {
		dbglog("CFL_Res.dat: section %lu has 0 items\n", (unsigned long)sectionIndex);
		return false;
	}

	const u8* table = sec + 4;
	if (itemIndex >= itemCount) {
		dbglog("CFL_Res.dat: section %lu index %lu >= itemCount %lu, nothing here (not an error)\n",
			(unsigned long)sectionIndex, (unsigned long)itemIndex, (unsigned long)itemCount);
		return false;
	}

	if ((const u8*)(table + 4 * (u32)itemCount + 4) > data + size) {
		dbglog("CFL_Res.dat: section %lu table out of bounds\n", (unsigned long)sectionIndex);
		return false;
	}

	u32 entry = rd32(table + 4 * itemIndex);
	u32 redir = entry >> 22;
	bool wasRedirected = (redir != 0);
	u32 effectiveIndex = itemIndex;
	if (wasRedirected) {
		effectiveIndex = redir - 1;
		entry = rd32(table + 4 * effectiveIndex);
	}
	u32 itemOff = entry & 0x3FFFFF;

	u32 itemEnd = rd32(table + 4 * (effectiveIndex + 1)) & 0x3FFFFF;
	if (itemEnd <= itemOff) {
		dbglog("CFL_Res.dat: section %lu item %lu is empty (size=0), nothing here (not an error)\n",
			(unsigned long)sectionIndex, (unsigned long)itemIndex);
		return false;
	}
	u32 itemSize = itemEnd - itemOff;

	dbglog("CFL_Res.dat: section %lu itemCount=%lu used=%lu size=%lu%s\n",
		(unsigned long)sectionIndex, (unsigned long)itemCount,
		(unsigned long)itemIndex, (unsigned long)itemSize, wasRedirected ? " [redirected]" : "");

	const u8* sectionHeaderEnd = table + 4 * (u32)itemCount + 4;
	const u8* item = sectionHeaderEnd + itemOff;
	if (item < data || item + itemSize > data + size) {
		dbglog("CFL_Res.dat: section %lu item offset out of bounds\n", (unsigned long)sectionIndex);
		return false;
	}

	*outItem = item;
	*outItemRemaining = itemSize;
	return true;
}

static u32 itemPrefixSize(u32 sectionIndex)
{
	if (sectionIndex == CFL_SECTION_FACE) return 0x24;
	if (sectionIndex == CFL_SECTION_HAIR) return 0x48;
	return 0;
}

static bool geomHeaderValid(u16 C, u16 N, u16 T, u16 I)
{
	if (C < 3 || C > 4096) return false;
	if (!(N == 0 || N == 1 || N == C)) return false;
	if (!(T == 0 || T == 1 || T == C)) return false;
	if (I > 1) return false;
	return true;
}

static bool parseGeometry(const u8* geom, u32 remaining, CFLModel* out)
{
	if (remaining < 8) return false;

	u16 C = rd16(geom + 0);
	u16 N = rd16(geom + 2);
	u16 T = rd16(geom + 4);
	u16 I = rd16(geom + 6);

	dbglog("  geom header: C=%u N=%u T=%u I=%u\n", C, N, T, I);

	if (!geomHeaderValid(C, N, T, I)) return false;

	const u8* p = geom + 8;
	const u8* end = geom + remaining;

	u32 perVertexBytes = 6  + (N == C ? 6 : 0) + (T == C ? 4 : 0);
	if (p + (u64)perVertexBytes * C > end) return false;

	float* positions = malloc(sizeof(float) * 3 * C);
	float* normals = malloc(sizeof(float) * 3 * C);
	float* texcoords = (T != 0) ? malloc(sizeof(float) * 2 * C) : NULL;
	if (!positions || !normals || (T != 0 && !texcoords)) {
		free(positions); free(normals); free(texcoords);
		return false;
	}

	for (u32 i = 0; i < C; i++) {
		positions[i * 3 + 0] = rds16(p + 0) / 256.0f;
		positions[i * 3 + 1] = rds16(p + 2) / 256.0f;
		positions[i * 3 + 2] = rds16(p + 4) / 256.0f;
		p += 6;

		if (N == C) {
			normals[i * 3 + 0] = rds16(p + 0) / 256.0f;
			normals[i * 3 + 1] = rds16(p + 2) / 256.0f;
			normals[i * 3 + 2] = rds16(p + 4) / 256.0f;
			p += 6;
		} else {
			normals[i * 3 + 0] = 0.0f;
			normals[i * 3 + 1] = 0.0f;
			normals[i * 3 + 2] = 1.0f;
		}

		if (T == C) {
			texcoords[i * 2 + 0] = rds16(p + 0) / 8192.0f;
			texcoords[i * 2 + 1] = rds16(p + 2) / 8192.0f;
			p += 4;
		}
	}

	float commonNormal[3] = { 0.0f, 0.0f, 1.0f };
	if (N == 1) {
		if (p + 6 > end) { free(positions); free(normals); free(texcoords); return false; }
		commonNormal[0] = rds16(p + 0) / 256.0f;
		commonNormal[1] = rds16(p + 2) / 256.0f;
		commonNormal[2] = rds16(p + 4) / 256.0f;
		p += 6;
	}
	if (N != C) {
		for (u32 i = 0; i < C; i++) {
			normals[i * 3 + 0] = commonNormal[0];
			normals[i * 3 + 1] = commonNormal[1];
			normals[i * 3 + 2] = commonNormal[2];
		}
	}

	float commonTexcoord[2] = { 0.0f, 0.0f };
	if (T == 1) {
		if (p + 4 > end) { free(positions); free(normals); free(texcoords); return false; }
		commonTexcoord[0] = rds16(p + 0) / 8192.0f;
		commonTexcoord[1] = rds16(p + 2) / 8192.0f;
		p += 4;
	}
	if (T == 1) {
		for (u32 i = 0; i < C; i++) {
			texcoords[i * 2 + 0] = commonTexcoord[0];
			texcoords[i * 2 + 1] = commonTexcoord[1];
		}
	}

	u8* indices = NULL;
	u32 indexCount = 0;
	if (I == 1) {
		if (p + 4 > end) { free(positions); free(normals); free(texcoords); return false; }
		p += 2;
		u16 J = rd16(p); p += 2;
		if (p + J > end) { free(positions); free(normals); free(texcoords); return false; }
		indices = malloc(J);
		if (!indices) { free(positions); free(normals); free(texcoords); return false; }
		memcpy(indices, p, J);
		indexCount = J;

		for (u16 k = 0; k < indexCount; k++) {
			if (indices[k] >= C) {
				dbglog("  index %u >= vertex count %u, rejecting item\n", indices[k], C);
				free(positions); free(normals); free(texcoords); free(indices);
				return false;
			}
		}
	}

	out->positions = positions;
	out->normals = normals;
	out->texcoords = texcoords;
	out->vertexCount = C;
	out->indices = indices;
	out->indexCount = indexCount;
	return true;
}

static bool loadResModel(const u8* data, u32 size, u32 sectionIndex, u32 itemIndex,
	CFLFaceAnchors* outAnchors, CFLModel* out)
{
	memset(out, 0, sizeof(*out));
	if (outAnchors) memset(outAnchors, 0, sizeof(*outAnchors));

	const u8* item;
	u32 itemRemaining;
	if (!findItem(data, size, sectionIndex, itemIndex, &item, &itemRemaining))
		return false;

	dbglog("  item remaining=%lu bytes, first bytes:", (unsigned long)itemRemaining);
	for (u32 i = 0; i < 96 && i < itemRemaining; i++) {
		if (i % 16 == 0) dbglog("\n  ");
		dbglog("%02X ", item[i]);
	}
	dbglog("\n");

	if (sectionIndex == CFL_SECTION_FACE && outAnchors && itemRemaining >= 0x24) {
		outAnchors->hair[0] = rdf32(item + 0x00);
		outAnchors->hair[1] = rdf32(item + 0x04);
		outAnchors->hair[2] = rdf32(item + 0x08);
		outAnchors->noseGlasses[0] = rdf32(item + 0x0C);
		outAnchors->noseGlasses[1] = rdf32(item + 0x10);
		outAnchors->noseGlasses[2] = rdf32(item + 0x14);
		outAnchors->goatee[0] = rdf32(item + 0x18);
		outAnchors->goatee[1] = rdf32(item + 0x1C);
		outAnchors->goatee[2] = rdf32(item + 0x20);
	}

	u32 prefix = itemPrefixSize(sectionIndex);
	if (itemRemaining > prefix && parseGeometry(item + prefix, itemRemaining - prefix, out)) {
		
		if (sectionIndex == CFL_SECTION_MASK && out->texcoords && out->vertexCount > 0) {
			float uMin = out->texcoords[0], uMax = out->texcoords[0];
			float vMin = out->texcoords[1], vMax = out->texcoords[1];
			for (u32 i = 0; i < out->vertexCount; i++) {
				float u = out->texcoords[i * 2 + 0];
				float v = out->texcoords[i * 2 + 1];
				if (u < uMin) uMin = u;
				if (u > uMax) uMax = u;
				if (v < vMin) vMin = v;
				if (v > vMax) vMax = v;
			}
			dbglog("  MASK mesh real UV range: U=[%.4f, %.4f] V=[%.4f, %.4f] (buildFaceMask assumes 0..1 for both)\n",
				uMin, uMax, vMin, vMax);
		}
		return true;
	}

	dbglog("CFL_Res.dat: failed to parse geometry (section %lu, item %lu)\n",
		(unsigned long)sectionIndex, (unsigned long)itemIndex);
	return false;
}

static void freeResModel(CFLModel* model)
{
	free(model->positions);
	free(model->normals);
	free(model->texcoords);
	free(model->indices);
	memset(model, 0, sizeof(*model));
}

static bool loadResTexture(const u8* data, u32 size, u32 sectionIndex, u32 itemIndex, CFLTexture* out)
{
	memset(out, 0, sizeof(*out));

	const u8* item;
	u32 itemRemaining;
	if (!findItem(data, size, sectionIndex, itemIndex, &item, &itemRemaining))
		return false;

	if (itemRemaining < 8) {
		dbglog("CFL_Res.dat: section %lu texture item too small\n", (unsigned long)sectionIndex);
		return false;
	}

	u16 width = rd16(item + 0x00);
	u16 height = rd16(item + 0x02);
	u8 mipmapCount = item[0x04];
	u8 format = item[0x05];
	u8 uWrap = item[0x06];
	u8 vWrap = item[0x07];
	const u8* texData = item + 0x08;

	static const u8 bitsPerPixel[14] = {
		4, 8, 4, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8
	};
	if (format >= 14) {
		dbglog("CFL_Res.dat: section %lu unknown texture format %u\n", (unsigned long)sectionIndex, format);
		return false;
	}

	u32 mWidth = 1; while (mWidth < width) mWidth <<= 1;
	u32 mHeight = 1; while (mHeight < height) mHeight <<= 1;
	u32 dataSize = mWidth * mHeight * bitsPerPixel[format] / 8;

	dbglog("  texture: %ux%u (padded %lux%lu) format=%u mip=%u uwrap=%u vwrap=%u size=%lu\n",
		width, height, (unsigned long)mWidth, (unsigned long)mHeight, format, mipmapCount, uWrap, vWrap,
		(unsigned long)dataSize);

	if (texData + dataSize > data + size) {
		dbglog("CFL_Res.dat: section %lu texture data out of bounds\n", (unsigned long)sectionIndex);
		return false;
	}

	out->width = width;
	out->height = height;
	out->format = format;
	out->uWrap = uWrap;
	out->vWrap = vWrap;
	out->data = texData;
	out->dataSize = dataSize;
	return true;
}

typedef CFLVertex Vertex;

typedef CFLPart Part;

static const float skinColors[6][3] = {
	{ 1.000f, 0.827f, 0.678f },
	{ 1.000f, 0.714f, 0.420f },
	{ 0.870f, 0.475f, 0.259f },
	{ 1.000f, 0.667f, 0.549f },
	{ 0.678f, 0.318f, 0.161f },
	{ 0.388f, 0.173f, 0.094f },
};

static const float hairColors[8][3] = {
	{ 0.118f, 0.102f, 0.094f },
	{ 0.251f, 0.125f, 0.063f },
	{ 0.361f, 0.094f, 0.039f },
	{ 0.486f, 0.227f, 0.078f },
	{ 0.471f, 0.471f, 0.502f },
	{ 0.306f, 0.243f, 0.063f },
	{ 0.533f, 0.345f, 0.094f },
	{ 0.816f, 0.627f, 0.290f },
};

static const float favoriteColors[12][3] = {
	{ 0.824f, 0.118f, 0.078f },
	{ 1.000f, 0.431f, 0.098f },
	{ 1.000f, 0.847f, 0.125f },
	{ 0.471f, 0.824f, 0.125f },
	{ 0.000f, 0.471f, 0.188f },
	{ 0.039f, 0.282f, 0.706f },
	{ 0.235f, 0.667f, 0.871f },
	{ 0.961f, 0.353f, 0.490f },
	{ 0.451f, 0.157f, 0.678f },
	{ 0.282f, 0.220f, 0.094f },
	{ 0.878f, 0.878f, 0.878f },
	{ 0.094f, 0.094f, 0.078f },
};

const float* CFL_GetFavoriteColor(u8 index)
{
	if (index > 11) index = 11;
	return favoriteColors[index];
}

static const int cflToGpuFormat[14] = {
	GPU_L4, GPU_L8, GPU_A4, GPU_A8, GPU_LA4, GPU_LA8, GPU_HILO8,
	GPU_RGB565, GPU_RGB8, GPU_RGBA5551, GPU_RGBA4, GPU_RGBA8, GPU_ETC1, GPU_ETC1A4,
};
static const int cflToGpuWrap[3] = { GPU_CLAMP_TO_EDGE, GPU_REPEAT, GPU_MIRRORED_REPEAT };

static const float glassColors[8][3] = {
	{ 0.094f, 0.094f, 0.094f },
	{ 0.376f, 0.219f, 0.062f },
	{ 0.658f, 0.062f, 0.031f },
	{ 0.125f, 0.188f, 0.407f },
	{ 0.658f, 0.376f, 0.000f },
	{ 0.470f, 0.439f, 0.407f },
	{ 0.950f, 0.850f, 0.150f },
	{ 0.950f, 0.950f, 0.950f },
};

static const float eyeColors1[6][3] = {
	{ 0.000f, 0.000f, 0.000f },
	{ 0.424f, 0.439f, 0.439f },
	{ 0.400f, 0.235f, 0.173f },
	{ 0.376f, 0.369f, 0.188f },
	{ 0.275f, 0.329f, 0.659f },
	{ 0.220f, 0.439f, 0.345f },
};
static const float eyeColor0Default[3] = { 0.0f, 0.0f, 0.0f };
static const float eyeColor0Orange[3]  = { 255.0f / 255.0f, 130.0f / 255.0f, 0.0f / 255.0f };
static const float eyeColor0Cyan[3]    = { 0.0f / 255.0f,   255.0f / 255.0f, 255.0f / 255.0f };
static const float* getEyeColor0(u8 eyeType)
{
	if (eyeType == 9)  return eyeColor0Orange;
	if (eyeType == 20) return eyeColor0Cyan;
	return eyeColor0Default;
}

static const float mouthColors0[5][3] = {
	{ 0.847f, 0.322f, 0.031f },
	{ 0.941f, 0.047f, 0.031f },
	{ 0.961f, 0.282f, 0.282f },
	{ 0.941f, 0.604f, 0.455f },
	{ 0.549f, 0.314f, 0.251f },
};
static const float mouthColors1[5][3] = {
	{ 0.510f, 0.188f, 0.094f },
	{ 0.471f, 0.047f, 0.047f },
	{ 0.533f, 0.125f, 0.157f },
	{ 0.863f, 0.471f, 0.314f },
	{ 0.275f, 0.118f, 0.039f },
};

static const float moleColor[3] = { 0.0706f, 0.0588f, 0.0588f };

static const u8 eyeRotOffset[80] = {
	29, 28, 28, 28, 29, 28, 28, 28,
	29, 28, 28, 28, 28, 29, 29, 28,
	28, 28, 29, 29, 28, 29, 28, 29,
	29, 28, 29, 28, 28, 29, 28, 28,
	28, 29, 29, 29, 28, 28, 29, 29,
	29, 28, 28, 29, 29, 29, 29, 29,
	29, 29, 29, 29, 28, 28, 28, 28,
	29, 28, 28, 29, 28, 28, 28, 28,
	28, 28, 28, 28, 28, 28, 28, 28,
	28, 28, 28, 28, 28, 28, 28, 28,
};
static const u8 eyebrowRotOffset[28] = {
	26, 26, 27, 25, 26, 25, 26, 25,
	28, 25, 26, 24, 27, 27, 26, 26,
	25, 25, 26, 26, 27, 26, 25, 27,
	26, 26, 26, 26,
};

typedef struct { u8 eyeR, eyeL, mouth; s8 eyeRotDelta, eyebrowRotDelta, eyebrowYDelta; } CFLExpressionTypes;
static const CFLExpressionTypes kExpressionTypes[CFL_EXPRESSION_COUNT] = {
	 { 0, 0, 0,  0,  0,  0 },
	 { 1, 1, 0,  0,  0,  0 },
	 { 0, 0, 1,  2,  2,  0 },
	 { 2, 2, 2, -2, -2,  0 },
	 { 3, 3, 0,  0,  0, -2 },
	 { 4, 4, 0,  0,  0,  0 },
	 { 0, 0, 3,  0,  0,  0 },
	 { 1, 1, 3,  0,  0,  0 },
	 { 0, 0, 3,  2,  2,  0 },
	 { 2, 2, 3, -2, -2,  0 },
	 { 3, 3, 3,  0,  0, -2 },
	 { 4, 4, 3,  0,  0,  0 },
	
	 { 5, 0, 0,  0,  0,  0 },
	 { 0, 5, 0,  0,  0,  0 },
	 { 5, 0, 3,  0,  0,  0 },
	 { 0, 5, 3,  0,  0,  0 },
	 { 5, 0, 5,  0,  0,  0 },
	 { 0, 5, 5,  0,  0,  0 },
	
	 { 5, 5, 2,  0,  0,  0 },
};

static u32 eyeIndexForType(const MiiData* mii, u8 type)
{
	switch (type) {
		case 0: case 2: return mii->eye_details.style;
		case 1: return 60;
		case 3: return 61;
		case 4: return 26;
		case 5: return 47;
		default: return mii->eye_details.style;
	}
}

static u32 mouthIndexForType(const MiiData* mii, u8 type)
{
	switch (type) {
		case 0: return mii->mouth_details.style;
		case 1: return 10;
		case 2: return 12;
		case 3: return 36;
		case 5: return 19;
		default: return mii->mouth_details.style;
	}
}

static const char* kExpressionNames[CFL_EXPRESSION_COUNT] = {
	"Normal", "Smile", "Anger", "Sorrow", "Surprise", "Blink", "OpenMouth",
	"Smile+OM", "Anger+OM", "Sorrow+OM", "Surprise+OM", "Blink+OM",
	"WinkL", "WinkR", "WinkL+OM", "WinkR+OM", "LikeWinkL", "LikeWinkR", "Frustrated",
};

const char* CFL_GetExpressionName(CFLExpression expression)
{
	if ((unsigned)expression >= CFL_EXPRESSION_COUNT) return "?";
	return kExpressionNames[expression];
}

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection, uLoc_modelView;

static C3D_LightEnv s_bakeLightEnv;
static C3D_Light s_bakeLight;

static u32 snapResolution(u32 raw)
{
	if (raw == 256) return 256;
	if (raw < 257) {
		if (raw == 64 || raw == 96) return 64;
		if (raw == 128 || raw == 224) return 128;
	} else {
		if (raw == 480) return 256;
		if (raw == 512) return 512;
		if (raw == 1024) return 1024;
	}
	dbglog("CFL: invalid resolution %lu, falling back to 64 (matches CFL's own real fallback)\n", (unsigned long)raw);
	return 64;
}
static Vertex* buildVertices(const CFLModel* model, const float translate[3], float partScale, bool flipX, u32* outCount)
{
	*outCount = 0;
	Vertex* verts = linearAlloc(sizeof(Vertex) * model->vertexCount);
	if (!verts) return NULL;
	for (u32 i = 0; i < model->vertexCount; i++) {
		float px = model->positions[i * 3 + 0];
		float py = model->positions[i * 3 + 1];
		float pz = model->positions[i * 3 + 2];
		float nx = model->normals[i * 3 + 0];
		float ny = model->normals[i * 3 + 1];
		float nz = model->normals[i * 3 + 2];

		if (flipX) { px = -px; nx = -nx; }
		if (partScale != 1.0f) { px *= partScale; py *= partScale; pz *= partScale; }
		if (translate) {
			px += translate[0];
			py += translate[1];
			pz += translate[2];
		}

		verts[i].position[0] = px;
		verts[i].position[1] = py;
		verts[i].position[2] = pz;
		verts[i].normal[0] = nx;
		verts[i].normal[1] = ny;
		verts[i].normal[2] = nz;
		verts[i].texcoord[0] = model->texcoords ? model->texcoords[i * 2 + 0] : 0.0f;
		verts[i].texcoord[1] = model->texcoords ? model->texcoords[i * 2 + 1] : 0.0f;
	}
	*outCount = model->vertexCount;
	return verts;
}

static void addPart(CFLCharModel* cm, const CFLModel* model, const float translate[3], float partScale, bool flipX, const float color[3], bool noSpecular)
{
	if (cm->partCount >= CFL_MAX_PARTS || model->vertexCount == 0) return;
	Part* part = &cm->parts[cm->partCount++];

	u32 count;
	part->vbo = buildVertices(model, translate, partScale, flipX, &count);
	if (!part->vbo) { memset(part, 0, sizeof(*part)); cm->partCount--; return; }
	part->vertexCount = count;
	part->color[0] = color[0];
	part->color[1] = color[1];
	part->color[2] = color[2];
	part->hasTexture = false;
	part->needsTint = false;
	part->isAlphaOnly = false;
	part->depthWrite = true;
	part->noSpecular = noSpecular;
	
	part->capBlend = false;

	if (model->indices && model->indexCount >= 3) {
		part->ibo = linearAlloc(model->indexCount);
		if (!part->ibo) {
			linearFree(part->vbo);
			memset(part, 0, sizeof(*part));
			cm->partCount--;
			return;
		}
		memcpy(part->ibo, model->indices, model->indexCount);
		part->indexCount = model->indexCount;
		part->useIndices = true;
	} else {
		part->ibo = NULL;
		part->useIndices = false;
		part->vertexCount -= part->vertexCount % 3;
	}
}

static void logNormalLightStats(const char* label, const CFLModel* model)
{
	if (!model->normals || model->vertexCount == 0) {
		dbglog("  %s normal stats: (no normal data)\n", label);
		return;
	}
	static const float L[3] = { -0.53906f, 0.53906f, 0.64697f };
	float minD = 1.0f, maxD = 0.0f, sumD = 0.0f;
	for (u32 i = 0; i < model->vertexCount; i++) {
		float nx = model->normals[i*3+0], ny = model->normals[i*3+1], nz = model->normals[i*3+2];
		float len = sqrtf(nx*nx + ny*ny + nz*nz);
		if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
		float d = L[0]*nx + L[1]*ny + L[2]*nz;
		if (d < 0.0f) d = 0.0f;
		if (d < minD) minD = d;
		if (d > maxD) maxD = d;
		sumD += d;
	}
	dbglog("  %s normal stats (%lu verts): d min=%.3f avg=%.3f max=%.3f (lit factor = 0.40+0.60*d)\n",
		label, (unsigned long)model->vertexCount, minD, sumD / model->vertexCount, maxD);
}

static void loadPart(CFLCharModel* cm, const u8* cflData, u32 cflSize, u32 section, u32 itemIndex,
	const float translate[3], float partScale, bool flipX, const float color[3], bool noSpecular, const char* label)
{
	CFLModel model;
	if (!loadResModel(cflData, cflSize, section, itemIndex, NULL, &model)) {
		dbglog("(no %s at section %lu item %lu)\n", label, (unsigned long)section, (unsigned long)itemIndex);
		return;
	}
	dbglog("%s: %lu verts, %lu indices\n", label, (unsigned long)model.vertexCount, (unsigned long)model.indexCount);
	addPart(cm, &model, translate, partScale, flipX, color, noSpecular);
	freeResModel(&model);
}

static void loadTexturedPart(CFLCharModel* cm, const u8* cflData, u32 cflSize,
	u32 modelSection, u32 modelIndex, u32 texSection, u32 texIndex,
	const float translate[3], float partScale, bool flipX, const float tint[3],
	const float solidFallback[3], bool depthWrite, bool noSpecular, bool capBlend, const char* label)
{
	CFLModel model;
	if (!loadResModel(cflData, cflSize, modelSection, modelIndex, NULL, &model)) {
		dbglog("(no %s at section %lu item %lu)\n", label, (unsigned long)modelSection, (unsigned long)modelIndex);
		return;
	}

	CFLTexture tex;
	if (!loadResTexture(cflData, cflSize, texSection, texIndex, &tex)) {
		if (solidFallback) {
			dbglog("(no %s texture at section %lu item %lu, rendering solid-colored)\n",
				label, (unsigned long)texSection, (unsigned long)texIndex);
			addPart(cm, &model, translate, partScale, flipX, solidFallback, noSpecular);
		} else {
			dbglog("(no %s texture at section %lu item %lu, skipping canvas too)\n",
				label, (unsigned long)texSection, (unsigned long)texIndex);
		}
		freeResModel(&model);
		return;
	}

	if (cm->partCount >= CFL_MAX_PARTS || model.vertexCount == 0) {
		freeResModel(&model);
		return;
	}

	Part* part = &cm->parts[cm->partCount++];
	u32 count;
	part->vbo = buildVertices(&model, translate, partScale, flipX, &count);
	if (!part->vbo) {
		memset(part, 0, sizeof(*part));
		cm->partCount--;
		freeResModel(&model);
		return;
	}
	part->vertexCount = count;
	bool needsTint = tex.format < CFL_TEXFMT_RG8;
	
	float capHalvedTint[3];
	const float* effectiveTint = tint;
	if (capBlend && needsTint) {
		capHalvedTint[0] = tint[0] * 0.5f;
		capHalvedTint[1] = tint[1] * 0.5f;
		capHalvedTint[2] = tint[2] * 0.5f;
		effectiveTint = capHalvedTint;
	}
	part->color[0] = needsTint ? effectiveTint[0] : 1.0f;
	part->color[1] = needsTint ? effectiveTint[1] : 1.0f;
	part->color[2] = needsTint ? effectiveTint[2] : 1.0f;
	part->needsTint = needsTint;
	part->capBlend = capBlend && needsTint;
	part->isAlphaOnly = (tex.format == CFL_TEXFMT_A4 || tex.format == CFL_TEXFMT_A8);
	part->depthWrite = depthWrite;
	part->noSpecular = noSpecular;

	if (model.indices && model.indexCount >= 3) {
		part->ibo = linearAlloc(model.indexCount);
		if (!part->ibo) {
			linearFree(part->vbo);
			memset(part, 0, sizeof(*part));
			cm->partCount--;
			freeResModel(&model);
			return;
		}
		memcpy(part->ibo, model.indices, model.indexCount);
		part->indexCount = model.indexCount;
		part->useIndices = true;
	} else {
		part->ibo = NULL;
		part->useIndices = false;
		part->vertexCount -= part->vertexCount % 3;
	}
	freeResModel(&model);

	u32 mWidth = 1; while (mWidth < tex.width) mWidth <<= 1;
	u32 mHeight = 1; while (mHeight < tex.height) mHeight <<= 1;
	if (!C3D_TexInit(&part->tex, mWidth, mHeight, cflToGpuFormat[tex.format])) {
		dbglog("(C3D_TexInit failed for %s, rendering untextured)\n", label);
		part->hasTexture = false;
		return;
	}
	C3D_TexUpload(&part->tex, tex.data);
	
	C3D_TexFlush(&part->tex);
	C3D_TexSetFilter(&part->tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&part->tex, cflToGpuWrap[tex.uWrap < 3 ? tex.uWrap : 0], cflToGpuWrap[tex.vWrap < 3 ? tex.vWrap : 0]);
	part->hasTexture = true;
	dbglog("%s: textured, %ux%u (padded %lux%lu)\n", label, tex.width, tex.height, (unsigned long)mWidth, (unsigned long)mHeight);
}

static void clearParts(CFLCharModel* cm)
{
	for (int i = 0; i < cm->partCount; i++) {
		linearFree(cm->parts[i].vbo);
		if (cm->parts[i].ibo) linearFree(cm->parts[i].ibo);
		if (cm->parts[i].hasTexture && i != cm->maskPartIndex) C3D_TexDelete(&cm->parts[i].tex);
	}
	cm->partCount = 0;
	cm->maskPartIndex = -1;
	for (int i = 0; i < CFL_EXPRESSION_COUNT; i++) {
		if (cm->maskTexBaked[i]) {
			C3D_TexDelete(&cm->maskTexForExpr[i]);
			cm->maskTexBaked[i] = false;
		}
	}
}

static int addTexturedPart(CFLCharModel* cm, const CFLModel* model, const float translate[3], float partScale, bool flipX, C3D_Tex* tex, bool depthWrite)
{
	if (cm->partCount >= CFL_MAX_PARTS || model->vertexCount == 0) return -1;
	int idx = cm->partCount;
	Part* part = &cm->parts[cm->partCount++];
	u32 count;
	part->vbo = buildVertices(model, translate, partScale, flipX, &count);
	if (!part->vbo) { memset(part, 0, sizeof(*part)); cm->partCount--; return -1; }
	part->vertexCount = count;
	part->color[0] = part->color[1] = part->color[2] = 1.0f;
	part->needsTint = false;
	part->isAlphaOnly = false;
	part->depthWrite = depthWrite;
	part->noSpecular = false;
	part->capBlend = false;
	if (model->indices && model->indexCount >= 3) {
		part->ibo = linearAlloc(model->indexCount);
		if (!part->ibo) {
			linearFree(part->vbo);
			memset(part, 0, sizeof(*part));
			cm->partCount--;
			return -1;
		}
		memcpy(part->ibo, model->indices, model->indexCount);
		part->indexCount = model->indexCount;
		part->useIndices = true;
	} else {
		part->ibo = NULL;
		part->useIndices = false;
		part->vertexCount -= part->vertexCount % 3;
	}
	part->tex = *tex;
	part->hasTexture = true;
	return idx;
}

typedef enum { MASK_ORIGIN_CENTER, MASK_ORIGIN_LEFT, MASK_ORIGIN_RIGHT } MaskOrigin;
typedef struct { float pos[2]; float scale[2]; float rot; MaskOrigin origin; } MaskPartsDesc;

static void buildMaskQuad(const MaskPartsDesc* d, Vertex outVerts[4], bool flipV)
{
	static const float texScaleX = 0.88961464f;
	static const float texScaleY = 0.9276675f;
	static const float localXPre[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	static const float localY[4]    = { -0.5f, 0.5f, 0.5f, -0.5f };
	static const float texVArr[4]   = { 1.0f, 0.0f, 0.0f, 1.0f };
	static const float texVArrFlipped[4] = { 0.0f, 1.0f, 1.0f, 0.0f };

	float originOffsetX = (d->origin == MASK_ORIGIN_CENTER) ? -0.5f : (d->origin == MASK_ORIGIN_LEFT) ? 0.0f : -1.0f;
	float rot = d->rot * (float)M_PI / 180.0f;
	float c = cosf(rot), s = sinf(rot);
	const float* vArr = flipV ? texVArrFlipped : texVArr;

	for (int i = 0; i < 4; i++) {
		float lx = localXPre[i] + originOffsetX;
		float ly = localY[i];
		float sx = lx * d->scale[0];
		float sy = ly * d->scale[1];
		float rx = sx * c - sy * s;
		float ry = sx * s + sy * c;
		float wx = d->pos[0] + rx * texScaleX;
		float wy = d->pos[1] + ry * texScaleY;

		outVerts[i].position[0] = wx / 32.0f - 1.0f;
		outVerts[i].position[1] = 1.0f - wy / 32.0f;
		outVerts[i].position[2] = 0.0f;
		outVerts[i].normal[0] = 0.0f; outVerts[i].normal[1] = 0.0f; outVerts[i].normal[2] = 1.0f;
		outVerts[i].texcoord[0] = (d->origin == MASK_ORIGIN_LEFT) ? (1.0f - localXPre[i]) : localXPre[i];
		outVerts[i].texcoord[1] = vArr[i];
	}
}

static void uploadMaterialColor(const float color[3])
{
	
	C3D_Material mtl;
	mtl.ambient[0] = color[2]; mtl.ambient[1] = color[1]; mtl.ambient[2] = color[0];
	mtl.diffuse[0] = mtl.diffuse[1] = mtl.diffuse[2] = 0.0f;
	mtl.specular0[0] = mtl.specular0[1] = mtl.specular0[2] = 0.0f;
	mtl.specular1[0] = mtl.specular1[1] = mtl.specular1[2] = 0.0f;
	mtl.emission[0] = mtl.emission[1] = mtl.emission[2] = 0.0f;
	C3D_LightEnvMaterial(&s_bakeLightEnv, &mtl);
}

#define MAX_PENDING_DECAL_CLEANUP 16
typedef struct { C3D_Tex tex; void* vbo; void* ibo; } PendingDecalCleanup;
static PendingDecalCleanup pendingDecalCleanup[MAX_PENDING_DECAL_CLEANUP];
static int pendingDecalCleanupCount = 0;

static void queueDecalCleanup(C3D_Tex tex, void* vbo, void* ibo)
{
	if (pendingDecalCleanupCount >= MAX_PENDING_DECAL_CLEANUP) {
		dbglog("queueDecalCleanup: pending list full, leaking one decal's buffers\n");
		return;
	}
	pendingDecalCleanup[pendingDecalCleanupCount++] = (PendingDecalCleanup){ tex, vbo, ibo };
}

static void flushDecalCleanup(void)
{
	for (int i = 0; i < pendingDecalCleanupCount; i++) {
		C3D_TexDelete(&pendingDecalCleanup[i].tex);
		linearFree(pendingDecalCleanup[i].vbo);
		linearFree(pendingDecalCleanup[i].ibo);
	}
	pendingDecalCleanupCount = 0;
}

static void drawDecalVerts(const Vertex verts[4], const CFLTexture* tex, const float tint[3])
{
	u32 mW = 1; while (mW < tex->width) mW <<= 1;
	u32 mH = 1; while (mH < tex->height) mH <<= 1;

	float uScale = (float)tex->width / (float)mW;
	float vScale = (float)tex->height / (float)mH;
	Vertex scaledVerts[4];
	memcpy(scaledVerts, verts, sizeof(scaledVerts));
	if (uScale != 1.0f || vScale != 1.0f) {
		for (int i = 0; i < 4; i++) {
			scaledVerts[i].texcoord[0] *= uScale;
			scaledVerts[i].texcoord[1] *= vScale;
		}
	}

	dbglog("  drawDecalVerts: tex=%ux%u fmt=%u verts NDC=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)\n",
		tex->width, tex->height, tex->format,
		verts[0].position[0], verts[0].position[1], verts[1].position[0], verts[1].position[1],
		verts[2].position[0], verts[2].position[1], verts[3].position[0], verts[3].position[1]);

	void* vbo = linearAlloc(sizeof(Vertex) * 4);
	if (!vbo) return;
	memcpy(vbo, scaledVerts, sizeof(Vertex) * 4);

	static const u8 quadIndices[6] = { 0, 1, 2, 0, 2, 3 };
	void* ibo = linearAlloc(sizeof(quadIndices));
	if (!ibo) { linearFree(vbo); return; }
	memcpy(ibo, quadIndices, sizeof(quadIndices));

	C3D_Tex gpuTex;
	if (!C3D_TexInit(&gpuTex, mW, mH, cflToGpuFormat[tex->format])) {
		dbglog("  drawDecalVerts: C3D_TexInit FAILED (%lux%lu fmt=%u) - decal silently skipped\n",
			(unsigned long)mW, (unsigned long)mH, tex->format);
		linearFree(vbo);
		linearFree(ibo);
		return;
	}
	C3D_TexUpload(&gpuTex, tex->data);
	
	C3D_TexFlush(&gpuTex);
	C3D_TexSetFilter(&gpuTex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&gpuTex, cflToGpuWrap[tex->uWrap < 3 ? tex->uWrap : 0], cflToGpuWrap[tex->vWrap < 3 ? tex->vWrap : 0]);

	bool needsTint = tex->format < CFL_TEXFMT_RG8;
	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	uploadMaterialColor(needsTint ? tint : white);

	C3D_TexBind(0, &gpuTex);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	bool isAlphaOnly = (tex->format == CFL_TEXFMT_A4 || tex->format == CFL_TEXFMT_A8);
	
	if (isAlphaOnly) {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
	} else {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	}
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnv* env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env1);
	C3D_DirtyTexEnv(env1);
	C3D_TexEnv* env2 = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env2);
	C3D_DirtyTexEnv(env2);

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo, sizeof(Vertex), 3, 0x210);
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_BYTE, ibo);

	queueDecalCleanup(gpuTex, vbo, ibo);
}

static void drawMaskDecal(const MaskPartsDesc* d, const CFLTexture* tex, const float tint[3])
{
	Vertex verts[4];
	buildMaskQuad(d, verts, false);
	drawDecalVerts(verts, tex, tint);
}

static void drawDualColorDecal(const MaskPartsDesc* d, const CFLTexture* tex, const float color0[3], const float color1[3],
	GPU_TEVOP_RGB color1Op, GPU_TEVOP_RGB extraOp, bool flipV)
{
	Vertex verts[4];
	buildMaskQuad(d, verts, flipV);

	u32 mW = 1; while (mW < tex->width) mW <<= 1;
	u32 mH = 1; while (mH < tex->height) mH <<= 1;
	float uScale = (float)tex->width / (float)mW;
	float vScale = (float)tex->height / (float)mH;
	Vertex scaledVerts[4];
	memcpy(scaledVerts, verts, sizeof(scaledVerts));
	if (uScale != 1.0f || vScale != 1.0f) {
		for (int i = 0; i < 4; i++) {
			scaledVerts[i].texcoord[0] *= uScale;
			scaledVerts[i].texcoord[1] *= vScale;
		}
	}

	dbglog("  drawDualColorDecal: tex=%ux%u fmt=%u verts NDC=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)\n",
		tex->width, tex->height, tex->format,
		verts[0].position[0], verts[0].position[1], verts[1].position[0], verts[1].position[1],
		verts[2].position[0], verts[2].position[1], verts[3].position[0], verts[3].position[1]);

	void* vbo = linearAlloc(sizeof(Vertex) * 4);
	if (!vbo) return;
	memcpy(vbo, scaledVerts, sizeof(Vertex) * 4);

	static const u8 quadIndices[6] = { 0, 1, 2, 0, 2, 3 };
	void* ibo = linearAlloc(sizeof(quadIndices));
	if (!ibo) { linearFree(vbo); return; }
	memcpy(ibo, quadIndices, sizeof(quadIndices));

	C3D_Tex gpuTex;
	if (!C3D_TexInit(&gpuTex, mW, mH, cflToGpuFormat[tex->format])) {
		dbglog("  drawDualColorDecal: C3D_TexInit FAILED (%lux%lu fmt=%u) - decal silently skipped\n",
			(unsigned long)mW, (unsigned long)mH, tex->format);
		linearFree(vbo);
		linearFree(ibo);
		return;
	}
	C3D_TexUpload(&gpuTex, tex->data);
	
	C3D_TexFlush(&gpuTex);
	C3D_TexSetFilter(&gpuTex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&gpuTex, cflToGpuWrap[tex->uWrap < 3 ? tex->uWrap : 0], cflToGpuWrap[tex->vWrap < 3 ? tex->vWrap : 0]);
	
	C3D_TexBind(0, &gpuTex);

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo, sizeof(Vertex), 3, 0x210);

	uploadMaterialColor(color0);
	C3D_TexEnv* env0 = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env0);
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, GPU_TEXTURE0, 0);
	C3D_TexEnvOpRgb(env0, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_R, GPU_TEVOP_RGB_SRC_COLOR);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env0, C3D_Alpha, GPU_REPLACE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_BYTE, ibo);

	env0 = C3D_GetTexEnv(0);
	uploadMaterialColor(color1);
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, GPU_TEXTURE0, 0);
	C3D_TexEnvOpRgb(env0, GPU_TEVOP_RGB_SRC_COLOR, color1Op, GPU_TEVOP_RGB_SRC_COLOR);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR & ~GPU_WRITE_ALPHA);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ZERO);
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_BYTE, ibo);

	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	env0 = C3D_GetTexEnv(0);
	uploadMaterialColor(white);
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvOpRgb(env0, extraOp, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_REPLACE);
	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_BYTE, ibo);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE_MINUS_DST_ALPHA, GPU_DST_ALPHA, GPU_ONE, GPU_ONE);

	queueDecalCleanup(gpuTex, vbo, ibo);
}

static void drawFullCanvasDecal(const CFLTexture* tex, const float tint[3])
{
	static const Vertex verts[4] = {
		{ {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
	};
	drawDecalVerts(verts, tex, tint);
}

static int wrapRot32(int r)
{
	r %= 32;
	if (r < 0) r += 32;
	return r;
}

static bool bakeMaskTexture(const u8* cflData, u32 cflSize, const MiiData* mii, CFLExpression expression, u32 canvasSize, C3D_Tex* outTex)
{
	static const float POS_X_ADD          = 3.5323312f;
	static const float POS_Y_ADD          = 4.629278f;
	static const float SPACING_MUL        = 0.88961464f;
	static const float POS_X_MUL          = 1.7792293f;
	static const float POS_Y_MUL          = 1.0760943f;
	static const float POS_Y_ADD_EYE      = 4.629278f + 13.822246f;
	static const float POS_Y_ADD_EYEBROW  = 4.629278f + 11.920528f;
	static const float POS_Y_ADD_MOUTH    = 4.629278f + 24.629572f;
	static const float POS_Y_ADD_MUSTACHE = 4.629278f + 27.134275f;
	static const float POS_X_ADD_MOLE     = 3.5323312f + 14.233834f;
	static const float POS_Y_ADD_MOLE     = 4.629278f + 11.178394f + 2.0f * 1.0760943f;
	(void)POS_X_ADD; (void)POS_Y_ADD;

	const CFLExpressionTypes* types = &kExpressionTypes[(unsigned)expression < CFL_EXPRESSION_COUNT ? expression : CFL_EXPRESSION_NORMAL];
	u32 eyeRIndex = eyeIndexForType(mii, types->eyeR);
	u32 eyeLIndex = eyeIndexForType(mii, types->eyeL);
	u32 mouthIndex = mouthIndexForType(mii, types->mouth);

	u8 eyeTypeForOffset = mii->eye_details.style < 80 ? mii->eye_details.style : 0;
	u8 eyebrowTypeForOffset = mii->eyebrow_details.style < 28 ? mii->eyebrow_details.style : 0;

	u16 eyebrowWord1;
	memcpy(&eyebrowWord1, (const u8*)&mii->eyebrow_details + 2, sizeof(u16));
	u8 eyebrowRotationReal = eyebrowWord1 & 0x1F;
	u8 eyebrowXSpacingReal = (eyebrowWord1 >> 5) & 0xF;
	u8 eyebrowYPositionReal = (eyebrowWord1 >> 9) & 0x1F;

	float eyeSpacingX = mii->eye_details.xspacing * SPACING_MUL;
	float eyeScale = 0.4f * mii->eye_details.scale + 1.0f;
	float eyeScaleX = 5.34375f * eyeScale;
	float eyeScaleYBase = 4.5f * eyeScale * (mii->eye_details.yscale * 0.12f + 0.64f);
	float eyePosY = mii->eye_details.yposition * POS_Y_MUL + POS_Y_ADD_EYE;

	int specialEyeType = -1;
	if (types->eyeR == 1 || types->eyeR == 3 || types->eyeR == 4 || types->eyeR == 5) specialEyeType = types->eyeR;
	else if (types->eyeL == 1 || types->eyeL == 3 || types->eyeL == 4 || types->eyeL == 5) specialEyeType = types->eyeL;

	int eyeRotateSharedRaw = (int)mii->eye_details.rotation + (int)eyeRotOffset[eyeTypeForOffset];
	if (specialEyeType >= 0) {
		u32 specialIdxU = eyeIndexForType(mii, (u8)specialEyeType);
		u8 specialIdx = specialIdxU < 80 ? (u8)specialIdxU : 0;
		eyeRotateSharedRaw += (int)eyeRotOffset[eyeTypeForOffset] - (int)eyeRotOffset[specialIdx];
	}
	eyeRotateSharedRaw += types->eyeRotDelta;
	float eyeRotateShared = (float)wrapRot32(eyeRotateSharedRaw) * (360.0f / 32.0f);
	float eyeRotateR = eyeRotateShared;
	float eyeRotateLBase = eyeRotateShared;

	float eyebrowSpacingX = eyebrowXSpacingReal * SPACING_MUL;
	float eyebrowScale = 0.4f * mii->eyebrow_details.scale + 1.0f;
	float eyebrowScaleX = 5.0625f * eyebrowScale;
	float eyebrowScaleY = 4.5f * eyebrowScale * (mii->eyebrow_details.yscale * 0.12f + 0.64f);
	float eyebrowPosY = ((float)eyebrowYPositionReal + types->eyebrowYDelta) * POS_Y_MUL + POS_Y_ADD_EYEBROW;
	int eyebrowRotateRaw = (int)eyebrowRotationReal + eyebrowRotOffset[eyebrowTypeForOffset] + types->eyebrowRotDelta;
	float eyebrowRotate = (float)wrapRot32(eyebrowRotateRaw) * (360.0f / 32.0f);

	float mouthScale = 0.4f * mii->mouth_details.scale + 1.0f;
	float mouthScaleX = 6.1875f * mouthScale;
	float mouthScaleY = 4.5f * mouthScale * (mii->mouth_details.yscale * 0.12f + 0.64f);
	float mouthPosY = mii->mustache_details.mouth_yposition * POS_Y_MUL + POS_Y_ADD_MOUTH;

	float minPartHeightRef = 12.0f * 64.0f / (float)canvasSize;
	bool eyeRNeedsHeightFloor = (eyeRIndex == 14 || eyeRIndex == 26);
	bool eyeLNeedsHeightFloor = (eyeLIndex == 14 || eyeLIndex == 26);
	float eyeScaleYR = (eyeRNeedsHeightFloor && eyeScaleYBase < minPartHeightRef) ? minPartHeightRef : eyeScaleYBase;
	float eyeScaleYL = (eyeLNeedsHeightFloor && eyeScaleYBase < minPartHeightRef) ? minPartHeightRef : eyeScaleYBase;
	bool mouthNeedsHeightFloor = (mouthIndex == 3 || mouthIndex == 15 || mouthIndex == 19 || mouthIndex == 20 ||
		mouthIndex == 21 || mouthIndex == 23 || mouthIndex == 25);
	if (mouthNeedsHeightFloor && mouthScaleY < minPartHeightRef) mouthScaleY = minPartHeightRef;

	float mustacheScale = 0.4f * mii->beard_details.scale + 1.0f;
	float mustacheScaleX = 4.5f * mustacheScale;
	float mustacheScaleY = 9.0f * mustacheScale;
	float mustachePosY = mii->beard_details.ypos * POS_Y_MUL + POS_Y_ADD_MUSTACHE;

	float moleScale = 0.4f * mii->mole_details.scale + 1.0f;
	float molePosX = mii->mole_details.xpos * POS_X_MUL + POS_X_ADD_MOLE;
	float molePosY = mii->mole_details.ypos * POS_Y_MUL + POS_Y_ADD_MOLE;

	u8 eyeColorIndex = mii->eye_details.color; if (eyeColorIndex >= 6) eyeColorIndex = 0;
	u8 eyebrowColorIndex = mii->eyebrow_details.color; if (eyebrowColorIndex >= 8) eyebrowColorIndex = 0;
	u8 mouthColorIndex = mii->mouth_details.color; if (mouthColorIndex >= 5) mouthColorIndex = 0;
	u8 beardColorIndex = mii->beard_details.color; if (beardColorIndex >= 8) beardColorIndex = 0;

	const float* eyeColor0Dbg = getEyeColor0(mii->eye_details.style);
	dbglog("FaceMask calc: COLOR eyeColorIndex=%u (raw=%u) eyeColors1=(%.3f,%.3f,%.3f) eyeColor0=(%.3f,%.3f,%.3f) [eyeType=%u] eyebrowColorIndex=%u (raw=%u) hairColors[eyebrow]=(%.3f,%.3f,%.3f)\n",
		eyeColorIndex, mii->eye_details.color, eyeColors1[eyeColorIndex][0], eyeColors1[eyeColorIndex][1], eyeColors1[eyeColorIndex][2],
		eyeColor0Dbg[0], eyeColor0Dbg[1], eyeColor0Dbg[2], mii->eye_details.style,
		eyebrowColorIndex, mii->eyebrow_details.color, hairColors[eyebrowColorIndex][0], hairColors[eyebrowColorIndex][1], hairColors[eyebrowColorIndex][2]);

	dbglog("FaceMask calc [expr=%s]: eyeR item=%lu eyeL item=%lu mouth item=%lu (Mii's own: eye=%u mouth=%u)\n",
		CFL_GetExpressionName(expression), (unsigned long)eyeRIndex, (unsigned long)eyeLIndex, (unsigned long)mouthIndex,
		mii->eye_details.style, mii->mouth_details.style);
	dbglog("FaceMask calc: eye pos=(%.2f,%.2f)/(%.2f,%.2f) scale=%.2f,%.2f rotR=%.2f rotL=%.2f\n",
		32.0f - eyeSpacingX, eyePosY, eyeSpacingX + 32.0f, eyePosY, eyeScaleX, eyeScaleYBase, eyeRotateR, eyeRotateLBase);
	dbglog("FaceMask calc: eyebrow pos=(%.2f,%.2f)/(%.2f,%.2f) scale=%.2f,%.2f rot=%.2f\n",
		32.0f - eyebrowSpacingX, eyebrowPosY, eyebrowSpacingX + 32.0f, eyebrowPosY, eyebrowScaleX, eyebrowScaleY, eyebrowRotate);
	dbglog("FaceMask calc: mouth pos=(32.00,%.2f) scale=%.2f,%.2f\n", mouthPosY, mouthScaleX, mouthScaleY);
	dbglog("FaceMask calc: mustache pos=(32.00,%.2f) scale=%.2f,%.2f\n", mustachePosY, mustacheScaleX, mustacheScaleY);
	dbglog("FaceMask calc: mole pos=(%.2f,%.2f) scale=%.2f\n", molePosX, molePosY, moleScale);

	C3D_Tex maskTex;
	if (!C3D_TexInitVRAM(&maskTex, canvasSize, canvasSize, GPU_RGBA8)) {
		dbglog("(C3D_TexInitVRAM failed for face mask, skipping)\n");
		return false;
	}
	
	C3D_RenderTarget* maskTarget = C3D_RenderTargetCreateFromTex(&maskTex, GPU_TEXFACE_2D, 0, -1);
	if (!maskTarget) {
		dbglog("(C3D_RenderTargetCreateFromTex failed for face mask, skipping)\n");
		C3D_TexDelete(&maskTex);
		return false;
	}

	C3D_Mtx identity;
	Mtx_Identity(&identity);

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		C3D_FrameDrawOn(maskTarget);
		C3D_SetViewport(0, 0, canvasSize, canvasSize);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &identity);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView,  &identity);
		
		C3D_LightEnvBind(&s_bakeLightEnv);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE_MINUS_DST_ALPHA, GPU_DST_ALPHA, GPU_ONE, GPU_ONE);
		C3D_RenderTargetClear(maskTarget, C3D_CLEAR_ALL, 0x00000000, 0);

		CFLTexture tex;
		MaskPartsDesc d;

		if (mii->mole_details.enable) {
			d = (MaskPartsDesc){ { molePosX, molePosY }, { moleScale, moleScale }, 0.0f, MASK_ORIGIN_CENTER };
			
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
			if (loadResTexture(cflData, cflSize, CFL_SECTION_MOLE, 1, &tex))
				drawMaskDecal(&d, &tex, moleColor);
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE_MINUS_DST_ALPHA, GPU_DST_ALPHA, GPU_ONE, GPU_ONE);
		}

		const float* eyeColor0 = getEyeColor0(mii->eye_details.style);
		
		bool flipVR = (types->eyeR == 4 || types->eyeR == 5);
		bool flipVL = (types->eyeL == 4 || types->eyeL == 5);
		d = (MaskPartsDesc){ { 32.0f - eyeSpacingX, eyePosY }, { eyeScaleX, eyeScaleYR }, eyeRotateR, MASK_ORIGIN_RIGHT };
		if (loadResTexture(cflData, cflSize, CFL_SECTION_EYE, eyeRIndex, &tex))
			drawDualColorDecal(&d, &tex, eyeColor0, eyeColors1[eyeColorIndex], GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_G, flipVR);

		d = (MaskPartsDesc){ { eyeSpacingX + 32.0f, eyePosY }, { eyeScaleX, eyeScaleYL }, 360.0f - eyeRotateLBase, MASK_ORIGIN_LEFT };
		if (loadResTexture(cflData, cflSize, CFL_SECTION_EYE, eyeLIndex, &tex))
			drawDualColorDecal(&d, &tex, eyeColor0, eyeColors1[eyeColorIndex], GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_G, flipVL);

		d = (MaskPartsDesc){ { 32.0f - eyebrowSpacingX, eyebrowPosY }, { eyebrowScaleX, eyebrowScaleY }, eyebrowRotate, MASK_ORIGIN_RIGHT };
		if (loadResTexture(cflData, cflSize, CFL_SECTION_EYEBROW, mii->eyebrow_details.style, &tex))
			drawMaskDecal(&d, &tex, hairColors[eyebrowColorIndex]);

		d = (MaskPartsDesc){ { eyebrowSpacingX + 32.0f, eyebrowPosY }, { eyebrowScaleX, eyebrowScaleY }, 360.0f - eyebrowRotate, MASK_ORIGIN_LEFT };
		if (loadResTexture(cflData, cflSize, CFL_SECTION_EYEBROW, mii->eyebrow_details.style, &tex))
			drawMaskDecal(&d, &tex, hairColors[eyebrowColorIndex]);

		d = (MaskPartsDesc){ { 32.0f, mouthPosY }, { mouthScaleX, mouthScaleY }, 0.0f, MASK_ORIGIN_CENTER };
		if (loadResTexture(cflData, cflSize, CFL_SECTION_MOUTH, mouthIndex, &tex))
			drawDualColorDecal(&d, &tex, mouthColors0[mouthColorIndex], mouthColors1[mouthColorIndex], GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_B, false);

		if (mii->mustache_details.mustache_style != 0) {
			d = (MaskPartsDesc){ { 32.0f, mustachePosY }, { mustacheScaleX, mustacheScaleY }, 0.0f, MASK_ORIGIN_RIGHT };
			if (loadResTexture(cflData, cflSize, CFL_SECTION_MUSTACHE, mii->mustache_details.mustache_style, &tex))
				drawMaskDecal(&d, &tex, hairColors[beardColorIndex]);

			d = (MaskPartsDesc){ { 32.0f, mustachePosY }, { mustacheScaleX, mustacheScaleY }, 0.0f, MASK_ORIGIN_LEFT };
			if (loadResTexture(cflData, cflSize, CFL_SECTION_MUSTACHE, mii->mustache_details.mustache_style, &tex))
				drawMaskDecal(&d, &tex, hairColors[beardColorIndex]);
		}
		
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
	C3D_FrameEnd(0);
	flushDecalCleanup();

	C3D_RenderTargetDelete(maskTarget);

	dbglog("Face mask: baked %lux%lu for expr=%s\n", (unsigned long)canvasSize, (unsigned long)canvasSize, CFL_GetExpressionName(expression));
	*outTex = maskTex;
	return true;
}

#define FACE_TEX_WIDTH  64
#define FACE_TEX_HEIGHT 128

static bool buildFaceTexture(CFLCharModel* cm, const u8* cflData, u32 cflSize, const MiiData* mii, const CFLModel* faceModel)
{
	if (!faceModel->texcoords) {
		dbglog("(face model has no texcoords, skipping faceline texture bake)\n");
		return false;
	}

	u8 skinIndex = mii->face_style.skinColor;
	if (skinIndex >= 6) skinIndex = 0;
	u8 beardColorIndex = mii->beard_details.color;
	if (beardColorIndex >= 8) beardColorIndex = 0;

	C3D_Tex faceTex;
	if (!C3D_TexInitVRAM(&faceTex, FACE_TEX_WIDTH, FACE_TEX_HEIGHT, GPU_RGBA8)) {
		dbglog("(C3D_TexInitVRAM failed for faceline texture, skipping)\n");
		return false;
	}
	
	C3D_RenderTarget* faceTarget = C3D_RenderTargetCreateFromTex(&faceTex, GPU_TEXFACE_2D, 0, -1);
	if (!faceTarget) {
		dbglog("(C3D_RenderTargetCreateFromTex failed for faceline texture, skipping)\n");
		C3D_TexDelete(&faceTex);
		return false;
	}

	C3D_Mtx identity;
	Mtx_Identity(&identity);

	u32 clearColor =
		((u32)(skinColors[skinIndex][0] * 255.0f) << 24) |
		((u32)(skinColors[skinIndex][1] * 255.0f) << 16) |
		((u32)(skinColors[skinIndex][2] * 255.0f) << 8) | 0xFF;

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		C3D_RenderTargetClear(faceTarget, C3D_CLEAR_ALL, clearColor, 0);
		C3D_FrameDrawOn(faceTarget);
		C3D_SetViewport(0, 0, FACE_TEX_WIDTH, FACE_TEX_HEIGHT);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &identity);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView,  &identity);
		
		C3D_LightEnvBind(&s_bakeLightEnv);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);

		CFLTexture tex;
		static const float white[3] = { 1.0f, 1.0f, 1.0f };
		static const float wrinkleColor[3] = { 0.0f, 0.0f, 0.0f };

		if (mii->face_details.makeup != 0 &&
			loadResTexture(cflData, cflSize, CFL_SECTION_FACET_MAKE, mii->face_details.makeup, &tex))
			drawFullCanvasDecal(&tex, white);

		if (mii->face_details.wrinkles != 0 &&
			loadResTexture(cflData, cflSize, CFL_SECTION_FACET_LINE, mii->face_details.wrinkles, &tex))
			drawFullCanvasDecal(&tex, wrinkleColor);

		if (mii->beard_details.style > 3 &&
			loadResTexture(cflData, cflSize, CFL_SECTION_FACET_BEARD, mii->beard_details.style - 3, &tex))
			drawFullCanvasDecal(&tex, hairColors[beardColorIndex]);
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
	C3D_FrameEnd(0);
	flushDecalCleanup();

	C3D_RenderTargetDelete(faceTarget);
	C3D_TexSetFilter(&faceTex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&faceTex, GPU_MIRRORED_REPEAT, GPU_CLAMP_TO_EDGE);
	if (addTexturedPart(cm, faceModel, NULL, 1.0f, false, &faceTex, true) < 0) {
		C3D_TexDelete(&faceTex);
		dbglog("(too many parts already, discarding faceline texture)\n");
		return false;
	}
	dbglog("Faceline texture: baked %dx%d, applied to face\n", FACE_TEX_WIDTH, FACE_TEX_HEIGHT);
	return true;
}

static const u8* g_cflData;
static u32 g_cflSize;

bool CFL_IsAvailable(void)
{
	return g_cflData != NULL;
}

static u16 cflComputeCRC16(u16 seed, const u8* data, u32 size)
{
	u32 crc = seed;
	for (u32 i = 0; i < size; i++) {
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000) ? ((0x1021 ^ (crc << 1)) & 0xFFFF) : ((crc << 1) & 0xFFFF);
		crc ^= data[i];
	}
	return (u16)crc;
}

bool CFL_MakeStoreData(const MiiData* mii, CFLStoreData* out)
{
	if (!mii || !out) return false;
	memcpy(&out->miiData, mii, sizeof(MiiData));
	out->pad[0] = 0;
	out->pad[1] = 0;
	u8* crcBytes = (u8*)out + (sizeof(CFLStoreData) - 2);
	crcBytes[0] = 0;
	crcBytes[1] = 0;
	u16 crc = cflComputeCRC16(0, (const u8*)out, sizeof(CFLStoreData));
	
	crcBytes[0] = (u8)(crc >> 8);
	crcBytes[1] = (u8)(crc & 0xFF);
	return true;
}

bool CFL_IsStoreDataValid(const CFLStoreData* storeData)
{
	if (!storeData) return false;
	return cflComputeCRC16(0, (const u8*)storeData, sizeof(CFLStoreData)) == 0;
}

#define CFL_DB_RECORD_OFFSET 8
#define CFL_DB_RECORD_SIZE 92
#define CFL_DB_RECORD_COUNT 100
#define CFL_DB_ID_OFFSET 12
#define CFL_DB_ID_LENGTH 10

static u8 g_cflDbCache[CFL_DB_RECORD_OFFSET + CFL_DB_RECORD_COUNT * CFL_DB_RECORD_SIZE];
static bool g_cflDbCacheValid = false;

static bool loadOfficialDatabaseArray(u8* outArray, size_t arraySize, const char* caller)
{
	u32 archivePath[3] = { MEDIATYPE_NAND, 0xf000000bU, 0x00048000U };
	FS_Path fsArchivePath = { PATH_BINARY, sizeof(archivePath), archivePath };

	FS_Archive archive;
	Result rc = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, fsArchivePath);
	if (R_FAILED(rc)) {
		dbglogErr("%s: could not open the shared Mii Maker extdata (%08lX)\n", caller, (unsigned long)rc);
		return false;
	}

	FS_Path filePath = fsMakePath(PATH_UTF16, u"/CFL_DB.dat");
	Handle file;
	rc = FSUSER_OpenFile(&file, archive, filePath, FS_OPEN_READ, 0);
	if (R_FAILED(rc)) {
		dbglogErr("%s: could not open CFL_DB.dat (%08lX) - has Mii Maker ever been opened?\n", caller, (unsigned long)rc);
		FSUSER_CloseArchive(archive);
		return false;
	}

	u32 got = 0;
	rc = FSFILE_Read(file, &got, 0, outArray, (u32)arraySize);
	FSFILE_Close(file);
	FSUSER_CloseArchive(archive);

	if (R_FAILED(rc) || got != arraySize) {
		dbglogErr("%s: short read on CFL_DB.dat (%lu of %u bytes)\n", caller, (unsigned long)got, (unsigned)arraySize);
		return false;
	}
	return true;
}

static void ensureDatabaseCacheLoaded(void)
{
	g_cflDbCacheValid = loadOfficialDatabaseArray(g_cflDbCache, sizeof(g_cflDbCache), "CFL_Initialize (database)");
	if (g_cflDbCacheValid) {
		dbglog("CFL_Initialize: database cached (%u bytes)\n", (unsigned)sizeof(g_cflDbCache));
	} else {
		dbglog("CFL_Initialize: no CFL_DB.dat database available - official-data queries will be unavailable\n");
	}
}

static bool officialRecordIsEmpty(const u8* record)
{
	const u8* recordId = record + CFL_DB_ID_OFFSET;
	for (int b = 0; b < CFL_DB_ID_LENGTH; b++) {
		if (recordId[b] != 0) return false;
	}
	return true;
}

static void unpackOfficialRecord(const u8* record, MiiData* outMii)
{
	memcpy(outMii, record, sizeof(MiiData));
}

bool CFL_SearchOfficialData(const MiiData* mii, u16* outIndex)
{
	if (!mii || !outIndex || !g_cflDbCacheValid) return false;

	const u8* miiBytes = (const u8*)mii;
	const u8* targetId = miiBytes + 12;

	for (int i = 0; i < CFL_DB_RECORD_COUNT; i++) {
		const u8* record = g_cflDbCache + CFL_DB_RECORD_OFFSET + i * CFL_DB_RECORD_SIZE;
		if (officialRecordIsEmpty(record)) continue;
		if (memcmp(record + CFL_DB_ID_OFFSET, targetId, CFL_DB_ID_LENGTH) == 0) {
			*outIndex = (u16)i;
			return true;
		}
	}
	return false;
}

bool CFL_IsAvailableOfficialData(u16 index)
{
	if (index >= CFL_DB_RECORD_COUNT || !g_cflDbCacheValid) return false;
	return !officialRecordIsEmpty(g_cflDbCache + CFL_DB_RECORD_OFFSET + index * CFL_DB_RECORD_SIZE);
}

bool CFL_GetOfficialData(u16 index, MiiData* outMii)
{
	if (!outMii || index >= CFL_DB_RECORD_COUNT || !g_cflDbCacheValid) return false;
	const u8* record = g_cflDbCache + CFL_DB_RECORD_OFFSET + index * CFL_DB_RECORD_SIZE;
	if (officialRecordIsEmpty(record)) return false;
	unpackOfficialRecord(record, outMii);
	return true;
}

int CFL_GetAvailableOfficialDataNum(void)
{
	if (!g_cflDbCacheValid) return -1;
	int count = 0;
	for (int i = 0; i < CFL_DB_RECORD_COUNT; i++) {
		if (!officialRecordIsEmpty(g_cflDbCache + CFL_DB_RECORD_OFFSET + i * CFL_DB_RECORD_SIZE)) count++;
	}
	return count;
}

bool CFL_GetMyMiiIndex(u16* outIndex)
{
	if (!outIndex) return false;
	*outIndex = 0;
	if (!g_cflDbCacheValid) return false;
	if (g_cflDbCache[0] != 'C' || g_cflDbCache[1] != 'F' || g_cflDbCache[2] != 'O' || g_cflDbCache[3] != 'G')
		return false;
	u8 index = g_cflDbCache[4];
	if (index >= CFL_DB_RECORD_COUNT) return false;
	*outIndex = index;
	return true;
}

#define CFL_RECENT_REGION_OFFSET 0xc820
#define CFL_RECENT_REGION_SIZE   0x1ca0
#define CFL_RECENT_COUNT_OFFSET  4
#define CFL_RECENT_HISTORY_OFFSET 8
#define CFL_RECENT_SLOT_COUNT    100
#define CFL_RECENT_RECORDS_OFFSET 0x6c
#define CFL_RECENT_RECORD_SIZE   72

typedef struct {
	u64 uid;
	u32 dateTime;
} CFLiUblEntry;
#define CFLI_UBL_MAX_ENTRIES 1000
#define CFLI_UBL_NG_TIME     7

static bool CFLi_UblIsExist(u64 authorId)
{
	u32 archivePath[3] = { MEDIATYPE_NAND, 0xf000000bU, 0x00048000U };
	FS_Path fsArchivePath = { PATH_BINARY, sizeof(archivePath), archivePath };

	FS_Archive archive;
	Result rc = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, fsArchivePath);
	if (R_FAILED(rc)) return false;

	FS_Path filePath = fsMakePath(PATH_UTF16, u"/ubll.lst");
	Handle file;
	rc = FSUSER_OpenFile(&file, archive, filePath, FS_OPEN_READ, 0);
	if (R_FAILED(rc)) {
		FSUSER_CloseArchive(archive);
		return false;
	}

	static CFLiUblEntry entries[CFLI_UBL_MAX_ENTRIES];
	u32 got = 0;
	rc = FSFILE_Read(file, &got, 0, entries, sizeof(entries));
	FSFILE_Close(file);
	FSUSER_CloseArchive(archive);

	if (R_FAILED(rc) || got != sizeof(entries)) return false;

	for (int i = 0; i < CFLI_UBL_MAX_ENTRIES; i++) {
		if (entries[i].uid == authorId && entries[i].dateTime != CFLI_UBL_NG_TIME) return true;
	}
	return false;
}

static bool CFLi_ReplaceProhibitedCharInfo(const MiiData* mii)
{
	u64 myAuthorId;
	if (R_SUCCEEDED(CFGU_GenHashConsoleUnique(0, &myAuthorId)) && myAuthorId == mii->system_id) {
		return false;
	}
	return CFLi_UblIsExist(mii->system_id);
}

static bool CFLi_AddRecentDBData(const MiiData* mii)
{
	if (!mii) return false;

	const u8* miiBytes = (const u8*)mii;
	bool idIsNull = true;
	for (int b = 0; b < CFL_DB_ID_LENGTH; b++) {
		if (miiBytes[CFL_DB_ID_OFFSET + b] != 0) { idIsNull = false; break; }
	}
	if (idIsNull) return false;

	for (u8 defaultIndex = 0; defaultIndex < 6; defaultIndex++) {
		u8 defaultId[CFL_DB_ID_LENGTH] = {
			0x80, 0x00, 0x00, defaultIndex,
			0xEC, 0xFF, 0x82, 0xD2, 0x00, 0x00,
		};
		if (memcmp(miiBytes + CFL_DB_ID_OFFSET, defaultId, CFL_DB_ID_LENGTH) == 0) {
			dbglog("CFLi_AddRecentDBData: skipped - matches default/guest Mii index %u\n", defaultIndex);
			return true;
		}
	}

	u16 alreadyOfficialIndex;
	if (CFL_SearchOfficialData(mii, &alreadyOfficialIndex)) {
		dbglog("CFLi_AddRecentDBData: skipped - already in the Official DB at index %u\n", alreadyOfficialIndex);
		return true;
	}

	u32 archivePath[3] = { MEDIATYPE_NAND, 0xf000000bU, 0x00048000U };
	FS_Path fsArchivePath = { PATH_BINARY, sizeof(archivePath), archivePath };
	FS_Archive archive;
	Result rc = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, fsArchivePath);
	if (R_FAILED(rc)) {
		dbglogErr("CFLi_AddRecentDBData: could not open the shared Mii Maker extdata (%08lX)\n", (unsigned long)rc);
		return false;
	}

	FS_Path filePath = fsMakePath(PATH_UTF16, u"/CFL_DB.dat");
	Handle file;
	rc = FSUSER_OpenFile(&file, archive, filePath, FS_OPEN_READ | FS_OPEN_WRITE, 0);
	if (R_FAILED(rc)) {
		dbglogErr("CFLi_AddRecentDBData: could not open CFL_DB.dat for writing (%08lX)\n", (unsigned long)rc);
		FSUSER_CloseArchive(archive);
		return false;
	}

	static u8 region[CFL_RECENT_REGION_SIZE];
	u32 got = 0;
	rc = FSFILE_Read(file, &got, CFL_RECENT_REGION_OFFSET, region, sizeof(region));
	if (R_FAILED(rc) || got != sizeof(region)) {
		dbglogErr("CFLi_AddRecentDBData: short read on the recent-DB region (%lu of %u bytes)\n",
			(unsigned long)got, (unsigned)sizeof(region));
		FSFILE_Close(file);
		FSUSER_CloseArchive(archive);
		return false;
	}

	if (region[0] != 'C' || region[1] != 'F' || region[2] != 'R' || region[3] != 'A') {
		dbglogErr("CFLi_AddRecentDBData: recent-DB region has the wrong magic - refusing to write\n");
		FSFILE_Close(file);
		FSUSER_CloseArchive(archive);
		return false;
	}
	if (cflComputeCRC16(0, region, sizeof(region)) != 0) {
		dbglogErr("CFLi_AddRecentDBData: recent-DB region failed its checksum - refusing to write\n");
		FSFILE_Close(file);
		FSUSER_CloseArchive(archive);
		return false;
	}

	u32 count;
	memcpy(&count, region + CFL_RECENT_COUNT_OFFSET, sizeof(count));
	u8* history = region + CFL_RECENT_HISTORY_OFFSET;
	u8* records = region + CFL_RECENT_RECORDS_OFFSET;

	if (count > CFL_RECENT_SLOT_COUNT) {
		dbglogErr("CFLi_AddRecentDBData: recent-DB count field is out of range (%lu) - refusing to write\n", (unsigned long)count);
		FSFILE_Close(file);
		FSUSER_CloseArchive(archive);
		return false;
	}

	int targetSlot = -1;
	for (u32 i = 0; i < count; i++) {
		u8 slot = history[i];
		if (slot >= CFL_RECENT_SLOT_COUNT) continue;
		const u8* record = records + (u32)slot * CFL_RECENT_RECORD_SIZE;
		if (memcmp(record + CFL_DB_ID_OFFSET, miiBytes + CFL_DB_ID_OFFSET, CFL_DB_ID_LENGTH) == 0) {
			targetSlot = slot;
			break;
		}
	}

	bool isNewEntry = false;
	if (targetSlot < 0) {
		
		if (count < CFL_RECENT_SLOT_COUNT) {
			for (int slot = 0; slot < CFL_RECENT_SLOT_COUNT; slot++) {
				const u8* record = records + (u32)slot * CFL_RECENT_RECORD_SIZE;
				if (officialRecordIsEmpty(record)) { targetSlot = slot; break; }
			}
		}
		if (targetSlot < 0) {
			
			if (count == 0) {
				dbglogErr("CFLi_AddRecentDBData: recent-DB count is 0 but no empty slot was found - refusing to write\n");
				FSFILE_Close(file);
				FSUSER_CloseArchive(archive);
				return false;
			}
			targetSlot = history[0];
			memmove(history, history + 1, count - 1);
			count--;
		}
		isNewEntry = true;
	}

	memcpy(records + (u32)targetSlot * CFL_RECENT_RECORD_SIZE, miiBytes, CFL_RECENT_RECORD_SIZE);

	if (isNewEntry) {
		history[count] = (u8)targetSlot;
		count++;
		memcpy(region + CFL_RECENT_COUNT_OFFSET, &count, sizeof(count));
	}

	region[CFL_RECENT_REGION_SIZE - 2] = 0;
	region[CFL_RECENT_REGION_SIZE - 1] = 0;
	u16 crc = cflComputeCRC16(0, region, sizeof(region));
	region[CFL_RECENT_REGION_SIZE - 2] = (u8)(crc >> 8);
	region[CFL_RECENT_REGION_SIZE - 1] = (u8)crc;

	u32 written = 0;
	rc = FSFILE_Write(file, &written, CFL_RECENT_REGION_OFFSET, region, sizeof(region), FS_WRITE_FLUSH);
	FSFILE_Close(file);
	FSUSER_CloseArchive(archive);

	if (R_FAILED(rc) || written != sizeof(region)) {
		dbglogErr("CFLi_AddRecentDBData: short write on the recent-DB region (%lu of %u bytes, %08lX)\n",
			(unsigned long)written, (unsigned)sizeof(region), (unsigned long)rc);
		return false;
	}

	dbglog("CFLi_AddRecentDBData: %s slot %d\n", isNewEntry ? "added to" : "updated", targetSlot);
	return true;
}

int CFL_GetWorkSize(bool hdModeEnabled)
{
	int base = hdModeEnabled ? 1024 : 128;
	int size = base * base * 3;
	return size + 0x400e0;
}

bool CFL_Initialize(void)
{
	dbglog("Opening CFL_Res.dat system archive...\n");
	u32 cflSize = 0;
	if (!loadMiiResource(&cflSize)) {
		dbglog("This needs full ARM11 FS permissions - make sure you're\n");
		dbglog("launching via Luma3DS/Rosalina's homebrew launcher.\n");
		return false;
	}
	g_cflData = g_cflResBuffer;
	g_cflSize = cflSize;

	/* MKW portrait generation never reads or writes the system Mii DB. */

	vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);

	uLoc_projection   = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
	uLoc_modelView    = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");

	C3D_LightEnvInit(&s_bakeLightEnv);
	
	C3D_LightEnvAmbient(&s_bakeLightEnv, 0.0f, 0.0f, 0.0f);
	C3D_LightInit(&s_bakeLight, &s_bakeLightEnv);
	C3D_LightAmbient(&s_bakeLight, 1.0f, 1.0f, 1.0f);
	C3D_LightDiffuse(&s_bakeLight, 0.0f, 0.0f, 0.0f);
	C3D_LightSpecular0(&s_bakeLight, 0.0f, 0.0f, 0.0f);
	C3D_LightSpecular1(&s_bakeLight, 0.0f, 0.0f, 0.0f);
	C3D_LightEnable(&s_bakeLight, true);

	CFL_RebindShader();

	return true;
}

void CFL_RebindShader(void)
{
	C3D_BindProgram(&program);

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2);

	C3D_CullFace(GPU_CULL_NONE);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	
	C3D_TexEnvSrc(env, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

void CFL_Finalize(void)
{
	C3D_BindProgram(NULL);
	shaderProgramFree(&program);
	DVLB_Free(vshader_dvlb);
	free(g_cflResBuffer);
	g_cflResBuffer = NULL;
	g_cflData = NULL;
	g_cflSize = 0;
	
	g_cflDbCacheValid = false;
}

void CFL_DeleteModel(CFLCharModel* cm)
{
	if (!cm) return;
	clearParts(cm);
	cm->valid = false;
}

bool CFL_HasCharModel(const CFLCharModel* cm)
{
	return cm && cm->valid && cm->partCount > 0;
}

bool CFL_InitCharModel(CFLCharModel* cm, const MiiData* miiIn, CFLResolution resolution, CFLExpressionFlag expressionFlags)
{
	return CFL_InitCharModelWithHairMode(cm, miiIn, resolution, expressionFlags, CFL_HAIR_NORMAL);
}

bool CFL_InitCharModelWithHairMode(CFLCharModel* cm, const MiiData* miiIn, CFLResolution resolution, CFLExpressionFlag expressionFlags, CFLHairMode hairMode)
{
	if (!cm || !miiIn || (unsigned)hairMode > CFL_HAIR_HIDDEN) return false;
	// Copy before clearing: callers may rebuild directly from &cm->mii.
	MiiData mii = *miiIn;
	if (!g_cflData) {
		dbglog("CFL_InitCharModel: called before a successful CFL_Initialize()\n");
		return false;
	}
	const u8* cflData = g_cflData;
	u32 cflSize = g_cflSize;

	CFL_RebindShader();

	dbglogVramStats("CFL_InitCharModel start", false);

	clearParts(cm);
	cm->valid = false;

	expressionFlags &= CFL_EXPRESSION_FLAG_ALL;
	if (expressionFlags == 0) {
		dbglog("CFL_InitCharModel: expressionFlags must have at least one bit set (matches CFL's own CFLi_GetExpressionCount(...) > 0 assert) - defaulting to NORMAL only.\n");
		expressionFlags = CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL);
	}
	u32 canvasSize = snapResolution(resolution);

	CFLExpression startExpr = CFL_EXPRESSION_NORMAL;
	if (!(expressionFlags & CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		for (int i = 0; i < CFL_EXPRESSION_COUNT; i++) {
			if (expressionFlags & CFL_EXPRESSION_FLAG(i)) { startExpr = (CFLExpression)i; break; }
		}
	}

	dbglog("face shape=%u skinColor=%u hair style=%u color=%u flip=%u sex=%u(%s) favColor=%u\n",
		mii.face_style.shape, mii.face_style.skinColor,
		mii.hair_style, mii.hair_details.color, mii.hair_details.flip,
		mii.mii_details.sex, mii.mii_details.sex ? "female" : "male", mii.mii_details.shirt_color);
	dbglog("resolution: requested=%lu snapped=%lu   expressionFlags=0x%lx startExpr=%s\n",
		(unsigned long)resolution, (unsigned long)canvasSize, (unsigned long)expressionFlags, CFL_GetExpressionName(startExpr));

	u8 skinIndex = mii.face_style.skinColor;
	if (skinIndex >= 6) skinIndex = 0;
	u8 hairColorIndex = mii.hair_details.color;
	if (hairColorIndex >= 8) hairColorIndex = 0;
	u8 favColorIndex = mii.mii_details.shirt_color;

	u32 hairIndex = (u32)mii.hair_style * 2 + (hairMode == CFL_HAIR_HAT);
	dbglog("Hair index: rawStyle=%u -> %lu\n", mii.hair_style, (unsigned long)hairIndex);

	CFLFaceAnchors anchors;
	{
		CFLModel faceModel;
		if (!loadResModel(cflData, cflSize, CFL_SECTION_FACE, mii.face_style.shape, &anchors, &faceModel)) {
			dbglogErr("\nFailed to parse the face model.\n");
			dbglogVramStats("CFL_InitCharModel face model parse failure", true);
			return false;
		}
		dbglog("Face: %lu verts, %lu indices (hair anchor %.2f %.2f %.2f)\n",
			(unsigned long)faceModel.vertexCount, (unsigned long)faceModel.indexCount,
			anchors.hair[0], anchors.hair[1], anchors.hair[2]);
		if (!buildFaceTexture(cm, cflData, cflSize, &mii, &faceModel))
			addPart(cm, &faceModel, NULL, 1.0f, false, skinColors[skinIndex], false);
		logNormalLightStats("FACE", &faceModel);
		freeResModel(&faceModel);
	}
	// A failed face allocation must not produce a supposedly valid icon.
	if (cm->partCount == 0) return false;

	{
		CFLModel canvasModel;
		if (loadResModel(cflData, cflSize, CFL_SECTION_MASK, mii.face_style.shape, NULL, &canvasModel)) {
			
			int requestedCount = 0, bakedCount = 0;
			u32 vramBeforeBakes = vramSpaceFree();
			dbglog("CFL_InitCharModel: VRAM free before baking = %lu bytes (%.1f KB)\n", (unsigned long)vramBeforeBakes, vramBeforeBakes / 1024.0f);
			for (int i = 0; i < CFL_EXPRESSION_COUNT; i++) {
				if (!(expressionFlags & CFL_EXPRESSION_FLAG(i))) continue;
				requestedCount++;
				if (bakeMaskTexture(cflData, cflSize, &mii, (CFLExpression)i, canvasSize, &cm->maskTexForExpr[i])) {
					C3D_TexSetFilter(&cm->maskTexForExpr[i], GPU_LINEAR, GPU_LINEAR);
					C3D_TexSetWrap(&cm->maskTexForExpr[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
					cm->maskTexBaked[i] = true;
					bakedCount++;
				} else {
					
					dbglogErr("CFL_InitCharModel: FAILED to bake MASK for expression %s (likely VRAM exhaustion)\n", CFL_GetExpressionName((CFLExpression)i));
					dbglogVramStats("  at failure", true);
				}
			}
			u32 vramAfterBakes = vramSpaceFree();
			u32 vramUsedByBakes = (vramBeforeBakes > vramAfterBakes) ? (vramBeforeBakes - vramAfterBakes) : 0;
			dbglog("CFL_InitCharModel: VRAM free after baking = %lu bytes (%.1f KB) - this model's %d successfully baked MASK texture(s) used ~%lu bytes (%.1f KB)\n",
				(unsigned long)vramAfterBakes, vramAfterBakes / 1024.0f, bakedCount, (unsigned long)vramUsedByBakes, vramUsedByBakes / 1024.0f);
			if (bakedCount < requestedCount) {
				dbglogErr("CFL_InitCharModel: only %d/%d requested expressions baked successfully\n", bakedCount, requestedCount);
			} else {
				dbglog("CFL_InitCharModel: all %d requested expressions baked successfully\n", requestedCount);
			}
			if (cm->maskTexBaked[startExpr]) {
				cm->maskPartIndex = addTexturedPart(cm, &canvasModel, NULL, 1.0f, false, &cm->maskTexForExpr[startExpr], false);
				if (cm->maskPartIndex < 0)
					dbglogErr("CFL_InitCharModel: too many parts already, MASK not attached (still pre-baked, unused)\n");
			} else {
				dbglogErr("CFL_InitCharModel: starting expression %s failed to bake, MASK will be missing\n", CFL_GetExpressionName(startExpr));
			}
			freeResModel(&canvasModel);
		} else {
			dbglogErr("(no face canvas at section %lu item %u, skipping MASK entirely)\n",
				(unsigned long)CFL_SECTION_MASK, mii.face_style.shape);
		}
	}

	if (hairMode != CFL_HAIR_HIDDEN) {
		loadTexturedPart(cm, cflData, cflSize, CFL_SECTION_CAP, hairIndex, CFL_SECTION_CAPTEX, mii.hair_style,
			anchors.hair, 1.0f, mii.hair_details.flip != 0,
			CFL_GetFavoriteColor(favColorIndex), NULL, true, false, true, "Cap");

		loadPart(cm, cflData, cflSize, CFL_SECTION_HAIR, hairIndex, anchors.hair, 1.0f, mii.hair_details.flip != 0,
			hairColors[hairColorIndex], false, "Hair");
		loadPart(cm, cflData, cflSize, CFL_SECTION_FOREHEAD, hairIndex, anchors.hair, 1.0f, mii.hair_details.flip != 0,
			skinColors[skinIndex], false, "Forehead");
	}

	if (mii.beard_details.style < 4) {
		u8 beardColorIndex = mii.beard_details.color;
		if (beardColorIndex >= 8) beardColorIndex = 0;
		loadPart(cm, cflData, cflSize, CFL_SECTION_GOATEE, mii.beard_details.style, anchors.goatee, 1.0f, false,
			hairColors[beardColorIndex], false, "Goatee");
	}

	{
		float noseScale = mii.nose_details.scale * 0.175f + 0.4f;
		float nosePos[3] = {
			anchors.noseGlasses[0],
			anchors.noseGlasses[1] + ((float)mii.nose_details.yposition - 8.0f) * -1.5f,
			anchors.noseGlasses[2],
		};
		static const float noselineColor[3] = { 0.0f, 0.0f, 0.0f };
		loadPart(cm, cflData, cflSize, CFL_SECTION_NOSE, mii.nose_details.style, nosePos, noseScale, false,
			skinColors[skinIndex], false, "Nose");
		loadTexturedPart(cm, cflData, cflSize, CFL_SECTION_NLINE, mii.nose_details.style,
			CFL_SECTION_NLINETEX, mii.nose_details.style, nosePos, noseScale, false, noselineColor, NULL, false, true, false, "Nose canvas");
	}

	if (mii.glasses_details.style != 0) {
		float glassScale = mii.glasses_details.scale * 0.15f + 0.4f;
		float glassPos[3] = {
			anchors.noseGlasses[0],
			anchors.noseGlasses[1] + ((float)mii.glasses_details.ypos - 11.0f) * -1.5f + 5.0f,
			anchors.noseGlasses[2] + 2.0f,
		};
		u8 glassColorIndex = mii.glasses_details.color;
		if (glassColorIndex >= 8) glassColorIndex = 0;
		
		loadTexturedPart(cm, cflData, cflSize, CFL_SECTION_GLASSES, 0,
			CFL_SECTION_GLASSES_TEX, mii.glasses_details.style, glassPos, glassScale, false,
			glassColors[glassColorIndex], NULL, true, false, false, "Glasses");
	}

	cm->mii = mii;
	cm->expressionFlags = expressionFlags;
	cm->expression = startExpr;
	cm->valid = true;

	/* MKW: rendering an explicitly chosen identity has no system DB side effects. */

	dbglog("CFL_InitCharModel: complete\n");
	return true;
}

bool CFL_SetExpression(CFLCharModel* cm, CFLExpression expression)
{
	if (!cm || (unsigned)expression >= CFL_EXPRESSION_COUNT) return false;
	if (!(cm->expressionFlags & CFL_EXPRESSION_FLAG(expression))) {
		dbglog("CFL_SetExpression: %s wasn't in the expressionFlags passed to InitCharModel - ignored\n",
			CFL_GetExpressionName(expression));
		return false;
	}
	if (!cm->maskTexBaked[expression]) {
		dbglogErr("CFL_SetExpression: %s was requested but failed to bake during InitCharModel - ignored\n",
			CFL_GetExpressionName(expression));
		return false;
	}
	if (!cm->valid || cm->maskPartIndex < 0) return false;

	cm->parts[cm->maskPartIndex].tex = cm->maskTexForExpr[expression];
	cm->expression = expression;
	dbglog("CFL_SetExpression: %s\n", CFL_GetExpressionName(expression));
	return true;
}

CFLExpression CFL_GetExpression(const CFLCharModel* cm)
{
	return cm ? cm->expression : CFL_EXPRESSION_NORMAL;
}

bool CFL_IsAvailableExpression(const CFLCharModel* cm, CFLExpression expression)
{
	if (!cm || (unsigned)expression >= CFL_EXPRESSION_COUNT) return false;
	return (cm->expressionFlags & CFL_EXPRESSION_FLAG(expression)) && cm->maskTexBaked[expression];
}

int CFL_GetPartCount(const CFLCharModel* cm)
{
	return cm ? cm->partCount : 0;
}

const CFLPart* CFL_GetPart(const CFLCharModel* cm, int index)
{
	if (!cm || index < 0 || index >= cm->partCount) return NULL;
	return &cm->parts[index];
}

CFLShaderLocations CFL_GetShaderLocations(void)
{
	CFLShaderLocations loc;
	loc.projection   = uLoc_projection;
	loc.modelView    = uLoc_modelView;
	return loc;
}

static C3D_LightEnv s_defaultLightEnv;
static C3D_Light s_defaultLight;
static C3D_LightLut s_defaultSpecularLut;
static bool s_defaultLightReady = false;

static void ensureDefaultShaderInit(void)
{
	if (s_defaultLightReady) return;
	s_defaultLightReady = true;
	C3D_LightEnvInit(&s_defaultLightEnv);
	
	C3D_LightEnvAmbient(&s_defaultLightEnv, 0.0f, 0.0f, 0.0f);
	LightLut_Phong(&s_defaultSpecularLut, 8.0f);
	C3D_LightEnvLut(&s_defaultLightEnv, GPU_LUT_D0, GPU_LUTINPUT_NH, false, &s_defaultSpecularLut);
	C3D_LightInit(&s_defaultLight, &s_defaultLightEnv);
	C3D_FVec lightDir = FVec4_New(-0.53906f, 0.53906f, 0.64697f, 0.0f);
	C3D_LightPosition(&s_defaultLight, &lightDir);
	C3D_LightAmbient(&s_defaultLight, 1.0f, 1.0f, 1.0f);
	C3D_LightDiffuse(&s_defaultLight, 1.0f, 1.0f, 1.0f);
	C3D_LightSpecular0(&s_defaultLight, 0.99608f, 0.99608f, 0.99608f);
	C3D_LightSpecular1(&s_defaultLight, 0.0f, 0.0f, 0.0f);
	C3D_LightEnable(&s_defaultLight, true);
}

void CFL_BindDefaultShader(void)
{
	ensureDefaultShaderInit();
	C3D_LightEnvBind(&s_defaultLightEnv);
}

void CFL_SetDefaultMaterial(const float color[3], bool noSpecular)
{
	static const float kShadowColor[3] = { 0.10196f, 0.09020f, 0.07843f };
	C3D_Material mtl;
	for (int i = 0; i < 3; i++) {
		float c = color[2 - i];
		mtl.ambient[i] = c * (1.0f - kShadowColor[i]);
		mtl.diffuse[i] = c * kShadowColor[i];
	}
	float spec = noSpecular ? 0.0f : 0.42f;
	mtl.specular0[0] = mtl.specular0[1] = mtl.specular0[2] = spec;
	mtl.specular1[0] = mtl.specular1[1] = mtl.specular1[2] = 0.0f;
	mtl.emission[0] = mtl.emission[1] = mtl.emission[2] = 0.0f;
	C3D_LightEnvMaterial(&s_defaultLightEnv, &mtl);
}

void CFL_DrawIconHead(CFLCharModel* cm, CFLExpression expression, const CFLIconSetting* setting, const C3D_Mtx* iconProjection, const C3D_Mtx* modelView)
{
	if (!cm || !cm->valid || !iconProjection || !modelView) return;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, iconProjection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView,  modelView);

	C3D_Tex* iconMaskTex = NULL;
	if (expression >= 0 && expression < CFL_EXPRESSION_COUNT && cm->maskTexBaked[expression])
		iconMaskTex = &cm->maskTexForExpr[expression];

	CFLIconCustomCallback customCallback = setting ? setting->customCallback : NULL;
	void* customArgument = setting ? setting->customArgument : NULL;

	for (int pass = 0; pass < 2; pass++) {
		bool texturedPass = (pass == 1);
		// Separate RGB blending from coverage accumulation. Using SRC_ALPHA
		// for alpha punches translucent holes in opaque skin beneath MASK.
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
		for (int i = 0; i < cm->partCount; i++) {
			const CFLPart* part = &cm->parts[i];
			if (part->hasTexture != texturedPass) continue;

			C3D_Tex* texToUse = (iconMaskTex && i == cm->maskPartIndex) ? iconMaskTex : (C3D_Tex*)&part->tex;

			C3D_DepthTest(true, GPU_LEQUAL, part->depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
			C3D_BufInfo* bufInfo = C3D_GetBufInfo();
			BufInfo_Init(bufInfo);
			BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

			if (part->hasTexture) C3D_TexBind(0, texToUse);

			if (customCallback) {
				customCallback(customArgument, part, iconProjection, modelView);
			} else {
				CFL_SetDefaultMaterial(part->color, part->noSpecular);

				C3D_TexEnv* env = C3D_GetTexEnv(0);
				if (part->hasTexture) {
					C3D_TexEnvInit(env);
					if (part->isAlphaOnly) {
						C3D_TexEnvSrc(env, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
						C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
					} else {
						C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, 0);
						C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
					}
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
				} else {
					C3D_TexEnvInit(env);
					C3D_TexEnvSrc(env, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
					C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
				}

                // RFL's icon material has a separate light/reflection sum.
                // PICA returns specular in SECONDARY, not PRIMARY; omitting
                // this stage left the configured native icon light invisible.
                C3D_TexEnv* shine = C3D_GetTexEnv(1);
                C3D_TexEnvInit(shine);
                C3D_TexEnvSrc(shine, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
                C3D_TexEnvFunc(shine, C3D_RGB, part->noSpecular ? GPU_REPLACE : GPU_ADD);
                C3D_TexEnvSrc(shine, C3D_Alpha, GPU_PREVIOUS, 0, 0);
                C3D_TexEnvFunc(shine, C3D_Alpha, GPU_REPLACE);

				if (part->capBlend) {
					C3D_TexEnv* env2 = C3D_GetTexEnv(2);
					C3D_TexEnvInit(env2);
					C3D_TexEnvSrc(env2, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, 0);
					C3D_TexEnvFunc(env2, C3D_RGB, GPU_ADD);
					C3D_TexEnvSrc(env2, C3D_Alpha, GPU_PREVIOUS, 0, 0);
					C3D_TexEnvFunc(env2, C3D_Alpha, GPU_REPLACE);
					C3D_DirtyTexEnv(env2);
				}
			}

			if (part->useIndices)
				C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
			else
				C3D_DrawArrays(GPU_TRIANGLES, 0, part->vertexCount);

			if (part->capBlend) {
				C3D_TexEnv* env2 = C3D_GetTexEnv(2);
				C3D_TexEnvInit(env2);
				C3D_DirtyTexEnv(env2);
			}
		}
	}
}

bool CFL_CommandMakeModelIcon(CFLCharModel* cm, CFLExpression expression, int iconSize, const CFLIconSetting* setting, C3D_Tex* outIcon)
{
	
	dbglog("CFL_CommandMakeModelIcon: start iconSize=%d\n", iconSize);

	if (!cm || !cm->valid || cm->partCount == 0 || iconSize < 8 || iconSize > 1024 ||
	    (iconSize & (iconSize - 1)) != 0 || !outIcon) return false;
	ensureDefaultShaderInit();

	if (outIcon->data) C3D_TexDelete(outIcon);
	if (!C3D_TexInitVRAM(outIcon, iconSize, iconSize, GPU_RGBA8)) {
		dbglog("CFL_CommandMakeModelIcon: C3D_TexInitVRAM failed (%dx%d)\n", iconSize, iconSize);
		return false;
	}
	C3D_RenderTarget* iconTarget = C3D_RenderTargetCreateFromTex(outIcon, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
	if (!iconTarget) {
		dbglog("CFL_CommandMakeModelIcon: C3D_RenderTargetCreateFromTex failed (%dx%d)\n", iconSize, iconSize);
		C3D_TexDelete(outIcon);
		return false;
	}

	C3D_Mtx iconProjection, iconModelView;
	Mtx_Persp(&iconProjection, C3D_AngleFromDegrees(9.8762f), 1.0f, 500.0f, 1000.0f, false);
	iconProjection.r[1].y = -iconProjection.r[1].y;
	iconProjection.r[2].x = 0.0f;
	iconProjection.r[2].y = 0.0f;
	iconProjection.r[2].z = 3.5f;
	iconProjection.r[2].w = 1750.0f;
	iconProjection.r[3].x = 0.0f;
	iconProjection.r[3].y = 0.0f;
	iconProjection.r[3].z = -1.0f;
	iconProjection.r[3].w = 0.0f;

	Mtx_Identity(&iconModelView);
	Mtx_Translate(&iconModelView, 0.0f, -34.5f, -600.0f, true);

	CFLIconBGType bgType = setting ? setting->bgType : CFL_ICON_BG_FAVORITE;
	u32 clearColor = 0x00000000;
	if (bgType == CFL_ICON_BG_DIRECT && setting) {
		clearColor =
			((u32)(setting->bgColor[0] * 255.0f) << 24) |
			((u32)(setting->bgColor[1] * 255.0f) << 16) |
			((u32)(setting->bgColor[2] * 255.0f) << 8)  |
			(u32)(setting->bgColor[3] * 255.0f);
	} else if (bgType == CFL_ICON_BG_FAVORITE) {
		const float* fc = CFL_GetFavoriteColor(cm->mii.mii_details.shirt_color);
		clearColor = ((u32)(fc[0] * 255.0f) << 24) | ((u32)(fc[1] * 255.0f) << 16) | ((u32)(fc[2] * 255.0f) << 8) | 0xFF;
	}

	bool hasCustomCallback = setting && setting->customCallback;

	dbglog("CFL_CommandMakeModelIcon: about to draw head\n");

	dbglog("CFL_CommandMakeModelIcon: about to C3D_FrameBegin(SYNCDRAW)\n");
	if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
		C3D_RenderTargetDelete(iconTarget);
		C3D_TexDelete(outIcon);
		return false;
	}
		dbglog("CFL_CommandMakeModelIcon: C3D_FrameBegin returned\n");
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		dbglog("CFL_CommandMakeModelIcon: GPUREG_FRAMEBUFFER_INVALIDATE written\n");
		if (bgType != CFL_ICON_BG_NO_CLEAR)
			C3D_RenderTargetClear(iconTarget, C3D_CLEAR_ALL, clearColor, 0xFFFFFF00);
		dbglog("CFL_CommandMakeModelIcon: C3D_RenderTargetClear returned\n");
		C3D_FrameDrawOn(iconTarget);
		dbglog("CFL_CommandMakeModelIcon: C3D_FrameDrawOn returned\n");
		C3D_SetViewport(0, 0, iconSize, iconSize);
		dbglog("CFL_CommandMakeModelIcon: C3D_SetViewport returned\n");
		if (!hasCustomCallback) C3D_LightEnvBind(&s_defaultLightEnv);
		dbglog("CFL_CommandMakeModelIcon: C3D_LightEnvBind returned\n");
		C3D_CullFace(GPU_CULL_NONE);
		dbglog("CFL_CommandMakeModelIcon: C3D_CullFace returned\n");

		{
			C3D_TexEnv* env1 = C3D_GetTexEnv(1);
			C3D_TexEnvInit(env1);
			C3D_TexEnvSrc(env1, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
			C3D_TexEnvFunc(env1, C3D_RGB, GPU_ADD);
			C3D_TexEnvSrc(env1, C3D_Alpha, GPU_PREVIOUS, 0, 0);
			C3D_TexEnvFunc(env1, C3D_Alpha, GPU_REPLACE);
			C3D_DirtyTexEnv(env1);
			C3D_TexEnv* env2 = C3D_GetTexEnv(2);
			C3D_TexEnvInit(env2);
			C3D_DirtyTexEnv(env2);
		}
		dbglog("CFL_CommandMakeModelIcon: TexEnv1/2 setup returned\n");

		C3D_Mtx headModelView = iconModelView;
		dbglog("CFL_CommandMakeModelIcon: about to draw head via cflIconDrawCharModelParts\n");
		CFL_DrawIconHead(cm, expression, setting, &iconProjection, &headModelView);
		dbglog("CFL_CommandMakeModelIcon: cflIconDrawCharModelParts (head) returned\n");

		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
	C3D_FrameEnd(0);

	dbglog("CFL_CommandMakeModelIcon: C3D_FrameEnd returned, finishing up\n");

	C3D_FrameSync();
	C3D_RenderTargetDelete(iconTarget);
	C3D_TexSetFilter(outIcon, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(outIcon, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	dbglog("CFL_CommandMakeModelIcon: done\n");
	return true;
}

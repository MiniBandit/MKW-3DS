
#pragma once

#include <3ds.h>
#include <citro3d.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CFL_EXPRESSION_NORMAL = 0,
	CFL_EXPRESSION_SMILE,
	CFL_EXPRESSION_ANGER,
	CFL_EXPRESSION_SORROW,
	CFL_EXPRESSION_SURPRISE,
	CFL_EXPRESSION_BLINK,
	CFL_EXPRESSION_OPENMOUTH,
	CFL_EXPRESSION_SMILE_OM,
	CFL_EXPRESSION_ANGER_OM,
	CFL_EXPRESSION_SORROW_OM,
	CFL_EXPRESSION_SURPRISE_OM,
	CFL_EXPRESSION_BLINK_OM,
	CFL_EXPRESSION_WINK_L,
	CFL_EXPRESSION_WINK_R,
	CFL_EXPRESSION_WINK_L_OM,
	CFL_EXPRESSION_WINK_R_OM,
	CFL_EXPRESSION_LIKE_WINK_L,
	CFL_EXPRESSION_LIKE_WINK_R,
	CFL_EXPRESSION_FRUSTRATED,
	CFL_EXPRESSION_COUNT
} CFLExpression;

typedef u32 CFLExpressionFlag;
#define CFL_EXPRESSION_FLAG(e) (1u << (u32)(e))
#define CFL_EXPRESSION_FLAG_ALL ((1u << CFL_EXPRESSION_COUNT) - 1u)

typedef int CFLResolution;
#define CFL_RESOLUTION_64   64
#define CFL_RESOLUTION_128  128
#define CFL_RESOLUTION_256  256
#define CFL_RESOLUTION_512  512
#define CFL_RESOLUTION_1024 1024

// Public immutable draw-vertex layout; consumers may copy into their scene shader.
typedef struct { float position[3], normal[3], texcoord[2]; } CFLVertex;

typedef struct {
	void* vbo;
	void* ibo;
	u32 vertexCount;
	u32 indexCount;
	bool useIndices;
	
	float color[3];
	bool hasTexture;
	C3D_Tex tex;
	
	bool needsTint;
	
	bool isAlphaOnly;
	
	bool depthWrite;
	
	bool noSpecular;
	
	bool capBlend;
} CFLPart;

#define CFL_MAX_PARTS 12

// Low two model-flag bits recovered from CFLi_InitResCharModel and the
// matching Miiverse FUN_00187458. Names here are reimplementation API names.
typedef enum {
	CFL_HAIR_NORMAL = 0,
	CFL_HAIR_HAT = 1, // alternate meshes fitted beneath an app-supplied hat
	CFL_HAIR_HIDDEN = 2, // omit cap, hair and forehead together
} CFLHairMode;

typedef struct {
	CFLPart parts[CFL_MAX_PARTS];
	int partCount;
	int maskPartIndex;
	C3D_Tex maskTexForExpr[CFL_EXPRESSION_COUNT];
	bool maskTexBaked[CFL_EXPRESSION_COUNT];
	CFLExpressionFlag expressionFlags;
	CFLExpression expression;
	MiiData mii;
	bool valid;
} CFLCharModel;

bool CFL_IsAvailable(void);

bool CFL_Initialize(void);

void CFL_Finalize(void);

void CFL_EnableSDDebug(bool enable);

void CFL_FlushSDDebug(void);

void dbglog(const char* fmt, ...);

void dbglogErr(const char* fmt, ...);

void dbglogVramStats(const char* context, bool onScreen);

bool CFL_MakeStoreData(const MiiData* mii, CFLStoreData* out);
bool CFL_IsStoreDataValid(const CFLStoreData* storeData);

bool CFL_SearchOfficialData(const MiiData* mii, u16* outIndex);

bool CFL_IsAvailableOfficialData(u16 index);
bool CFL_GetOfficialData(u16 index, MiiData* outMii);
int CFL_GetAvailableOfficialDataNum(void);

bool CFL_GetMyMiiIndex(u16* outIndex);

int CFL_GetWorkSize(bool hdModeEnabled);

const float* CFL_GetFavoriteColor(u8 index);

bool CFL_InitCharModel(CFLCharModel* model, const MiiData* mii, CFLResolution resolution, CFLExpressionFlag expressionFlags);

// Explicit hair/hat mode; the original entry point defaults to NORMAL.
// Rebuild the model to change mode. This does not invent/load a hat mesh.
bool CFL_InitCharModelWithHairMode(CFLCharModel* model, const MiiData* mii, CFLResolution resolution, CFLExpressionFlag expressionFlags, CFLHairMode hairMode);

void CFL_DeleteModel(CFLCharModel* model);

bool CFL_HasCharModel(const CFLCharModel* model);

bool CFL_SetExpression(CFLCharModel* model, CFLExpression expression);
CFLExpression CFL_GetExpression(const CFLCharModel* model);
const char* CFL_GetExpressionName(CFLExpression expression);

bool CFL_IsAvailableExpression(const CFLCharModel* model, CFLExpression expression);

int CFL_GetPartCount(const CFLCharModel* model);
const CFLPart* CFL_GetPart(const CFLCharModel* model, int index);

typedef struct {
	int projection;
	int modelView;
} CFLShaderLocations;

CFLShaderLocations CFL_GetShaderLocations(void);

void CFL_BindDefaultShader(void);
void CFL_SetDefaultMaterial(const float color[3], bool noSpecular);

void CFL_RebindShader(void);

typedef enum {
	CFL_ICON_BG_FAVORITE = 0,
	CFL_ICON_BG_DIRECT = 1,
	CFL_ICON_BG_NO_CLEAR = 2,
} CFLIconBGType;

typedef void (*CFLIconCustomCallback)(void* customArgument, const CFLPart* part, const C3D_Mtx* projection, const C3D_Mtx* modelView);

typedef struct {
	CFLIconBGType bgType;
	float bgColor[4];
	CFLIconCustomCallback customCallback;
	void* customArgument;
} CFLIconSetting;

// Reimplementation extension: draw the head into the caller's active icon
// target, allowing custom bodies/cameras without duplicating CFL's renderer.
// Call inside a C3D frame with the CFL shader, lighting and attributes set up.
// Does not allocate, begin/end frames, or own the supplied model/matrices.
void CFL_DrawIconHead(CFLCharModel* model, CFLExpression expression, const CFLIconSetting* setting, const C3D_Mtx* projection, const C3D_Mtx* modelView);

bool CFL_CommandMakeModelIcon(CFLCharModel* model, CFLExpression expression, int iconSize, const CFLIconSetting* setting, C3D_Tex* outIcon);

#ifdef __cplusplus
}
#endif

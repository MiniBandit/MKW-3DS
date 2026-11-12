# cfl-mii

A reimplementation of Nintendo's CFL Mii-rendering library for 3DS
homebrew, built on top of libctru/citro3d.

It opens the console's system `CFL_Res.dat` Mii resource archive,
parses it, and builds a fully-shaded, textured 3D character model
(head, hair, face, eyes, eyebrows, mouth, nose, glasses, facial hair)
from a `MiiData` struct - the same data libctru's Mii Selector applet
hands you. It can also search/read the console's real Mii database
(`CFL_DB.dat`) and encode/decode the standard `CFLStoreData` exchange
format.

Reverse-engineered from a retail 3DS title binary's debug info and
cross-referenced against the real, compiled RFL (Wii) and FFL (Wii U)
Mii libraries - real function/type/constant names throughout, not
invented ones, wherever the decompile could confirm them.

## Building and using as a library

This repo is a static library. With devkitARM/devkitPro installed:

```
make            # builds lib/libCFL.a
make install    # copies include/ and lib/ into $DEVKITPRO/portlibs/3ds
```

Use it from another 3DS project by adding this directory (or the install
location) to `LIBDIRS`, `-lCFL` before `-lcitro3d`/`-lctru` in `LIBS`, and
`#include <cfl_mii.h>` (public header in `include/`). The GPU shader is
assembled into the library, so nothing else needs to be shipped. The companion
demo app [cfl-tool](https://github.com/jamesrhg/cfl-tool) consumes it exactly
this way, as a git submodule.

**Head only, on purpose.** This library builds and renders a Mii
*head* - nothing below the neck. Earlier versions experimented with
also owning body-model loading/skinning/animation (real IQM files,
`CFL_LoadBodyModel`/`CFL_AttachBody`/`CFL_PoseBodyModel`) directly in
this library; that was a real, working feature for a while, but it
never matched what real CFL itself does (confirmed via the decompile:
`CFLIconSetting`'s own real struct has no body field, and nothing in
real CFL's own API takes one either - attaching a body to a head is an
application-level concern in real Mii software too, the same way
`ariankordi/FFL.js`'s own real `attachHeadToBody` lives outside FFL
itself). All of that code has been removed - this library is now
exactly the same shape real CFL's own public API is: it builds a head,
optionally renders it to an icon texture, and does nothing else.
**If you want a full-body Mii (real IQM body models, per-Mii build/
height scaling, skeletal animation playback, head-on-body icons), see
[cfl-tool](https://github.com/jamesrhg/cfl-tool)** - the companion demo
app that consumes this library, which owns all of that entirely on the
app side, using nothing but this library's own public surface
(`CFLCharModel`/`CFLPart`, `CFL_GetShaderLocations`,
`CFL_BindDefaultShader`/`CFL_SetDefaultMaterial`) to draw a head
alongside a body it loads and skins itself. `cfl-tool`'s own `main.c`
is a real, complete, working reference for exactly how to do this
against this library as it exists today.

## Building

This is a source library, not a standalone app - it has no `main()`.
Add `source/` to a devkitARM 3DS project's `SOURCES`/`INCLUDES` and
link against `citro3d`/`ctru`.

## Design

- **Instance-based.** `CFLCharModel` is a plain, caller-owned struct
  (`CFLCharModel model = {0};` on the stack, in an array, wherever) -
  this library never allocates one itself, only fills in one you
  already own. Hold as many as you want at once.
- **Data only, not drawing.** `CFL_InitCharModel` builds vertex/index
  buffers and baked textures; it never issues a draw call. Real CFL
  doesn't either (confirmed via the decompile) - rendering (camera,
  blend/depth state, draw order, lighting) is entirely your own job.
  `CFL_GetPartCount`/`CFL_GetPart` hand you the built data; an optional
  default shader (`CFL_BindDefaultShader`/`CFL_SetDefaultMaterial`) is
  there if you don't want to write your own. The one deliberate
  exception is `CFL_CommandMakeModelIcon`, which really does render (a
  real function whose whole job is producing a picture).
- **`bool`, not `CFLResult`.** Real CFL returns a `CFLResult` enum this
  project never captured the exact values of; every function here
  returns `bool` instead (`true` = success) to avoid guessing at
  error-code semantics on top of everything else.
- **Standard formats, not invented ones, wherever a real standard
  applies.** Mii data itself is the real 3DS `MiiData` layout
  (libctru's own struct); Mii exchange data is the real `CFLStoreData`
  wrapper.

## API reference

### Lifecycle

```c
bool CFL_Initialize(void);
void CFL_Finalize(void);
bool CFL_IsAvailable(void);
void CFL_EnableSDDebug(bool enable);
```

- **`CFL_Initialize()`** - call once, after `C3D_Init()`. Opens the
  system `CFL_Res.dat` archive, sets up the shared shader/vertex
  attributes every `CFL_InitCharModel` call needs, and caches the
  console's real Mii database (`CFL_DB.dat`) if one exists. Returns
  `false` if the archive can't be opened - almost always means the app
  wasn't launched with full ARM11 filesystem permissions (launch via
  Luma3DS/Rosalina's homebrew launcher, not a plain 3dsx loader).
- **`CFL_Finalize()`** - call once, after destroying every
  `CFLCharModel`, before `C3D_Fini()`. Frees the shader program and
  the archive buffer.
- **`CFL_IsAvailable()`** - `true` once `CFL_Initialize` has succeeded
  and `CFL_Finalize` hasn't been called since. Cheap to check anywhere.
- **`CFL_EnableSDDebug(true/false)`** - opens/closes this library's own
  `sdmc:/3ds/cfl_test.txt` diagnostic log (off by default). Useful
  while developing - every archive-parse and model-build step logs
  what it's doing and why a part was skipped, if one was.

### Character models

`CFL_InitCharModelWithHairMode(model, mii, resolution, expressionFlags, mode)`
adds explicit `CFL_HAIR_NORMAL`, `CFL_HAIR_HAT` and `CFL_HAIR_HIDDEN` modes.
Hat mode selects the alternate meshes designed to fit beneath an app-owned
hat; it does not supply a hat model. The original initialization function
defaults to normal hair. See [`docs/cfl-miiverse-comparison.md`](docs/cfl-miiverse-comparison.md) for
the Miiverse function mappings and validation limits.

```c
typedef struct {
    CFLPart parts[CFL_MAX_PARTS];
    int partCount;
    MiiData mii;
    bool valid;
    // ...
} CFLCharModel;

bool CFL_InitCharModel(CFLCharModel* model, const MiiData* mii,
                        CFLResolution resolution, CFLExpressionFlag expressionFlags);
void CFL_DeleteModel(CFLCharModel* model);
bool CFL_HasCharModel(const CFLCharModel* model);
```

- **`CFL_InitCharModel(model, mii, resolution, expressionFlags)`** -
  builds a full character model (face, mask, hair, cap, goatee, nose,
  glasses - everything this library knows how to render) from a real
  `MiiData` (the same struct libctru's Mii Selector applet returns).
  `model` must be zero-initialized before its *first* use (`= {0}`,
  `memset`, or plain static/global storage) - every call after that is
  always safe, since `CFL_InitCharModel` tears down whatever `model`
  previously held before rebuilding it, so calling it again on the
  same instance with a different Mii is the normal way to "change"
  a model.
  `resolution` is one of the `CFL_RESOLUTION_*` constants below and
  controls only the eye/eyebrow/mouth/mustache/mole decal canvas -
  `expressionFlags` (`CFL_EXPRESSION_FLAG(e)` OR'd together, or
  `CFL_EXPRESSION_FLAG_ALL`) declares up front which facial
  expressions this model will ever need; one texture is pre-baked per
  requested expression, so switching later is instant. **Real memory
  cost to plan around**: each baked expression texture is
  `resolution * resolution * 4` bytes - requesting every expression at
  a high resolution for several simultaneous models is a real way to
  exhaust VRAM on original-model 3DS hardware. Request only what you
  need. Returns `false` only if the face itself couldn't be built (or
  `CFL_Initialize` was never called); every other part is optional and
  silently skipped on failure (check the SD log via
  `CFL_EnableSDDebug(true)` if a Mii is missing a part you expected).
  **Real side effect, matching real CFL exactly**: a successful build
  also adds the Mii to the console's real "recently seen" Mii list
  (skipped automatically if it's already in your saved collection) -
  this is what real `CFL_InitCharModel` does too, not something this
  library adds on top.
- **`CFL_DeleteModel(model)`** - frees every part and every pre-baked
  expression texture `model` owns. Safe on a zeroed or already-deleted
  instance. `CFL_InitCharModel` already calls this internally before
  rebuilding, so you only need to call it yourself for explicit
  cleanup (e.g. before the instance's own storage - a stack frame, a
  freed array slot - goes away).
- **`CFL_HasCharModel(model)`** - `true` once `CFL_InitCharModel` has
  successfully built at least the face.

```c
typedef int CFLResolution;
#define CFL_RESOLUTION_64   64
#define CFL_RESOLUTION_128  128
#define CFL_RESOLUTION_256  256
#define CFL_RESOLUTION_512  512
#define CFL_RESOLUTION_1024 1024
```

### Expressions

```c
typedef enum {
    CFL_EXPRESSION_NORMAL = 0, CFL_EXPRESSION_SMILE, CFL_EXPRESSION_ANGER,
    CFL_EXPRESSION_SORROW, CFL_EXPRESSION_SURPRISE, CFL_EXPRESSION_BLINK,
    CFL_EXPRESSION_OPENMOUTH, CFL_EXPRESSION_SMILE_OM, CFL_EXPRESSION_ANGER_OM,
    CFL_EXPRESSION_SORROW_OM, CFL_EXPRESSION_SURPRISE_OM, CFL_EXPRESSION_BLINK_OM,
    CFL_EXPRESSION_WINK_L, CFL_EXPRESSION_WINK_R, CFL_EXPRESSION_WINK_L_OM,
    CFL_EXPRESSION_WINK_R_OM, CFL_EXPRESSION_LIKE_WINK_L, CFL_EXPRESSION_LIKE_WINK_R,
    CFL_EXPRESSION_FRUSTRATED, CFL_EXPRESSION_COUNT
} CFLExpression;

#define CFL_EXPRESSION_FLAG(e) (1u << (u32)(e))
#define CFL_EXPRESSION_FLAG_ALL ((1u << CFL_EXPRESSION_COUNT) - 1u)

bool CFL_SetExpression(CFLCharModel* model, CFLExpression expression);
CFLExpression CFL_GetExpression(const CFLCharModel* model);
const char* CFL_GetExpressionName(CFLExpression expression);
bool CFL_IsAvailableExpression(const CFLCharModel* model, CFLExpression expression);
```

- **`CFL_SetExpression(model, expr)`** - switches the model's bound
  MASK texture. `expr` must have been included in `expressionFlags` at
  `CFL_InitCharModel` time *and* have baked successfully - check
  `CFL_IsAvailableExpression` first if you're not sure (see
  [Validation](#validation-checklist) below). Instant - no GPU work
  beyond a texture rebind, no rebuild.
- **`CFL_GetExpression(model)`** - the currently-bound expression.
- **`CFL_GetExpressionName(expr)`** - a plain C string (`"Normal"`,
  `"Smile"`, ...), handy for debug overlays.
- **`CFL_IsAvailableExpression(model, expr)`** - `true` if `expr` can
  be switched to *right now* (requested **and** successfully baked -
  a request can still fail per-expression under VRAM pressure).

### Render data

```c
typedef struct {
    void* vbo; void* ibo;
    u32 vertexCount, indexCount;
    bool useIndices;
    float color[3];
    bool hasTexture;
    C3D_Tex tex;
    bool needsTint;      // true: multiply `color` in. false: texture RGB is already correct.
    bool isAlphaOnly;    // true: REPLACE with `color` for RGB. false: MODULATE by texture RGB.
    bool depthWrite;     // false for decal overlays (MASK, nose canvas) sitting on another part.
    bool noSpecular;     // true for flat 2D overlays that read wrong with a specular highlight.
    bool capBlend;       // true only for CAP - see its own note below.
} CFLPart;

int CFL_GetPartCount(const CFLCharModel* model);
const CFLPart* CFL_GetPart(const CFLCharModel* model, int index);

typedef struct { int projection; int modelView; } CFLShaderLocations;
CFLShaderLocations CFL_GetShaderLocations(void);
void CFL_RebindShader(void);
```

- **`CFL_GetPartCount`/`CFL_GetPart`** - the built model as a plain
  array of draw-ready parts. Loop over them and draw however you like
  (see the rendering example below for the exact TEV/blend setup this
  library's own parts expect - `needsTint`/`isAlphaOnly`/`capBlend`
  specifically matter, getting them backwards silently discards real
  texture detail, tints the wrong thing, or colors a hat's own
  underside pitch black instead of the real half-brightness favorite
  color real CFL shows there).
- **`part->capBlend`** - `true` only for the CAP part. Real CFL's own
  cap-tint formula isn't a plain modulate the way every other tinted
  part is - it's `favoriteColor * (texColor + 1) / 2`, so the
  texture's own real intensity only ever chooses between half- and
  full-brightness favorite color, never all the way down to black.
  `color` is already pre-halved by this library for a `capBlend` part,
  so a plain `texture * color` modulate (stage 0) gets you halfway
  there - drawing a `capBlend` part needs one extra TEV stage,
  `PREVIOUS + color` (additive), bracketed around *just that part's*
  own draw call (see the example below). Every other part is unaffected
  either way.
- **`CFL_GetShaderLocations()`** - the shared vertex shader's
  `projection`/`modelView` uniform locations, so you can upload your
  own camera matrices each frame. This library has no opinion on
  lighting or camera - those are entirely your choice, same as real
  CFL (confirmed via RFL's own real source: each game supplies its own
  light values, the library never fixes one).
- **`CFL_RebindShader()`** - **call this once per frame, before
  drawing any `CFLPart`, if you also use citro2d (or anything else
  that touches the active shader/attribute state) anywhere in the same
  frame.** citro2d's own `C2D_Prepare()`/text calls silently rebind
  their own default shader; without re-binding this library's own
  shader afterward, every subsequent `CFLPart` draw call breaks. Not
  needed in a citro3d-only app (the binding sticks for the whole
  program).

### Default shader (optional)

```c
void CFL_BindDefaultShader(void);
void CFL_SetDefaultMaterial(const float color[3], bool noSpecular);
```

A ready-made shading implementation you can use instead of writing
your own `C3D_LightEnv`/material setup - matching real FFL's own
`FFLDefaultShader` role (a convenience the library provides, never
forced). `CFL_BindDefaultShader()` once per frame before drawing any
part with it; `CFL_SetDefaultMaterial(part->color, part->noSpecular)`
once per part, right before its draw call. Nothing else in this
library depends on either being called - build your own lighting from
scratch if you want different shading.

### Body models

Not part of this library. See the note at the top of this README - a
full-body Mii (loading a real IQM body, per-Mii build/height scaling,
skeletal animation, head-on-body icons) is entirely
[cfl-tool](https://github.com/jamesrhg/cfl-tool)'s own job, built
against nothing but this library's own public API. The rest of this
section used to document `CFL_LoadBodyModel`/`CFL_AttachBody`/
`CFL_PoseBodyModel` and friends - all removed; kept here, briefly, only
so old links/searches land somewhere useful.

### Icon rendering

```c
typedef enum {
    CFL_ICON_BG_FAVORITE = 0,  // fill with the Mii's own favorite color
    CFL_ICON_BG_DIRECT = 1,    // fill with setting->bgColor
    CFL_ICON_BG_NO_CLEAR = 2,  // don't clear the canvas at all
} CFLIconBGType;

typedef void (*CFLIconCustomCallback)(void* customArgument, const CFLPart* part,
                                       const C3D_Mtx* projection, const C3D_Mtx* modelView);

typedef struct {
    CFLIconBGType bgType;
    float bgColor[4];                     // used when bgType == CFL_ICON_BG_DIRECT
    CFLIconCustomCallback customCallback;  // NULL = use this library's own default shading
    void* customArgument;
} CFLIconSetting;

bool CFL_CommandMakeModelIcon(CFLCharModel* model, CFLExpression expression,
                               int iconSize, const CFLIconSetting* setting, C3D_Tex* outIcon);
```

Real CFL function (`CFL_CommandMakeModelIcon`, DWARF-confirmed), and
the one real exception to "CFL doesn't draw" - its whole job is
producing a rendered square texture, using real CFL's own fixed icon
camera (not customizable). `expression` must be one of the bits
declared at `CFL_InitCharModel` time; if it wasn't successfully baked,
the icon falls back to whichever expression is currently bound rather
than failing (this function never mutates `model`). `setting` may be
`NULL` for plain defaults.

Head only, same as every other draw path in this library now - no body
attachment option here at all. `cfl-tool`'s own `appMakeModelIconWithBody`
is a real, complete, from-scratch reimplementation of this exact
function's own real camera/render-target/depth setup, plus a body drawn
alongside it entirely on the app side - see
[cfl-tool](https://github.com/jamesrhg/cfl-tool) if that's what you need.

**Ownership**: `CFL_CommandMakeModelIcon` creates a fresh render
target, draws into it, and tears the render target back down again -
all within the one call. `*outIcon` is a completely ordinary
`C3D_Tex`; you own it exactly the way you'd own any other texture this
library hands back (e.g. `CFLPart.tex`) and free it with a plain
`C3D_TexDelete(&icon)` when you're done. Calling
`CFL_CommandMakeModelIcon` again on the *same* `C3D_Tex` (e.g. to
render a new expression) is always safe without freeing it yourself
first - this function frees any previous render already on that struct
automatically before making a new one, so repeated calls on one
persistent `C3D_Tex` never leak.

### Mii database access

```c
bool CFL_SearchOfficialData(const MiiData* mii, u16* outIndex);
bool CFL_IsAvailableOfficialData(u16 index);
bool CFL_GetOfficialData(u16 index, MiiData* outMii);
int  CFL_GetAvailableOfficialDataNum(void);
bool CFL_GetMyMiiIndex(u16* outIndex);
```

Real, read-only access to the console's own Mii database
(`CFL_DB.dat`, the same 100-slot store Mii Maker/the Mii Selector use)
- cached once at `CFL_Initialize` time, matching real CFL's own real
init order. **All of these can legitimately fail** if the console has
never had Mii Maker opened (no database file exists yet) - always
check the return value, don't assume the database is there.

- **`CFL_SearchOfficialData(mii, &index)`** - find a specific Mii's
  real slot (0-99) by identity (Mii ID + creator MAC, not by name).
  This is the real mechanism games use to make the Mii Selector applet
  reopen on the Mii it last showed (`miiSelectorSetInitialIndex`).
- **`CFL_IsAvailableOfficialData(index)`** - does this raw slot
  (0-99) hold a Mii at all?
- **`CFL_GetOfficialData(index, &mii)`** - fetch the actual `MiiData`
  at a slot.
- **`CFL_GetAvailableOfficialDataNum()`** - how many of the 100 slots
  are occupied. Returns `-1` if the database isn't available at all
  (distinct from a real `0`, which means "database exists, but
  empty").
- **`CFL_GetMyMiiIndex(&index)`** - the console owner's own Mii index.
  Returns `false` (with `*outIndex` still set to `0`) if unavailable -
  a caller that doesn't check the return sees the same "default to 0"
  behavior real CFL's own callers would, but checking it tells you
  whether that `0` is real.

### Store data (export/import)

```c
bool CFL_MakeStoreData(const MiiData* mii, CFLStoreData* out);
bool CFL_IsStoreDataValid(const CFLStoreData* storeData);
```

`CFLStoreData` (libctru's own real struct, `<3ds/mii.h>`) is the
standard checksummed wrapper Miis are exchanged in (QR codes,
StreetPass, NFC) - `{ MiiData miiData; u8 pad[2]; u16 crc16; }`.
`CFL_MakeStoreData` computes the real CRC16-CCITT self-check (zero the
checksum field, CRC the whole 96-byte structure including the zeroed
field, write the result back) and fills `out`. `CFL_IsStoreDataValid`
re-runs that same check and confirms it comes out to `0` - use this on
any `CFLStoreData` you didn't just create yourself (e.g. one decoded
from a QR code or received over StreetPass) before trusting its
`miiData` field.

### Utility

```c
const float* CFL_GetFavoriteColor(u8 index);
int CFL_GetWorkSize(bool hdModeEnabled);
void dbglog(const char* fmt, ...);
void dbglogErr(const char* fmt, ...);
void dbglogVramStats(const char* context, bool onScreen);
```

- **`CFL_GetFavoriteColor(index)`** - the real 12-entry favorite-color
  table as an `{r,g,b}` float triple. Out-of-range indices clamp to
  the last entry (11/black), matching real CFL's own real behavior -
  not a fallback to index 0.
- **`CFL_GetWorkSize(hdModeEnabled)`** - informational only (this
  library manages its own citro3d allocations, it never takes real
  CFL's memory-arena parameters) - shows what real CFL's own work
  buffer would have needed for the same operation.
- **`dbglog`/`dbglogErr`/`dbglogVramStats`** - printf-style logging
  into the log `CFL_EnableSDDebug` opens - `dbglogErr` also prints to
  the bottom-screen console, for genuine failures worth surfacing
  immediately.

## Validation checklist

Real, easy-to-hit failure cases this library reports rather than
crashing on - check these rather than assuming success:

- **`CFL_Initialize()` can fail** - almost always missing ARM11 FS
  permissions. Don't call anything else in this library if it returns
  `false`.
- **`CFL_InitCharModel` can partially fail** - it only returns `false`
  if the face itself couldn't be built; every other part (hair, cap,
  goatee, nose, glasses, and each individual pre-baked expression) can
  silently fail on its own (most commonly VRAM exhaustion from
  requesting too many expressions/too high a resolution across too
  many simultaneous models). Check `CFL_HasCharModel` for the
  minimum-viable case, and the SD log (`CFL_EnableSDDebug(true)`) for
  exactly what got skipped.
- **`CFL_SetExpression` fails silently on an unavailable expression**
  (wrong flag, or that one failed to bake) - it leaves the current
  expression alone rather than doing anything destructive, but check
  `CFL_IsAvailableExpression` first if your UI needs to know in
  advance whether a switch will work.
- **Every `CFL_*OfficialData` function can fail if Mii Maker has never
  been opened on this console** - there's no `CFL_DB.dat` to read yet.
  This is a normal, expected state (common on a fresh console/emulator
  profile), not an error to alarm the user over.
- **`CFL_IsStoreDataValid` before trusting any `CFLStoreData` you
  didn't create yourself** - a corrupted or malformed QR code/NFC tag
  should never be fed straight into `CFL_InitCharModel` unchecked.
- **`CFLCharModel` must be zero-initialized before its first use** -
  static/global storage already gets this for free from C; a stack
  variable needs `= {0}` explicitly.

## Examples

All of these compile and link as-is against nothing but this library,
libctru, and citro3d - no other project files needed. For a full-body
example (loading/skinning/animating a real IQM body alongside a head
built from this library), see
[cfl-tool](https://github.com/jamesrhg/cfl-tool)'s own `main.c` instead -
that's real, complete, app-side code, not something this library's own
README demonstrates anymore.
that one standalone.

### Rendering a CharModel with the default shader

```c
#include <3ds.h>
#include <citro3d.h>
#include "cfl_mii.h"

typedef struct { float position[3]; float normal[3]; float texcoord[2]; } Vertex;

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

// The actual per-part draw loop, taking an explicit modelView - factored
// out this way (rather than building a fixed modelView internally) so
// the "body model" example further down can reuse this unchanged and
// just hand it a DIFFERENT modelView (the head repositioned to a body's
// own neck bone) instead of duplicating this whole loop.
static void drawModelPartsAt(const CFLCharModel* model, const C3D_Mtx* projection, const C3D_Mtx* modelView)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView, modelView);

	CFL_BindDefaultShader();

	// Stage 1 adds the default shader's specular term on top of stage
	// 0's diffuse+ambient result - required for CFL_SetDefaultMaterial's
	// specular to actually show up.
	C3D_TexEnv* env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env1);
	C3D_TexEnvSrc(env1, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
	C3D_TexEnvFunc(env1, C3D_RGB, GPU_ADD);
	C3D_TexEnvSrc(env1, C3D_Alpha, GPU_PREVIOUS, 0, 0);
	C3D_TexEnvFunc(env1, C3D_Alpha, GPU_REPLACE);
	C3D_DirtyTexEnv(env1);

	int partCount = CFL_GetPartCount(model);
	for (int pass = 0; pass < 2; pass++) {
		bool texturedPass = (pass == 1);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

		for (int i = 0; i < partCount; i++) {
			const CFLPart* part = CFL_GetPart(model, i);
			if (part->hasTexture != texturedPass) continue;

			C3D_DepthTest(true, GPU_GEQUAL, part->depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);

			C3D_BufInfo* bufInfo = C3D_GetBufInfo();
			BufInfo_Init(bufInfo);
			BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

			CFL_SetDefaultMaterial(part->color, part->noSpecular);

			C3D_TexEnv* env0 = C3D_GetTexEnv(0);
			if (part->hasTexture) {
				C3D_TexBind(0, (C3D_Tex*)&part->tex);
				C3D_TexEnvInit(env0);
				// Alpha-only formats (A4/A8) have undocumented RGB output
				// on real hardware - REPLACE with the tint directly.
				// Luminance formats (I4/I8/IA4/IA8) carry real per-pixel
				// pattern data - MODULATE preserves it.
				if (part->isAlphaOnly) {
					C3D_TexEnvSrc(env0, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
					C3D_TexEnvFunc(env0, C3D_RGB, GPU_REPLACE);
				} else {
					C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, 0);
					C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
				}
				C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, 0, 0);
				C3D_TexEnvFunc(env0, C3D_Alpha, GPU_REPLACE);
			} else {
				C3D_TexEnvInit(env0);
				C3D_TexEnvSrc(env0, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
				C3D_TexEnvFunc(env0, C3D_Both, GPU_REPLACE);
			}

			// CAP's own real tint formula needs one extra additive stage -
			// see CFLPart.capBlend's own note above. Bracketed around just
			// this one part's own draw call, nothing else.
			if (part->capBlend) {
				C3D_TexEnv* env2 = C3D_GetTexEnv(2);
				C3D_TexEnvInit(env2);
				C3D_TexEnvSrc(env2, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, 0);
				C3D_TexEnvFunc(env2, C3D_RGB, GPU_ADD);
				C3D_TexEnvSrc(env2, C3D_Alpha, GPU_PREVIOUS, 0, 0);
				C3D_TexEnvFunc(env2, C3D_Alpha, GPU_REPLACE);
				C3D_DirtyTexEnv(env2);
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

static void drawModel(const CFLCharModel* model, const C3D_Mtx* projection)
{
	C3D_Mtx modelView;
	Mtx_Identity(&modelView);
	Mtx_Translate(&modelView, 0.0f, 0.0f, -2.0f, true);
	Mtx_Scale(&modelView, 0.032f, 0.032f, 0.032f);
	drawModelPartsAt(model, projection, &modelView);
}

int main(void)
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	if (!CFL_Initialize()) {
		gfxExit();
		return 1;
	}

	// Real Mii data from the system Mii Selector applet - CFL only
	// builds a model from MiiData you hand it, it has no opinion on how
	// you got it.
	MiiSelectorConf conf;
	MiiSelectorReturn ret;
	miiSelectorInit(&conf);
	miiSelectorLaunch(&conf, &ret);

	CFLCharModel model = {0}; // must be zero-initialized before first use
	if (!CFL_InitCharModel(&model, &ret.mii, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		// Face itself failed to build - CFL_Initialize probably didn't
		// succeed, or the archive is missing/corrupt. Bail out rather
		// than drawing a model with no parts.
		CFL_Finalize();
		C3D_Fini();
		gfxExit();
		return 1;
	}

	C3D_Mtx projection;
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(50.0f), C3D_AspectRatioTop, 0.01f, 1000.0f, false);

	CFL_RebindShader();

	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x404040FF, 0);
			C3D_FrameDrawOn(target);
			drawModel(&model, &projection);
		C3D_FrameEnd(0);
	}

	CFL_DeleteModel(&model);
	CFL_Finalize();
	C3D_Fini();
	gfxExit();
	return 0;
}
```

### Multiple simultaneous models

`CFLCharModel` is a plain caller-owned struct, so holding several at
once is just an array - no special API needed:

```c
#define MAX_MII 4
static CFLCharModel models[MAX_MII] = {0}; // zero-initialized: static storage
static int modelCount = 0;

static bool addMii(const MiiData* mii)
{
	if (modelCount >= MAX_MII) return false;
	// A lower resolution and a smaller expression set here keeps VRAM
	// usage sane across several simultaneous models - see the
	// CFL_InitCharModel docs above for the real per-expression cost.
	if (!CFL_InitCharModel(&models[modelCount], mii, CFL_RESOLUTION_64,
	                        CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		return false;
	}
	modelCount++;
	return true;
}

// ...later, in your draw loop: just loop over models[0..modelCount) and
// draw each with drawModel() from the example above, at a different
// world-space offset per model.
```

### Switching expressions

```c
// Requested up front at CFL_InitCharModel time:
CFL_InitCharModel(&model, &mii, CFL_RESOLUTION_128,
                   CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) |
                   CFL_EXPRESSION_FLAG(CFL_EXPRESSION_SMILE)  |
                   CFL_EXPRESSION_FLAG(CFL_EXPRESSION_SURPRISE));

// ...later, an instant switch - no rebuild:
if (CFL_IsAvailableExpression(&model, CFL_EXPRESSION_SMILE)) {
	CFL_SetExpression(&model, CFL_EXPRESSION_SMILE);
} else {
	// Wasn't requested above, or failed to bake (VRAM pressure) -
	// CFL_GetExpression(&model) still reflects whatever's actually shown.
}
```

### Listing every Mii saved on the console

```c
int total = CFL_GetAvailableOfficialDataNum();
if (total < 0) {
	// No CFL_DB.dat at all - Mii Maker has never been opened on this
	// console. A real, normal state to handle, not necessarily an error.
} else {
	for (u16 i = 0; i < 100; i++) {
		if (!CFL_IsAvailableOfficialData(i)) continue;
		MiiData mii;
		if (CFL_GetOfficialData(i, &mii)) {
			// mii is a real, valid MiiData from slot i.
		}
	}
}
```

### Re-opening the Mii Selector on the last-picked Mii

```c
static MiiData lastPicked;
static bool haveLastPicked = false;

static void pickMii(void)
{
	MiiSelectorConf conf;
	miiSelectorInit(&conf);

	u16 initialIndex;
	if (haveLastPicked && CFL_SearchOfficialData(&lastPicked, &initialIndex)) {
		miiSelectorSetInitialIndex(&conf, initialIndex);
	}

	MiiSelectorReturn ret;
	miiSelectorLaunch(&conf, &ret);
	lastPicked = ret.mii;
	haveLastPicked = true;
}
```

### Exporting and validating `CFLStoreData`

```c
// Export: real Mii -> the standard checksummed exchange format.
CFLStoreData store;
if (CFL_MakeStoreData(&mii, &store)) {
	// store is now ready to write into a QR code, send over StreetPass, etc.
}

// Import: never trust a CFLStoreData you didn't just create yourself.
CFLStoreData received = /* decoded from a QR code, NFC tag, etc. */;
if (CFL_IsStoreDataValid(&received)) {
	CFL_InitCharModel(&model, &received.miiData, CFL_RESOLUTION_128,
	                   CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL));
} else {
	// Checksum failed - corrupted or malformed data, don't use it.
}
```

### Creating a transparent icon

```c
#include <3ds.h>
#include <citro3d.h>
#include "cfl_mii.h"

int main(void)
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	if (!CFL_Initialize()) {
		gfxExit();
		return 1;
	}

	MiiSelectorConf conf;
	MiiSelectorReturn ret;
	miiSelectorInit(&conf);
	miiSelectorLaunch(&conf, &ret);

	CFLCharModel model = {0};
	CFL_InitCharModel(&model, &ret.mii, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL));

	// Transparent background instead of the library's own default
	// (CFL_ICON_BG_FAVORITE) - alpha=0 clears to fully transparent.
	CFLIconSetting setting = { CFL_ICON_BG_DIRECT, { 0.0f, 0.0f, 0.0f, 0.0f }, NULL, NULL };

	C3D_Tex icon = {0};
	if (CFL_CommandMakeModelIcon(&model, CFL_EXPRESSION_NORMAL, 256, &setting, &icon)) {
		// `icon` is a normal C3D_Tex with real per-pixel alpha - draw it
		// as an alpha-blended textured quad, same as any other
		// transparent texture. Release with a plain C3D_TexDelete when done.
		C3D_TexDelete(&icon);
	}

	CFL_DeleteModel(&model);
	CFL_Finalize();
	C3D_Fini();
	gfxExit();
	return 0;
}
```

## Status

Reverse-engineered from a retail 3DS title binary's debug info and
cross-referenced against the real, compiled RFL (Wii) and FFL (Wii U)
Mii libraries. Most of it has been validated on real 3DS hardware;
some corners are still being tracked down.

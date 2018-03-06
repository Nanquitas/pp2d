/*  This file is part of pp2d
>   Copyright (C) 2017/2018 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
> 
>   https://discord.gg/bGKEyfY
*/
 
/**
 * Plug & Play 2D
 * @file pp2d.c
 * @author Bernardo Giordano
 * @date 25 February 2018
 * @brief pp2d implementation
 */

#include "pp2d.h"

/*
 * Internal structs
 */

// Vertex
typedef struct
{ 
    float x, y; 
    float u, v;
}   PP2Di_Vertex;

// Vbo buffer and positions
typedef struct
{
    u32              startPos;
    u32              currentPos;
    GPU_Primitive_t  primitive;
    PP2Di_Vertex *   data;
}   PP2Di_Vbo;

// Tex env settings
typedef enum
{
    NONE,
    TEXTURE_BLENDING,   ///< Apply a texture to a target (blending through alpha)
    MIX_COLOR_AND_TEXTURE, ///< Blend a color using texture's alpha
    COLOR_BLENDING,     ///< Blend a color using the color's alpha component
}   PP2Di_TexEnvType;

typedef struct
{
    C3D_TexEnv  textureBlending;
    C3D_TexEnv  mixColorAndTexture;
    C3D_TexEnv  colorBlending;

    PP2Di_TexEnvType    current;
}   PP2Di_TexEnv;

// Targets
typedef struct
{
    C3D_RenderTarget *  topLeft;
    C3D_RenderTarget *  topRight;
    C3D_RenderTarget *  bottom;
}   PP2Di_Targets;

// Shader
typedef struct
{
    DVLB_s *            vshaderDvlb;
    shaderProgram_s     program;
    int                 projectionLocation;
}   PP2Di_Shader;

// Scene
typedef struct
{
    C3D_Mtx     projectionTopLeft;
    C3D_Mtx     projectionTopRight;
    C3D_Mtx     projectionBottom;
}   PP2Di_Scene;

// Cached glyph data
typedef struct
{
    u8 left;       ///< Horizontal offset to draw the glyph with.
    u8 glyphWidth; ///< Width of the glyph.
    u8 charWidth;  ///< Width of the character, that is, horizontal distance to advance.
    struct
    {
        float left;
        float right;
        float top;
        float bottom;
    } texcoords; ///< Texcoords in the glyphsheet
}   PP2Di_Glyph;

// Sysfont
typedef struct
{
    u8      cellWidth;    ///< Width of a glyph cell.
    u8      cellHeight;   ///< Height of a glyph cell.
    u8      baselinePos;  ///< Vertical position of the baseline.
    float   textScale;
    PP2D_TexRef *   glyphSheets; ///< Non cached sysfont's glyphs
    PP2D_TexRef     glyphSheetsCache; ///< Texture used for the cache
    PP2Di_Glyph     glyphs[128]; ///< Glyphs cached
}   PP2Di_Font;

// pp2d context
typedef struct
{
    bool            isInitialized : 1;
    bool            shapeOutlining : 1;
    PP2Di_Vbo       vbo;
    PP2Di_Targets   targets;
    PP2Di_Shader    shader;
    PP2Di_Scene     scene;
    PP2Di_Font      sysfont;
    PP2Di_TexEnv    texenv;
    PP2D_TexRef     bindedTex;
    float           outlineThickness;
    PP2D_Color      outlineColor;
}   PP2Di_Context;

static PP2Di_Context    g_pp2dContext = {0};

// Internal functions

static void             pp2di_cache_sysfont(void);
static void             pp2d_get_text_size_internal(float* width, float* height, float scaleX, float scaleY, int wrapX, const char* text);
static PP2D_TexRef      pp2di_new_texture(void);

static inline PP2Di_Vbo *pp2di_get_vbo(void)
{
    return &g_pp2dContext.vbo;
}

static inline void pp2di_update_projection(C3D_Mtx *projection)
{
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_pp2dContext.shader.projectionLocation, projection);
}

static inline void pp2di_add_text_vertex(float vx, float vy, float tx, float ty)
{
    PP2Di_Vbo * vbo = pp2di_get_vbo();

    PP2Di_Vertex *vertex = &vbo->data[vbo->currentPos++];

    vertex->x = vx;
    vertex->y = vy;
    vertex->u = tx;
    vertex->v = ty;
}

static inline void pp2di_add_text_vertex_model(float posX, float posY, float texX, float texY, const C3D_Mtx *model)
{
    C3D_FVec vec = FVec4_New(posX, posY, 0.f, 1.f);

    pp2di_add_text_vertex(FVec4_Dot(vec, model->r[0]), FVec4_Dot(vec, model->r[1]), texX, texY);
}

static inline void pp2di_draw_arrays(void)
{
    PP2Di_Vbo * vbo = pp2di_get_vbo();

    C3D_DrawArrays(vbo->primitive, vbo->startPos, vbo->currentPos - vbo->startPos);
    vbo->startPos = vbo->currentPos;
}

static inline void pp2di_draw_unprocessed_queue(void)
{
    PP2Di_Vbo * vbo = pp2di_get_vbo();

    if (vbo->currentPos != vbo->startPos)
    {
        C3D_DrawArrays(vbo->primitive, vbo->startPos, vbo->currentPos - vbo->startPos);
        vbo->startPos = vbo->currentPos;
    }
}

static inline void pp2di_use_primitive(GPU_Primitive_t prim)
{
    PP2Di_Vbo * vbo = pp2di_get_vbo();

    if (vbo->primitive != prim)
        pp2di_draw_unprocessed_queue();

    vbo->primitive = prim;
}

static inline bool pp2di_bind_texture(PP2D_TexRef texture)
{
    if (g_pp2dContext.bindedTex != texture)
    {
        pp2di_draw_unprocessed_queue();
        C3D_TexBind(0, (C3D_Tex *)&texture->texture);
        g_pp2dContext.bindedTex = texture;
        return true;
    }
    return false;
}

static inline bool pp2di_has_vbo_enough_space(u32 nb)
{
    return pp2di_get_vbo()->currentPos + nb < PP2D_MAX_VERTICES;
}

static inline C3D_TexEnv *pp2di_get_texenv(PP2Di_TexEnv *texenv, PP2Di_TexEnvType type)
{
    if (type == TEXTURE_BLENDING) return &texenv->textureBlending;
    if (type == MIX_COLOR_AND_TEXTURE) return &texenv->mixColorAndTexture;
    if (type == COLOR_BLENDING) return &texenv->colorBlending;

    return NULL;
}

static inline void pp2di_set_texenv(PP2Di_TexEnvType type, u32 color)
{
    PP2Di_TexEnv    *texenv = &g_pp2dContext.texenv;
    C3D_TexEnv      *env = pp2di_get_texenv(texenv, texenv->current);

    // If we're changing the settings
    if (texenv->current != type || (type >= MIX_COLOR_AND_TEXTURE && color != env->color))
    {
        // Render any queued vertices with current settings
        pp2di_draw_unprocessed_queue();

        // Change current env
        texenv->current = type;

        // Nothing to do if we decided to set NONE (custom settings ?)
        if (type == NONE) return;

        // Get the suitable env ptr and apply it
        env = pp2di_get_texenv(texenv, type);
        if (type >= MIX_COLOR_AND_TEXTURE)
            env->color = color;
        C3D_SetTexEnv(0, env);
    }
}

static u32 nextPow2(u32 v)
{
    #define TEX_MIN_SIZE 64
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return (v >= TEX_MIN_SIZE ? v : TEX_MIN_SIZE);
}

void    pp2d_init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
    //C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_COLOR, GPU_DST_COLOR, GPU_SRC_ALPHA, GPU_DST_ALPHA);

    // Init target
    {
        PP2Di_Targets    *targets = &g_pp2dContext.targets;
        C3D_RenderTarget *target;

        // Top Left
        target = C3D_RenderTargetCreate(PP2D_SCREEN_HEIGHT, PP2D_SCREEN_TOP_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        C3D_RenderTargetSetClear(target, C3D_CLEAR_ALL, PP2D_DEFAULT_COLOR_BG, 0);
        C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
        targets->topLeft = target;
        
        // Top Right
        target = C3D_RenderTargetCreate(PP2D_SCREEN_HEIGHT, PP2D_SCREEN_TOP_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        C3D_RenderTargetSetClear(target, C3D_CLEAR_ALL, PP2D_DEFAULT_COLOR_BG, 0);
        C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
        targets->topRight = target;
        
        // Bottom
        target = C3D_RenderTargetCreate(PP2D_SCREEN_HEIGHT, PP2D_SCREEN_BOTTOM_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        C3D_RenderTargetSetClear(target, C3D_CLEAR_ALL, PP2D_DEFAULT_COLOR_BG, 0);
        C3D_RenderTargetSetOutput(target, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
        targets->bottom = target;
    }

    // Init shader
    {
        PP2Di_Shader    *shader = &g_pp2dContext.shader;

        shader->vshaderDvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
        shaderProgramInit(&shader->program);
        shaderProgramSetVsh(&shader->program, &shader->vshaderDvlb->DVLE[0]);
        C3D_BindProgram(&shader->program);
        shader->projectionLocation = shaderInstanceGetUniformLocation(shader->program.vertexShader, "projection");
    }

    // Init scene
    {
        PP2Di_Scene     *scene = &g_pp2dContext.scene;

        Mtx_OrthoTilt(&scene->projectionTopLeft, 0, PP2D_SCREEN_TOP_WIDTH, PP2D_SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f, true);
        Mtx_OrthoTilt(&scene->projectionTopRight, 0, PP2D_SCREEN_TOP_WIDTH, PP2D_SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f, true);
        Mtx_OrthoTilt(&scene->projectionBottom, 0, PP2D_SCREEN_BOTTOM_WIDTH, PP2D_SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f, true);
    }

    // Init vbo
    {
        PP2Di_Vbo   * vbo = pp2di_get_vbo();
        C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
        C3D_BufInfo * bufInfo = C3D_GetBufInfo();

        AttrInfo_Init(attrInfo);
        AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 2);
        AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);

        vbo->data = (PP2Di_Vertex *)linearAlloc(sizeof(PP2Di_Vertex) * PP2D_MAX_VERTICES);
        
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo, vbo->data, sizeof(PP2Di_Vertex), 2, 0x10);
    }

    // Init TexEnv
    {
        PP2Di_TexEnv *  texenv = & g_pp2dContext.texenv;
        C3D_TexEnv   *  env;

        // TEXTURE_BLENDING
        env = &texenv->textureBlending;
        TexEnv_Init(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
        C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

        // MIX_COLOR_AND_TEXTURE
        env = &texenv->mixColorAndTexture;
        TexEnv_Init(env);
        C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, 0, 0);
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
        C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

        // COLOR_BLENDING
        env = &texenv->colorBlending;
        TexEnv_Init(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
        C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    }

    // Init Font
    {
        fontEnsureMapped();

        TGLP_s      *glyphInfo = fontGetGlyphInfo();
        PP2Di_Font  *font = &g_pp2dContext.sysfont;        
        
        font->glyphSheets = (PP2D_TexRef *)malloc(sizeof(PP2D_TexRef) * glyphInfo->nSheets);
        for (u32 i = 0; i < glyphInfo->nSheets; ++i)
        {
            font->glyphSheets[i] = pp2di_new_texture();
            C3D_Tex* tex = (C3D_Tex *)&font->glyphSheets[i]->texture;
            tex->data = fontGetGlyphSheetTex(i);
            tex->fmt = glyphInfo->sheetFmt;
            tex->size = glyphInfo->sheetSize;
            tex->width = glyphInfo->sheetWidth;
            tex->height = glyphInfo->sheetHeight;
            tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
                | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
            tex->border = 0;
            tex->lodParam = 0;
        }

        font->textScale = 20.f / fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(0x3042))->glyphWidth;
        pp2di_cache_sysfont();
    }
}

void    pp2d_exit(void)
{    
    // Clean vbo
    {
        PP2Di_Vbo *vbo = pp2di_get_vbo();

        linearFree(vbo->data);
    }

    // Clean targets
    {
        PP2Di_Targets *targets = &g_pp2dContext.targets;

        C3D_RenderTargetDelete(targets->topLeft);
        C3D_RenderTargetDelete(targets->topRight);
        C3D_RenderTargetDelete(targets->bottom);
    }

    // Clean shader
    {
        PP2Di_Shader *shader = &g_pp2dContext.shader;

        DVLB_Free(shader->vshaderDvlb);
        shaderProgramFree(&shader->program);
    }

    // Clean font
    {
        PP2Di_Font  *font = &g_pp2dContext.sysfont;

        // Destroy all textures
        for (u32 i = 0; i < fontGetGlyphInfo()->nSheets; ++i)
        {
            PP2D_TexRef tex = font->glyphSheets[i];

            ((C3D_Tex *)&tex->texture)->data = NULL;
            pp2d_destroy_texture(tex);
        }
        free(font->glyphSheets);

        // Destroy cached texture
        AtomicDecrement(&font->glyphSheetsCache->refCount);
        pp2d_destroy_texture(font->glyphSheetsCache);
    }
    
    C3D_Fini();
    gfxExit();
}

void    pp2d_set_3D(bool enable)
{
    gfxSet3D(enable);
}

void    pp2d_set_screen_color(gfxScreen_t target, u32 color)
{
    PP2Di_Targets *targets = &g_pp2dContext.targets;

    if (target == GFX_TOP)
    {
        C3D_RenderTargetSetClear(targets->topLeft, C3D_CLEAR_ALL, color, 0);
        C3D_RenderTargetSetClear(targets->topRight, C3D_CLEAR_ALL, color, 0);
    }
    else
    {
        C3D_RenderTargetSetClear(targets->bottom, C3D_CLEAR_ALL, color, 0);
    }
}

void    pp2d_frame_begin(gfxScreen_t target, gfx3dSide_t side)
{
    PP2Di_Vbo *vbo = pp2di_get_vbo();

    vbo->currentPos = vbo->startPos = 0;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    pp2d_frame_draw_on(target, side);
}

void    pp2d_frame_draw_on(gfxScreen_t target, gfx3dSide_t side)
{
    pp2di_draw_unprocessed_queue();

    PP2Di_Targets   *targets = &g_pp2dContext.targets;
    PP2Di_Scene     *scene = &g_pp2dContext.scene;

    if (target == GFX_TOP)
    {
        C3D_FrameDrawOn(side == GFX_LEFT ? targets->topLeft : targets->topRight);
        pp2di_update_projection(side == GFX_LEFT ? &scene->projectionTopLeft : &scene->projectionTopRight);
    } 
    else
    {
        C3D_FrameDrawOn(targets->bottom);
        pp2di_update_projection(&scene->projectionBottom);
    }
}

void    pp2d_frame_end(void)
{
    pp2di_draw_unprocessed_queue();
    C3D_FrameEnd(0);
}

static void     pp2di_cache_sysfont(void)
{
    TGLP_s              *TGLP = fontGetGlyphInfo();
    C3D_RenderTarget    *target;
    PP2Di_Font          *font = &g_pp2dContext.sysfont;
    PP2D_TexRef         *glyphsheets = font->glyphSheets;
    C3D_Tex             *tex;
    C3D_Mtx             projection;
    fontGlyphPos_s      data;
    float               left = 0.f;
    float               top = 0.f;

    // Create texture
    font->glyphSheetsCache = pp2di_new_texture();
    tex = (C3D_Tex *)&font->glyphSheetsCache->texture;
    AtomicIncrement(&font->glyphSheetsCache->refCount);

    // Init texture for the cache (8 rows of 16 glyphs => 128 glyphs capacity)
    C3D_TexInit(tex, nextPow2(16 * TGLP->cellWidth), nextPow2(8 * TGLP->cellHeight), GPU_RB_RGBA4);
    tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
    | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
    tex->border = 0;
    tex->lodParam = 0;

    // Init projection & apply it to vtx shader
    Mtx_Ortho(&projection, 0.f, tex->width, tex->height, 0.f, 0.f, 1.f, true);
    pp2di_update_projection(&projection);

    // Init render target
    target = C3D_RenderTargetCreateFromTex(tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24);

    // Clear texture
    C3D_FrameBufClear(&target->frameBuf, C3D_CLEAR_ALL, 0, 0);

    // Init C3D rendering
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameDrawOn(target);

    // Set rendering options
    pp2di_set_texenv(TEXTURE_BLENDING, 0);

    // Set primitive mode
    pp2di_use_primitive(GPU_TRIANGLE_STRIP);

    // Init sysfont's globals
    font->cellWidth = TGLP->cellWidth;
    font->cellHeight = TGLP->cellHeight;
    font->baselinePos = TGLP->baselinePos;

    // Parse the sysfont and cache the glyphs
    for (u32 i = 0; i < 128; ++i)
    {
        if (i && !(i % 16))
        {
            top += TGLP->cellHeight;
            left = 0.f;
        }
        else if (i > 0)
            left += TGLP->cellWidth;

        int     glyphIdx = fontGlyphIndexFromCodePoint(i);
        charWidthInfo_s *cwi = fontGetCharWidthInfo(glyphIdx);
        PP2Di_Glyph   *glyph = &font->glyphs[i];

        fontCalcGlyphPos(&data, glyphIdx, GLYPH_POS_CALC_VTXCOORD, 1.f, 1.f);

        glyph->left = cwi->left;
        glyph->charWidth = cwi->charWidth;
        glyph->glyphWidth = cwi->glyphWidth;

        float tx = left / (float)tex->width;
        float ty = 1.0f - top / (float)tex->height;
        float tw = (float)(cwi->glyphWidth) / (float)tex->width;
        float th = (float)(TGLP->cellHeight) / (float)tex->height;

        glyph->texcoords.left = tx;
        glyph->texcoords.right = tx + tw;
        glyph->texcoords.top = ty;
        glyph->texcoords.bottom = ty - th;

        pp2di_bind_texture(glyphsheets[data.sheetIndex]);

        data.vtxcoord.left = 0.f;
        data.vtxcoord.right = glyph->glyphWidth;
        data.vtxcoord.top = 0.f;
        data.vtxcoord.bottom = TGLP->cellHeight;

        pp2di_add_text_vertex(left + data.vtxcoord.left, top + data.vtxcoord.bottom, data.texcoord.left, data.texcoord.bottom);
        pp2di_add_text_vertex(left + data.vtxcoord.right, top + data.vtxcoord.bottom, data.texcoord.right, data.texcoord.bottom);
        pp2di_add_text_vertex(left + data.vtxcoord.left, top + data.vtxcoord.top, data.texcoord.left, data.texcoord.top);
        pp2di_add_text_vertex(left + data.vtxcoord.right, top + data.vtxcoord.top, data.texcoord.right, data.texcoord.top);

        pp2di_draw_arrays();
    }

    // End rendering
    C3D_FrameEnd(0);

    // Delete render target
    C3D_RenderTargetDelete(target);
}

void pp2d_draw_rectangle(int x, int y, int width, int height, u32 color)
{
    // Check that there's enough space in the vbo
    if (!pp2di_has_vbo_enough_space(6))
        return;

    // Set primitive mode
    pp2di_use_primitive(GPU_TRIANGLE_STRIP);

    // Set rendering options (fragment)
    pp2di_set_texenv(COLOR_BLENDING, color);

    // Send vertices
    pp2di_add_text_vertex(        x, y + height, 0, 0);
    pp2di_add_text_vertex(x + width, y + height, 0, 0);
    pp2di_add_text_vertex(        x,          y, 0, 0);
    pp2di_add_text_vertex(x + width,          y, 0, 0);
}

void pp2d_draw_text(float x, float y, float scaleX, float scaleY, u32 color, const char* text)
{
    pp2d_draw_text_wrap(x, y, scaleX, scaleY, color, -1, text);
}

void pp2d_draw_text_center(gfxScreen_t target, float y, float scaleX, float scaleY, u32 color, const char* text)
{
    float width = pp2d_get_text_width(text, scaleX, scaleY);
    float x = ((target == GFX_TOP ? PP2D_SCREEN_TOP_WIDTH : PP2D_SCREEN_BOTTOM_WIDTH) - width) / 2;
    pp2d_draw_text_wrap(x, y, scaleX, scaleY, color, -1, text);
}

void pp2d_draw_text_wrap(float x, float y, float scaleX, float scaleY, u32 color, float wrapX, const char* text)
{
    if (text == NULL)
        return;

    // Set primitive mode
    pp2di_use_primitive(GPU_TRIANGLE_STRIP);

    ssize_t     units;
    uint32_t    code;
    float       firstX = x;
    PP2Di_Font *font = &g_pp2dContext.sysfont;
    PP2D_TexRef *glyphsheets = font->glyphSheets;
    const uint8_t* p = (const uint8_t*)text;
    
    scaleX *= font->textScale;
    scaleY *= font->textScale;

    // Set rendering settings
    pp2di_set_texenv(MIX_COLOR_AND_TEXTURE, color);

    do
    {
        if (!*p) 
        {
            break;
        }
        
        units = decode_utf8(&code, p);

        if (units == -1)
            break;

        p += units;
        
        if (code == '\n' || (wrapX != -1 && x + scaleX * fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(code))->charWidth >= firstX + wrapX))
        {
            x = firstX;
            y += scaleY*fontGetInfo()->lineFeed;
            p -= code == '\n' ? 0 : 1;
        }
        else if (code > 0)
        {
            if (!pp2di_has_vbo_enough_space(4))
                break;

            if (code < 128)
            {
                // Bind the cached texture
                pp2di_bind_texture(font->glyphSheetsCache);

                // Get the glyph
                PP2Di_Glyph   *glyph = &font->glyphs[code];

                struct
                {
                    float left;
                    float right;
                    float top;
                    float bottom;
                } vtxcoord;

                float vx = scaleX * (float)glyph->left;
                float vw = scaleX * (float)glyph->glyphWidth;
                float vh = scaleY * (float)font->cellHeight;

                vtxcoord.left = vx;
                vtxcoord.top = 0.f;
                vtxcoord.right = vx+vw;
                vtxcoord.bottom = vh;

                // Add the vertices
                pp2di_add_text_vertex(x+vtxcoord.left,  y+vtxcoord.bottom, glyph->texcoords.left,  glyph->texcoords.bottom);                
                pp2di_add_text_vertex(x+vtxcoord.right, y+vtxcoord.bottom, glyph->texcoords.right, glyph->texcoords.bottom);
                pp2di_add_text_vertex(x+vtxcoord.left,  y+vtxcoord.top,    glyph->texcoords.left,  glyph->texcoords.top);
                pp2di_add_text_vertex(x+vtxcoord.right, y+vtxcoord.top,    glyph->texcoords.right, glyph->texcoords.top);
                

                x += (float)glyph->charWidth * scaleX;
            }
            else
            {
                int glyphIdx = fontGlyphIndexFromCodePoint(code);
                fontGlyphPos_s data;
                fontCalcGlyphPos(&data, glyphIdx, GLYPH_POS_CALC_VTXCOORD, scaleX, scaleY);

                // Bind the texture
                pp2di_bind_texture(glyphsheets[data.sheetIndex]);

                // Add the vertices
                pp2di_add_text_vertex(x+data.vtxcoord.left,  y+data.vtxcoord.bottom, data.texcoord.left,  data.texcoord.bottom);
                pp2di_add_text_vertex(x+data.vtxcoord.right, y+data.vtxcoord.bottom, data.texcoord.right, data.texcoord.bottom);
                pp2di_add_text_vertex(x+data.vtxcoord.left,  y+data.vtxcoord.top,    data.texcoord.left,  data.texcoord.top);
                pp2di_add_text_vertex(x+data.vtxcoord.right, y+data.vtxcoord.top,    data.texcoord.right, data.texcoord.top);                

                x += data.xAdvance;
            }
        }
    } while (code > 0);

    // Render the queue
    pp2di_draw_unprocessed_queue();
}

void pp2d_draw_textf(float x, float y, float scaleX, float scaleY, u32 color, const char* text, ...) 
{
    char buffer[256];
    va_list args;
    va_start(args, text);
    vsnprintf(buffer, 256, text, args);
    pp2d_draw_text(x, y, scaleX, scaleY, color, buffer);
    va_end(args);
}

float pp2d_get_text_height(const char* text, float scaleX, float scaleY)
{
    float height;
    pp2d_get_text_size_internal(NULL, &height, scaleX, scaleY, -1, text);
    return height;
}

float pp2d_get_text_height_wrap(const char* text, float scaleX, float scaleY, int wrapX)
{
    float height;
    pp2d_get_text_size_internal(NULL, &height, scaleX, scaleY, wrapX, text);
    return height;
}

void pp2d_get_text_size(float* width, float* height, float scaleX, float scaleY, const char* text)
{
    pp2d_get_text_size_internal(width, height, scaleX, scaleY, -1, text);
}

static void pp2d_get_text_size_internal(float* width, float* height, float scaleX, float scaleY, int wrapX, const char* text)
{
    float maxW = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    
    ssize_t  units;
    uint32_t code;
    float x = 0;
    float firstX = x;
    const uint8_t* p = (const uint8_t*)text;
    
    scaleX *= g_pp2dContext.sysfont.textScale;
    scaleY *= g_pp2dContext.sysfont.textScale;
    
    do
    {
        if (!*p)
        {
            break;
        }
        
        units = decode_utf8(&code, p);
        if (units == -1)
        {
            break;
        }
        p += units;
        
        if (code == '\n' || (wrapX != -1 && x + scaleX * fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(code))->charWidth >= firstX + wrapX))
        {
            x = firstX;
            h += scaleY*fontGetInfo()->lineFeed;
            p -= code == '\n' ? 0 : 1;
            if (w > maxW)
            {
                maxW = w;
            }
            w = 0.f;
        }
        else if (code > 0)
        {
            float len = (scaleX * fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(code))->charWidth);
            w += len;
            x += len;
        }
    } while (code > 0);
    
    if (width)
    {
        *width = w > maxW ? w : maxW;
    }
    
    if (height)
    {
        h += scaleY*fontGetInfo()->lineFeed;
        *height = h;
    }
}

float pp2d_get_text_width(const char* text, float scaleX, float scaleY)
{
    float width;
    pp2d_get_text_size_internal(&width, NULL, scaleX, scaleY, -1, text);
    return width;
}

u32    pp2di_png_to_texture(C3D_Tex *tex, const char *path)
{
    u32 *   imageData = NULL;
    u32 *   temp = NULL;
    u32     res = 0;
    u32     width, height;
    u32     powWidth, powHeight;

    // Try to load the image
    if ((res = lodepng_decode32_file((u8 **)&imageData, (unsigned *)&width, (unsigned *)&height, path)) != 0)
        goto error;

    // Adjust size to be a power of 2 (HW restriction)
    powWidth = nextPow2(width);
    powHeight = nextPow2(height);

    // Allocate a new buffer in the linear memory to be tiled
    if ((temp = (u32 *)linearAlloc(powWidth * powHeight * 4)) == NULL)
    {
        res = -1;
        goto error;
    }

    // Copy the image data and swap the bytes order (RGBA => ABGR)
    u32 *src = imageData;
    u32 *end = src + width * height;
    u32 *dst = temp;

    while (src < end)
        *dst++ = __builtin_bswap32(*src++);

    // Flush linear memory
    GSPGPU_FlushDataCache(temp, powWidth * powHeight * 4);

    // Init the texture
    C3D_TexInit(tex, powWidth, powHeight, GPU_RGBA8);
    tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
        | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
    tex->border = 0;
    tex->lodParam = 0;

    // Tile the data and transfer it to the texture
    u32 dim = GX_BUFFER_DIM(powWidth, powHeight);
    C3D_SafeDisplayTransfer(temp, dim, tex->data, dim, TEXTURE_TRANSFER_FLAGS(GPU_RGBA8));
    gspWaitForPPF();

    // Flush the texture and we're done
    C3D_TexFlush(tex);

error:
    // Free our buffers
    if (imageData) free(imageData);
    if (temp) linearFree(temp);
    return res;
}

PP2D_TexRef     pp2di_new_texture(void)
{
    static u32  uidCount = 0;

    PP2D_Tex *  tex = (PP2D_Tex *)malloc(sizeof(PP2D_Tex));

    if (!tex)
        return NULL;

    // Clear the struct
    memset(tex, 0, sizeof(PP2D_Tex));

    // Set uid
    tex->uid = ++uidCount;

    return tex;
}

PP2D_TexRef     pp2d_texture_from_png(const char *path)
{
    PP2D_Tex *  tex = (PP2D_Tex *)pp2di_new_texture();

    if (!tex)
        goto error;

    // Try to load the image
    if (pp2di_png_to_texture(&tex->texture, path))
        goto error;

    return tex;

error:
    if (tex)
    {
        C3D_TexDelete(&tex->texture);
        free(tex);
    }

    return NULL;
}

void            pp2d_destroy_texture(PP2D_TexRef texture)
{
    // If the texture is invalid or in use, abort
    if (!texture || !texture->uid || AtomicRead(&texture->refCount) > 0)
        return;

    PP2D_Tex *  tex = (PP2D_Tex *)texture;

    // Destroy the texture
    C3D_TexDelete(&tex->texture);

    // Reset uid
    tex->uid = 0;
}

PP2D_Sprite *   pp2d_new_sprite_textured(const float posX, const float posY, PP2D_TexRef texture, const PP2D_TexCoords texcoords)
{
    // If the texture is invalid, abort
    if (!texture || !texture->uid)
        return NULL;

    PP2D_Sprite     *sprite = (PP2D_Sprite *)malloc(sizeof(PP2D_Sprite));

    if (sprite == NULL) return sprite;

    // Clear the struct
    memset(sprite, 0, sizeof(PP2D_Sprite));

    // Init parameters
    sprite->updateModel = true;
    sprite->updateDimensions = false;
    sprite->destroyTexture = false;
    sprite->isColoredSprite = false;
    sprite->_posX = sprite->posX = posX;
    sprite->_posY = sprite->posY = posY;
    sprite->_width = sprite->width = texcoords.right - texcoords.left;
    sprite->_height = sprite->height = texcoords.bottom - texcoords.top;
    sprite->scaleX = 1.f;
    sprite->scaleY = 1.f;
    sprite->color = (PP2D_Color){0xFFFFFFFF};
    sprite->rotation = Quat_Identity();

    // Init texture
    sprite->texture = texture;
    AtomicIncrement(&texture->refCount);

    float           texWidth = (float)texture->texture.width;
    float           texHeight = (float)texture->texture.height;
    PP2D_TexCoords *texCoords = &sprite->texcoords;

    texCoords->left = texcoords.left / texWidth;
    texCoords->right = texCoords->left + sprite->width / texWidth;
    texCoords->top = 1.f - texcoords.top / texHeight;
    texCoords->bottom = texCoords->top - sprite->height / texHeight;

    return sprite;
}

PP2D_Sprite *   pp2d_new_sprite_colored(const float posX, const float posY, const float width, const float height, const PP2D_Color color)
{
    PP2D_Sprite     *sprite = (PP2D_Sprite *)malloc(sizeof(PP2D_Sprite));

    if (sprite == NULL) return sprite;

    // Clear the struct
    memset(sprite, 0, sizeof(PP2D_Sprite));

    // Init parameters
    sprite->updateModel = true;
    sprite->updateDimensions = false;
    sprite->destroyTexture = false;
    sprite->isColoredSprite = true;
    sprite->_posX = sprite->posX = posX;
    sprite->_posY = sprite->posY = posY;
    sprite->_width = sprite->width = width;
    sprite->_height = sprite->height = height;
    sprite->scaleX = 1.f;
    sprite->scaleY = 1.f;
    sprite->color = color;
    sprite->rotation = Quat_Identity();
    sprite->texture = NULL;

    return sprite;
}

PP2D_Sprite *   pp2d_sprite_move(PP2D_Sprite *sprite, const float offsetX, const float offsetY)
{
    if (!sprite) goto exit;

    // Update the "real" position
    sprite->_posX += offsetX;
    sprite->_posY += offsetY;

    // Update the "scaled" position
    sprite->posX += offsetX;
    sprite->posY += offsetY;

    sprite->updateModel = true;

exit:
    return sprite;
}

PP2D_Sprite *   pp2d_sprite_rotate(PP2D_Sprite *sprite, const float degrees)
{
    if (!sprite) goto exit;

    sprite->rotation = Quat_RotateZ(sprite->rotation, C3D_AngleFromDegrees(degrees), false);

    sprite->updateModel = true;

exit:
    return sprite;
}

PP2D_Sprite *   pp2d_sprite_scale(PP2D_Sprite *sprite, const float scaleX, const float scaleY)
{
    if (!sprite) goto exit;

    sprite->scaleX += scaleX;
    sprite->scaleY += scaleY;

    if (sprite->scaleX < 0.f)
        sprite->scaleX = 0.f; 

    if (sprite->scaleY < 0.f)
        sprite->scaleY = 0.f; 

    sprite->updateDimensions = true;
    sprite->updateModel = true;

exit:
    return sprite;
}

PP2D_Sprite *   pp2d_sprite_update(PP2D_Sprite *sprite)
{
    if (!sprite) goto exit;

    if (sprite->updateDimensions)
    {
        float   width = sprite->_width;
        float   height = sprite->_height;

        // Update dimensions
        sprite->width = width * sprite->scaleX;
        sprite->height = height * sprite->scaleY;

        // Update positions
        sprite->posX = sprite->_posX + (width - sprite->width) / 2.f;
        sprite->posY = sprite->_posY + (height - sprite->height) / 2.f;

        sprite->updateDimensions = false;
    }

    if (sprite->updateModel)
    {
        C3D_Mtx     model;
        C3D_Mtx     rotation;
        C3D_Mtx     scale;
        float       xCenter = sprite->_width / 2.f;
        float       yCenter = sprite->_height / 2.f;

        Mtx_Identity(&model);
        Mtx_Translate(&model, -xCenter, -yCenter, -0.5f, false);

        Mtx_Identity(&scale);
        Mtx_Scale(&scale, sprite->scaleX, sprite->scaleY, 0.f);

        Mtx_FromQuat(&rotation, sprite->rotation);

        Mtx_Multiply(&rotation, &rotation, &scale);
        Mtx_Multiply(&model, &rotation, &model);
        Mtx_Translate(&model, sprite->_posX + xCenter, sprite->_posY + yCenter, 0.5f, false);
        Mtx_Copy(&sprite->model, &model);

        sprite->updateModel = false;
    }

exit:
    return sprite;
}

PP2D_Sprite *   pp2d_sprite_draw(PP2D_Sprite *sprite)
{
    if (!sprite || !pp2di_has_vbo_enough_space(6)) return sprite;

    /*if (sprite->updateModel) ///< Force it or not force it ?
        pp2d_sprite_update(sprite); */

    bool            outlining = g_pp2dContext.shapeOutlining;
    float           width = sprite->_width;
    float           height = sprite->_height;
    float           left = 0.f, top = 0.f;
    C3D_Mtx         scaled;
    C3D_Mtx *       modelMtx = &sprite->model;
    PP2D_TexCoords *texcoords = &sprite->texcoords;

    // If we're in outlining mode
    if (outlining)
    {
        float       scale = g_pp2dContext.outlineThickness;

        C3D_Mtx     model;
        C3D_Mtx     scaleMtx;
        C3D_Mtx     rotation;
        float       xCenter = width / 2.f;
        float       yCenter = width / 2.f;

        Mtx_Identity(&model);
        Mtx_Translate(&model, -xCenter, -yCenter, 0.f, false);

        Mtx_Identity(&scaleMtx);
        Mtx_Scale(&scaleMtx, sprite->scaleX + scale, sprite->scaleY + scale, 0.f);

        Mtx_FromQuat(&rotation, sprite->rotation);

        Mtx_Multiply(&rotation, &rotation, &scaleMtx);
        Mtx_Multiply(&scaled, &rotation, &model);
        Mtx_Translate(&scaled, sprite->_posX + xCenter, sprite->_posY + yCenter, 0.f, false);

        modelMtx = &scaled;

        // Configure tex env
        pp2di_set_texenv(sprite->isColoredSprite ? COLOR_BLENDING : MIX_COLOR_AND_TEXTURE, g_pp2dContext.outlineColor.raw);
    }
    else
    {
        // If this sprite is a colored shape
        if (sprite->isColoredSprite)
            pp2di_set_texenv(COLOR_BLENDING, sprite->color.raw);
        else ///< Textured sprite
        {
            // Bind texture and set tex env
            pp2di_bind_texture(sprite->texture);
            pp2di_set_texenv(TEXTURE_BLENDING, 0);
        }
    }

    // Draw sprite
    pp2di_use_primitive(GPU_TRIANGLES);
    
    // Add vertices to draw queue
    pp2di_add_text_vertex_model(left, top, texcoords->left, texcoords->top, modelMtx);
    pp2di_add_text_vertex_model(left, height, texcoords->left, texcoords->bottom, modelMtx);
    pp2di_add_text_vertex_model(width, height, texcoords->right, texcoords->bottom, modelMtx);

    pp2di_add_text_vertex_model(width, height, texcoords->right, texcoords->bottom, modelMtx);
    pp2di_add_text_vertex_model(width, top, texcoords->right, texcoords->top, modelMtx);        
    pp2di_add_text_vertex_model(left, top, texcoords->left, texcoords->top, modelMtx);

    return sprite;
}

void            pp2d_destroy_sprite(PP2D_Sprite *sprite)
{
    if (!sprite) return;

    // Decrease texture's refCount
    if (sprite->texture)
        AtomicDecrement(&sprite->texture->refCount);

    // Destroy texture if needed
    if (sprite->destroyTexture)
        pp2d_destroy_texture(sprite->texture);

    // Clear structure
    memset(sprite, 0, sizeof(PP2D_Sprite));

    // Relese resources
    free(sprite);   
}

void            pp2d_shape_outlining_begin(void)
{
    // Enable stencil
    C3D_StencilTest(true, GPU_ALWAYS, 1, 0xFF, 0xFF);
    // Enable alpha test
    C3D_AlphaTest(true, GPU_GREATER, 0); 
}

void            pp2d_shape_outlining_apply(const PP2D_Color color, float thickness)
{
    // Render queue
    pp2di_draw_unprocessed_queue();

    // Enable shape outlining and set thickness
    g_pp2dContext.shapeOutlining = true;
    g_pp2dContext.outlineThickness = thickness / 100.f;
    g_pp2dContext.outlineColor = color;

    // Disable Alpha test
    C3D_AlphaTest(false, GPU_GREATER, 0);

    // Change stencil test
    C3D_StencilTest(true, GPU_NOTEQUAL, 1, 0xFF, 0x00);
}

void        pp2d_shape_outlining_end(void)
{
    // Render queue
    pp2di_draw_unprocessed_queue();

    // Disable shape outlining
    g_pp2dContext.shapeOutlining = false;

    // Disable stencil test
    C3D_StencilTest(false, GPU_ALWAYS, 1, 0xFF, 0xFF);
}

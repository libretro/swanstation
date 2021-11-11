#include "gpu_sw_backend.h"
#include "common/assert.h"
#include "common/log.h"
#include "gpu_sw_backend.h"
#include "host_display.h"
#include "system.h"
#include <algorithm>
#include <cmath>

Log_SetChannel(GPU_SW_Backend);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // warning C4324: structure was padded due to alignment specifier
#pragma warning(disable : 4189) // warning C4189: local variable is initialized but not referenced
#endif

GPU_SW_Backend::GPU_SW_Backend() : GPUBackend()
{
}

static ptrdiff_t getVRAMSizeInBytes(int scale)
{
  return VRAM_WIDTH * VRAM_HEIGHT * sizeof(u16) * (scale*scale);
}

void GPU_SW_Backend::SetUprenderScale(int scale)
{
  //const GPURenderer renderer = GetRendererType();
  if (g_settings.gpu_renderer != GPURenderer::Software)
  {
      scale = 1;
  }
  else
  {
     switch (scale)
     {
       case 1:  
       case 2:  
       case 4: 
         // OK!
       break;

       case 3:
         scale = 4;
       break;

       default:
         // if the input scale is invalid then default to either the current setting (if valid)
         // or native res if vram memory is not initialized yet.
         if (scale > 4)
           scale = 4;
         else if (!m_upram) 
           scale = 1;
         else
           return;
     }
  }

  // TODO: capture current VRAM and re-upload after resolution change.


  auto new_upshift = (int)log2(scale);
  if (m_upram_ptr && (m_uprender_shift == new_upshift))
    return;

  if (m_upram)
  {
    free(m_upram);
    m_upram = nullptr;
  }

  m_uprender_shift = new_upshift;

  if (scale == 1)
  {
    m_upram_ptr = m_vram.data();
  }
  else
  {
    m_upram = (u16*)malloc(getVRAMSizeInBytes(scale));
    m_upram_ptr = m_upram;

    UpdateVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT, GetVRAMshadowPtr(), {});
  }
}

GPU_SW_Backend::~GPU_SW_Backend()
{
  if (m_upram)
  {
    free(m_upram);
    m_upram = nullptr;
  }
}

bool GPU_SW_Backend::Initialize(bool force_thread)
{
  SetUprenderScale(g_settings.gpu_resolution_soft_scale);
  return GPUBackend::Initialize(force_thread);
}

void GPU_SW_Backend::UpdateSettings()
{
  GPUBackend::UpdateSettings();   // invokes Sync()  [inheritance style coding caveat]

  // function internally shortcuts out if setting is up-to-date.
  SetUprenderScale(g_settings.gpu_resolution_soft_scale);
}

void GPU_SW_Backend::Reset(bool clear_vram)
{
  GPUBackend::Reset(clear_vram);

  if (clear_vram)
    memset(m_upram_ptr, 0, getVRAMSizeInBytes(uprender_scale()));
}

void GPU_SW_Backend::DrawPolygon(const GPUBackendDrawPolygonCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};
  const bool dithering_enable = rc.IsDitheringEnabled() && cmd->draw_mode.dither_enable;

  auto DrawFunction = GetDrawTriangleFunction(0 
    | (!!rc.shading_enable       )  * TShaderParam_ShadingEnable
    | (!!rc.texture_enable       )  * TShaderParam_TextureEnable
    | (!!rc.raw_texture_enable   )  * TShaderParam_RawTextureEnable
    | (!!rc.transparency_enable  )  * TShaderParam_TransparencyEnable
    | (dithering_enable          )  * TShaderParam_DitheringEnable
    | (!!cmd->params.GetMaskAND())  * TShaderParam_MaskAndEnable
    | (!!cmd->params.GetMaskOR ())  * TShaderParam_MaskOrEnable,

    m_uprender_shift
  );

  (this->*DrawFunction)(cmd, &cmd->vertices[0], &cmd->vertices[1], &cmd->vertices[2]);
  if (rc.quad_polygon)
    (this->*DrawFunction)(cmd, &cmd->vertices[2], &cmd->vertices[1], &cmd->vertices[3]);
}

void GPU_SW_Backend::DrawRectangle(const GPUBackendDrawRectangleCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};

  auto DrawFunction = GetDrawRectangleFunction(0 
    | (!!rc.texture_enable       )  * TRectShader_TextureEnable
    | (!!rc.raw_texture_enable   )  * TRectShader_RawTextureEnable
    | (!!rc.transparency_enable  )  * TRectShader_TransparencyEnable
    | (!!cmd->params.GetMaskAND())  * TRectShader_MaskAndEnable
    | (!!cmd->params.GetMaskOR ())  * TRectShader_MaskOrEnable,
  
    m_uprender_shift
  );

  (this->*DrawFunction)(cmd);
}

void GPU_SW_Backend::DrawLine(const GPUBackendDrawLineCommand* cmd)
{
  auto DrawFunction = GetDrawLineFunction(0 
    | (!!cmd->rc.shading_enable       )  * TLineShader_ShadingEnable
    | (!!cmd->rc.transparency_enable  )  * TLineShader_TransparencyEnable
    | (cmd->IsDitheringEnabled()      )  * TLineShader_DitheringEnable
    | (!!cmd->params.GetMaskAND()     )  * TLineShader_MaskAndEnable
    | (!!cmd->params.GetMaskOR ()     )  * TLineShader_MaskOrEnable,

    m_uprender_shift
  );

  for (u16 i = 1; i < cmd->num_vertices; i++)
    (this->*DrawFunction)(cmd, &cmd->vertices[i - 1], &cmd->vertices[i]);
}

constexpr GPU_SW_Backend::DitherLUT GPU_SW_Backend::ComputeDitherLUT()
{
  DitherLUT lut = {};
  for (u32 i = 0; i < DITHER_MATRIX_SIZE; i++)
  {
    for (u32 j = 0; j < DITHER_MATRIX_SIZE; j++)
    {
      for (u32 value = 0; value < DITHER_LUT_SIZE; value++)
      {
        const s32 dithered_value = (static_cast<s32>(value) + DITHER_MATRIX[i][j]) >> 3;
        lut[i][j][value] = static_cast<u8>((dithered_value < 0) ? 0 : ((dithered_value > 31) ? 31 : dithered_value));
      }
    }
  }
  return lut;
}

static constexpr GPU_SW_Backend::DitherLUT s_dither_lut = GPU_SW_Backend::ComputeDitherLUT();

template<u32 TShaderParams, int TUprenderShift>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::ShadePixel(const GPUBackendDrawCommand* cmd, s32 x, s32 y, u8 color_r,
                                                      u8 color_g, u8 color_b, u8 texcoord_x_u8, u8 texcoord_y_u8)
{
// shading parameter is ignored for ShadePixel (should always be 0)
//constexpr bool shading_enable       = (TShaderParams & TShaderParam_ShadingEnable     ) == TShaderParam_ShadingEnable     ;
  constexpr bool texture_enable       = (TShaderParams & TShaderParam_TextureEnable     ) == TShaderParam_TextureEnable     ;
  constexpr bool raw_texture_enable   = (TShaderParams & TShaderParam_RawTextureEnable  ) == TShaderParam_RawTextureEnable  ;
  constexpr bool transparency_enable  = (TShaderParams & TShaderParam_TransparencyEnable) == TShaderParam_TransparencyEnable;
  constexpr bool dithering_enable     = (TShaderParams & TShaderParam_DitheringEnable   ) == TShaderParam_DitheringEnable   ;
  constexpr bool mask_and_enable      = (TShaderParams & TShaderParam_MaskAndEnable     ) == TShaderParam_MaskAndEnable     ;
  constexpr bool mask_or_enable       = (TShaderParams & TShaderParam_MaskOrEnable      ) == TShaderParam_MaskOrEnable      ;

  VRAMPixel color;
  if constexpr (texture_enable)
  {
    // Apply texture window
    int texcoord_x = (texcoord_x_u8 & cmd->window.and_x) | cmd->window.or_x;
    int texcoord_y = (texcoord_y_u8 & cmd->window.and_y) | cmd->window.or_y;

    VRAMPixel texture_color;
    switch (cmd->draw_mode.texture_mode)
    {
      case GPUTextureMode::Palette4Bit:
      {
        const u16 palette_value =
          GetPixel<TUprenderShift>((cmd->draw_mode.GetTexturePageBaseX() + (texcoord_x / 4)) % VRAM_WIDTH,
                                     (cmd->draw_mode.GetTexturePageBaseY() + (texcoord_y / 1)) % VRAM_HEIGHT);
        const u16 palette_index = (palette_value >> ((texcoord_x % 4) * 4)) & 0x0Fu;

        texture_color.bits =
          GetPixel<TUprenderShift>((cmd->palette.GetXBase() + (palette_index)) % VRAM_WIDTH, cmd->palette.GetYBase());
      }
      break;

      case GPUTextureMode::Palette8Bit:
      {
        const u16 palette_value =
          GetPixel<TUprenderShift>((cmd->draw_mode.GetTexturePageBaseX() + (texcoord_x / 2)) % VRAM_WIDTH,
                                     (cmd->draw_mode.GetTexturePageBaseY() + (texcoord_y / 1)) % VRAM_HEIGHT);
        const u16 palette_index = (palette_value >> ((texcoord_x % 2) * 8)) & 0xFFu;
        texture_color.bits =
          GetPixel<TUprenderShift>((cmd->palette.GetXBase() + (palette_index)) % VRAM_WIDTH, cmd->palette.GetYBase());
      }
      break;

      default:
      {
        texture_color.bits = GetPixel<TUprenderShift>((cmd->draw_mode.GetTexturePageBaseX() + (texcoord_x)) % VRAM_WIDTH,
                                                        (cmd->draw_mode.GetTexturePageBaseY() + (texcoord_y)) % VRAM_HEIGHT);
      }
      break;
    }

    if (texture_color.bits == 0)
      return;

    if constexpr (raw_texture_enable)
    {
      color.bits = texture_color.bits;
    }
    else
    {
      const u32 dither_y = (dithering_enable) ? (y & 3u) : 2u;
      const u32 dither_x = (dithering_enable) ? (x & 3u) : 3u;

      color.bits = (ZeroExtend16(s_dither_lut[dither_y][dither_x][(u16(texture_color.r) * u16(color_r)) >> 4]) << 0) |
                   (ZeroExtend16(s_dither_lut[dither_y][dither_x][(u16(texture_color.g) * u16(color_g)) >> 4]) << 5) |
                   (ZeroExtend16(s_dither_lut[dither_y][dither_x][(u16(texture_color.b) * u16(color_b)) >> 4]) << 10) |
                   (texture_color.bits & 0x8000u);
    }
  }
  else
  {
    const u32 dither_y = (dithering_enable) ? (y & 3u) : 2u;
    const u32 dither_x = (dithering_enable) ? (x & 3u) : 3u;

    // Non-textured transparent polygons don't set bit 15, but are treated as transparent.
    color.bits = (ZeroExtend16(s_dither_lut[dither_y][dither_x][color_r]) << 0) |
                 (ZeroExtend16(s_dither_lut[dither_y][dither_x][color_g]) << 5) |
                 (ZeroExtend16(s_dither_lut[dither_y][dither_x][color_b]) << 10) | (transparency_enable ? 0x8000u : 0);
  }

  auto constexpr vram_upsize_x  = VRAM_WIDTH << TUprenderShift;

  if constexpr (transparency_enable || mask_and_enable)
  {
    const VRAMPixel bg_color { UPRAM_ACCESSOR[vram_upsize_x * y + x] };

    if constexpr (mask_and_enable)
    {
      const u16 mask_and = cmd->params.GetMaskAND();
      if ((bg_color.bits & mask_and) != 0)
        return;
    }

    if (transparency_enable && (color.bits & 0x8000u || !texture_enable))
    {
      // Based on blargg's efficient 15bpp pixel math.
      u32 bg_bits = ZeroExtend32(bg_color.bits);
      u32 fg_bits = ZeroExtend32(color.bits);
      switch (cmd->draw_mode.transparency_mode)
      {
        case GPUTransparencyMode::HalfBackgroundPlusHalfForeground:
        {
          bg_bits |= 0x8000u;
          color.bits = Truncate16(((fg_bits + bg_bits) - ((fg_bits ^ bg_bits) & 0x0421u)) >> 1);
        }
        break;

        case GPUTransparencyMode::BackgroundPlusForeground:
        {
          bg_bits &= ~0x8000u;

          const u32 sum = fg_bits + bg_bits;
          const u32 carry = (sum - ((fg_bits ^ bg_bits) & 0x8421u)) & 0x8420u;

          color.bits = Truncate16((sum - carry) | (carry - (carry >> 5)));
        }
        break;

        case GPUTransparencyMode::BackgroundMinusForeground:
        {
          bg_bits |= 0x8000u;
          fg_bits &= ~0x8000u;

          const u32 diff = bg_bits - fg_bits + 0x108420u;
          const u32 borrow = (diff - ((bg_bits ^ fg_bits) & 0x108420u)) & 0x108420u;

          color.bits = Truncate16((diff - borrow) & (borrow - (borrow >> 5)));
        }
        break;

        case GPUTransparencyMode::BackgroundPlusQuarterForeground:
        {
          bg_bits &= ~0x8000u;
          fg_bits = ((fg_bits >> 2) & 0x1CE7u) | 0x8000u;

          const u32 sum = fg_bits + bg_bits;
          const u32 carry = (sum - ((fg_bits ^ bg_bits) & 0x8421u)) & 0x8420u;

          color.bits = Truncate16((sum - carry) | (carry - (carry >> 5)));
        }
        break;
      }

      // See above.
      if constexpr (!texture_enable)
        color.bits &= ~0x8000u;
    }
  }

  u16 result = color.bits;

  if constexpr (mask_or_enable)
    result |= cmd->params.GetMaskOR();

  UPRAM_ACCESSOR[vram_upsize_x * y + x] = result;
}

template<u32 TRectParams, int TUprenderShift>
void GPU_SW_Backend::DrawRectangle(const GPUBackendDrawRectangleCommand* cmd)
{
  constexpr bool texture_enable       = (TRectParams & TRectShader_TextureEnable     ) == TRectShader_TextureEnable     ;
  constexpr bool raw_texture_enable   = (TRectParams & TRectShader_RawTextureEnable  ) == TRectShader_RawTextureEnable  ;
  constexpr bool transparency_enable  = (TRectParams & TRectShader_TransparencyEnable) == TRectShader_TransparencyEnable;
  constexpr bool mask_and_enable      = (TRectParams & TRectShader_MaskAndEnable     ) == TRectShader_MaskAndEnable     ;
  constexpr bool mask_or_enable       = (TRectParams & TRectShader_MaskOrEnable      ) == TRectShader_MaskOrEnable      ;

  const s32 origin_x = cmd->x;
  const s32 origin_y = cmd->y;
  const auto [r, g, b] = UnpackColorRGB24(cmd->color);
  const auto [origin_texcoord_x, origin_texcoord_y] = UnpackTexcoord(cmd->texcoord);

  auto upscale = uprender_scale();

  auto origin_x_up = origin_x * upscale;
  auto origin_y_up = origin_y * upscale;

  auto size_x_up = cmd->width  * upscale;
  auto size_y_up = cmd->height * upscale;

  for (int offset_y = 0; offset_y < size_y_up; offset_y++)
  {
    const s32 y_up = origin_y_up + static_cast<s32>(offset_y);
    if (y_up < static_cast<s32>(m_drawing_area.top * upscale) || y_up > static_cast<s32>(m_drawing_area.bottom * upscale) ||
        (cmd->params.interlaced_rendering && cmd->params.active_line_lsb == (Truncate8(static_cast<u32>(y_up)) & 1u)))
    {
      continue;
    }

    const u8 texcoord_y = Truncate8(ZeroExtend32(origin_texcoord_y) + (offset_y / upscale));

    for (int offset_x = 0; offset_x < size_x_up; offset_x++)
    {
      const s32 x_up = origin_x_up + static_cast<s32>(offset_x);
      if (x_up < static_cast<s32>(m_drawing_area.left * upscale) || x_up > static_cast<s32>(m_drawing_area.right * upscale))
        continue;

      // TODO: sub-pixel texture sampling?
      // Not sure it matters unless we're using tex replacements, as PSX was very likely using 1:1 scale textures
      // for rectangle draws.
      const u8 texcoord_x = Truncate8(ZeroExtend32(origin_texcoord_x) + (offset_x / upscale));

      constexpr u32 ShaderParams = 
        TShaderParam_ShadingEnable      * 0                   |
        TShaderParam_TextureEnable      * texture_enable      |
        TShaderParam_RawTextureEnable   * raw_texture_enable  |
        TShaderParam_TransparencyEnable * transparency_enable |
        TShaderParam_DitheringEnable    * 0                   |
        TShaderParam_MaskAndEnable      * 1                   |
        TShaderParam_MaskOrEnable       * 1                   ;

      ShadePixel<ShaderParams, TUprenderShift>(
        cmd, static_cast<u32>(x_up), static_cast<u32>(y_up), r, g, b, texcoord_x, texcoord_y);
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// Polygon and line rasterization ported from Mednafen
//////////////////////////////////////////////////////////////////////////

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING 10

static ALWAYS_INLINE_RELEASE s64 MakePolyXFP(s32 x)
{
  return ((u64)x << 32) + ((1ULL << 32) - (1 << 11));
}

static ALWAYS_INLINE_RELEASE s64 MakePolyXFPStep(s32 dx, s32 dy)
{
  s64 ret;
  s64 dx_ex = (u64)dx << 32;

  if (dx_ex < 0)
    dx_ex -= dy - 1;

  if (dx_ex > 0)
    dx_ex += dy - 1;

  ret = dx_ex / dy;

  return (ret);
}

static ALWAYS_INLINE_RELEASE s32 GetPolyXFP_Int(s64 xfp)
{
  return (xfp >> 32);
}

template<bool shading_enable, bool texture_enable>
bool ALWAYS_INLINE_RELEASE GPU_SW_Backend::CalcIDeltas(i_deltas& idl, const GPUBackendDrawPolygonCommand::Vertex* A,
                                                       const GPUBackendDrawPolygonCommand::Vertex* B,
                                                       const GPUBackendDrawPolygonCommand::Vertex* C)
{
#define CALCIS(x, y) (((B->x - A->x) * (C->y - B->y)) - ((C->x - B->x) * (B->y - A->y)))

  s32 denom = CALCIS(x, y);

  if (!denom)
    return false;

  if constexpr (shading_enable)
  {
    idl.dr_dx = (u32)(CALCIS(r, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dr_dy = (u32)(CALCIS(x, r) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.dg_dx = (u32)(CALCIS(g, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dg_dy = (u32)(CALCIS(x, g) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.db_dx = (u32)(CALCIS(b, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.db_dy = (u32)(CALCIS(x, b) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  }

  if constexpr (texture_enable)
  {
    idl.du_dx = (u32)(CALCIS(u, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.du_dy = (u32)(CALCIS(x, u) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.dv_dx = (u32)(CALCIS(v, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dv_dy = (u32)(CALCIS(x, v) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  }

  return true;

#undef CALCIS
}

template<bool shading_enable, bool texture_enable, typename I_GROUP>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::AddIDeltas_DX(I_GROUP& ig, const i_deltas& idl, AddDeltasScalar_t count /*= 1*/)
{
  if constexpr (shading_enable)
  {
    ig.r += (idl.dr_dx * count);
    ig.g += (idl.dg_dx * count);
    ig.b += (idl.db_dx * count);
  }

  if constexpr (texture_enable)
  {
    ig.u += (idl.du_dx * count);
    ig.v += (idl.dv_dx * count);
  }
}

template<bool shading_enable, bool texture_enable, typename I_GROUP>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::AddIDeltas_DY(I_GROUP& ig, const i_deltas& idl, AddDeltasScalar_t count /*= 1*/)
{
  if constexpr (shading_enable)
  {
    ig.r += (idl.dr_dy * count);
    ig.g += (idl.dg_dy * count);
    ig.b += (idl.db_dy * count);
  }

  if constexpr (texture_enable)
  {
    ig.u += (idl.du_dy * count);
    ig.v += (idl.dv_dy * count);
  }
}

struct i_group_float
{
  float u, v;
  float r, g, b;
};

template<u32 TShaderParams, int TUprenderShift>
void GPU_SW_Backend::DrawSpan(const GPUBackendDrawPolygonCommand* cmd, s32 y_up, s32 x_start_up, s32 x_bound_up, i_group igi,
                              const i_deltas& idl)
{
  constexpr int upscale = (1 << TUprenderShift);

  constexpr bool shading_enable       = (TShaderParams & TShaderParam_ShadingEnable     ) == TShaderParam_ShadingEnable     ;
  constexpr bool texture_enable       = (TShaderParams & TShaderParam_TextureEnable     ) == TShaderParam_TextureEnable     ;
  constexpr bool raw_texture_enable   = (TShaderParams & TShaderParam_RawTextureEnable  ) == TShaderParam_RawTextureEnable  ;
  constexpr bool transparency_enable  = (TShaderParams & TShaderParam_TransparencyEnable) == TShaderParam_TransparencyEnable;
  constexpr bool dithering_enable     = (TShaderParams & TShaderParam_DitheringEnable   ) == TShaderParam_DitheringEnable   ;
  constexpr bool mask_and_enable      = (TShaderParams & TShaderParam_MaskAndEnable     ) == TShaderParam_MaskAndEnable     ;
  constexpr bool mask_or_enable       = (TShaderParams & TShaderParam_MaskOrEnable      ) == TShaderParam_MaskOrEnable      ;

  auto y_native       = y_up       / upscale;
  auto x_start_native = x_start_up / upscale;
  auto x_bound_native = x_bound_up / upscale;

  if (cmd->params.interlaced_rendering && cmd->params.active_line_lsb == (y_up & 1u))
    return;

  s32 x_ig_adjust = x_start_up;
  s32 w_native    = x_bound_native - x_start_native;
  s32 w_up        = x_bound_up     - x_start_up;
  s32 x_native    = SignExtendN<11, s32>(x_start_native);
  s32 x_up        = SignExtendN<11 + TUprenderShift, s32>(x_start_up);

  if (x_up < static_cast<s32>(m_drawing_area.left * upscale))
  {
    s32 delta_up = static_cast<s32>(m_drawing_area.left * upscale) - x_up;
    x_ig_adjust += delta_up;
    x_up        += delta_up;
    w_up        -= delta_up;
  }

  if ((x_up + w_up) > (static_cast<s32>(m_drawing_area.right * upscale) + upscale))
  {
    w_up = static_cast<s32>(m_drawing_area.right * upscale) + upscale - x_up;
  }

  if (w_up <= 0)
    return;

#if 0
  if (x_native < static_cast<s32>(m_drawing_area.left))
  {
    s32 delta_native = static_cast<s32>(m_drawing_area.left) - x_native;
    x_native += delta_native;
    w_native -= delta_native;
  }

  if ((x_native + w_native) > (static_cast<s32>(m_drawing_area.right) + 1))
  {
    w_native = (static_cast<s32>(m_drawing_area.right + 1))  - x_native;
  }

  if (w_native <= 0)
    return;
#endif

#if USE_FLOAT_STEP
  i_group_float igf = {};

  igf.r = (float)igi.r / (1ll << (COORD_FBS + COORD_POST_PADDING));
  igf.g = (float)igi.g / (1ll << (COORD_FBS + COORD_POST_PADDING));
  igf.b = (float)igi.b / (1ll << (COORD_FBS + COORD_POST_PADDING));
  igf.u = (float)igi.u / (1ll << (COORD_FBS + COORD_POST_PADDING));
  igf.v = (float)igi.v / (1ll << (COORD_FBS + COORD_POST_PADDING));

  float postpad_scalar = 1ll << (COORD_FBS + COORD_POST_PADDING;

  AddIDeltas_DX<shading_enable, texture_enable>(igf, idl, (float)x_ig_adjust / postpad_scalar);
  AddIDeltas_DY<shading_enable, texture_enable>(igf, idl, (float)y_up        / postpad_scalar);
#endif

#if USE_INT_STEP
  AddIDeltas_DX<shading_enable, texture_enable>(igi, idl, x_ig_adjust);
  AddIDeltas_DY<shading_enable, texture_enable>(igi, idl, y_up       );
#endif

  do
  {
    DebugAssert(x_up <= static_cast<s32>(m_drawing_area.right+1) * upscale);

#if USE_FLOAT_STEP
    auto r = Truncate8((int)floorf(igf.r));
    auto g = Truncate8((int)floorf(igf.g));
    auto b = Truncate8((int)floorf(igf.b));
    auto u = Truncate8((int)floorf(igf.u));
    auto v = Truncate8((int)floorf(igf.v));
#endif

#if USE_INT_STEP
    auto ri = Truncate8(igi.r >> (COORD_FBS + COORD_POST_PADDING));
    auto gi = Truncate8(igi.g >> (COORD_FBS + COORD_POST_PADDING));
    auto bi = Truncate8(igi.b >> (COORD_FBS + COORD_POST_PADDING));
    auto ui = Truncate8(igi.u >> (COORD_FBS + COORD_POST_PADDING));
    auto vi = Truncate8(igi.v >> (COORD_FBS + COORD_POST_PADDING));
#endif

    //DebugAssert(r == ri && u == ui);
    //DebugAssert(g == gi && v == vi);
    //DebugAssert(b == bi);

    constexpr u32 ShaderParams = 
      TShaderParam_ShadingEnable      * 0                   |
      TShaderParam_TextureEnable      * texture_enable      |
      TShaderParam_RawTextureEnable   * raw_texture_enable  |
      TShaderParam_TransparencyEnable * transparency_enable |
      TShaderParam_DitheringEnable    * dithering_enable    |
      TShaderParam_MaskAndEnable      * mask_and_enable     |
      TShaderParam_MaskOrEnable       * mask_or_enable      ;

    // FIXME: also need to clip uprender right edge.
    if (x_up >= 0) {
      ShadePixel<ShaderParams, TUprenderShift>(
        cmd, x_up, y_up,
#if USE_INT_STEP
        ri, gi, bi, ui, vi
#else
        r, g, b, u, v
#endif
      );
    }
    x_up++;

#if USE_FLOAT_STEP
    AddIDeltas_DX<shading_enable, texture_enable>(igf, idl, 1.0f / postpad_scalar);
#endif

#if USE_INT_STEP
    AddIDeltas_DX<shading_enable, texture_enable>(igi, idl);
#endif

  } while (--w_up > 0);
}

template<u32 TShaderParams, int TUprenderShift>
void GPU_SW_Backend::DrawTriangle(const GPUBackendDrawPolygonCommand* cmd,
                                  const GPUBackendDrawPolygonCommand::Vertex* v0n,
                                  const GPUBackendDrawPolygonCommand::Vertex* v1n,
                                  const GPUBackendDrawPolygonCommand::Vertex* v2n)
{
  int constexpr upscale = (1 << TUprenderShift);

  constexpr bool shading_enable       = (TShaderParams & TShaderParam_ShadingEnable     ) == TShaderParam_ShadingEnable     ;
  constexpr bool texture_enable       = (TShaderParams & TShaderParam_TextureEnable     ) == TShaderParam_TextureEnable     ;
  constexpr bool raw_texture_enable   = (TShaderParams & TShaderParam_RawTextureEnable  ) == TShaderParam_RawTextureEnable  ;
  constexpr bool transparency_enable  = (TShaderParams & TShaderParam_TransparencyEnable) == TShaderParam_TransparencyEnable;
  constexpr bool dithering_enable     = (TShaderParams & TShaderParam_DitheringEnable   ) == TShaderParam_DitheringEnable   ;
  constexpr bool mask_and_enable      = (TShaderParams & TShaderParam_MaskAndEnable     ) == TShaderParam_MaskAndEnable     ;
  constexpr bool mask_or_enable       = (TShaderParams & TShaderParam_MaskOrEnable      ) == TShaderParam_MaskOrEnable      ;

  struct TriangleHalf
  {
    u64 x_coord[2];
    u64 x_step[2];

    s32 y_coord;
    s32 y_bound;

    bool dec_mode;
  } tripart[2];

  i_group ig;
  i_deltas idl;

  if (1)
  {
    u32 core_vertex;
    {
      u32 cvtemp = 0;

      if (v1n->x <= v0n->x)
      {
        if (v2n->x <= v1n->x)
          cvtemp = (1 << 2);
        else
          cvtemp = (1 << 1);
      }
      else if (v2n->x < v0n->x)
        cvtemp = (1 << 2);
      else
        cvtemp = (1 << 0);

      if (v2n->y < v1n->y)
      {
        std::swap(v2n, v1n);
        cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
      }

      if (v1n->y < v0n->y)
      {
        std::swap(v1n, v0n);
        cvtemp = ((cvtemp >> 1) & 0x1) | ((cvtemp << 1) & 0x2) | (cvtemp & 0x4);
      }

      if (v2n->y < v1n->y)
      {
        std::swap(v2n, v1n);
        cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
      }

      core_vertex = cvtemp >> 1;
    }

    if (v0n->y == v2n->y)
      return;

    if (static_cast<u32>(std::abs(v2n->x - v0n->x)) >= MAX_PRIMITIVE_WIDTH ||
        static_cast<u32>(std::abs(v2n->x - v1n->x)) >= MAX_PRIMITIVE_WIDTH ||
        static_cast<u32>(std::abs(v1n->x - v0n->x)) >= MAX_PRIMITIVE_WIDTH ||
        static_cast<u32>(v2n->y - v0n->y) >= MAX_PRIMITIVE_HEIGHT)
    {
      return;
    }

    s64 bound_coord_us;
    s64 bound_coord_ls;
    bool right_facing;

    GPUBackendDrawPolygonCommand::Vertex up_v0 = *v0n;
    GPUBackendDrawPolygonCommand::Vertex up_v1 = *v1n;
    GPUBackendDrawPolygonCommand::Vertex up_v2 = *v2n;

    GPUBackendDrawPolygonCommand::Vertex* v0 = &up_v0;
    GPUBackendDrawPolygonCommand::Vertex* v1 = &up_v1;
    GPUBackendDrawPolygonCommand::Vertex* v2 = &up_v2;

    v0->x *= upscale;
    v0->y *= upscale;
    v1->x *= upscale;
    v1->y *= upscale;
    v2->x *= upscale;
    v2->y *= upscale;

    s64 base_coord = MakePolyXFP(v0->x);
    s64 base_step = MakePolyXFPStep((v2->x - v0->x), (v2->y - v0->y));

    if (v1->y == v0->y)
    {
      bound_coord_us = 0;
      right_facing = (bool)(v1->x > v0->x);
    }
    else
    {
      bound_coord_us = MakePolyXFPStep((v1->x - v0->x), (v1->y - v0->y));
      right_facing = (bool)(bound_coord_us > base_step);
    }

    if (v2->y == v1->y)
      bound_coord_ls = 0;
    else
      bound_coord_ls = MakePolyXFPStep((v2->x - v1->x), (v2->y - v1->y));

    if (!CalcIDeltas<shading_enable, texture_enable>(idl, v0, v1, v2))
      return;

    const GPUBackendDrawPolygonCommand::Vertex* vertices[3] = {v0, v1, v2};
    int upshift = TUprenderShift;

    if constexpr (texture_enable)
    {
      ig.u = (COORD_MF_INT(vertices[core_vertex]->u) + (1 << (COORD_FBS - 1 - upshift))) << COORD_POST_PADDING;
      ig.v = (COORD_MF_INT(vertices[core_vertex]->v) + (1 << (COORD_FBS - 1 - upshift))) << COORD_POST_PADDING;
    }

    ig.r = (COORD_MF_INT(vertices[core_vertex]->r) + (1 << (COORD_FBS - 1 - upshift))) << COORD_POST_PADDING;
    ig.g = (COORD_MF_INT(vertices[core_vertex]->g) + (1 << (COORD_FBS - 1 - upshift))) << COORD_POST_PADDING;
    ig.b = (COORD_MF_INT(vertices[core_vertex]->b) + (1 << (COORD_FBS - 1 - upshift))) << COORD_POST_PADDING;

    AddIDeltas_DX<shading_enable, texture_enable>(ig, idl, -vertices[core_vertex]->x);
    AddIDeltas_DY<shading_enable, texture_enable>(ig, idl, -vertices[core_vertex]->y);

    u32 vo = 0;
    u32 vp = 0;
    if (core_vertex != 0)
      vo = 1;
    if (core_vertex == 2)
      vp = 3;

    {
      TriangleHalf* tp = &tripart[vo];
      tp->y_coord = vertices[0 ^ vo]->y;
      tp->y_bound = vertices[1 ^ vo]->y;
      tp->x_coord[right_facing] = MakePolyXFP(vertices[0 ^ vo]->x);
      tp->x_step[right_facing] = bound_coord_us;
      tp->x_coord[!right_facing] = base_coord + ((vertices[vo]->y - vertices[0]->y) * base_step);
      tp->x_step[!right_facing] = base_step;
      tp->dec_mode = vo;
    }

    {
      TriangleHalf* tp = &tripart[vo ^ 1];
      tp->y_coord = vertices[1 ^ vp]->y;
      tp->y_bound = vertices[2 ^ vp]->y;
      tp->x_coord[right_facing] = MakePolyXFP(vertices[1 ^ vp]->x);
      tp->x_step[right_facing] = bound_coord_ls;
      tp->x_coord[!right_facing] =
        base_coord + ((vertices[1 ^ vp]->y - vertices[0]->y) *
                      base_step); // base_coord + ((vertices[1].y - vertices[0].y) * base_step);
      tp->x_step[!right_facing] = base_step;
      tp->dec_mode = vp;
    }
  }

  // TODO: none of the code above this line depends on template parameters.
  // We could generalize that code and then 

  for (u32 i = 0; i < 2; i++)
  {
    s32 yi = tripart[i].y_coord;
    s32 yb = tripart[i].y_bound;

    u64 lc = tripart[i].x_coord[0];
    u64 ls = tripart[i].x_step[0];

    u64 rc = tripart[i].x_coord[1];
    u64 rs = tripart[i].x_step[1];

    if (tripart[i].dec_mode)
    {
      while (yi > yb)
      {
        yi--;
        lc -= ls;
        rc -= rs;

        s32 y = SignExtendN<11 + TUprenderShift, s32>(yi);

        if (y < static_cast<s32>(m_drawing_area.top) * upscale)
          break;

        if (y > (static_cast<s32>(m_drawing_area.bottom) * upscale) + (upscale/2))
          continue;

        DrawSpan<TShaderParams, TUprenderShift>(
          cmd, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
      }
    }
    else
    {
      while (yi < yb)
      {
        s32 y = SignExtendN<11 + TUprenderShift, s32>(yi);

        if (y > static_cast<s32>(m_drawing_area.bottom) * upscale)
          break;

        if (y >= static_cast<s32>(m_drawing_area.top) * upscale)
        {
          DrawSpan<TShaderParams, TUprenderShift>(
            cmd, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
        }

        yi++;
        lc += ls;
        rc += rs;
      }
    }
  }
}

GPU_SW_Backend::DrawTriangleFunction GPU_SW_Backend::GetDrawTriangleFunction(u32 shaderParams, int uprender_shift)
{
#define FN(bmask)   &GPU_SW_Backend::DrawTriangle<bmask,0>
#define F2(bmask)   &GPU_SW_Backend::DrawTriangle<bmask,1>
#define F4(bmask)   &GPU_SW_Backend::DrawTriangle<bmask,2>

  static constexpr DrawTriangleFunction funcs[3][TShaderParams_MAX] = {
    {
      FN(0x000),FN(0x001),FN(0x002),FN(0x003),FN(0x004),FN(0x005),FN(0x006),FN(0x007),
      FN(0x008),FN(0x009),FN(0x00a),FN(0x00b),FN(0x00c),FN(0x00d),FN(0x00e),FN(0x00f),
      FN(0x010),FN(0x011),FN(0x012),FN(0x013),FN(0x014),FN(0x015),FN(0x016),FN(0x017),
      FN(0x018),FN(0x019),FN(0x01a),FN(0x01b),FN(0x01c),FN(0x01d),FN(0x01e),FN(0x01f),
      FN(0x020),FN(0x021),FN(0x022),FN(0x023),FN(0x024),FN(0x025),FN(0x026),FN(0x027),
      FN(0x028),FN(0x029),FN(0x02a),FN(0x02b),FN(0x02c),FN(0x02d),FN(0x02e),FN(0x02f),
      FN(0x030),FN(0x031),FN(0x032),FN(0x033),FN(0x034),FN(0x035),FN(0x036),FN(0x037),
      FN(0x038),FN(0x039),FN(0x03a),FN(0x03b),FN(0x03c),FN(0x03d),FN(0x03e),FN(0x03f),
      FN(0x040),FN(0x041),FN(0x042),FN(0x043),FN(0x044),FN(0x045),FN(0x046),FN(0x047),
      FN(0x048),FN(0x049),FN(0x04a),FN(0x04b),FN(0x04c),FN(0x04d),FN(0x04e),FN(0x04f),
      FN(0x050),FN(0x051),FN(0x052),FN(0x053),FN(0x054),FN(0x055),FN(0x056),FN(0x057),
      FN(0x058),FN(0x059),FN(0x05a),FN(0x05b),FN(0x05c),FN(0x05d),FN(0x05e),FN(0x05f),
      FN(0x060),FN(0x061),FN(0x062),FN(0x063),FN(0x064),FN(0x065),FN(0x066),FN(0x067),
      FN(0x068),FN(0x069),FN(0x06a),FN(0x06b),FN(0x06c),FN(0x06d),FN(0x06e),FN(0x06f),
      FN(0x070),FN(0x071),FN(0x072),FN(0x073),FN(0x074),FN(0x075),FN(0x076),FN(0x077),
      FN(0x078),FN(0x079),FN(0x07a),FN(0x07b),FN(0x07c),FN(0x07d),FN(0x07e),FN(0x07f),
    },
    {
      F2(0x000),F2(0x001),F2(0x002),F2(0x003),F2(0x004),F2(0x005),F2(0x006),F2(0x007),
      F2(0x008),F2(0x009),F2(0x00a),F2(0x00b),F2(0x00c),F2(0x00d),F2(0x00e),F2(0x00f),
      F2(0x010),F2(0x011),F2(0x012),F2(0x013),F2(0x014),F2(0x015),F2(0x016),F2(0x017),
      F2(0x018),F2(0x019),F2(0x01a),F2(0x01b),F2(0x01c),F2(0x01d),F2(0x01e),F2(0x01f),
      F2(0x020),F2(0x021),F2(0x022),F2(0x023),F2(0x024),F2(0x025),F2(0x026),F2(0x027),
      F2(0x028),F2(0x029),F2(0x02a),F2(0x02b),F2(0x02c),F2(0x02d),F2(0x02e),F2(0x02f),
      F2(0x030),F2(0x031),F2(0x032),F2(0x033),F2(0x034),F2(0x035),F2(0x036),F2(0x037),
      F2(0x038),F2(0x039),F2(0x03a),F2(0x03b),F2(0x03c),F2(0x03d),F2(0x03e),F2(0x03f),
      F2(0x040),F2(0x041),F2(0x042),F2(0x043),F2(0x044),F2(0x045),F2(0x046),F2(0x047),
      F2(0x048),F2(0x049),F2(0x04a),F2(0x04b),F2(0x04c),F2(0x04d),F2(0x04e),F2(0x04f),
      F2(0x050),F2(0x051),F2(0x052),F2(0x053),F2(0x054),F2(0x055),F2(0x056),F2(0x057),
      F2(0x058),F2(0x059),F2(0x05a),F2(0x05b),F2(0x05c),F2(0x05d),F2(0x05e),F2(0x05f),
      F2(0x060),F2(0x061),F2(0x062),F2(0x063),F2(0x064),F2(0x065),F2(0x066),F2(0x067),
      F2(0x068),F2(0x069),F2(0x06a),F2(0x06b),F2(0x06c),F2(0x06d),F2(0x06e),F2(0x06f),
      F2(0x070),F2(0x071),F2(0x072),F2(0x073),F2(0x074),F2(0x075),F2(0x076),F2(0x077),
      F2(0x078),F2(0x079),F2(0x07a),F2(0x07b),F2(0x07c),F2(0x07d),F2(0x07e),F2(0x07f),
    },
    {
      F4(0x000),F4(0x001),F4(0x002),F4(0x003),F4(0x004),F4(0x005),F4(0x006),F4(0x007),
      F4(0x008),F4(0x009),F4(0x00a),F4(0x00b),F4(0x00c),F4(0x00d),F4(0x00e),F4(0x00f),
      F4(0x010),F4(0x011),F4(0x012),F4(0x013),F4(0x014),F4(0x015),F4(0x016),F4(0x017),
      F4(0x018),F4(0x019),F4(0x01a),F4(0x01b),F4(0x01c),F4(0x01d),F4(0x01e),F4(0x01f),
      F4(0x020),F4(0x021),F4(0x022),F4(0x023),F4(0x024),F4(0x025),F4(0x026),F4(0x027),
      F4(0x028),F4(0x029),F4(0x02a),F4(0x02b),F4(0x02c),F4(0x02d),F4(0x02e),F4(0x02f),
      F4(0x030),F4(0x031),F4(0x032),F4(0x033),F4(0x034),F4(0x035),F4(0x036),F4(0x037),
      F4(0x038),F4(0x039),F4(0x03a),F4(0x03b),F4(0x03c),F4(0x03d),F4(0x03e),F4(0x03f),
      F4(0x040),F4(0x041),F4(0x042),F4(0x043),F4(0x044),F4(0x045),F4(0x046),F4(0x047),
      F4(0x048),F4(0x049),F4(0x04a),F4(0x04b),F4(0x04c),F4(0x04d),F4(0x04e),F4(0x04f),
      F4(0x050),F4(0x051),F4(0x052),F4(0x053),F4(0x054),F4(0x055),F4(0x056),F4(0x057),
      F4(0x058),F4(0x059),F4(0x05a),F4(0x05b),F4(0x05c),F4(0x05d),F4(0x05e),F4(0x05f),
      F4(0x060),F4(0x061),F4(0x062),F4(0x063),F4(0x064),F4(0x065),F4(0x066),F4(0x067),
      F4(0x068),F4(0x069),F4(0x06a),F4(0x06b),F4(0x06c),F4(0x06d),F4(0x06e),F4(0x06f),
      F4(0x070),F4(0x071),F4(0x072),F4(0x073),F4(0x074),F4(0x075),F4(0x076),F4(0x077),
      F4(0x078),F4(0x079),F4(0x07a),F4(0x07b),F4(0x07c),F4(0x07d),F4(0x07e),F4(0x07f),
    }
  };

#undef FN
#undef F2
#undef F4

  return funcs[uprender_shift][shaderParams];
}

enum
{
  Line_XY_FractBits = 32
};
enum
{
  Line_RGB_FractBits = 12
};

struct line_fxp_coord
{
  u64 x, y;
  u32 r, g, b;
};

struct line_fxp_step
{
  s64 dx_dk, dy_dk;
  s32 dr_dk, dg_dk, db_dk;
};

static ALWAYS_INLINE_RELEASE s64 LineDivide(s64 delta, s32 dk)
{
  delta = (u64)delta << Line_XY_FractBits;

  if (delta < 0)
    delta -= dk - 1;
  if (delta > 0)
    delta += dk - 1;

  return (delta / dk);
}

template<u32 TLineParams, int TUprenderShift>
void GPU_SW_Backend::DrawLine(const GPUBackendDrawLineCommand* cmd, const GPUBackendDrawLineCommand::Vertex* p0,
                              const GPUBackendDrawLineCommand::Vertex* p1)
{
  constexpr auto upscale = (1<<TUprenderShift);
  constexpr bool shading_enable       = (TLineParams & TLineShader_ShadingEnable     ) == TLineShader_ShadingEnable     ;
  constexpr bool transparency_enable  = (TLineParams & TLineShader_TransparencyEnable) == TLineShader_TransparencyEnable;
  constexpr bool dithering_enable     = (TLineParams & TLineShader_DitheringEnable   ) == TLineShader_DitheringEnable   ;
  constexpr bool mask_and_enable      = (TLineParams & TLineShader_MaskAndEnable     ) == TLineShader_MaskAndEnable     ;
  constexpr bool mask_or_enable       = (TLineParams & TLineShader_MaskOrEnable      ) == TLineShader_MaskOrEnable      ;

  const s32 i_dx = std::abs(p1->x - p0->x);
  const s32 i_dy = std::abs(p1->y - p0->y);
  const s32 k = (i_dx > i_dy) ? i_dx : i_dy;
  if (i_dx >= MAX_PRIMITIVE_WIDTH || i_dy >= MAX_PRIMITIVE_HEIGHT)
    return;

  if (p0->x >= p1->x && k > 0)
    std::swap(p0, p1);

  line_fxp_step step;
  if (k == 0)
  {
    step.dx_dk = 0;
    step.dy_dk = 0;

    if constexpr (shading_enable)
    {
      step.dr_dk = 0;
      step.dg_dk = 0;
      step.db_dk = 0;
    }
  }
  else
  {
    step.dx_dk = LineDivide(p1->x - p0->x, k);
    step.dy_dk = LineDivide(p1->y - p0->y, k);

    if constexpr (shading_enable)
    {
      step.dr_dk = (s32)((u32)(p1->r - p0->r) << Line_RGB_FractBits) / k;
      step.dg_dk = (s32)((u32)(p1->g - p0->g) << Line_RGB_FractBits) / k;
      step.db_dk = (s32)((u32)(p1->b - p0->b) << Line_RGB_FractBits) / k;
    }
  }

  line_fxp_coord cur_point;
  cur_point.x = ((u64)p0->x << Line_XY_FractBits) | (1ULL << (Line_XY_FractBits - 1));
  cur_point.y = ((u64)p0->y << Line_XY_FractBits) | (1ULL << (Line_XY_FractBits - 1));

  cur_point.x -= 1024;

  if (step.dy_dk < 0)
    cur_point.y -= 1024;

  if constexpr (shading_enable)
  {
    cur_point.r = (p0->r << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
    cur_point.g = (p0->g << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
    cur_point.b = (p0->b << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
  }

  for (s32 i = 0; i <= k; i++)
  {
    // Sign extension is not necessary here for x and y, due to the maximum values that ClipX1 and ClipY1 can contain.
    const s32 x = (cur_point.x >> Line_XY_FractBits) & 2047;
    const s32 y = (cur_point.y >> Line_XY_FractBits) & 2047;

    if ((!cmd->params.interlaced_rendering || cmd->params.active_line_lsb != (Truncate8(static_cast<u32>(y)) & 1u)) &&
        x >= static_cast<s32>(m_drawing_area.left) && x <= static_cast<s32>(m_drawing_area.right) &&
        y >= static_cast<s32>(m_drawing_area.top)  && y <= static_cast<s32>(m_drawing_area.bottom))
    {
      const u8 r = shading_enable ? static_cast<u8>(cur_point.r >> Line_RGB_FractBits) : p0->r;
      const u8 g = shading_enable ? static_cast<u8>(cur_point.g >> Line_RGB_FractBits) : p0->g;
      const u8 b = shading_enable ? static_cast<u8>(cur_point.b >> Line_RGB_FractBits) : p0->b;

      // FIXME: this is sufficient for horizontal or vertical lines, but diaglonals will likely
      // need more advanced heuristics or anti-aliasing to avoid looking ugly and/or causing unsightly
      // coverage artifacts.

      for (int yu=0; yu<upscale; ++yu)
      {
        for (int xu=0; xu<upscale; ++xu)
        {
          constexpr u32 ShaderParams = 
            TShaderParam_ShadingEnable      * 0                   |
            TShaderParam_TextureEnable      * 0                   |
            TShaderParam_RawTextureEnable   * 0                   |
            TShaderParam_TransparencyEnable * transparency_enable |
            TShaderParam_DitheringEnable    * dithering_enable    |
            TShaderParam_MaskAndEnable      * 1                   |
            TShaderParam_MaskOrEnable       * 1                   ;

          ShadePixel<ShaderParams, TUprenderShift>(cmd,
            static_cast<u32>((x * upscale) + xu),
            static_cast<u32>((y * upscale) + yu),
            r, g, b, 0, 0
          );
        }
      }
    }
    cur_point.x += step.dx_dk;
    cur_point.y += step.dy_dk;

    if constexpr (shading_enable)
    {
      cur_point.r += step.dr_dk;
      cur_point.g += step.dg_dk;
      cur_point.b += step.db_dk;
    }
  }
}

GPU_SW_Backend::DrawLineFunction GPU_SW_Backend::GetDrawLineFunction(u32 lineShaderParams, int upshift)
{
#define FN(bmask) &GPU_SW_Backend::DrawLine<bmask, 0>
#define F2(bmask) &GPU_SW_Backend::DrawLine<bmask, 1>
#define F4(bmask) &GPU_SW_Backend::DrawLine<bmask, 2>

  static constexpr DrawLineFunction funcs[3][TLineShader_MAX] = {
    {
      FN(0x000),FN(0x001),FN(0x002),FN(0x003),FN(0x004),FN(0x005),FN(0x006),FN(0x007),
      FN(0x008),FN(0x009),FN(0x00a),FN(0x00b),FN(0x00c),FN(0x00d),FN(0x00e),FN(0x00f),
      FN(0x010),FN(0x011),FN(0x012),FN(0x013),FN(0x014),FN(0x015),FN(0x016),FN(0x017),
      FN(0x018),FN(0x019),FN(0x01a),FN(0x01b),FN(0x01c),FN(0x01d),FN(0x01e),FN(0x01f),    
    },
    {
      F2(0x000),F2(0x001),F2(0x002),F2(0x003),F2(0x004),F2(0x005),F2(0x006),F2(0x007),
      F2(0x008),F2(0x009),F2(0x00a),F2(0x00b),F2(0x00c),F2(0x00d),F2(0x00e),F2(0x00f),
      F2(0x010),F2(0x011),F2(0x012),F2(0x013),F2(0x014),F2(0x015),F2(0x016),F2(0x017),
      F2(0x018),F2(0x019),F2(0x01a),F2(0x01b),F2(0x01c),F2(0x01d),F2(0x01e),F2(0x01f),
    },
    {
      F4(0x000),F4(0x001),F4(0x002),F4(0x003),F4(0x004),F4(0x005),F4(0x006),F4(0x007),
      F4(0x008),F4(0x009),F4(0x00a),F4(0x00b),F4(0x00c),F4(0x00d),F4(0x00e),F4(0x00f),
      F4(0x010),F4(0x011),F4(0x012),F4(0x013),F4(0x014),F4(0x015),F4(0x016),F4(0x017),
      F4(0x018),F4(0x019),F4(0x01a),F4(0x01b),F4(0x01c),F4(0x01d),F4(0x01e),F4(0x01f),
    }
  };

#undef F

#undef FN
#undef F2
#undef F4

  return funcs[upshift][lineShaderParams];
}

GPU_SW_Backend::DrawRectangleFunction
GPU_SW_Backend::GetDrawRectangleFunction(u32 rectShaderParams, int upshift)
{
#define FN(bmask) &GPU_SW_Backend::DrawRectangle<bmask, 0>
#define F2(bmask) &GPU_SW_Backend::DrawRectangle<bmask, 1>
#define F4(bmask) &GPU_SW_Backend::DrawRectangle<bmask, 2>

  static constexpr DrawRectangleFunction funcs[3][TRectShader_MAX] = {
    {
      FN(0x000),FN(0x001),FN(0x002),FN(0x003),FN(0x004),FN(0x005),FN(0x006),FN(0x007),
      FN(0x008),FN(0x009),FN(0x00a),FN(0x00b),FN(0x00c),FN(0x00d),FN(0x00e),FN(0x00f),
      FN(0x010),FN(0x011),FN(0x012),FN(0x013),FN(0x014),FN(0x015),FN(0x016),FN(0x017),
      FN(0x018),FN(0x019),FN(0x01a),FN(0x01b),FN(0x01c),FN(0x01d),FN(0x01e),FN(0x01f),    
    },
    {
      F2(0x000),F2(0x001),F2(0x002),F2(0x003),F2(0x004),F2(0x005),F2(0x006),F2(0x007),
      F2(0x008),F2(0x009),F2(0x00a),F2(0x00b),F2(0x00c),F2(0x00d),F2(0x00e),F2(0x00f),
      F2(0x010),F2(0x011),F2(0x012),F2(0x013),F2(0x014),F2(0x015),F2(0x016),F2(0x017),
      F2(0x018),F2(0x019),F2(0x01a),F2(0x01b),F2(0x01c),F2(0x01d),F2(0x01e),F2(0x01f),
    },
    {
      F4(0x000),F4(0x001),F4(0x002),F4(0x003),F4(0x004),F4(0x005),F4(0x006),F4(0x007),
      F4(0x008),F4(0x009),F4(0x00a),F4(0x00b),F4(0x00c),F4(0x00d),F4(0x00e),F4(0x00f),
      F4(0x010),F4(0x011),F4(0x012),F4(0x013),F4(0x014),F4(0x015),F4(0x016),F4(0x017),
      F4(0x018),F4(0x019),F4(0x01a),F4(0x01b),F4(0x01c),F4(0x01d),F4(0x01e),F4(0x01f),
    }
  };

#undef FN
#undef F2
#undef F4

  return funcs[upshift][rectShaderParams];
}

void GPU_SW_Backend::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color, GPUBackendCommandParameters params)
{
  const u16 color16 = VRAMRGBA8888ToRGBA5551(color);

  if (m_uprender_shift == 0)
  {
    if ((x + width) <= VRAM_WIDTH && !params.interlaced_rendering)
    {
      for (u32 yoffs = 0; yoffs < height; yoffs++)
      {
        const u32 row = (y + yoffs) % VRAM_HEIGHT;
        std::fill_n(&UPRAM_ACCESSOR[row * VRAM_WIDTH + x], width, color16);
      }
    }
    else if (params.interlaced_rendering)
    {
      // Hardware tests show that fills seem to break on the first two lines when the offset matches the displayed field.
      const u32 active_field = params.active_line_lsb;
      for (u32 yoffs = 0; yoffs < height; yoffs++)
      {
        const u32 row = (y + yoffs) % VRAM_HEIGHT;
        if ((row & u32(1)) == active_field)
          continue;
           
        u16* row_ptr = &UPRAM_ACCESSOR[row * VRAM_WIDTH];
        for (u32 xoffs = 0; xoffs < width; xoffs++)
        {
          const u32 col = (x + xoffs) % VRAM_WIDTH;
          row_ptr[col] = color16;
        }
      }
    }
    else
    {
      for (u32 yoffs = 0; yoffs < height; yoffs++)
      {
        const u32 row = (y + yoffs) % VRAM_HEIGHT;
        u16* row_ptr = &UPRAM_ACCESSOR[row * VRAM_WIDTH];
        for (u32 xoffs = 0; xoffs < width; xoffs++)
        {
          const u32 col = (x + xoffs) % VRAM_WIDTH;
          row_ptr[col] = color16;
        }
      }
    }
  }
  else
  {
    const u32 active_field = params.active_line_lsb;
    for (u32 yoffs = 0; yoffs < height; yoffs++)
    {
      for(u32 xoffs = 0; xoffs < width; xoffs++)
      {
        const u32 row = (y + yoffs) % VRAM_HEIGHT;
        if (params.interlaced_rendering && (row & u32(1)) == active_field)
          continue;

        SetPixelSlow(x+xoffs, y+yoffs, color16);
      }
    }
  }
}


void GPU_SW_Backend::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data,
                                GPUBackendCommandParameters params)
{
  // Fast path when the copy is not oversized.
  if ((x + width) <= VRAM_WIDTH && (y + height) <= VRAM_HEIGHT && !params.IsMaskingEnabled())
  {
    const u16* src_ptr = static_cast<const u16*>(data);
    if (m_uprender_shift == 0)
    {
      u16* dst_ptr = &UPRAM_ACCESSOR[y * VRAM_WIDTH + x];
      for (u32 yoffs = 0; yoffs < height; yoffs++)
      {
        std::copy_n(src_ptr, width, dst_ptr);
        src_ptr += width;
        dst_ptr += VRAM_WIDTH;
      }
    }
    else
    {
      for (u32 yoffs = 0; yoffs < height; yoffs++)
      {
        for(u32 xoffs = 0; xoffs < width; xoffs++, src_ptr++)
        {
          SetPixelSlow(x+xoffs, y+yoffs, src_ptr[0]);
        }
      }
    }
  }
  else
  {
    // Slow path when we need to handle wrap-around.
    const u16* src_ptr = static_cast<const u16*>(data);
    const u16 mask_and = params.GetMaskAND();
    const u16 mask_or = params.GetMaskOR();

    for (u32 row = 0; row < height;)
    {
      auto dsty = (y + row++) % VRAM_HEIGHT;
      for (u32 col = 0; col < width;)
      {
        // TODO: Handle unaligned reads...
        auto dstx = (x + col++) % VRAM_WIDTH;

        auto pixel = GetPixelSlow(dstx, dsty);
        if ((pixel & mask_and) == 0)
          SetPixelSlow(dstx, dsty, *(src_ptr++) | mask_or);
      }
    }
  }
}

void GPU_SW_Backend::ReadVRAM(u32 x, u32 y, u32 width, u32 height)
{
  // copy from m_upram_ptr to m_vram_ptr using simple sparse read logic.

  auto* shadow_ptr = GetVRAMshadowPtr();
  auto upscale = uprender_scale();

  if (upscale == 1)
  {
    if (shadow_ptr == GetUPRAM())
      return;
 
    // performance failure - vram copy is not needed when running native res.
    DebugAssert(shadow_ptr == GetUPRAM());
  }

  for (u32 yoffs = 0; yoffs < height; yoffs++)
  {
    const u32 row = (y + yoffs) % VRAM_HEIGHT;
    const u16* src = UPRAM_ACCESSOR + (row * VRAM_WIDTH * upscale * upscale);		// skip 2 rows, hence 2nd upscale mult
          u16* dst = shadow_ptr     + (row * VRAM_WIDTH);
    for (u32 xoffs = 0; xoffs < width; xoffs++)
    {
      const u32 col = (x + xoffs) % VRAM_WIDTH;
      dst[col] = src[col * upscale];
    }
  }

}

void GPU_SW_Backend::Sync(bool allow_sleep)
{
  GPUBackend::Sync(allow_sleep);
  ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
}


void GPU_SW_Backend::CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height,
                              GPUBackendCommandParameters params)
{
  // Break up oversized copies. This behavior has not been verified on console.
  if ((src_x + width) > VRAM_WIDTH || (dst_x + width) > VRAM_WIDTH)
  {
    u32 remaining_rows = height;
    u32 current_src_y = src_y;
    u32 current_dst_y = dst_y;
    while (remaining_rows > 0)
    {
      const u32 rows_to_copy =
        std::min<u32>(remaining_rows, std::min<u32>(VRAM_HEIGHT - current_src_y, VRAM_HEIGHT - current_dst_y));

      u32 remaining_columns = width;
      u32 current_src_x = src_x;
      u32 current_dst_x = dst_x;
      while (remaining_columns > 0)
      {
        const u32 columns_to_copy =
          std::min<u32>(remaining_columns, std::min<u32>(VRAM_WIDTH - current_src_x, VRAM_WIDTH - current_dst_x));
        CopyVRAM(current_src_x, current_src_y, current_dst_x, current_dst_y, columns_to_copy, rows_to_copy, params);
        current_src_x = (current_src_x + columns_to_copy) % VRAM_WIDTH;
        current_dst_x = (current_dst_x + columns_to_copy) % VRAM_WIDTH;
        remaining_columns -= columns_to_copy;
      }

      current_src_y = (current_src_y + rows_to_copy) % VRAM_HEIGHT;
      current_dst_y = (current_dst_y + rows_to_copy) % VRAM_HEIGHT;
      remaining_rows -= rows_to_copy;
    }

    return;
  }

  // This doesn't have a fast path, but do we really need one? It's not common.
  const u16 mask_and = params.GetMaskAND();
  const u16 mask_or = params.GetMaskOR();

  // Copy in reverse when src_x < dst_x, this is verified on console.
  if (src_x < dst_x || ((src_x + width - 1) % VRAM_WIDTH) < ((dst_x + width - 1) % VRAM_WIDTH))
  {
    for (u32 row = 0; row < height; row++)
    {
      auto sy = (src_y + row) % VRAM_HEIGHT;
      auto dy = (dst_y + row) % VRAM_HEIGHT;

      for (s32 col = static_cast<s32>(width - 1); col >= 0; col--)
      {
        auto sx = (src_x + static_cast<u32>(col)) % VRAM_WIDTH;
        auto dx = (dst_x + static_cast<u32>(col)) % VRAM_WIDTH;
        auto src_pixel = GetPixelSlow(sx, sy);
        auto dst_pixel = GetPixelSlow(dx, dy);
        if ((dst_pixel & mask_and) == 0)
          SetPixelSlow(dx, dy, src_pixel | mask_or);
      }
    }
  }
  else
  {
    for (u32 row = 0; row < height; row++)
    {
      auto sy = (src_y + row) % VRAM_HEIGHT;
      auto dy = (dst_y + row) % VRAM_HEIGHT;

      for (u32 col = 0; col < width; col++)
      {
        auto sx = (src_x + static_cast<u32>(col)) % VRAM_WIDTH;
        auto dx = (dst_x + static_cast<u32>(col)) % VRAM_WIDTH;
        auto src_pixel = GetPixelSlow(sx, sy);                                            
        auto dst_pixel = GetPixelSlow(dx, dy);
        if ((dst_pixel & mask_and) == 0)
          SetPixelSlow(dx, dy, src_pixel | mask_or);
      }
    }
  }
}

void GPU_SW_Backend::FlushRender() {}

void GPU_SW_Backend::DrawingAreaChanged() {}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

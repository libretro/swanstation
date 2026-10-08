#include "gpu_sw_backend.h"
#include "gpu_sw_backend.h"
#include "host_display.h"
#include "system.h"
#include <algorithm>

GPU_SW_Backend::GPU_SW_Backend() : GPUBackend()
{
  m_vram.fill(0);
  m_vram_ptr = m_vram.data();
}

GPU_SW_Backend::~GPU_SW_Backend() = default;

bool GPU_SW_Backend::Initialize(bool force_thread)
{
  return GPUBackend::Initialize(force_thread);
}

void GPU_SW_Backend::Reset(bool clear_vram)
{
  GPUBackend::Reset(clear_vram);

  if (clear_vram)
    m_vram.fill(0);
}

void GPU_SW_Backend::DrawPolygon(const GPUBackendDrawPolygonCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};
  const bool dithering_enable = rc.IsDitheringEnabled() && cmd->draw_mode.dither_enable;

  const DrawTriangleFunction DrawFunction = GetDrawTriangleFunction(
    rc.shading_enable, rc.texture_enable, rc.raw_texture_enable, rc.transparency_enable, dithering_enable);

  (this->*DrawFunction)(cmd, &cmd->vertices[0], &cmd->vertices[1], &cmd->vertices[2]);
  if (rc.quad_polygon)
    (this->*DrawFunction)(cmd, &cmd->vertices[2], &cmd->vertices[1], &cmd->vertices[3]);
}

void GPU_SW_Backend::DrawRectangle(const GPUBackendDrawRectangleCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};

  const DrawRectangleFunction DrawFunction =
    GetDrawRectangleFunction(rc.texture_enable, rc.raw_texture_enable, rc.transparency_enable);

  (this->*DrawFunction)(cmd);
}

void GPU_SW_Backend::DrawLine(const GPUBackendDrawLineCommand* cmd)
{
  const DrawLineFunction DrawFunction =
    GetDrawLineFunction(cmd->rc.shading_enable, cmd->rc.transparency_enable, cmd->IsDitheringEnabled());

  for (uint16_t i = 1; i < cmd->num_vertices; i++)
    (this->*DrawFunction)(cmd, &cmd->vertices[i - 1], &cmd->vertices[i]);
}

constexpr GPU_SW_Backend::DitherLUT GPU_SW_Backend::ComputeDitherLUT()
{
  DitherLUT lut = {};
  for (uint32_t i = 0; i < DITHER_MATRIX_SIZE; i++)
  {
    for (uint32_t j = 0; j < DITHER_MATRIX_SIZE; j++)
    {
      for (uint32_t value = 0; value < DITHER_LUT_SIZE; value++)
      {
        const int32_t dithered_value = (static_cast<int32_t>(value) + DITHER_MATRIX[i][j]) >> 3;
        lut[i][j][value] = static_cast<uint8_t>((dithered_value < 0) ? 0 : ((dithered_value > 31) ? 31 : dithered_value));
      }
    }
  }
  return lut;
}

static constexpr GPU_SW_Backend::DitherLUT s_dither_lut = GPU_SW_Backend::ComputeDitherLUT();

template<bool texture_enable, bool raw_texture_enable, bool transparency_enable, bool dithering_enable>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::ShadePixel(const GPUBackendDrawCommand* cmd, uint32_t x, uint32_t y, uint8_t color_r,
                                                      uint8_t color_g, uint8_t color_b, uint8_t texcoord_x, uint8_t texcoord_y)
{
  VRAMPixel color;
  if constexpr (texture_enable)
  {
    // Apply texture window
    texcoord_x = (texcoord_x & cmd->window.and_x) | cmd->window.or_x;
    texcoord_y = (texcoord_y & cmd->window.and_y) | cmd->window.or_y;

    VRAMPixel texture_color;
    switch (cmd->draw_mode.texture_mode)
    {
      case GPUTextureMode::Palette4Bit:
      {
        const uint16_t palette_value =
          GetPixel((cmd->draw_mode.GetTexturePageBaseX() + static_cast<uint32_t>(texcoord_x / 4)) % VRAM_WIDTH,
                   (cmd->draw_mode.GetTexturePageBaseY() + static_cast<uint32_t>(texcoord_y)) % VRAM_HEIGHT);
        const uint16_t palette_index = (palette_value >> ((texcoord_x % 4) * 4)) & 0x0Fu;

        texture_color.bits =
          GetPixel((cmd->palette.GetXBase() + static_cast<uint32_t>(palette_index)) % VRAM_WIDTH, cmd->palette.GetYBase());
      }
      break;

      case GPUTextureMode::Palette8Bit:
      {
        const uint16_t palette_value =
          GetPixel((cmd->draw_mode.GetTexturePageBaseX() + static_cast<uint32_t>(texcoord_x / 2)) % VRAM_WIDTH,
                   (cmd->draw_mode.GetTexturePageBaseY() + static_cast<uint32_t>(texcoord_y)) % VRAM_HEIGHT);
        const uint16_t palette_index = (palette_value >> ((texcoord_x % 2) * 8)) & 0xFFu;
        texture_color.bits =
          GetPixel((cmd->palette.GetXBase() + static_cast<uint32_t>(palette_index)) % VRAM_WIDTH, cmd->palette.GetYBase());
      }
      break;

      default:
      {
        texture_color.bits = GetPixel((cmd->draw_mode.GetTexturePageBaseX() + static_cast<uint32_t>(texcoord_x)) % VRAM_WIDTH,
                                      (cmd->draw_mode.GetTexturePageBaseY() + static_cast<uint32_t>(texcoord_y)) % VRAM_HEIGHT);
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
      const uint32_t dither_y = (dithering_enable) ? (y & 3u) : 2u;
      const uint32_t dither_x = (dithering_enable) ? (x & 3u) : 3u;

      color.bits = (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][(uint16_t(texture_color.r) * uint16_t(color_r)) >> 4]) << 0) |
                   (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][(uint16_t(texture_color.g) * uint16_t(color_g)) >> 4]) << 5) |
                   (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][(uint16_t(texture_color.b) * uint16_t(color_b)) >> 4]) << 10) |
                   (texture_color.bits & 0x8000u);
    }
  }
  else
  {
    const uint32_t dither_y = (dithering_enable) ? (y & 3u) : 2u;
    const uint32_t dither_x = (dithering_enable) ? (x & 3u) : 3u;

    // Non-textured transparent polygons don't set bit 15, but are treated as transparent.
    color.bits = (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][color_r]) << 0) |
                 (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][color_g]) << 5) |
                 (static_cast<uint16_t>(s_dither_lut[dither_y][dither_x][color_b]) << 10) | (transparency_enable ? 0x8000u : 0);
  }

  const VRAMPixel bg_color{GetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y))};
  if constexpr (transparency_enable)
  {
    if (color.bits & 0x8000u || !texture_enable)
    {
      // Based on blargg's efficient 15bpp pixel math.
      uint32_t bg_bits = static_cast<uint32_t>(bg_color.bits);
      uint32_t fg_bits = static_cast<uint32_t>(color.bits);
      switch (cmd->draw_mode.transparency_mode)
      {
        case GPUTransparencyMode::HalfBackgroundPlusHalfForeground:
        {
          bg_bits |= 0x8000u;
          color.bits = static_cast<uint16_t>(((fg_bits + bg_bits) - ((fg_bits ^ bg_bits) & 0x0421u)) >> 1);
        }
        break;

        case GPUTransparencyMode::BackgroundPlusForeground:
        {
          bg_bits &= ~0x8000u;

          const uint32_t sum = fg_bits + bg_bits;
          const uint32_t carry = (sum - ((fg_bits ^ bg_bits) & 0x8421u)) & 0x8420u;

          color.bits = static_cast<uint16_t>((sum - carry) | (carry - (carry >> 5)));
        }
        break;

        case GPUTransparencyMode::BackgroundMinusForeground:
        {
          bg_bits |= 0x8000u;
          fg_bits &= ~0x8000u;

          const uint32_t diff = bg_bits - fg_bits + 0x108420u;
          const uint32_t borrow = (diff - ((bg_bits ^ fg_bits) & 0x108420u)) & 0x108420u;

          color.bits = static_cast<uint16_t>((diff - borrow) & (borrow - (borrow >> 5)));
        }
        break;

        case GPUTransparencyMode::BackgroundPlusQuarterForeground:
        {
          bg_bits &= ~0x8000u;
          fg_bits = ((fg_bits >> 2) & 0x1CE7u) | 0x8000u;

          const uint32_t sum = fg_bits + bg_bits;
          const uint32_t carry = (sum - ((fg_bits ^ bg_bits) & 0x8421u)) & 0x8420u;

          color.bits = static_cast<uint16_t>((sum - carry) | (carry - (carry >> 5)));
        }
        break;
      }

      // See above.
      if constexpr (!texture_enable)
        color.bits &= ~0x8000u;
    }
  }

  const uint16_t mask_and = cmd->params.GetMaskAND();
  if ((bg_color.bits & mask_and) != 0)
    return;

  SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), color.bits | cmd->params.GetMaskOR());
}

template<bool texture_enable, bool raw_texture_enable, bool transparency_enable>
void GPU_SW_Backend::DrawRectangle(const GPUBackendDrawRectangleCommand* cmd)
{
  const int32_t origin_x = cmd->x;
  const int32_t origin_y = cmd->y;
  const auto [r, g, b] = UnpackColorRGB24(cmd->color);
  const auto [origin_texcoord_x, origin_texcoord_y] = UnpackTexcoord(cmd->texcoord);

  for (uint32_t offset_y = 0; offset_y < cmd->height; offset_y++)
  {
    const int32_t y = origin_y + static_cast<int32_t>(offset_y);
    if (y < static_cast<int32_t>(m_drawing_area.top) || y > static_cast<int32_t>(m_drawing_area.bottom) ||
        (cmd->params.interlaced_rendering && cmd->params.active_line_lsb == (y & 1)))
    {
      continue;
    }

    const uint8_t texcoord_y = static_cast<uint8_t>(origin_texcoord_y + offset_y);

    for (uint32_t offset_x = 0; offset_x < cmd->width; offset_x++)
    {
      const int32_t x = origin_x + static_cast<int32_t>(offset_x);
      if (x < static_cast<int32_t>(m_drawing_area.left) || x > static_cast<int32_t>(m_drawing_area.right))
        continue;

      const uint8_t texcoord_x = static_cast<uint8_t>(origin_texcoord_x + offset_x);

      ShadePixel<texture_enable, raw_texture_enable, transparency_enable, false>(
        cmd, static_cast<uint32_t>(x), static_cast<uint32_t>(y), r, g, b, texcoord_x, texcoord_y);
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// Polygon and line rasterization ported from Mednafen
//////////////////////////////////////////////////////////////////////////

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING 12

static ALWAYS_INLINE_RELEASE int64_t MakePolyXFP(int32_t x)
{
  return ((uint64_t)x << 32) + ((1ULL << 32) - (1 << 11));
}

static ALWAYS_INLINE_RELEASE int64_t MakePolyXFPStep(int32_t dx, int32_t dy)
{
  int64_t ret;
  int64_t dx_ex = (uint64_t)dx << 32;

  if (dx_ex < 0)
    dx_ex -= dy - 1;

  if (dx_ex > 0)
    dx_ex += dy - 1;

  ret = dx_ex / dy;

  return (ret);
}

static ALWAYS_INLINE_RELEASE int32_t GetPolyXFP_Int(int64_t xfp)
{
  return (xfp >> 32);
}

template<bool shading_enable, bool texture_enable>
bool ALWAYS_INLINE_RELEASE GPU_SW_Backend::CalcIDeltas(i_deltas& idl, const GPUBackendDrawPolygonCommand::Vertex* A,
                                                       const GPUBackendDrawPolygonCommand::Vertex* B,
                                                       const GPUBackendDrawPolygonCommand::Vertex* C)
{
#define CALCIS(x, y) (((B->x - A->x) * (C->y - B->y)) - ((C->x - B->x) * (B->y - A->y)))

  int32_t denom = CALCIS(x, y);

  if (!denom)
    return false;

  if constexpr (shading_enable)
  {
    idl.dr_dx = (uint32_t)(CALCIS(r, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dr_dy = (uint32_t)(CALCIS(x, r) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.dg_dx = (uint32_t)(CALCIS(g, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dg_dy = (uint32_t)(CALCIS(x, g) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.db_dx = (uint32_t)(CALCIS(b, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.db_dy = (uint32_t)(CALCIS(x, b) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  }

  if constexpr (texture_enable)
  {
    idl.du_dx = (uint32_t)(CALCIS(u, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.du_dy = (uint32_t)(CALCIS(x, u) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

    idl.dv_dx = (uint32_t)(CALCIS(v, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
    idl.dv_dy = (uint32_t)(CALCIS(x, v) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  }

  return true;

#undef CALCIS
}

template<bool shading_enable, bool texture_enable>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::AddIDeltas_DX(i_group& ig, const i_deltas& idl, uint32_t count /*= 1*/)
{
  if constexpr (shading_enable)
  {
    ig.r += idl.dr_dx * count;
    ig.g += idl.dg_dx * count;
    ig.b += idl.db_dx * count;
  }

  if constexpr (texture_enable)
  {
    ig.u += idl.du_dx * count;
    ig.v += idl.dv_dx * count;
  }
}

template<bool shading_enable, bool texture_enable>
void ALWAYS_INLINE_RELEASE GPU_SW_Backend::AddIDeltas_DY(i_group& ig, const i_deltas& idl, uint32_t count /*= 1*/)
{
  if constexpr (shading_enable)
  {
    ig.r += idl.dr_dy * count;
    ig.g += idl.dg_dy * count;
    ig.b += idl.db_dy * count;
  }

  if constexpr (texture_enable)
  {
    ig.u += idl.du_dy * count;
    ig.v += idl.dv_dy * count;
  }
}

template<bool shading_enable, bool texture_enable, bool raw_texture_enable, bool transparency_enable,
         bool dithering_enable>
void GPU_SW_Backend::DrawSpan(const GPUBackendDrawPolygonCommand* cmd, int32_t y, int32_t x_start, int32_t x_bound, i_group ig,
                              const i_deltas& idl)
{
  if (cmd->params.interlaced_rendering && cmd->params.active_line_lsb == (y & 1))
    return;

  int32_t x_ig_adjust = x_start;
  int32_t w = x_bound - x_start;
  int32_t x = TruncateGPUVertexPosition(x_start);

  if (x < static_cast<int32_t>(m_drawing_area.left))
  {
    int32_t delta = static_cast<int32_t>(m_drawing_area.left) - x;
    x_ig_adjust += delta;
    x += delta;
    w -= delta;
  }

  if ((x + w) > (static_cast<int32_t>(m_drawing_area.right) + 1))
    w = static_cast<int32_t>(m_drawing_area.right) + 1 - x;

  if (w <= 0)
    return;

  AddIDeltas_DX<shading_enable, texture_enable>(ig, idl, x_ig_adjust);
  AddIDeltas_DY<shading_enable, texture_enable>(ig, idl, y);

  do
  {
    const uint32_t r = ig.r >> (COORD_FBS + COORD_POST_PADDING);
    const uint32_t g = ig.g >> (COORD_FBS + COORD_POST_PADDING);
    const uint32_t b = ig.b >> (COORD_FBS + COORD_POST_PADDING);
    const uint32_t u = ig.u >> (COORD_FBS + COORD_POST_PADDING);
    const uint32_t v = ig.v >> (COORD_FBS + COORD_POST_PADDING);

    /* y is the untruncated span coordinate; the clipped row is y modulo the VRAM height. */
    ShadePixel<texture_enable, raw_texture_enable, transparency_enable, dithering_enable>(
      cmd, static_cast<uint32_t>(x), static_cast<uint32_t>(y) & VRAM_HEIGHT_MASK, static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(u),
      static_cast<uint8_t>(v));

    x++;
    AddIDeltas_DX<shading_enable, texture_enable>(ig, idl);
  } while (--w > 0);
}

template<bool shading_enable, bool texture_enable, bool raw_texture_enable, bool transparency_enable,
         bool dithering_enable>
void GPU_SW_Backend::DrawTriangle(const GPUBackendDrawPolygonCommand* cmd,
                                  const GPUBackendDrawPolygonCommand::Vertex* v0,
                                  const GPUBackendDrawPolygonCommand::Vertex* v1,
                                  const GPUBackendDrawPolygonCommand::Vertex* v2)
{
  uint32_t core_vertex;
  {
    uint32_t cvtemp = 0;

    if (v1->x <= v0->x)
    {
      if (v2->x <= v1->x)
        cvtemp = (1 << 2);
      else
        cvtemp = (1 << 1);
    }
    else if (v2->x < v0->x)
      cvtemp = (1 << 2);
    else
      cvtemp = (1 << 0);

    if (v2->y < v1->y)
    {
      std::swap(v2, v1);
      cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
    }

    if (v1->y < v0->y)
    {
      std::swap(v1, v0);
      cvtemp = ((cvtemp >> 1) & 0x1) | ((cvtemp << 1) & 0x2) | (cvtemp & 0x4);
    }

    if (v2->y < v1->y)
    {
      std::swap(v2, v1);
      cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
    }

    core_vertex = cvtemp >> 1;
  }

  if (v0->y == v2->y)
    return;

  if (static_cast<uint32_t>(std::abs(v2->x - v0->x)) >= MAX_PRIMITIVE_WIDTH ||
      static_cast<uint32_t>(std::abs(v2->x - v1->x)) >= MAX_PRIMITIVE_WIDTH ||
      static_cast<uint32_t>(std::abs(v1->x - v0->x)) >= MAX_PRIMITIVE_WIDTH ||
      static_cast<uint32_t>(v2->y - v0->y) >= MAX_PRIMITIVE_HEIGHT)
  {
    return;
  }

  int64_t base_coord = MakePolyXFP(v0->x);
  int64_t base_step = MakePolyXFPStep((v2->x - v0->x), (v2->y - v0->y));
  int64_t bound_coord_us;
  int64_t bound_coord_ls;
  bool right_facing;

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

  i_deltas idl;
  if (!CalcIDeltas<shading_enable, texture_enable>(idl, v0, v1, v2))
    return;

  const GPUBackendDrawPolygonCommand::Vertex* vertices[3] = {v0, v1, v2};

  i_group ig;
  if constexpr (texture_enable)
  {
    ig.u = (COORD_MF_INT(vertices[core_vertex]->u) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
    ig.v = (COORD_MF_INT(vertices[core_vertex]->v) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
  }

  ig.r = (COORD_MF_INT(vertices[core_vertex]->r) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
  ig.g = (COORD_MF_INT(vertices[core_vertex]->g) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
  ig.b = (COORD_MF_INT(vertices[core_vertex]->b) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;

  AddIDeltas_DX<shading_enable, texture_enable>(ig, idl, -vertices[core_vertex]->x);
  AddIDeltas_DY<shading_enable, texture_enable>(ig, idl, -vertices[core_vertex]->y);

  struct TriangleHalf
  {
    uint64_t x_coord[2];
    uint64_t x_step[2];

    int32_t y_coord;
    int32_t y_bound;

    bool dec_mode;
  } tripart[2];

  uint32_t vo = 0;
  uint32_t vp = 0;
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

  for (uint32_t i = 0; i < 2; i++)
  {
    int32_t yi = tripart[i].y_coord;
    int32_t yb = tripart[i].y_bound;

    uint64_t lc = tripart[i].x_coord[0];
    uint64_t ls = tripart[i].x_step[0];

    uint64_t rc = tripart[i].x_coord[1];
    uint64_t rs = tripart[i].x_step[1];

    if (tripart[i].dec_mode)
    {
      while (yi > yb)
      {
        yi--;
        lc -= ls;
        rc -= rs;

        int32_t y = TruncateGPUVertexPosition(yi);

        if (y < static_cast<int32_t>(m_drawing_area.top))
          break;

        if (y > static_cast<int32_t>(m_drawing_area.bottom))
          continue;

        DrawSpan<shading_enable, texture_enable, raw_texture_enable, transparency_enable, dithering_enable>(
          cmd, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
      }
    }
    else
    {
      while (yi < yb)
      {
        int32_t y = TruncateGPUVertexPosition(yi);

        if (y > static_cast<int32_t>(m_drawing_area.bottom))
          break;

        if (y >= static_cast<int32_t>(m_drawing_area.top))
        {

          DrawSpan<shading_enable, texture_enable, raw_texture_enable, transparency_enable, dithering_enable>(
            cmd, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
        }

        yi++;
        lc += ls;
        rc += rs;
      }
    }
  }
}

constexpr int Line_XY_FractBits = 32;
constexpr int Line_RGB_FractBits = 12;

struct line_fxp_coord
{
  uint64_t x, y;
  uint32_t r, g, b;
};

struct line_fxp_step
{
  int64_t dx_dk, dy_dk;
  int32_t dr_dk, dg_dk, db_dk;
};

static ALWAYS_INLINE_RELEASE int64_t LineDivide(int64_t delta, int32_t dk)
{
  delta = (uint64_t)delta << Line_XY_FractBits;

  if (delta < 0)
    delta -= dk - 1;
  if (delta > 0)
    delta += dk - 1;

  return (delta / dk);
}

template<bool shading_enable, bool transparency_enable, bool dithering_enable>
void GPU_SW_Backend::DrawLine(const GPUBackendDrawLineCommand* cmd, const GPUBackendDrawLineCommand::Vertex* p0,
                              const GPUBackendDrawLineCommand::Vertex* p1)
{
  const int32_t i_dx = std::abs(p1->x - p0->x);
  const int32_t i_dy = std::abs(p1->y - p0->y);
  const int32_t k = (i_dx > i_dy) ? i_dx : i_dy;
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
      step.dr_dk = (int32_t)((uint32_t)(p1->r - p0->r) << Line_RGB_FractBits) / k;
      step.dg_dk = (int32_t)((uint32_t)(p1->g - p0->g) << Line_RGB_FractBits) / k;
      step.db_dk = (int32_t)((uint32_t)(p1->b - p0->b) << Line_RGB_FractBits) / k;
    }
  }

  line_fxp_coord cur_point;
  cur_point.x = ((uint64_t)p0->x << Line_XY_FractBits) | (1ULL << (Line_XY_FractBits - 1));
  cur_point.y = ((uint64_t)p0->y << Line_XY_FractBits) | (1ULL << (Line_XY_FractBits - 1));

  cur_point.x -= 1024;

  if (step.dy_dk < 0)
    cur_point.y -= 1024;

  if constexpr (shading_enable)
  {
    cur_point.r = (p0->r << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
    cur_point.g = (p0->g << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
    cur_point.b = (p0->b << Line_RGB_FractBits) | (1 << (Line_RGB_FractBits - 1));
  }

  for (int32_t i = 0; i <= k; i++)
  {
    // Sign extension is not necessary here for x and y, due to the maximum values that ClipX1 and ClipY1 can contain.
    const int32_t x = (cur_point.x >> Line_XY_FractBits) & 2047;
    const int32_t y = (cur_point.y >> Line_XY_FractBits) & 2047;

    if ((!cmd->params.interlaced_rendering || cmd->params.active_line_lsb != (y & 1)) &&
        x >= static_cast<int32_t>(m_drawing_area.left) && x <= static_cast<int32_t>(m_drawing_area.right) &&
        y >= static_cast<int32_t>(m_drawing_area.top) && y <= static_cast<int32_t>(m_drawing_area.bottom))
    {
      const uint8_t r = shading_enable ? static_cast<uint8_t>(cur_point.r >> Line_RGB_FractBits) : p0->r;
      const uint8_t g = shading_enable ? static_cast<uint8_t>(cur_point.g >> Line_RGB_FractBits) : p0->g;
      const uint8_t b = shading_enable ? static_cast<uint8_t>(cur_point.b >> Line_RGB_FractBits) : p0->b;

      ShadePixel<false, false, transparency_enable, dithering_enable>(cmd, static_cast<uint32_t>(x), static_cast<uint32_t>(y), r,
                                                                      g, b, 0, 0);
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

void GPU_SW_Backend::FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color, GPUBackendCommandParameters params)
{
  const uint16_t color16 = VRAMRGBA8888ToRGBA5551(color);
  if ((x + width) <= VRAM_WIDTH && !params.interlaced_rendering)
  {
    for (uint32_t yoffs = 0; yoffs < height; yoffs++)
    {
      const uint32_t row = (y + yoffs) % VRAM_HEIGHT;
      std::fill_n(&m_vram_ptr[row * VRAM_WIDTH + x], width, color16);
    }
  }
  else if (params.interlaced_rendering)
  {
    // Hardware tests show that fills seem to break on the first two lines when the offset matches the displayed field.
    const uint32_t active_field = params.active_line_lsb;
    for (uint32_t yoffs = 0; yoffs < height; yoffs++)
    {
      const uint32_t row = (y + yoffs) % VRAM_HEIGHT;
      if ((row & uint32_t(1)) == active_field)
        continue;

      uint16_t* row_ptr = &m_vram_ptr[row * VRAM_WIDTH];
      for (uint32_t xoffs = 0; xoffs < width; xoffs++)
      {
        const uint32_t col = (x + xoffs) % VRAM_WIDTH;
        row_ptr[col] = color16;
      }
    }
  }
  else
  {
    for (uint32_t yoffs = 0; yoffs < height; yoffs++)
    {
      const uint32_t row = (y + yoffs) % VRAM_HEIGHT;
      uint16_t* row_ptr = &m_vram_ptr[row * VRAM_WIDTH];
      for (uint32_t xoffs = 0; xoffs < width; xoffs++)
      {
        const uint32_t col = (x + xoffs) % VRAM_WIDTH;
        row_ptr[col] = color16;
      }
    }
  }
}

void GPU_SW_Backend::UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data,
                                GPUBackendCommandParameters params)
{
  // Fast path when the copy is not oversized.
  if ((x + width) <= VRAM_WIDTH && (y + height) <= VRAM_HEIGHT && !params.IsMaskingEnabled())
  {
    const uint16_t* src_ptr = static_cast<const uint16_t*>(data);
    uint16_t* dst_ptr = &m_vram_ptr[y * VRAM_WIDTH + x];
    for (uint32_t yoffs = 0; yoffs < height; yoffs++)
    {
      std::copy_n(src_ptr, width, dst_ptr);
      src_ptr += width;
      dst_ptr += VRAM_WIDTH;
    }
  }
  else
  {
    // Slow path when we need to handle wrap-around.
    const uint16_t* src_ptr = static_cast<const uint16_t*>(data);
    const uint16_t mask_and = params.GetMaskAND();
    const uint16_t mask_or = params.GetMaskOR();

    for (uint32_t row = 0; row < height;)
    {
      uint16_t* dst_row_ptr = &m_vram_ptr[((y + row++) % VRAM_HEIGHT) * VRAM_WIDTH];
      for (uint32_t col = 0; col < width;)
      {
        // TODO: Handle unaligned reads...
        uint16_t* pixel_ptr = &dst_row_ptr[(x + col++) % VRAM_WIDTH];
        if (((*pixel_ptr) & mask_and) == 0)
          *pixel_ptr = *(src_ptr++) | mask_or;
      }
    }
  }
}

void GPU_SW_Backend::CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height,
                              GPUBackendCommandParameters params)
{
  // Break up oversized copies. This behavior has not been verified on console.
  if ((src_x + width) > VRAM_WIDTH || (dst_x + width) > VRAM_WIDTH)
  {
    uint32_t remaining_rows = height;
    uint32_t current_src_y = src_y;
    uint32_t current_dst_y = dst_y;
    while (remaining_rows > 0)
    {
      const uint32_t rows_to_copy =
        std::min<uint32_t>(remaining_rows, std::min<uint32_t>(VRAM_HEIGHT - current_src_y, VRAM_HEIGHT - current_dst_y));

      uint32_t remaining_columns = width;
      uint32_t current_src_x = src_x;
      uint32_t current_dst_x = dst_x;
      while (remaining_columns > 0)
      {
        const uint32_t columns_to_copy =
          std::min<uint32_t>(remaining_columns, std::min<uint32_t>(VRAM_WIDTH - current_src_x, VRAM_WIDTH - current_dst_x));
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
  const uint16_t mask_and = params.GetMaskAND();
  const uint16_t mask_or = params.GetMaskOR();

  // Copy in reverse when src_x < dst_x, this is verified on console.
  if (src_x < dst_x || ((src_x + width - 1) % VRAM_WIDTH) < ((dst_x + width - 1) % VRAM_WIDTH))
  {
    for (uint32_t row = 0; row < height; row++)
    {
      const uint16_t* src_row_ptr = &m_vram_ptr[((src_y + row) % VRAM_HEIGHT) * VRAM_WIDTH];
      uint16_t* dst_row_ptr = &m_vram_ptr[((dst_y + row) % VRAM_HEIGHT) * VRAM_WIDTH];

      for (int32_t col = static_cast<int32_t>(width - 1); col >= 0; col--)
      {
        const uint16_t src_pixel = src_row_ptr[(src_x + static_cast<uint32_t>(col)) % VRAM_WIDTH];
        uint16_t* dst_pixel_ptr = &dst_row_ptr[(dst_x + static_cast<uint32_t>(col)) % VRAM_WIDTH];
        if ((*dst_pixel_ptr & mask_and) == 0)
          *dst_pixel_ptr = src_pixel | mask_or;
      }
    }
  }
  else
  {
    for (uint32_t row = 0; row < height; row++)
    {
      const uint16_t* src_row_ptr = &m_vram_ptr[((src_y + row) % VRAM_HEIGHT) * VRAM_WIDTH];
      uint16_t* dst_row_ptr = &m_vram_ptr[((dst_y + row) % VRAM_HEIGHT) * VRAM_WIDTH];

      for (uint32_t col = 0; col < width; col++)
      {
        const uint16_t src_pixel = src_row_ptr[(src_x + col) % VRAM_WIDTH];
        uint16_t* dst_pixel_ptr = &dst_row_ptr[(dst_x + col) % VRAM_WIDTH];
        if ((*dst_pixel_ptr & mask_and) == 0)
          *dst_pixel_ptr = src_pixel | mask_or;
      }
    }
  }
}

void GPU_SW_Backend::FlushRender() {}

GPU_SW_Backend::DrawLineFunction GPU_SW_Backend::GetDrawLineFunction(bool shading_enable, bool transparency_enable,
                                                                     bool dithering_enable)
{
  static constexpr DrawLineFunction funcs[2][2][2] = {
    {{&GPU_SW_Backend::DrawLine<false, false, false>, &GPU_SW_Backend::DrawLine<false, false, true>},
     {&GPU_SW_Backend::DrawLine<false, true, false>, &GPU_SW_Backend::DrawLine<false, true, true>}},
    {{&GPU_SW_Backend::DrawLine<true, false, false>, &GPU_SW_Backend::DrawLine<true, false, true>},
     {&GPU_SW_Backend::DrawLine<true, true, false>, &GPU_SW_Backend::DrawLine<true, true, true>}}};

  return funcs[uint8_t(shading_enable)][uint8_t(transparency_enable)][uint8_t(dithering_enable)];
}

GPU_SW_Backend::DrawRectangleFunction
GPU_SW_Backend::GetDrawRectangleFunction(bool texture_enable, bool raw_texture_enable, bool transparency_enable)
{
  static constexpr DrawRectangleFunction funcs[2][2][2] = {
    {{&GPU_SW_Backend::DrawRectangle<false, false, false>, &GPU_SW_Backend::DrawRectangle<false, false, true>},
     {&GPU_SW_Backend::DrawRectangle<false, false, false>, &GPU_SW_Backend::DrawRectangle<false, false, true>}},
    {{&GPU_SW_Backend::DrawRectangle<true, false, false>, &GPU_SW_Backend::DrawRectangle<true, false, true>},
     {&GPU_SW_Backend::DrawRectangle<true, true, false>, &GPU_SW_Backend::DrawRectangle<true, true, true>}}};

  return funcs[uint8_t(texture_enable)][uint8_t(raw_texture_enable)][uint8_t(transparency_enable)];
}

GPU_SW_Backend::DrawTriangleFunction GPU_SW_Backend::GetDrawTriangleFunction(bool shading_enable, bool texture_enable,
                                                                             bool raw_texture_enable,
                                                                             bool transparency_enable,
                                                                             bool dithering_enable)
{
  static constexpr DrawTriangleFunction funcs[2][2][2][2][2] = {
    {{{{&GPU_SW_Backend::DrawTriangle<false, false, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<false, false, false, false, true>},
       {&GPU_SW_Backend::DrawTriangle<false, false, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<false, false, false, true, true>}},
      {{&GPU_SW_Backend::DrawTriangle<false, false, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<false, false, false, false, false>},
       {&GPU_SW_Backend::DrawTriangle<false, false, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<false, false, false, true, false>}}},
     {{{&GPU_SW_Backend::DrawTriangle<false, true, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<false, true, false, false, true>},
       {&GPU_SW_Backend::DrawTriangle<false, true, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<false, true, false, true, true>}},
      {{&GPU_SW_Backend::DrawTriangle<false, true, true, false, false>,
        &GPU_SW_Backend::DrawTriangle<false, true, true, false, false>},
       {&GPU_SW_Backend::DrawTriangle<false, true, true, true, false>,
        &GPU_SW_Backend::DrawTriangle<false, true, true, true, false>}}}},
    {{{{&GPU_SW_Backend::DrawTriangle<true, false, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<true, false, false, false, true>},
       {&GPU_SW_Backend::DrawTriangle<true, false, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<true, false, false, true, true>}},
      {{&GPU_SW_Backend::DrawTriangle<true, false, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<true, false, false, false, false>},
       {&GPU_SW_Backend::DrawTriangle<true, false, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<true, false, false, true, false>}}},
     {{{&GPU_SW_Backend::DrawTriangle<true, true, false, false, false>,
        &GPU_SW_Backend::DrawTriangle<true, true, false, false, true>},
       {&GPU_SW_Backend::DrawTriangle<true, true, false, true, false>,
        &GPU_SW_Backend::DrawTriangle<true, true, false, true, true>}},
      {{&GPU_SW_Backend::DrawTriangle<true, true, true, false, false>,
        &GPU_SW_Backend::DrawTriangle<true, true, true, false, false>},
       {&GPU_SW_Backend::DrawTriangle<true, true, true, true, false>,
        &GPU_SW_Backend::DrawTriangle<true, true, true, true, false>}}}}};

  return funcs[uint8_t(shading_enable)][uint8_t(texture_enable)][uint8_t(raw_texture_enable)][uint8_t(transparency_enable)]
              [uint8_t(dithering_enable)];
}

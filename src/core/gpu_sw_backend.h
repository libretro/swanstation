#pragma once
#include "gpu_backend.h"
#include <array>
#include <memory>
#include <vector>

#define SW_GPU_VRAM_BOUNDS_CHECK  0
#define USE_FLOAT_STEP 0
#define USE_INT_STEP   1

#if USE_FLOAT_STEP
using AddDeltasScalar_t = float;
#endif

#if USE_INT_STEP
using AddDeltasScalar_t = int;
#endif

#define UPRAM_ACCESSOR m_upram_ptr

class GPU_SW_Backend final : public GPUBackend
{
protected:
  // TODO : rename this to TTrigShader_*
  static constexpr u32 TShaderParam_ShadingEnable        = (1<<0);
  static constexpr u32 TShaderParam_TextureEnable        = (1<<1);
  static constexpr u32 TShaderParam_RawTextureEnable     = (1<<2);
  static constexpr u32 TShaderParam_TransparencyEnable   = (1<<3);
  static constexpr u32 TShaderParam_DitheringEnable      = (1<<4);
  static constexpr u32 TShaderParam_MaskAndEnable        = (1<<5);
  static constexpr u32 TShaderParam_MaskOrEnable         = (1<<6);
  static constexpr u32 TShaderParams_MAX                 = (1<<7);

// ideal layout for rectangles ...
  static constexpr u32 TRectShader_TransparencyEnable   = (1<<0);
  static constexpr u32 TRectShader_TextureEnable        = (1<<1);
  static constexpr u32 TRectShader_RawTextureEnable     = (1<<2);
  static constexpr u32 TRectShader_MaskAndEnable        = (1<<3);
  static constexpr u32 TRectShader_MaskOrEnable         = (1<<4);
  static constexpr u32 TRectShader_MAX                  = (1<<5);

// ideal layout for lines ...
  static constexpr u32 TLineShader_TransparencyEnable   = (1<<0);
  static constexpr u32 TLineShader_ShadingEnable        = (1<<1);
  static constexpr u32 TLineShader_DitheringEnable      = (1<<2);
  static constexpr u32 TLineShader_MaskAndEnable        = (1<<3);
  static constexpr u32 TLineShader_MaskOrEnable         = (1<<4);
  static constexpr u32 TLineShader_MAX                  = (1<<5);

public:
  GPU_SW_Backend();
  ~GPU_SW_Backend() override;

  bool Initialize(bool force_thread) override;
  void SetUprenderScale(int scale);
  void Reset(bool clear_vram) override;
  void UpdateSettings() override;

  ALWAYS_INLINE u16* GetUPRAM() const { return m_upram_ptr; }
  ALWAYS_INLINE u16* GetVRAMshadowPtr() override { return m_vram.data(); }

  ALWAYS_INLINE int uprender_scale() {
    return (1 << m_uprender_shift);
  }


  // jstine - uprender TODO - rename this to texel, since it's no longer pixel logic.
  // and double check all referenced uses.

  
  template <int RESOLUTION_SHIFT>
  ALWAYS_INLINE u16 GetPixel(const int x, const int y) const
  {
    auto upshift = RESOLUTION_SHIFT;
    auto vram_uprender_size_x = VRAM_WIDTH << upshift;
    return UPRAM_ACCESSOR[((y * vram_uprender_size_x) << upshift) + (x << upshift)];
  }

  ALWAYS_INLINE u16 GetPixelSlow(const int x, const int y) const
  {
    auto upshift = m_uprender_shift;
    auto vram_uprender_size_x = VRAM_WIDTH << upshift;
    return UPRAM_ACCESSOR[((y * vram_uprender_size_x) << upshift) + (x << upshift)];
  }

  ALWAYS_INLINE void _impl_SetPixel(const int x, const int y, const u16 value, int upshift)
  {
    auto upscale = (1<<upshift);
    auto vram_uprender_size_x = VRAM_WIDTH * upscale;

    if (upshift == 0)
      UPRAM_ACCESSOR[vram_uprender_size_x * y + x] = value;
    else
    {
      /* Duplicate the pixel as many times as necessary (nearest neighbour upscaling) */
      for (int dy = 0; dy < upscale; dy++)
      {
        for (int dx = 0; dx < upscale; dx++)
        {
          int y_up = (y * upscale) + dy;
          int x_up = (x * upscale) + dx;
          UPRAM_ACCESSOR[(y_up * vram_uprender_size_x) + x_up] = value;
        }
      }
    }
  }

  template <int RESOLUTION_SHIFT>
  ALWAYS_INLINE void SetPixel(const int x, const int y, const u16 value)
  {
    _impl_SetPixel(x, y, value, RESOLUTION_SHIFT);
  }

  ALWAYS_INLINE void SetPixelSlow(const int x, const int y, const u16 value)
  {
    _impl_SetPixel(x, y, value, m_uprender_shift);
  }

  // this is actually (31 * 255) >> 4) == 494, but to simplify addressing we use the next power of two (512)
  static constexpr u32 DITHER_LUT_SIZE = 512;
  using DitherLUT = std::array<std::array<std::array<u8, 512>, DITHER_MATRIX_SIZE>, DITHER_MATRIX_SIZE>;
  static constexpr DitherLUT ComputeDitherLUT();

  void Sync(bool allow_sleep) override;

protected:
  union VRAMPixel
  {
    u16 bits;

    BitField<u16, u8, 0, 5> r;
    BitField<u16, u8, 5, 5> g;
    BitField<u16, u8, 10, 5> b;
    BitField<u16, bool, 15, 1> c;

    void Set(u8 r_, u8 g_, u8 b_, bool c_ = false)
    {
      bits = (ZeroExtend16(r_)) | (ZeroExtend16(g_) << 5) | (ZeroExtend16(b_) << 10) | (static_cast<u16>(c_) << 15);
    }

    static VRAMPixel Create(u8 r_, u8 g_, u8 b_, bool c_ = false)
    {
      VRAMPixel result;
      result.Set(r_, g_, b_, c_);
      return result;
    }

    void ClampAndSet(u8 r_, u8 g_, u8 b_, bool c_ = false)
    {
      Set(std::min<u8>(r_, 0x1F), std::min<u8>(g_, 0x1F), std::min<u8>(b_, 0x1F), c_);
    }

    void SetRGB24(u32 rgb24, bool c_ = false)
    {
      bits = Truncate16(((rgb24 >> 3) & 0x1F) | (((rgb24 >> 11) & 0x1F) << 5) | (((rgb24 >> 19) & 0x1F) << 10)) |
             (static_cast<u16>(c_) << 15);
    }

    void SetRGB24(u8 r8, u8 g8, u8 b8, bool c_ = false)
    {
      bits = (ZeroExtend16(r8 >> 3)) | (ZeroExtend16(g8 >> 3) << 5) | (ZeroExtend16(b8 >> 3) << 10) |
             (static_cast<u16>(c_) << 15);
    }

    void SetRGB24Dithered(u32 x, u32 y, u8 r8, u8 g8, u8 b8, bool c_ = false)
    {
      const s32 offset = DITHER_MATRIX[y & 3][x & 3];
      r8 = static_cast<u8>(std::clamp<s32>(static_cast<s32>(ZeroExtend32(r8)) + offset, 0, 255));
      g8 = static_cast<u8>(std::clamp<s32>(static_cast<s32>(ZeroExtend32(g8)) + offset, 0, 255));
      b8 = static_cast<u8>(std::clamp<s32>(static_cast<s32>(ZeroExtend32(b8)) + offset, 0, 255));
      SetRGB24(r8, g8, b8, c_);
    }

    u32 ToRGB24() const
    {
      const u32 r_ = ZeroExtend32(r.GetValue());
      const u32 g_ = ZeroExtend32(g.GetValue());
      const u32 b_ = ZeroExtend32(b.GetValue());

      return ((r_ << 3) | (r_ & 7)) | (((g_ << 3) | (g_ & 7)) << 8) | (((b_ << 3) | (b_ & 7)) << 16);
    }
  };

  static constexpr std::tuple<u8, u8> UnpackTexcoord(u16 texcoord)
  {
    return std::make_tuple(static_cast<u8>(texcoord), static_cast<u8>(texcoord >> 8));
  }

  static constexpr std::tuple<u8, u8, u8> UnpackColorRGB24(u32 rgb24)
  {
    return std::make_tuple(static_cast<u8>(rgb24), static_cast<u8>(rgb24 >> 8), static_cast<u8>(rgb24 >> 16));
  }

public:
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color, GPUBackendCommandParameters params) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, GPUBackendCommandParameters params) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height,
                GPUBackendCommandParameters params) override;

protected:
  void DrawPolygon(const GPUBackendDrawPolygonCommand* cmd) override;
  void DrawLine(const GPUBackendDrawLineCommand* cmd) override;
  void DrawRectangle(const GPUBackendDrawRectangleCommand* cmd) override;
  void FlushRender() override;
  void DrawingAreaChanged() override;

  //////////////////////////////////////////////////////////////////////////
  // Rasterization
  //////////////////////////////////////////////////////////////////////////
  template<u32 TShaderParams, int RESOLUTION_SHIFT>
  void ShadePixel(const GPUBackendDrawCommand* cmd, s32 x, s32 y, u8 color_r, u8 color_g, u8 color_b, u8 texcoord_x,
                  u8 texcoord_y);


  VRAMPixel PlotPixelBlend(GPUTransparencyMode blendMode, VRAMPixel bg_pix, VRAMPixel fore_pix);

  //////////////////////////////////////////////////////////////////////////
  // Polygon and line rasterization ported from Mednafen
  //////////////////////////////////////////////////////////////////////////
  struct i_deltas
  {
    u32 du_dx, dv_dx;
    u32 dr_dx, dg_dx, db_dx;

    u32 du_dy, dv_dy;
    u32 dr_dy, dg_dy, db_dy;
  };

  struct i_group
  {
    u32 u, v;
    u32 r, g, b;
  };

  template<bool shading_enable, bool texture_enable>
  bool CalcIDeltas(i_deltas& idl, const GPUBackendDrawPolygonCommand::Vertex* A,
                   const GPUBackendDrawPolygonCommand::Vertex* B, const GPUBackendDrawPolygonCommand::Vertex* C);

  template<bool shading_enable, bool texture_enable, typename I_GROUP>
  void AddIDeltas_DX(I_GROUP& ig, const i_deltas& idl, AddDeltasScalar_t count = 1);

  template<bool shading_enable, bool texture_enable, typename I_GROUP>
  void AddIDeltas_DY(I_GROUP& ig, const i_deltas& idl, AddDeltasScalar_t count = 1);

  template<u32 TShaderParam, int RESOLUTION_SHIFT=0>
  void DrawSpan(const GPUBackendDrawPolygonCommand* cmd, s32 y, s32 x_start, s32 x_bound, i_group ig,
                const i_deltas& idl);


  using DrawTriangleFunction = void (GPU_SW_Backend::*)(const GPUBackendDrawPolygonCommand* cmd,
                                                        const GPUBackendDrawPolygonCommand::Vertex* v0,
                                                        const GPUBackendDrawPolygonCommand::Vertex* v1,
                                                        const GPUBackendDrawPolygonCommand::Vertex* v2);

  using DrawLineFunction = void (GPU_SW_Backend::*)(const GPUBackendDrawLineCommand* cmd,
                                                    const GPUBackendDrawLineCommand::Vertex* p0,
                                                    const GPUBackendDrawLineCommand::Vertex* p1);
  using DrawRectangleFunction = void (GPU_SW_Backend::*)(const GPUBackendDrawRectangleCommand* cmd);

  template<u32 TShaderParam, int RESOLUTION_SHIFT=0>
  void DrawTriangle(const GPUBackendDrawPolygonCommand* cmd,
                    const GPUBackendDrawPolygonCommand::Vertex* v0,
                    const GPUBackendDrawPolygonCommand::Vertex* v1,
                    const GPUBackendDrawPolygonCommand::Vertex* v2);

  template<u32 TLineParams, int RESOLUTION_SHIFT>
  void DrawLine(const GPUBackendDrawLineCommand* cmd,
                const GPUBackendDrawLineCommand::Vertex* p0,
                const GPUBackendDrawLineCommand::Vertex* p1);

  template<u32 TRectParams, int RESOLUTION_SHIFT>
  void DrawRectangle(const GPUBackendDrawRectangleCommand* cmd);


  DrawTriangleFunction  GetDrawTriangleFunction   (u32 trigShaderParams, int upshift);
  DrawRectangleFunction GetDrawRectangleFunction  (u32 rectShaderParams, int upshift);
  DrawLineFunction      GetDrawLineFunction       (u32 lineShaderParams, int upshift);

  int m_uprender_shift = 0;   // 0 = native, 1 = 2x, 2 = 4x, etc.

  std::array<u16, VRAM_WIDTH * VRAM_HEIGHT> m_vram;
  u16* m_upram = nullptr;
  u16* m_upram_ptr = nullptr;
};

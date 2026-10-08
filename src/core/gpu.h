#pragma once
#include "common/bitfield.h"
#include "common/fifo_queue.h"
#include "common/rectangle.h"
#include "gpu_types.h"
#include "timers.h"
#include "types.h"
#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <vector>

class StateWrapper;

class HostDisplay;
class HostDisplayTexture;

class TimingEvent;
class Timers;

class GPU
{
public:
  enum class BlitterState : uint8_t
  {
    Idle,
    ReadingVRAM,
    WritingVRAM,
    DrawingPolyLine
  };

  enum class DMADirection : uint32_t
  {
    Off = 0,
    FIFO = 1,
    CPUtoGP0 = 2,
    GPUREADtoCPU = 3
  };

  static constexpr uint32_t MAX_FIFO_SIZE = 4096, DOT_TIMER_INDEX = 0, HBLANK_TIMER_INDEX = 1;

  /* Longer poly-lines are drawn in pieces, so the vertex buffer stays bounded. */
  static constexpr uint32_t MAX_POLYLINE_VERTICES = 1024;

  /* DMA time one command batch may take before it counts as GPU time; see ExecuteCommands(). */
  static constexpr TickCount MAX_DMA_TICKS_PER_BATCH = 65536;

  static constexpr uint16_t NTSC_TICKS_PER_LINE = 3413, NTSC_HSYNC_TICKS = 200, NTSC_TOTAL_LINES = 263,
                       PAL_TICKS_PER_LINE = 3406,
                       PAL_HSYNC_TICKS = 200, // actually one more on odd lines
    PAL_TOTAL_LINES = 314;

  static constexpr uint16_t NTSC_HORIZONTAL_ACTIVE_START = 488, NTSC_HORIZONTAL_ACTIVE_END = 3288,
                       NTSC_VERTICAL_ACTIVE_START = 16, NTSC_VERTICAL_ACTIVE_END = 256,
                       PAL_HORIZONTAL_ACTIVE_START = 487, PAL_HORIZONTAL_ACTIVE_END = 3282,
                       PAL_VERTICAL_ACTIVE_START = 20, PAL_VERTICAL_ACTIVE_END = 308;

  // Base class constructor.
  GPU();
  virtual ~GPU();

  virtual bool Initialize(HostDisplay* host_display);
  virtual void Reset(bool clear_vram);
  virtual bool DoState(StateWrapper& sw, HostDisplayTexture** save_to_texture, bool update_display);

  // Graphics API state reset/restore - call when drawing the UI etc.
  virtual void ResetGraphicsAPIState();
  virtual void RestoreGraphicsAPIState();

  void CPUClockChanged();

  // MMIO access
  uint32_t ReadRegister(uint32_t offset);
  void WriteRegister(uint32_t offset, uint32_t value);

  // DMA access
  void DMARead(uint32_t* words, uint32_t word_count);

  ALWAYS_INLINE bool BeginDMAWrite() const { return (m_GPUSTAT.dma_direction == DMADirection::CPUtoGP0); }
  ALWAYS_INLINE void DMAWrite(uint32_t address, uint32_t value)
  {
    /* Only a block or manual transfer far larger than any game uses fills the FIFO; past that the words are
     * dropped, as pushing would wrap over unread ones. */
    if (!m_fifo.IsFull())
      m_fifo.Push((static_cast<uint64_t>(address) << 32) | static_cast<uint64_t>(value));
  }
  void EndDMAWrite();

  /// Returns true if no data is being sent from VRAM to the DAC or that no portion of VRAM would be visible on screen.
  ALWAYS_INLINE bool IsDisplayDisabled() const
  {
    return m_GPUSTAT.display_disable || m_crtc_state.display_vram_width == 0 || m_crtc_state.display_vram_height == 0;
  }

  /// Returns true if scanout should be interlaced.
  ALWAYS_INLINE bool IsInterlacedDisplayEnabled() const
  {
    return (!m_force_progressive_scan) && m_GPUSTAT.vertical_interlace;
  }

  /// Returns true if interlaced rendering is enabled and force progressive scan is disabled.
  ALWAYS_INLINE bool IsInterlacedRenderingEnabled() const
  {
    return (!m_force_progressive_scan) && m_GPUSTAT.SkipDrawingToActiveField();
  }

  /// Returns the number of pending GPU ticks.
  TickCount GetPendingCRTCTicks() const;
  TickCount GetPendingCommandTicks() const;

  /// Returns true if enough ticks have passed for the raster to be on the next line.
  bool IsCRTCScanlinePending() const;

  /// Returns true if a raster scanline or command execution is pending.
  bool IsCommandCompletionPending() const;

  /// Synchronizes the CRTC, updating the hblank timer.
  void SynchronizeCRTC();

  /// Recompile shaders/recreate framebuffers when needed.
  virtual void UpdateSettings();

  float ComputeVerticalFrequency() const;
  float GetDisplayAspectRatio() const;

  // gpu_hw_d3d11.cpp
  static std::unique_ptr<GPU> CreateHardwareD3D11Renderer();

  // gpu_hw_d3d12.cpp
  static std::unique_ptr<GPU> CreateHardwareD3D12Renderer();

  // gpu_hw_opengl.cpp
  static std::unique_ptr<GPU> CreateHardwareOpenGLRenderer();

  // gpu_hw_vulkan.cpp
  static std::unique_ptr<GPU> CreateHardwareVulkanRenderer();

  // gpu_sw.cpp
  static std::unique_ptr<GPU> CreateSoftwareRenderer();

  // Converts window coordinates into horizontal ticks and scanlines. Returns false if out of range. Used for lightguns.
  bool ConvertScreenCoordinatesToBeamTicksAndLines(int32_t window_x, int32_t window_y, float x_scale, float y_scale,
                                                   uint32_t* out_tick, uint32_t* out_line) const;

  // Returns the video clock frequency.
  TickCount GetCRTCFrequency() const;

protected:
  TickCount CRTCTicksToSystemTicks(TickCount crtc_ticks, TickCount fractional_ticks) const;
  TickCount SystemTicksToCRTCTicks(TickCount sysclk_ticks, TickCount* fractional_ticks) const;

  // The GPU internally appears to run at 2x the system clock.
  ALWAYS_INLINE static constexpr TickCount GPUTicksToSystemTicks(TickCount gpu_ticks)
  {
    return std::max<TickCount>((gpu_ticks + 1) >> 1, 1);
  }
  ALWAYS_INLINE static constexpr TickCount SystemTicksToGPUTicks(TickCount sysclk_ticks) { return sysclk_ticks << 1; }

  static constexpr std::tuple<uint8_t, uint8_t> UnpackTexcoord(uint16_t texcoord)
  {
    return std::make_tuple(static_cast<uint8_t>(texcoord), static_cast<uint8_t>(texcoord >> 8));
  }

  void SoftReset();

  // Sets dots per scanline
  void UpdateCRTCConfig();
  void UpdateCRTCDisplayParameters();

  // Update ticks for this execution slice
  void UpdateCRTCTickEvent();
  void UpdateCommandTickEvent();

  // Updates dynamic bits in GPUSTAT (ready to send VRAM/ready to receive DMA)
  void UpdateDMARequest();
  void UpdateGPUIdle();

  // Ticks for hblank/vblank.
  void CRTCTickEvent(TickCount ticks);
  void CommandTickEvent(TickCount ticks);

  /// Returns 0 if the currently-displayed field is on odd lines (1,3,5,...) or 1 if even (2,4,6,...).
  ALWAYS_INLINE uint32_t GetInterlacedDisplayField() const { return static_cast<uint32_t>(m_crtc_state.interlaced_field); }

  /// Returns 0 if the currently-displayed field is on an even line in VRAM, otherwise 1.
  ALWAYS_INLINE uint32_t GetActiveLineLSB() const { return static_cast<uint32_t>(m_crtc_state.active_line_lsb); }

  /// Sets/decodes GP0(E1h) (set draw mode).
  void SetDrawMode(uint16_t bits);

  /// Sets/decodes polygon/rectangle texture palette value.
  void SetTexturePalette(uint16_t bits);

  /// Sets/decodes texture window bits.
  void SetTextureWindow(uint32_t value);

  uint32_t ReadGPUREAD();
  void FinishVRAMWrite();

  /// Returns the number of vertices in the buffered poly-line.
  ALWAYS_INLINE uint32_t GetPolyLineVertexCount() const
  {
    return (static_cast<uint32_t>(m_blit_buffer.size()) + static_cast<uint32_t>(m_render_command.shading_enable)) >>
           static_cast<uint8_t>(m_render_command.shading_enable);
  }

  /// Returns true if the drawing area is valid (i.e. left <= right, top <= bottom).
  ALWAYS_INLINE bool IsDrawingAreaIsValid() const { return m_drawing_area.Valid(); }

  /// Clamps the specified coordinates to the drawing area.
  ALWAYS_INLINE void ClampCoordinatesToDrawingArea(int32_t* x, int32_t* y)
  {
    const int32_t x_value = *x;
    if (x_value < static_cast<int32_t>(m_drawing_area.left))
      *x = m_drawing_area.left;
    else if (x_value >= static_cast<int32_t>(m_drawing_area.right))
      *x = m_drawing_area.right - 1;

    const int32_t y_value = *y;
    if (y_value < static_cast<int32_t>(m_drawing_area.top))
      *y = m_drawing_area.top;
    else if (y_value >= static_cast<int32_t>(m_drawing_area.bottom))
      *y = m_drawing_area.bottom - 1;
  }

  void AddCommandTicks(TickCount ticks);

  void WriteGP1(uint32_t value);
  void EndCommand();
  void ExecuteCommands();

  // Rendering in the backend
  virtual void ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
  virtual void FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
  virtual void UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask);
  virtual void CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);
  virtual void DispatchRenderCommand();
  virtual void FlushRender();
  virtual void ClearDisplay();
  virtual void UpdateDisplay();

  ALWAYS_INLINE void AddDrawTriangleTicks(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, bool shaded, bool textured,
                                          bool semitransparent)
  {
    // This will not produce the correct results for triangles which are partially outside the clip area.
    // However, usually it'll undershoot not overshoot. If we wanted to make this more accurate, we'd need to intersect
    // the edges with the clip rectangle.
    ClampCoordinatesToDrawingArea(&x1, &y1);
    ClampCoordinatesToDrawingArea(&x2, &y2);
    ClampCoordinatesToDrawingArea(&x3, &y3);

    TickCount pixels = std::abs((x1 * y2 + x2 * y3 + x3 * y1 - x1 * y3 - x2 * y1 - x3 * y2) / 2);
    if (textured)
      pixels += pixels;
    if (semitransparent || m_GPUSTAT.check_mask_before_draw)
      pixels += (pixels + 1) / 2;
    if (m_GPUSTAT.SkipDrawingToActiveField())
      pixels /= 2;

    AddCommandTicks(pixels);
  }
  ALWAYS_INLINE void AddDrawRectangleTicks(uint32_t width, uint32_t height, bool textured, bool semitransparent)
  {
    uint32_t ticks_per_row = width;
    if (textured)
      ticks_per_row += width;
    if (semitransparent || m_GPUSTAT.check_mask_before_draw)
      ticks_per_row += (width + 1u) / 2u;
    if (m_GPUSTAT.SkipDrawingToActiveField())
      height = std::max<uint32_t>(height / 2, 1u);

    AddCommandTicks(ticks_per_row * height);
  }
  ALWAYS_INLINE void AddDrawLineTicks(uint32_t width, uint32_t height, bool shaded)
  {
    if (m_GPUSTAT.SkipDrawingToActiveField())
      height = std::max<uint32_t>(height / 2, 1u);

    AddCommandTicks(std::max(width, height));
  }

  HostDisplay* m_host_display = nullptr;

  std::unique_ptr<TimingEvent> m_crtc_tick_event;
  std::unique_ptr<TimingEvent> m_command_tick_event;

  // Pointer to VRAM, used for reads/writes. In the hardware backends, this is the shadow buffer.
  uint16_t* m_vram_ptr = nullptr;

  union GPUSTAT
  {
    uint32_t bits;
    BitField<uint32_t, uint8_t, 0, 4> texture_page_x_base;
    BitField<uint32_t, uint8_t, 4, 1> texture_page_y_base;
    BitField<uint32_t, GPUTransparencyMode, 5, 2> semi_transparency_mode;
    BitField<uint32_t, GPUTextureMode, 7, 2> texture_color_mode;
    BitField<uint32_t, bool, 9, 1> dither_enable;
    BitField<uint32_t, bool, 10, 1> draw_to_displayed_field;
    BitField<uint32_t, bool, 11, 1> set_mask_while_drawing;
    BitField<uint32_t, bool, 12, 1> check_mask_before_draw;
    BitField<uint32_t, uint8_t, 13, 1> interlaced_field;
    BitField<uint32_t, bool, 14, 1> reverse_flag;
    BitField<uint32_t, bool, 15, 1> texture_disable;
    BitField<uint32_t, uint8_t, 16, 1> horizontal_resolution_2;
    BitField<uint32_t, uint8_t, 17, 2> horizontal_resolution_1;
    BitField<uint32_t, bool, 19, 1> vertical_resolution;
    BitField<uint32_t, bool, 20, 1> pal_mode;
    BitField<uint32_t, bool, 21, 1> display_area_color_depth_24;
    BitField<uint32_t, bool, 22, 1> vertical_interlace;
    BitField<uint32_t, bool, 23, 1> display_disable;
    BitField<uint32_t, bool, 24, 1> interrupt_request;
    BitField<uint32_t, bool, 25, 1> dma_data_request;
    BitField<uint32_t, bool, 26, 1> gpu_idle;
    BitField<uint32_t, bool, 27, 1> ready_to_send_vram;
    BitField<uint32_t, bool, 28, 1> ready_to_recieve_dma;
    BitField<uint32_t, DMADirection, 29, 2> dma_direction;
    BitField<uint32_t, bool, 31, 1> display_line_lsb;

    ALWAYS_INLINE bool IsMaskingEnabled() const
    {
      static constexpr uint32_t MASK = ((1 << 11) | (1 << 12));
      return ((bits & MASK) != 0);
    }
    ALWAYS_INLINE bool SkipDrawingToActiveField() const
    {
      static constexpr uint32_t MASK = (1 << 19) | (1 << 22) | (1 << 10);
      static constexpr uint32_t ACTIVE = (1 << 19) | (1 << 22);
      return ((bits & MASK) == ACTIVE);
    }
    ALWAYS_INLINE bool InInterleaved480iMode() const
    {
      static constexpr uint32_t ACTIVE = (1 << 19) | (1 << 22);
      return ((bits & ACTIVE) == ACTIVE);
    }

    // During transfer/render operations, if ((dst_pixel & mask_and) == 0) { pixel = src_pixel | mask_or }
    ALWAYS_INLINE uint16_t GetMaskAND() const
    {
      // return check_mask_before_draw ? 0x8000 : 0x0000;
      return static_cast<uint16_t>((bits << 3) & 0x8000);
    }
    ALWAYS_INLINE uint16_t GetMaskOR() const
    {
      // return set_mask_while_drawing ? 0x8000 : 0x0000;
      return static_cast<uint16_t>((bits << 4) & 0x8000);
    }
  } m_GPUSTAT = {};

  struct DrawMode
  {
    static constexpr uint16_t PALETTE_MASK = UINT16_C(0b0111111111111111);
    static constexpr uint32_t TEXTURE_WINDOW_MASK = UINT32_C(0b11111111111111111111);

    // original values
    GPUDrawModeReg mode_reg;
    uint16_t palette_reg; // from vertex
    uint32_t texture_window_value;

    // decoded values
    uint32_t texture_page_x;
    uint32_t texture_page_y;
    uint32_t texture_palette_x;
    uint32_t texture_palette_y;
    GPUTextureWindow texture_window;
    bool texture_x_flip;
    bool texture_y_flip;
    bool texture_page_changed;
    bool texture_window_changed;

    /// Returns a rectangle comprising the texture palette area.
    ALWAYS_INLINE_RELEASE Common::Rectangle<uint32_t> GetTexturePaletteRectangle() const
    {
      static constexpr std::array<uint32_t, 4> palette_widths = {{16, 256, 0, 0}};
      return Common::Rectangle<uint32_t>::FromExtents(texture_palette_x, texture_palette_y,
                                                 palette_widths[static_cast<uint8_t>(mode_reg.texture_mode.GetValue())], 1);
    }

    ALWAYS_INLINE bool IsTexturePageChanged() const { return texture_page_changed; }
    ALWAYS_INLINE void SetTexturePageChanged() { texture_page_changed = true; }
    ALWAYS_INLINE void ClearTexturePageChangedFlag() { texture_page_changed = false; }

    ALWAYS_INLINE bool IsTextureWindowChanged() const { return texture_window_changed; }
    ALWAYS_INLINE void SetTextureWindowChanged() { texture_window_changed = true; }
    ALWAYS_INLINE void ClearTextureWindowChangedFlag() { texture_window_changed = false; }
  } m_draw_mode = {};

  Common::Rectangle<uint32_t> m_drawing_area{0, 0, VRAM_WIDTH, VRAM_HEIGHT};

  struct DrawingOffset
  {
    int32_t x;
    int32_t y;
  } m_drawing_offset = {};

  bool m_console_is_pal = false;
  bool m_set_texture_disable_mask = false;
  bool m_drawing_area_changed = false;
  bool m_force_progressive_scan = false;
  bool m_force_ntsc_timings = false;

  struct CRTCState
  {
    struct Regs
    {
      static constexpr uint32_t DISPLAY_ADDRESS_START_MASK = 0b111'11111111'11111110;
      static constexpr uint32_t HORIZONTAL_DISPLAY_RANGE_MASK = 0b11111111'11111111'11111111;
      static constexpr uint32_t VERTICAL_DISPLAY_RANGE_MASK = 0b1111'11111111'11111111;

      union
      {
        uint32_t display_address_start;
        BitField<uint32_t, uint16_t, 0, 10> X;
        BitField<uint32_t, uint16_t, 10, 9> Y;
      };
      union
      {
        uint32_t horizontal_display_range;
        BitField<uint32_t, uint16_t, 0, 12> X1;
        BitField<uint32_t, uint16_t, 12, 12> X2;
      };

      union
      {
        uint32_t vertical_display_range;
        BitField<uint32_t, uint16_t, 0, 10> Y1;
        BitField<uint32_t, uint16_t, 10, 10> Y2;
      };
    } regs;

    uint16_t dot_clock_divider;

    // Size of the simulated screen in pixels. Depending on crop mode, this may include overscan area.
    uint16_t display_width;
    uint16_t display_height;

    // Top-left corner in screen coordinates where the outputted portion of VRAM is first visible.
    uint16_t display_origin_left;
    uint16_t display_origin_top;

    // Rectangle in VRAM coordinates describing the area of VRAM that is visible on screen.
    uint16_t display_vram_left;
    uint16_t display_vram_top;
    uint16_t display_vram_width;
    uint16_t display_vram_height;

    // Visible range of the screen, in GPU ticks/lines. Clamped to lie within the active video region.
    uint16_t horizontal_visible_start;
    uint16_t horizontal_visible_end;
    uint16_t vertical_visible_start;
    uint16_t vertical_visible_end;

    uint16_t horizontal_display_start;
    uint16_t horizontal_display_end;
    uint16_t vertical_display_start;
    uint16_t vertical_display_end;

    uint16_t horizontal_total;
    uint16_t horizontal_sync_start; // <- not currently saved to state, so we don't have to bump the version
    uint16_t vertical_total;

    TickCount fractional_ticks;
    TickCount current_tick_in_scanline;
    uint32_t current_scanline;

    TickCount fractional_dot_ticks; // only used when timer0 is enabled

    bool in_hblank;
    bool in_vblank;

    uint8_t interlaced_field; // 0 = odd, 1 = even
    uint8_t interlaced_display_field;
    uint8_t active_line_lsb;
  } m_crtc_state = {};

  BlitterState m_blitter_state = BlitterState::Idle;
  uint32_t m_command_total_words = 0;
  TickCount m_pending_command_ticks = 0;

  /// GPUREAD value for non-VRAM-reads.
  uint32_t m_GPUREAD_latch = 0;

  /// True if currently executing/syncing.
  bool m_syncing = false;
  bool m_fifo_pushed = false;

  struct VRAMTransfer
  {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t col;
    uint16_t row;
  } m_vram_transfer = {};

  HeapFIFOQueue<uint64_t, MAX_FIFO_SIZE> m_fifo;
  std::vector<uint32_t> m_blit_buffer;
  uint32_t m_blit_remaining_words;
  GPURenderCommand m_render_command{};

  ALWAYS_INLINE uint32_t FifoPop() { return static_cast<uint32_t>(m_fifo.Pop()); }
  ALWAYS_INLINE uint32_t FifoPeek() { return static_cast<uint32_t>(m_fifo.Peek()); }
  ALWAYS_INLINE uint32_t FifoPeek(uint32_t i) { return static_cast<uint32_t>(m_fifo.Peek(i)); }

  TickCount m_max_run_ahead = 128;
  uint32_t m_fifo_size = 128;

private:
  using GP0CommandHandler = bool (GPU::*)();
  using GP0CommandHandlerTable = std::array<GP0CommandHandler, 256>;
  static GP0CommandHandlerTable GenerateGP0CommandHandlerTable();

  // Rendering commands, returns false if not enough data is provided
  bool HandleUnknownGP0Command();
  bool HandleNOPCommand();
  bool HandleClearCacheCommand();
  bool HandleInterruptRequestCommand();
  bool HandleSetDrawModeCommand();
  bool HandleSetTextureWindowCommand();
  bool HandleSetDrawingAreaTopLeftCommand();
  bool HandleSetDrawingAreaBottomRightCommand();
  bool HandleSetDrawingOffsetCommand();
  bool HandleSetMaskBitCommand();
  bool HandleRenderPolygonCommand();
  bool HandleRenderRectangleCommand();
  bool HandleRenderLineCommand();
  bool HandleRenderPolyLineCommand();
  bool HandleFillRectangleCommand();
  bool HandleCopyRectangleCPUToVRAMCommand();
  bool HandleCopyRectangleVRAMToCPUCommand();
  bool HandleCopyRectangleVRAMToVRAMCommand();

  static const GP0CommandHandlerTable s_GP0_command_handler_table;
};

extern std::unique_ptr<GPU> g_gpu;

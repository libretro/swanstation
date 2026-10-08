#include "common/string_util.h"
#include "cpu_core.h"
#include "gpu.h"
#include "interrupt_controller.h"
#include "system.h"
#include "texture_replacements.h"

#define CHECK_COMMAND_SIZE(num_words)                                                                                  \
  if (m_fifo.GetSize() < num_words)                                                                                    \
  {                                                                                                                    \
    m_command_total_words = num_words;                                                                                 \
    return false;                                                                                                      \
  }

static constexpr uint32_t ReplaceZero(uint32_t value, uint32_t value_for_zero)
{
  return value == 0 ? value_for_zero : value;
}

void GPU::ExecuteCommands()
{
  m_syncing = true;

  /* Only DMA transfers feeding the FIFO advance the CPU's pending ticks while this runs. */
  TickCount dma_start_ticks = CPU::GetPendingTicks();

  for (;;)
  {
    if (m_pending_command_ticks <= m_max_run_ahead && !m_fifo.IsEmpty())
    {
      switch (m_blitter_state)
      {
        case BlitterState::Idle:
        {
          const uint32_t command = FifoPeek(0) >> 24;
          if ((this->*s_GP0_command_handler_table[command])())
            continue;
          else
            goto batch_done;
        }

        case BlitterState::WritingVRAM:
        {
          const uint32_t words_to_copy = std::min(m_blit_remaining_words, m_fifo.GetSize());
          m_blit_buffer.reserve(m_blit_buffer.size() + words_to_copy);
          for (uint32_t i = 0; i < words_to_copy; i++)
            m_blit_buffer.push_back(FifoPop());
          m_blit_remaining_words -= words_to_copy;

          if (m_blit_remaining_words == 0)
            FinishVRAMWrite();

          continue;
        }

        case BlitterState::ReadingVRAM:
        {
          goto batch_done;
        }
        break;

        case BlitterState::DrawingPolyLine:
        {
          const uint32_t words_per_vertex = m_render_command.shading_enable ? 2 : 1;
          uint32_t terminator_index =
            m_render_command.shading_enable ? ((static_cast<uint32_t>(m_blit_buffer.size()) & 1u) ^ 1u) : 0u;
          for (; terminator_index < m_fifo.GetSize(); terminator_index += words_per_vertex)
          {
            // polyline must have at least two vertices, and the terminator is (word & 0xf000f000) == 0x50005000.
            // terminator is on the first word for the vertex
            if ((FifoPeek(terminator_index) & UINT32_C(0xF000F000)) == UINT32_C(0x50005000))
              break;
          }

          const bool found_terminator = (terminator_index < m_fifo.GetSize());
          const uint32_t words_to_copy = std::min(terminator_index, m_fifo.GetSize());
          if (words_to_copy > 0)
          {
            m_blit_buffer.reserve(m_blit_buffer.size() + words_to_copy);
            for (uint32_t i = 0; i < words_to_copy; i++)
              m_blit_buffer.push_back(FifoPop());
          }

          if (found_terminator)
          {
            // drop terminator
            m_fifo.RemoveOne();
            DispatchRenderCommand();
            m_blit_buffer.clear();
            EndCommand();
            continue;
          }

          /* Without a terminator the guest can send vertices forever; draw what is buffered and carry on from
           * its last vertex. Shaded words alternate colour and position after the first position, so an even
           * count ends with the colour of a vertex still to come, which is kept. */
          if (GetPolyLineVertexCount() >= MAX_POLYLINE_VERTICES)
          {
            DispatchRenderCommand();

            const size_t count = m_blit_buffer.size();
            if (!m_render_command.shading_enable)
            {
              const uint32_t last_position = m_blit_buffer[count - 1];
              m_blit_buffer.clear();
              m_blit_buffer.push_back(last_position);
            }
            else
            {
              const bool pending_colour = (count & 1u) == 0;
              const size_t last_position_index = pending_colour ? (count - 2) : (count - 1);
              const uint32_t last_position = m_blit_buffer[last_position_index];
              const uint32_t pending = m_blit_buffer[count - 1];
              m_render_command.color_for_first_vertex = m_blit_buffer[last_position_index - 1] & UINT32_C(0x00FFFFFF);
              m_blit_buffer.clear();
              m_blit_buffer.push_back(last_position);
              if (pending_colour)
                m_blit_buffer.push_back(pending);
            }
          }
        }
        break;
      }
    }

  batch_done:
    m_fifo_pushed = false;
    UpdateDMARequest();
    if (!m_fifo_pushed)
      break;

    /* Commands that take no GPU time let a looping DMA linked list refill the FIFO forever without time
     * passing. Past this much DMA time, count it as GPU time, so the FIFO fills and the events get a turn. */
    const TickCount dma_ticks = CPU::GetPendingTicks() - dma_start_ticks;
    if (dma_ticks > MAX_DMA_TICKS_PER_BATCH)
    {
      AddCommandTicks(SystemTicksToGPUTicks(dma_ticks));
      dma_start_ticks = CPU::GetPendingTicks();
    }
  }

  UpdateGPUIdle();
  m_syncing = false;
}

void GPU::EndCommand()
{
  m_blitter_state = BlitterState::Idle;
  m_command_total_words = 0;
}

GPU::GP0CommandHandlerTable GPU::GenerateGP0CommandHandlerTable()
{
  GP0CommandHandlerTable table = {};
  for (uint32_t i = 0; i < static_cast<uint32_t>(table.size()); i++)
    table[i] = &GPU::HandleUnknownGP0Command;
  table[0x00] = &GPU::HandleNOPCommand;
  table[0x01] = &GPU::HandleClearCacheCommand;
  table[0x02] = &GPU::HandleFillRectangleCommand;
  table[0x03] = &GPU::HandleNOPCommand;
  for (uint32_t i = 0x04; i <= 0x1E; i++)
    table[i] = &GPU::HandleNOPCommand;
  table[0x1F] = &GPU::HandleInterruptRequestCommand;
  for (uint32_t i = 0x20; i <= 0x7F; i++)
  {
    const GPURenderCommand rc{i << 24};
    switch (rc.primitive)
    {
      case GPUPrimitive::Polygon:
        table[i] = &GPU::HandleRenderPolygonCommand;
        break;
      case GPUPrimitive::Line:
        table[i] = rc.polyline ? &GPU::HandleRenderPolyLineCommand : &GPU::HandleRenderLineCommand;
        break;
      case GPUPrimitive::Rectangle:
        table[i] = &GPU::HandleRenderRectangleCommand;
        break;
      default:
        table[i] = &GPU::HandleUnknownGP0Command;
        break;
    }
  }
  table[0xE0] = &GPU::HandleNOPCommand;
  table[0xE1] = &GPU::HandleSetDrawModeCommand;
  table[0xE2] = &GPU::HandleSetTextureWindowCommand;
  table[0xE3] = &GPU::HandleSetDrawingAreaTopLeftCommand;
  table[0xE4] = &GPU::HandleSetDrawingAreaBottomRightCommand;
  table[0xE5] = &GPU::HandleSetDrawingOffsetCommand;
  table[0xE6] = &GPU::HandleSetMaskBitCommand;
  for (uint32_t i = 0xE7; i <= 0xEF; i++)
    table[i] = &GPU::HandleNOPCommand;
  for (uint32_t i = 0x80; i <= 0x9F; i++)
    table[i] = &GPU::HandleCopyRectangleVRAMToVRAMCommand;
  for (uint32_t i = 0xA0; i <= 0xBF; i++)
    table[i] = &GPU::HandleCopyRectangleCPUToVRAMCommand;
  for (uint32_t i = 0xC0; i <= 0xDF; i++)
    table[i] = &GPU::HandleCopyRectangleVRAMToCPUCommand;

  table[0xFF] = &GPU::HandleNOPCommand;

  return table;
}

bool GPU::HandleUnknownGP0Command()
{
  m_fifo.RemoveOne();
  EndCommand();
  return true;
}

bool GPU::HandleNOPCommand()
{
  m_fifo.RemoveOne();
  EndCommand();
  return true;
}

bool GPU::HandleClearCacheCommand()
{
  m_fifo.RemoveOne();
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleInterruptRequestCommand()
{
  if (!m_GPUSTAT.interrupt_request)
  {
    m_GPUSTAT.interrupt_request = true;
    g_interrupt_controller.InterruptRequest(InterruptController::IRQ::GPU);
  }

  m_fifo.RemoveOne();
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawModeCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;
  SetDrawMode(static_cast<uint16_t>(param));
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetTextureWindowCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;
  SetTextureWindow(param);
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingAreaTopLeftCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;
  const uint32_t left = param & VRAM_WIDTH_MASK;
  const uint32_t top = (param >> 10) & VRAM_HEIGHT_MASK;
  if (m_drawing_area.left != left || m_drawing_area.top != top)
  {
    FlushRender();

    m_drawing_area.left = left;
    m_drawing_area.top = top;
    m_drawing_area_changed = true;
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingAreaBottomRightCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;

  const uint32_t right = param & VRAM_WIDTH_MASK;
  const uint32_t bottom = (param >> 10) & VRAM_HEIGHT_MASK;
  if (m_drawing_area.right != right || m_drawing_area.bottom != bottom)
  {
    FlushRender();

    m_drawing_area.right = right;
    m_drawing_area.bottom = bottom;
    m_drawing_area_changed = true;
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingOffsetCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;
  // Sign-extend 11-bit drawing offset to int32_t.
  const int32_t x = static_cast<int32_t>((param & 0x7FFu) << 21) >> 21;
  const int32_t y = static_cast<int32_t>(((param >> 11) & 0x7FFu) << 21) >> 21;
  if (m_drawing_offset.x != x || m_drawing_offset.y != y)
  {
    FlushRender();

    m_drawing_offset.x = x;
    m_drawing_offset.y = y;
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetMaskBitCommand()
{
  const uint32_t param = FifoPop() & 0x00FFFFFFu;

  constexpr uint32_t gpustat_mask = (1 << 11) | (1 << 12);
  const uint32_t gpustat_bits = (param & 0x03) << 11;
  if ((m_GPUSTAT.bits & gpustat_mask) != gpustat_bits)
  {
    FlushRender();
    m_GPUSTAT.bits = (m_GPUSTAT.bits & ~gpustat_mask) | gpustat_bits;
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleRenderPolygonCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};

  // shaded vertices use the colour from the first word for the first vertex
  const uint32_t words_per_vertex = 1 + static_cast<uint32_t>(rc.texture_enable) + static_cast<uint32_t>(rc.shading_enable);
  const uint32_t num_vertices = rc.quad_polygon ? 4 : 3;
  const uint32_t total_words = words_per_vertex * num_vertices + static_cast<uint32_t>(!rc.shading_enable);
  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  // setup time
  static constexpr uint16_t s_setup_time[2][2][2] = {{{46, 226}, {334, 496}}, {{82, 262}, {370, 532}}};
  const TickCount setup_ticks = static_cast<TickCount>(s_setup_time[static_cast<uint8_t>(rc.quad_polygon)][static_cast<uint8_t>(rc.shading_enable)][static_cast<uint8_t>(rc.texture_enable)]);
  AddCommandTicks(setup_ticks);

  // set draw state up
  if (rc.texture_enable)
  {
    const uint16_t texpage_attribute = static_cast<uint16_t>((rc.shading_enable ? FifoPeek(5) : FifoPeek(4)) >> 16);
    SetDrawMode((texpage_attribute & GPUDrawModeReg::POLYGON_TEXPAGE_MASK) |
                (m_draw_mode.mode_reg.bits & ~GPUDrawModeReg::POLYGON_TEXPAGE_MASK));
    SetTexturePalette(static_cast<uint16_t>(FifoPeek(2) >> 16));
  }

  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderRectangleCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};
  const uint32_t total_words =
    2 + static_cast<uint32_t>(rc.texture_enable) + static_cast<uint32_t>(rc.rectangle_size == GPUDrawRectangleSize::Variable);

  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  if (rc.texture_enable)
    SetTexturePalette(static_cast<uint16_t>(FifoPeek(2) >> 16));

  const TickCount setup_ticks = 16;
  AddCommandTicks(setup_ticks);

  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderLineCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};
  const uint32_t total_words = rc.shading_enable ? 4 : 3;
  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderPolyLineCommand()
{
  // always read the first two vertices, we test for the terminator after that
  const GPURenderCommand rc{FifoPeek(0)};
  const uint32_t min_words = rc.shading_enable ? 3 : 4;
  CHECK_COMMAND_SIZE(min_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  const TickCount setup_ticks = 16;
  AddCommandTicks(setup_ticks);

  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  const uint32_t words_to_pop = min_words - 1;
  // m_blit_buffer.resize(words_to_pop);
  // FifoPopRange(m_blit_buffer.data(), words_to_pop);
  m_blit_buffer.clear();
  m_blit_buffer.reserve(words_to_pop);
  for (uint32_t i = 0; i < words_to_pop; i++)
    m_blit_buffer.push_back(static_cast<uint32_t>(FifoPop()));

  // polyline goes via a different path through the blit buffer
  m_blitter_state = BlitterState::DrawingPolyLine;
  m_command_total_words = 0;
  return true;
}

bool GPU::HandleFillRectangleCommand()
{
  CHECK_COMMAND_SIZE(3);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  FlushRender();

  const uint32_t color = FifoPop() & 0x00FFFFFF;
  const uint32_t dst_x = FifoPeek() & 0x3F0;
  const uint32_t dst_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const uint32_t width = ((FifoPeek() & VRAM_WIDTH_MASK) + 0xF) & ~0xF;
  const uint32_t height = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;

  if (width > 0 && height > 0)
    FillVRAM(dst_x, dst_y, width, height, color);

  AddCommandTicks(46 + ((width / 8) + 9) * height);
  EndCommand();
  return true;
}

bool GPU::HandleCopyRectangleCPUToVRAMCommand()
{
  CHECK_COMMAND_SIZE(3);
  m_fifo.RemoveOne();

  const uint32_t dst_x = FifoPeek() & VRAM_WIDTH_MASK;
  const uint32_t dst_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const uint32_t copy_width = ReplaceZero(FifoPeek() & VRAM_WIDTH_MASK, 0x400);
  const uint32_t copy_height = ReplaceZero((FifoPop() >> 16) & VRAM_HEIGHT_MASK, 0x200);
  const uint32_t num_pixels = copy_width * copy_height;
  const uint32_t num_words = ((num_pixels + 1) / 2);

  EndCommand();

  m_blitter_state = BlitterState::WritingVRAM;
  m_blit_buffer.reserve(num_words);
  m_blit_remaining_words = num_words;
  m_vram_transfer.x = static_cast<uint16_t>(dst_x);
  m_vram_transfer.y = static_cast<uint16_t>(dst_y);
  m_vram_transfer.width = static_cast<uint16_t>(copy_width);
  m_vram_transfer.height = static_cast<uint16_t>(copy_height);
  return true;
}

void GPU::FinishVRAMWrite()
{
  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  FlushRender();

  if (m_blit_remaining_words == 0)
  {
    UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y, m_vram_transfer.width, m_vram_transfer.height,
               m_blit_buffer.data(), m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
  }
  else
  {
    const uint32_t num_pixels = static_cast<uint32_t>(m_vram_transfer.width) * static_cast<uint32_t>(m_vram_transfer.height);
    const uint32_t num_words = (num_pixels + 1) / 2;
    const uint32_t transferred_words = num_words - m_blit_remaining_words;
    const uint32_t transferred_pixels = transferred_words * 2;
    const uint32_t transferred_full_rows = transferred_pixels / m_vram_transfer.width;
    const uint32_t transferred_width_last_row = transferred_pixels % m_vram_transfer.width;
    const uint8_t* blit_ptr = reinterpret_cast<const uint8_t*>(m_blit_buffer.data());
    if (transferred_full_rows > 0)
    {
      UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y, m_vram_transfer.width, transferred_full_rows, blit_ptr,
                 m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
      blit_ptr += (static_cast<uint32_t>(m_vram_transfer.width) * transferred_full_rows) * sizeof(uint16_t);
    }
    if (transferred_width_last_row > 0)
    {
      UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y + transferred_full_rows, transferred_width_last_row, 1, blit_ptr,
                 m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
    }
  }

  m_blit_buffer.clear();
  m_vram_transfer = {};
  m_blitter_state = BlitterState::Idle;
}

bool GPU::HandleCopyRectangleVRAMToCPUCommand()
{
  CHECK_COMMAND_SIZE(3);
  m_fifo.RemoveOne();

  m_vram_transfer.x = static_cast<uint16_t>(FifoPeek() & VRAM_WIDTH_MASK);
  m_vram_transfer.y = static_cast<uint16_t>((FifoPop() >> 16) & VRAM_HEIGHT_MASK);
  m_vram_transfer.width = ((static_cast<uint16_t>(FifoPeek()) - 1) & VRAM_WIDTH_MASK) + 1;
  m_vram_transfer.height = ((static_cast<uint16_t>(FifoPop() >> 16) - 1) & VRAM_HEIGHT_MASK) + 1;

  // all rendering should be done first...
  FlushRender();

  // ensure VRAM shadow is up to date
  ReadVRAM(m_vram_transfer.x, m_vram_transfer.y, m_vram_transfer.width, m_vram_transfer.height);

  // switch to pixel-by-pixel read state
  m_blitter_state = BlitterState::ReadingVRAM;
  m_command_total_words = 0;
  return true;
}

bool GPU::HandleCopyRectangleVRAMToVRAMCommand()
{
  CHECK_COMMAND_SIZE(4);
  m_fifo.RemoveOne();

  const uint32_t src_x = FifoPeek() & VRAM_WIDTH_MASK;
  const uint32_t src_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const uint32_t dst_x = FifoPeek() & VRAM_WIDTH_MASK;
  const uint32_t dst_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const uint32_t width = ReplaceZero(FifoPeek() & VRAM_WIDTH_MASK, 0x400);
  const uint32_t height = ReplaceZero((FifoPop() >> 16) & VRAM_HEIGHT_MASK, 0x200);
  // Some VRAM copies aren't going to do anything. Most games seem to send a 2x2 VRAM copy at the end of a frame.
  const bool skip_copy =
    width == 0 || height == 0 || (src_x == dst_x && src_y == dst_y && !m_GPUSTAT.set_mask_while_drawing);
  if (!skip_copy)
  {
    FlushRender();
    CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);
  }

  AddCommandTicks(width * height * 2);
  EndCommand();
  return true;
}

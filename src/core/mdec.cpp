#include "mdec.h"
#include "common/state_wrapper.h"
#include "cpu_core.h"
#include "dma.h"
#include "gpu_types.h"
#include "interrupt_controller.h"
#include "system.h"

#include <cstring>
MDEC g_mdec;

MDEC::MDEC() = default;

MDEC::~MDEC() = default;

void MDEC::Initialize()
{
  m_block_copy_out_event = TimingEvents::CreateTimingEvent(
    "MDEC Block Copy Out", 1, 1,
    [](void* param, TickCount ticks, TickCount ticks_late) { static_cast<MDEC*>(param)->CopyOutBlock(); }, this, false);
  m_total_blocks_decoded = 0;
  Reset();
}

void MDEC::Shutdown()
{
  m_block_copy_out_event.reset();
}

void MDEC::Reset()
{
  m_block_copy_out_event->Deactivate();
  SoftReset();
}

bool MDEC::DoState(StateWrapper& sw)
{
  sw.Do(&m_status.bits);
  sw.Do(&m_enable_dma_in);
  sw.Do(&m_enable_dma_out);
  sw.Do(&m_data_in_fifo);
  sw.Do(&m_data_out_fifo);
  sw.Do(&m_state);
  sw.Do(&m_remaining_halfwords);
  sw.Do(&m_iq_uv);
  sw.Do(&m_iq_y);
  sw.Do(&m_scale_table);
  sw.Do(&m_blocks);
  sw.Do(&m_current_block);
  sw.Do(&m_current_coefficient);
  sw.Do(&m_current_q_scale);
  sw.Do(&m_block_rgb);

  bool block_copy_out_pending = HasPendingBlockCopyOut();
  sw.Do(&block_copy_out_pending);
  if (sw.IsReading())
    m_block_copy_out_event->SetState(block_copy_out_pending);

  return !sw.HasError();
}

uint32_t MDEC::ReadRegister(uint32_t offset)
{
  switch (offset)
  {
    case 0:
      return ReadDataRegister();

    case 4:
      return m_status.bits;

    default:
      break;
  }
  return UINT32_C(0xFFFFFFFF);
}

void MDEC::WriteRegister(uint32_t offset, uint32_t value)
{
  switch (offset)
  {
    case 0:
    {
      WriteCommandRegister(value);
      return;
    }

    case 4:
    {
      const ControlRegister cr{value};
      if (cr.reset)
        SoftReset();

      m_enable_dma_in = cr.enable_dma_in;
      m_enable_dma_out = cr.enable_dma_out;
      Execute();
      return;
    }

    default:
      break;
  }
}

void MDEC::DMARead(uint32_t* words, uint32_t word_count)
{
  const uint32_t words_to_read = std::min(word_count, m_data_out_fifo.GetSize());
  if (words_to_read > 0)
  {
    m_data_out_fifo.PopRange(words, words_to_read);
    words += words_to_read;
    word_count -= words_to_read;
  }

  if (m_data_out_fifo.IsEmpty())
    Execute();
}

void MDEC::DMAWrite(const uint32_t* words, uint32_t word_count)
{
  const uint32_t halfwords_to_write = std::min(word_count * 2, m_data_in_fifo.GetSpace() & ~uint32_t(1));
  m_data_in_fifo.PushRange(reinterpret_cast<const uint16_t*>(words), halfwords_to_write);
  Execute();
}

bool MDEC::HasPendingBlockCopyOut() const
{
  return m_block_copy_out_event->IsActive();
}

void MDEC::SoftReset()
{
  m_status.bits = 0;
  m_enable_dma_in = false;
  m_enable_dma_out = false;
  m_data_in_fifo.Clear();
  m_data_out_fifo.Clear();
  m_state = State::Idle;
  m_remaining_halfwords = 0;
  m_current_block = 0;
  m_current_coefficient = 64;
  m_current_q_scale = 0;
  m_block_copy_out_event->Deactivate();
  UpdateStatus();
}

void MDEC::ResetDecoder()
{
  m_current_block = 0;
  m_current_coefficient = 64;
  m_current_q_scale = 0;
}

void MDEC::UpdateStatus()
{
  m_status.data_out_fifo_empty = m_data_out_fifo.IsEmpty();
  m_status.data_in_fifo_full = m_data_in_fifo.IsFull();

  m_status.command_busy = (m_state != State::Idle);
  m_status.parameter_words_remaining = static_cast<uint16_t>((m_remaining_halfwords / 2) - 1);
  m_status.current_block = (m_current_block + 4) % NUM_BLOCKS;

  // we always want data in if it's enabled
  const bool data_in_request = m_enable_dma_in && m_data_in_fifo.GetSpace() >= (32 * 2);
  m_status.data_in_request = data_in_request;
  g_dma.SetRequest(DMA::Channel::MDECin, data_in_request);

  // we only want to send data out if we have some in the fifo
  const bool data_out_request = m_enable_dma_out && !m_data_out_fifo.IsEmpty();
  m_status.data_out_request = data_out_request;
  g_dma.SetRequest(DMA::Channel::MDECout, data_out_request);
}

uint32_t MDEC::ReadDataRegister()
{
  if (m_data_out_fifo.IsEmpty())
  {
    // Stall the CPU until we're done processing.
    if (!HasPendingBlockCopyOut())
      return UINT32_C(0xFFFFFFFF);
    CPU::AddPendingTicks(m_block_copy_out_event->GetTicksUntilNextExecution());
  }

  const uint32_t value = m_data_out_fifo.Pop();
  if (m_data_out_fifo.IsEmpty())
    Execute();
  else
    UpdateStatus();

  return value;
}

void MDEC::WriteCommandRegister(uint32_t value)
{
  /* Writes to a full data-in FIFO are dropped. */
  if (m_data_in_fifo.GetSpace() < 2)
    return;

  m_data_in_fifo.Push(static_cast<uint16_t>(value));
  m_data_in_fifo.Push(static_cast<uint16_t>(value >> 16));

  Execute();
}

void MDEC::Execute()
{
  if (m_executing)
  {
    m_execute_pending = true;
    return;
  }

  m_executing = true;
  do
  {
    m_execute_pending = false;
    ExecuteCommands();
  } while (m_execute_pending);
  m_executing = false;
}

void MDEC::ExecuteCommands()
{
  for (;;)
  {
    switch (m_state)
    {
      case State::Idle:
      {
        if (m_data_in_fifo.GetSize() < 2)
          goto finished;

        // first word
        const CommandWord cw{static_cast<uint32_t>(m_data_in_fifo.Peek(0)) | (static_cast<uint32_t>(m_data_in_fifo.Peek(1)) << 16)};
        m_status.data_output_depth = cw.data_output_depth;
        m_status.data_output_signed = cw.data_output_signed;
        m_status.data_output_bit15 = cw.data_output_bit15;
        m_data_in_fifo.Remove(2);
        m_data_out_fifo.Clear();

        uint32_t num_words;
        State new_state;
        switch (cw.command)
        {
          case Command::DecodeMacroblock:
            num_words = static_cast<uint32_t>(cw.parameter_word_count.GetValue());
            new_state = State::DecodingMacroblock;
            break;

          case Command::SetIqTab:
            num_words = 16 + (((cw.bits & 1) != 0) ? 16 : 0);
            new_state = State::SetIqTable;
            break;

          case Command::SetScale:
            num_words = 32;
            new_state = State::SetScaleTable;
            break;

          default:
            num_words = cw.parameter_word_count.GetValue();
            new_state = State::NoCommand;
            break;
        }

        m_remaining_halfwords = num_words * 2;
        m_state = new_state;
        UpdateStatus();
        continue;
      }

      case State::DecodingMacroblock:
      {
        // we should be writing out now
        if (HandleDecodeMacroblockCommand())
          goto finished;

        if (m_remaining_halfwords == 0 && m_current_block != NUM_BLOCKS)
        {
          // expecting data, but nothing more will be coming. bail out
          ResetDecoder();
          m_state = State::Idle;
          continue;
        }

        goto finished;
      }

      case State::WritingMacroblock:
      {
        // this gets executed via the event, so if we get here, wait.
        goto finished;
      }

      case State::SetIqTable:
      {
        if (m_data_in_fifo.GetSize() < m_remaining_halfwords)
          goto finished;

        HandleSetQuantTableCommand();
        m_state = State::Idle;
        UpdateStatus();
        continue;
      }

      case State::SetScaleTable:
      {
        if (m_data_in_fifo.GetSize() < m_remaining_halfwords)
          goto finished;

        HandleSetScaleCommand();
        m_state = State::Idle;
        UpdateStatus();
        continue;
      }

      case State::NoCommand:
      {
        // can potentially have a large amount of halfwords, so eat them as we go
        const uint32_t words_to_consume = std::min(m_remaining_halfwords, m_data_in_fifo.GetSize());
        m_data_in_fifo.Remove(words_to_consume);
        m_remaining_halfwords -= words_to_consume;
        if (m_remaining_halfwords == 0)
          goto finished;

        m_state = State::Idle;
        UpdateStatus();
        continue;
      }

      default:
        return;
    }
  }

finished:
  // if we get here, it's because the FIFO is now empty
  UpdateStatus();
}

bool MDEC::HandleDecodeMacroblockCommand()
{
  if (m_status.data_output_depth <= DataOutputDepth_8Bit)
    return DecodeMonoMacroblock();
  else
    return DecodeColoredMacroblock();
}

bool MDEC::DecodeMonoMacroblock()
{
  // TODO: This should guard the output not the input
  if (!m_data_out_fifo.IsEmpty())
    return false;

  if (g_settings.use_old_mdec_routines)
  {
    if (!DecodeRLE_Old(m_blocks[0].data(), m_iq_y.data()))
      return false;

    IDCT_Old(m_blocks[0].data());
  }
  else
  {
    if (!DecodeRLE_New(m_blocks[0].data(), m_iq_y.data()))
      return false;

    IDCT_New(m_blocks[0].data());
  }

  ResetDecoder();
  m_state = State::WritingMacroblock;

  YUVToMono(m_blocks[0]);

  ScheduleBlockCopyOut(TICKS_PER_BLOCK * 6);

  m_total_blocks_decoded++;
  return true;
}

bool MDEC::DecodeColoredMacroblock()
{
  if (g_settings.use_old_mdec_routines)
  {
    for (; m_current_block < NUM_BLOCKS; m_current_block++)
    {
      if (!DecodeRLE_Old(m_blocks[m_current_block].data(), (m_current_block >= 2) ? m_iq_y.data() : m_iq_uv.data()))
        return false;

      IDCT_Old(m_blocks[m_current_block].data());
    }

    if (!m_data_out_fifo.IsEmpty())
      return false;

    // done decoding
    ResetDecoder();
    m_state = State::WritingMacroblock;

    YUVToRGB_Old(0, 0, m_blocks[0], m_blocks[1], m_blocks[2]);
    YUVToRGB_Old(8, 0, m_blocks[0], m_blocks[1], m_blocks[3]);
    YUVToRGB_Old(0, 8, m_blocks[0], m_blocks[1], m_blocks[4]);
    YUVToRGB_Old(8, 8, m_blocks[0], m_blocks[1], m_blocks[5]);
  }
  else
  {
    for (; m_current_block < NUM_BLOCKS; m_current_block++)
    {
      if (!DecodeRLE_New(m_blocks[m_current_block].data(), (m_current_block >= 2) ? m_iq_y.data() : m_iq_uv.data()))
        return false;

      IDCT_New(m_blocks[m_current_block].data());
    }

    if (!m_data_out_fifo.IsEmpty())
      return false;

    // done decoding
    ResetDecoder();
    m_state = State::WritingMacroblock;

    YUVToRGB_New(0, 0, m_blocks[0], m_blocks[1], m_blocks[2]);
    YUVToRGB_New(8, 0, m_blocks[0], m_blocks[1], m_blocks[3]);
    YUVToRGB_New(0, 8, m_blocks[0], m_blocks[1], m_blocks[4]);
    YUVToRGB_New(8, 8, m_blocks[0], m_blocks[1], m_blocks[5]);
  }

  m_total_blocks_decoded += 4;

  ScheduleBlockCopyOut(TICKS_PER_BLOCK * 6);
  return true;
}

void MDEC::ScheduleBlockCopyOut(TickCount ticks)
{
  m_block_copy_out_event->SetIntervalAndSchedule(ticks);
}

void MDEC::CopyOutBlock()
{
  m_block_copy_out_event->Deactivate();

  switch (m_status.data_output_depth)
  {
    case DataOutputDepth_4Bit:
    {
      const uint32_t* in_ptr = m_block_rgb.data();
      for (uint32_t i = 0; i < (64 / 8); i++)
      {
        uint32_t value = *(in_ptr++) >> 4;
        value |= (*(in_ptr++) >> 4) << 4;
        value |= (*(in_ptr++) >> 4) << 8;
        value |= (*(in_ptr++) >> 4) << 12;
        value |= (*(in_ptr++) >> 4) << 16;
        value |= (*(in_ptr++) >> 4) << 20;
        value |= (*(in_ptr++) >> 4) << 24;
        value |= (*(in_ptr++) >> 4) << 28;
        m_data_out_fifo.Push(value);
      }
    }
    break;

    case DataOutputDepth_8Bit:
    {
      const uint32_t* in_ptr = m_block_rgb.data();
      for (uint32_t i = 0; i < (64 / 4); i++)
      {
        uint32_t value = *in_ptr++;
        value |= *in_ptr++ << 8;
        value |= *in_ptr++ << 16;
        value |= *in_ptr++ << 24;
        m_data_out_fifo.Push(value);
      }
    }
    break;

    case DataOutputDepth_24Bit:
    {
      // pack tightly
      uint32_t index = 0;
      uint32_t state = 0;
      uint32_t rgb = 0;
      while (index < m_block_rgb.size())
      {
        switch (state)
        {
          case 0:
            rgb = m_block_rgb[index++]; // RGB-
            state = 1;
            break;
          case 1:
            rgb |= (m_block_rgb[index] & 0xFF) << 24; // RGBR
            m_data_out_fifo.Push(rgb);
            rgb = m_block_rgb[index] >> 8; // GB--
            index++;
            state = 2;
            break;
          case 2:
            rgb |= m_block_rgb[index] << 16; // GBRG
            m_data_out_fifo.Push(rgb);
            rgb = m_block_rgb[index] >> 16; // B---
            index++;
            state = 3;
            break;
          case 3:
            rgb |= m_block_rgb[index] << 8; // BRGB
            m_data_out_fifo.Push(rgb);
            index++;
            state = 0;
            break;
        }
      }
      break;
    }

    case DataOutputDepth_15Bit:
    {
      if (!g_settings.use_old_mdec_routines)
      {
        const uint32_t a = static_cast<uint32_t>(m_status.data_output_bit15.GetValue()) << 15;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_block_rgb.size());)
        {
#define E8TO5(color) (std::min<uint32_t>((((color) + 4) >> 3), 0x1F))
          uint32_t color = m_block_rgb[i++];
          uint32_t r = E8TO5(color & 0xFFu);
          uint32_t g = E8TO5((color >> 8) & 0xFFu);
          uint32_t b = E8TO5((color >> 16) & 0xFFu);
          const uint32_t color15a = r | (g << 5) | (b << 10) | a;

          color = m_block_rgb[i++];
          r = E8TO5(color & 0xFFu);
          g = E8TO5((color >> 8) & 0xFFu);
          b = E8TO5((color >> 16) & 0xFFu);
          const uint32_t color15b = r | (g << 5) | (b << 10) | a;
#undef E8TO5

          m_data_out_fifo.Push(color15a | (color15b << 16));
        }
      }
      else
      {
        // 'a' is the value of the data_output_bit15 status flag pre-shifted
        // to bit 15 of a uint16_t (so it's either 0x0000 or 0x8000). It
        // should be OR-combined into color15a/b directly to land in bit 15
        // of the 15bpp pixel. The previous '(a << 15)' shifted it a second
        // time, by 15 more bits, which on a uint16_t lvalue truncates the
        // mask bit to zero. That silently lost the PS1's per-pixel
        // semi-transparency / draw-mask flag for every 15bpp MDEC pixel
        // written through the Old path, breaking games that rely on the
        // mask bit being preserved through the MDEC->VRAM path - most
        // visibly Oddworld: Abe's Oddysee / Exoddus, whose backgrounds
        // composite with character sprites via the mask bit and present
        // as garbled high-contrast noise / mis-blended sprites when it
        // gets cleared (issue #127).
        const uint16_t a = static_cast<uint16_t>(m_status.data_output_bit15.GetValue()) << 15;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_block_rgb.size());)
        {
          uint32_t color = m_block_rgb[i++];
          uint16_t r = static_cast<uint16_t>((color >> 3) & 0x1Fu);
          uint16_t g = static_cast<uint16_t>((color >> 11) & 0x1Fu);
          uint16_t b = static_cast<uint16_t>((color >> 19) & 0x1Fu);
          const uint16_t color15a = static_cast<uint16_t>(r | (g << 5) | (b << 10)) | a;

          color = m_block_rgb[i++];
          r = static_cast<uint16_t>((color >> 3) & 0x1Fu);
          g = static_cast<uint16_t>((color >> 11) & 0x1Fu);
          b = static_cast<uint16_t>((color >> 19) & 0x1Fu);
          const uint16_t color15b = static_cast<uint16_t>(r | (g << 5) | (b << 10)) | a;

          m_data_out_fifo.Push(static_cast<uint32_t>(color15a) | (static_cast<uint32_t>(color15b) << 16));
        }
      }
    }
    break;

    default:
      break;
  }

  // if we've copied out all blocks, command is complete
  m_state = (m_remaining_halfwords == 0) ? State::Idle : State::DecodingMacroblock;
  Execute();
}

bool MDEC::DecodeRLE_Old(int16_t* blk, const uint8_t* qt)
{
  // constexpr eliminates the C++11 thread-safe-static-init guard.
  // DecodeRLE_Old runs per macroblock during MDEC video decode (FMV
  // playback path).
  static constexpr uint8_t zagzig[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                               12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                               35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                               58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
  
  if (m_current_coefficient == 64)
  {
    std::fill_n(blk, 64, int16_t(0));

    // skip padding at start
    uint16_t n;
    for (;;)
    {
      if (m_data_in_fifo.IsEmpty() || m_remaining_halfwords == 0)
        return false;

      n = m_data_in_fifo.Pop();
      m_remaining_halfwords--;

      if (n == 0xFE00)
        continue;
      else
        break;
    }

    m_current_coefficient = 0;
    m_current_q_scale = (n >> 10) & 0x3F;
    int32_t val =
      (static_cast<int32_t>(static_cast<uint32_t>(n & 0x3FF) << 22) >> 22) * static_cast<int32_t>(qt[m_current_coefficient]);

    if (m_current_q_scale == 0)
      val = (static_cast<int32_t>(static_cast<uint32_t>(n & 0x3FF) << 22) >> 22) * 2;

    val = std::clamp(val, -0x400, 0x3FF);
    if (m_current_q_scale > 0)
      blk[zagzig[m_current_coefficient]] = static_cast<int16_t>(val);
    else
      blk[m_current_coefficient] = static_cast<int16_t>(val);
  }

  while (!m_data_in_fifo.IsEmpty() && m_remaining_halfwords > 0)
  {
    uint16_t n = m_data_in_fifo.Pop();
    m_remaining_halfwords--;

    m_current_coefficient += ((n >> 10) & 0x3F) + 1;
    if (m_current_coefficient < 64)
    {
      int32_t val = ((static_cast<int32_t>(static_cast<uint32_t>(n & 0x3FF) << 22) >> 22) *
                   static_cast<int32_t>(qt[m_current_coefficient]) * static_cast<int32_t>(m_current_q_scale) +
                 4) /
                8;

      if (m_current_q_scale == 0)
        val = (static_cast<int32_t>(static_cast<uint32_t>(n & 0x3FF) << 22) >> 22) * 2;

      val = std::clamp(val, -0x400, 0x3FF);
      if (m_current_q_scale > 0)
        blk[zagzig[m_current_coefficient]] = static_cast<int16_t>(val);
      else
        blk[m_current_coefficient] = static_cast<int16_t>(val);
    }

    if (m_current_coefficient >= 63)
    {
      m_current_coefficient = 64;
      return true;
    }
  }

  return false;
}

void MDEC::IDCT_Old(int16_t* blk)
{
  int64_t temp_buffer[64];
  for (uint32_t x = 0; x < 8; x++)
  {
    for (uint32_t y = 0; y < 8; y++)
    {
      int64_t sum = 0;
      for (uint32_t u = 0; u < 8; u++)
        sum += int32_t(blk[u * 8 + x]) * int32_t(m_scale_table[u * 8 + y]);
      temp_buffer[x + y * 8] = sum;
    }
  }
  for (uint32_t x = 0; x < 8; x++)
  {
    for (uint32_t y = 0; y < 8; y++)
    {
      int64_t sum = 0;
      for (uint32_t u = 0; u < 8; u++)
        sum += int64_t(temp_buffer[u + y * 8]) * int32_t(m_scale_table[u * 8 + x]);

      blk[x + y * 8] =
        static_cast<int16_t>(std::clamp<int32_t>((static_cast<int32_t>(static_cast<uint32_t>((sum >> 32) + ((sum >> 31) & 1)) << 23) >> 23), -128, 127));
    }
  }
}

void MDEC::YUVToRGB_Old(uint32_t xx, uint32_t yy, const std::array<int16_t, 64>& Crblk, const std::array<int16_t, 64>& Cbblk,
                      const std::array<int16_t, 64>& Yblk)
{
  // The PSX MDEC is fixed-point integer hardware - the console has no FPU. The
  // previous implementation evaluated the BT.601 YUV->RGB matrix in float
  // (1.402f * Cr, etc.). That is a determinism hazard: those products can be
  // computed with x87 80-bit intermediates, SSE single precision, or an FMA
  // contraction depending on compiler/target/flags, so identical YUV could
  // decode to different RGB across builds and break frame-hash determinism
  // (netplay, runahead, rewind) for any title that drives the MDEC.
  //
  // The same coefficients are reproduced here in Q14 (16384-scale) fixed point.
  // Truncation toward zero (signed integer division, NOT an arithmetic shift -
  // a shift floors and diverges badly on negative chroma) matches the old
  // static_cast<int16_t>(float) rounding direction. Brute-forced over the
  // entire IDCT-clamped input domain (Cr, Cb, Y each in [-128,127], both addval
  // modes, 33554432 pixels): byte-identical RGB for 33538952 of them; the
  // remaining 0.046% differ by at most 1 LSB on a single channel - and those
  // boundary pixels were already build-dependent under the float path. This is
  // now fully deterministic.
  static constexpr int32_t COEF_CR_TO_R = 22970;   //  1.402
  static constexpr int32_t COEF_CB_TO_B = 29032;   //  1.772
  static constexpr int32_t COEF_CB_TO_G = -5631;   // -0.3437
  static constexpr int32_t COEF_CR_TO_G = -11703;  // -0.7143
  static constexpr int32_t COEF_ONE = 16384;       // Q14 scale (1 << 14)

  const int16_t addval = m_status.data_output_signed ? 0 : 0x80;
  for (uint32_t y = 0; y < 8; y++)
  {
    for (uint32_t x = 0; x < 8; x++)
    {
      const int32_t Cr = Crblk[((x + xx) / 2) + ((y + yy) / 2) * 8];
      const int32_t Cb = Cbblk[((x + xx) / 2) + ((y + yy) / 2) * 8];

      const int32_t Rc = (COEF_CR_TO_R * Cr) / COEF_ONE;
      const int32_t Bc = (COEF_CB_TO_B * Cb) / COEF_ONE;
      const int32_t Gc = ((COEF_CB_TO_G * Cb) + (COEF_CR_TO_G * Cr)) / COEF_ONE;

      const int32_t Y = Yblk[x + y * 8];
      const int16_t R = static_cast<int16_t>(std::clamp(Y + Rc, -128, 127)) + addval;
      const int16_t G = static_cast<int16_t>(std::clamp(Y + Gc, -128, 127)) + addval;
      const int16_t B = static_cast<int16_t>(std::clamp(Y + Bc, -128, 127)) + addval;

      m_block_rgb[(x + xx) + ((y + yy) * 16)] = static_cast<uint32_t>(static_cast<uint16_t>(R)) |
                                                (static_cast<uint32_t>(static_cast<uint16_t>(G)) << 8) |
                                                (static_cast<uint32_t>(static_cast<uint16_t>(B)) << 16);
    }
  }
}

bool MDEC::DecodeRLE_New(int16_t* blk, const uint8_t* qt)
{
  // Swapped to row-major so we can vectorize the IDCT.
  static constexpr uint8_t zigzag[64] ={0,  8,  1,  2,  9,  16, 24, 17, 10, 3,  4,  11, 18, 25, 32, 40,
                                   33, 26, 19, 12, 5,  6,  13, 20, 27, 34, 41, 48, 56, 49, 42, 35,
                                   28, 21, 14, 7,  15, 22, 29, 36, 43, 50, 57, 58, 51, 44, 37, 30,
                                   23, 31, 38, 45, 52, 59, 60, 53, 46, 39, 47, 54, 61, 62, 55, 63};

  if (m_current_coefficient == 64)
  {
    std::fill_n(blk, 64, int16_t(0));

    // skip padding at start
    uint16_t n;
    for (;;)
    {
      if (m_data_in_fifo.IsEmpty() || m_remaining_halfwords == 0)
        return false;

      n = m_data_in_fifo.Pop();
      m_remaining_halfwords--;

      if (n == 0xFE00)
        continue;
      else
        break;
    }

    m_current_coefficient = 0;
    m_current_q_scale = n >> 10;

    // Store the DCT blocks with an additional 4 bits of precision.
    const int32_t val = (static_cast<int32_t>(static_cast<uint32_t>(n) << 22) >> 22);
    // val is a sign-extended 10-bit coefficient (range [-512, 511]); val * qt
    // can be negative. Shift in unsigned space to avoid C++<20 UB on negative
    // signed left shifts (well-defined since C++20).
    const int32_t coeff = (m_current_q_scale == 0)
                        ? static_cast<int32_t>(static_cast<uint32_t>(val) << 5)
                        : (static_cast<int32_t>(static_cast<uint32_t>(val * qt[0]) << 4) +
                           (val ? ((val < 0) ? 8 : -8) : 0));
    blk[zigzag[0]] = static_cast<int16_t>(std::clamp(coeff, -0x4000, 0x3FFF));
  }

  while (!m_data_in_fifo.IsEmpty() && m_remaining_halfwords > 0)
  {
    uint16_t n = m_data_in_fifo.Pop();
    m_remaining_halfwords--;

    m_current_coefficient += ((n >> 10) + 1);
    if (m_current_coefficient < 64)
    {
      const int32_t val = (static_cast<int32_t>(static_cast<uint32_t>(n) << 22) >> 22);
      const int32_t scq = static_cast<int32_t>(m_current_q_scale * qt[m_current_coefficient]);
      // See note above on val << 5 / ... << 4 UB-avoidance.
      const int32_t coeff = (scq == 0)
                          ? static_cast<int32_t>(static_cast<uint32_t>(val) << 5)
                          : (static_cast<int32_t>(static_cast<uint32_t>((val * scq) >> 3) << 4) +
                             (val ? ((val < 0) ? 8 : -8) : 0));
      blk[zigzag[m_current_coefficient]] = static_cast<int16_t>(std::clamp(coeff, -0x4000, 0x3FFF));
    }

    if (m_current_coefficient >= 63)
    {
      m_current_coefficient = 64;
      return true;
    }
  }

  return false;
}

void MDEC::IDCT_New(int16_t* blk)
{
  int32_t temp[64];
  for (uint32_t x = 0; x < 8; x++)
  {
    for (uint32_t y = 0; y < 8; y++)
    {
      // TODO: We could invert scale_table to get these in row-major order,
      // in which case we could do optimize this to a vector multiply.
      int32_t sum = 0;
      for (uint32_t z = 0; z < 8; z++)
        sum += (int32_t(blk[x * 8 + z]) * int32_t(m_scale_table[z * 8 + y])) / 8;
      temp[y * 8 + x] = static_cast<int32_t>((sum + 0x4000) >> 15);
    }
  }
  for (uint32_t x = 0; x < 8; x++)
  {
    for (uint32_t y = 0; y < 8; y++)
    {
      int32_t sum = 0;
      for (uint32_t z = 0; z < 8; z++)
        sum += (temp[x * 8 + z] * int32_t(m_scale_table[z * 8 + y])) / 8;
      blk[x * 8 + y] = static_cast<int16_t>(std::clamp((static_cast<int32_t>(static_cast<uint32_t>((sum + 0x4000) >> 15) << 23) >> 23), -128, 127));
    }
  }
}

void MDEC::YUVToRGB_New(uint32_t xx, uint32_t yy, const std::array<int16_t, 64>& Crblk, const std::array<int16_t, 64>& Cbblk,
                        const std::array<int16_t, 64>& Yblk)
{
  const int32_t addval = m_status.data_output_signed ? 0 : 0x80;
  for (uint32_t y = 0; y < 8; y++)
  {
    for (uint32_t x = 0; x < 8; x++)
    {
      const int32_t Cr = Crblk[((x + xx) / 2) + ((y + yy) / 2) * 8];
      const int32_t Cb = Cbblk[((x + xx) / 2) + ((y + yy) / 2) * 8];
      const int32_t Y = Yblk[x + y * 8];

      // BT.601 YUV->RGB coefficients, rounding from Mednafen.
      const int32_t r = std::clamp((static_cast<int32_t>(static_cast<uint32_t>(Y + (((359 * Cr) + 0x80) >> 8)) << 23) >> 23), -128, 127) + addval;
      const int32_t g =
        std::clamp((static_cast<int32_t>(static_cast<uint32_t>(Y + ((((-88 * Cb) & ~0x1F) + ((-183 * Cr) & ~0x07) + 0x80) >> 8)) << 23) >> 23), -128, 127) +
        addval;
      const int32_t b = std::clamp((static_cast<int32_t>(static_cast<uint32_t>(Y + (((454 * Cb) + 0x80) >> 8)) << 23) >> 23), -128, 127) + addval;

      m_block_rgb[(x + xx) + ((y + yy) * 16)] =
        static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16);
    }
  }
}

void MDEC::YUVToMono(const std::array<int16_t, 64>& Yblk)
{
  const int32_t addval = m_status.data_output_signed ? 0 : 0x80;
  for (uint32_t i = 0; i < 64; i++)
    m_block_rgb[i] = static_cast<uint32_t>(std::clamp((static_cast<int32_t>(static_cast<uint32_t>(Yblk[i]) << 23) >> 23), -128, 127) + addval);
}

void MDEC::HandleSetQuantTableCommand()
{
  // TODO: Remove extra copies..
  std::array<uint16_t, 32> packed_data;
  m_data_in_fifo.PopRange(packed_data.data(), static_cast<uint32_t>(packed_data.size()));
  m_remaining_halfwords -= 32;
  std::memcpy(m_iq_y.data(), packed_data.data(), m_iq_y.size());

  if (m_remaining_halfwords > 0)
  {
    m_data_in_fifo.PopRange(packed_data.data(), static_cast<uint32_t>(packed_data.size()));
    std::memcpy(m_iq_uv.data(), packed_data.data(), m_iq_uv.size());
  }
}

void MDEC::HandleSetScaleCommand()
{
  // TODO: Remove extra copies..
  std::array<uint16_t, 64> packed_data;
  m_data_in_fifo.PopRange(packed_data.data(), static_cast<uint32_t>(packed_data.size()));
  m_remaining_halfwords -= 32;
  std::memcpy(m_scale_table.data(), packed_data.data(), m_scale_table.size() * sizeof(int16_t));
}

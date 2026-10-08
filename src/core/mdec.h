#pragma once
#include "common/bitfield.h"
#include "common/fifo_queue.h"
#include "types.h"
#include <array>
#include <memory>

class StateWrapper;

class TimingEvent;

class MDEC
{
public:
  MDEC();
  ~MDEC();

  void Initialize();
  void Shutdown();
  void Reset();
  bool DoState(StateWrapper& sw);

  // I/O
  uint32_t ReadRegister(uint32_t offset);
  void WriteRegister(uint32_t offset, uint32_t value);

  void DMARead(uint32_t* words, uint32_t word_count);
  void DMAWrite(const uint32_t* words, uint32_t word_count);

private:
  static constexpr uint32_t DATA_IN_FIFO_SIZE = 1024;
  static constexpr uint32_t DATA_OUT_FIFO_SIZE = 768;
  static constexpr uint32_t NUM_BLOCKS = 6;
  static constexpr TickCount TICKS_PER_BLOCK = 448;

  enum DataOutputDepth : uint8_t
  {
    DataOutputDepth_4Bit = 0,
    DataOutputDepth_8Bit = 1,
    DataOutputDepth_24Bit = 2,
    DataOutputDepth_15Bit = 3
  };

  enum class Command : uint8_t
  {
    None = 0,
    DecodeMacroblock = 1,
    SetIqTab = 2,
    SetScale = 3
  };

  enum class State : uint8_t
  {
    Idle,
    DecodingMacroblock,
    WritingMacroblock,
    SetIqTable,
    SetScaleTable,
    NoCommand
  };

  union StatusRegister
  {
    uint32_t bits;

    BitField<uint32_t, bool, 31, 1> data_out_fifo_empty;
    BitField<uint32_t, bool, 30, 1> data_in_fifo_full;
    BitField<uint32_t, bool, 29, 1> command_busy;
    BitField<uint32_t, bool, 28, 1> data_in_request;
    BitField<uint32_t, bool, 27, 1> data_out_request;
    BitField<uint32_t, DataOutputDepth, 25, 2> data_output_depth;
    BitField<uint32_t, bool, 24, 1> data_output_signed;
    BitField<uint32_t, uint8_t, 23, 1> data_output_bit15;
    BitField<uint32_t, uint8_t, 16, 3> current_block;
    BitField<uint32_t, uint16_t, 0, 16> parameter_words_remaining;
  };

  union ControlRegister
  {
    uint32_t bits;
    BitField<uint32_t, bool, 31, 1> reset;
    BitField<uint32_t, bool, 30, 1> enable_dma_in;
    BitField<uint32_t, bool, 29, 1> enable_dma_out;
  };

  union CommandWord
  {
    uint32_t bits;

    BitField<uint32_t, Command, 29, 3> command;
    BitField<uint32_t, DataOutputDepth, 27, 2> data_output_depth;
    BitField<uint32_t, bool, 26, 1> data_output_signed;
    BitField<uint32_t, uint8_t, 25, 1> data_output_bit15;
    BitField<uint32_t, uint16_t, 0, 16> parameter_word_count;
  };

  bool HasPendingBlockCopyOut() const;

  void SoftReset();
  void ResetDecoder();
  void UpdateStatus();

  uint32_t ReadDataRegister();
  void WriteCommandRegister(uint32_t value);
  void Execute();

  bool HandleDecodeMacroblockCommand();
  void HandleSetQuantTableCommand();
  void HandleSetScaleCommand();

  bool DecodeMonoMacroblock();
  bool DecodeColoredMacroblock();
  void ScheduleBlockCopyOut(TickCount ticks);
  void CopyOutBlock();

  bool DecodeRLE_Old(int16_t* blk, const uint8_t* qt);
  void IDCT_Old(int16_t* blk);
  void YUVToRGB_Old(uint32_t xx, uint32_t yy, const std::array<int16_t, 64>& Crblk, const std::array<int16_t, 64>& Cbblk,
                         const std::array<int16_t, 64>& Yblk);

  bool DecodeRLE_New(int16_t* blk, const uint8_t* qt);
  void IDCT_New(int16_t* blk);
  void YUVToRGB_New(uint32_t xx, uint32_t yy, const std::array<int16_t, 64>& Crblk, const std::array<int16_t, 64>& Cbblk,
                         const std::array<int16_t, 64>& Yblk);

  void YUVToMono(const std::array<int16_t, 64>& Yblk);

  StatusRegister m_status = {};
  bool m_enable_dma_in = false;
  bool m_enable_dma_out = false;

  // Even though the DMA is in words, we access the FIFO as halfwords.
  InlineFIFOQueue<uint16_t, DATA_IN_FIFO_SIZE / sizeof(uint16_t)> m_data_in_fifo;
  InlineFIFOQueue<uint32_t, DATA_OUT_FIFO_SIZE / sizeof(uint32_t)> m_data_out_fifo;
  State m_state = State::Idle;
  uint32_t m_remaining_halfwords = 0;

  std::array<uint8_t, 64> m_iq_uv{};
  std::array<uint8_t, 64> m_iq_y{};

  std::array<int16_t, 64> m_scale_table{};

  // blocks, for colour: 0 - Crblk, 1 - Cbblk, 2-5 - Y 1-4
  std::array<std::array<int16_t, 64>, NUM_BLOCKS> m_blocks;
  uint32_t m_current_block = 0;        // block (0-5)
  uint32_t m_current_coefficient = 64; // k (in block)
  uint16_t m_current_q_scale = 0;

  std::array<uint32_t, 256> m_block_rgb{};
  std::unique_ptr<TimingEvent> m_block_copy_out_event;

  uint32_t m_total_blocks_decoded = 0;
};

extern MDEC g_mdec;

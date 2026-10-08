#pragma once
#include "common/bitfield.h"
#include "types.h"
#include <array>
#include <memory>
#include <vector>

class StateWrapper;

class TimingEvent;

class DMA
{
public:
  static constexpr uint32_t NUM_CHANNELS = 7;

  enum class Channel : uint32_t
  {
    MDECin = 0,
    MDECout = 1,
    GPU = 2,
    CDROM = 3,
    SPU = 4,
    PIO = 5,
    OTC = 6
  };

  DMA();
  ~DMA();

  void Initialize();
  void Shutdown();
  void Reset();
  bool DoState(StateWrapper& sw);

  uint32_t ReadRegister(uint32_t offset);
  void WriteRegister(uint32_t offset, uint32_t value);

  void SetRequest(Channel channel, bool request);

  // changing interfaces
  void SetMaxSliceTicks(TickCount ticks) { m_max_slice_ticks = ticks; }
  void SetHaltTicks(TickCount ticks) { m_halt_ticks = ticks; }

private:
  static constexpr PhysicalMemoryAddress BASE_ADDRESS_MASK = UINT32_C(0x00FFFFFF);
  static constexpr PhysicalMemoryAddress ADDRESS_MASK = UINT32_C(0x001FFFFC);

  enum class SyncMode : uint32_t
  {
    Manual = 0,
    Request = 1,
    LinkedList = 2,
    Reserved = 3
  };

  void ClearState();

  // is everything enabled for a channel to operate?
  bool CanTransferChannel(Channel channel, bool ignore_halt) const;
  bool IsTransferHalted() const;
  void UpdateIRQ();

  // returns false if the DMA should now be halted
  TickCount GetTransferSliceTicks() const;
  TickCount GetTransferHaltTicks() const;
  bool TransferChannel(Channel channel);
  bool DoTransferChannel(Channel channel);
  void HaltTransfer(TickCount duration);
  void UnhaltTransfer(TickCount ticks);

  // from device -> memory
  TickCount TransferDeviceToMemory(Channel channel, uint32_t address, uint32_t increment, uint32_t word_count);

  // from memory -> device
  TickCount TransferMemoryToDevice(Channel channel, uint32_t address, uint32_t increment, uint32_t word_count);

  // configuration
  TickCount m_max_slice_ticks = 1000;
  TickCount m_halt_ticks = 100;

  std::vector<uint32_t> m_transfer_buffer;
  std::unique_ptr<TimingEvent> m_unhalt_event;
  TickCount m_halt_ticks_remaining = 0;

  /* A device can raise a request from inside a transfer; that transfer runs after the current one. */
  bool m_transfer_in_progress = false;
  bool m_transfer_deferred = false;

  struct ChannelState
  {
    uint32_t base_address = 0;

    union BlockControl
    {
      uint32_t bits;
      union
      {
        BitField<uint32_t, uint32_t, 0, 16> word_count;

        uint32_t GetWordCount() const { return (word_count == 0) ? 0x10000 : word_count; }
      } manual;
      union
      {
        BitField<uint32_t, uint32_t, 0, 16> block_size;
        BitField<uint32_t, uint32_t, 16, 16> block_count;

        uint32_t GetBlockSize() const { return (block_size == 0) ? 0x10000 : block_size; }
        uint32_t GetBlockCount() const { return (block_count == 0) ? 0x10000 : block_count; }
      } request;
    } block_control = {};

    union ChannelControl
    {
      uint32_t bits;
      BitField<uint32_t, bool, 0, 1> copy_to_device;
      BitField<uint32_t, bool, 1, 1> address_step_reverse;
      BitField<uint32_t, bool, 8, 1> chopping_enable;
      BitField<uint32_t, SyncMode, 9, 2> sync_mode;
      BitField<uint32_t, uint32_t, 16, 3> chopping_dma_window_size;
      BitField<uint32_t, uint32_t, 20, 3> chopping_cpu_window_size;
      BitField<uint32_t, bool, 24, 1> enable_busy;
      BitField<uint32_t, bool, 28, 1> start_trigger;

      static constexpr uint32_t WRITE_MASK = 0b01110001'01110111'00000111'00000011;
    } channel_control = {};

    bool request = false;
  };

  std::array<ChannelState, NUM_CHANNELS> m_state;

  union DPCR
  {
    uint32_t bits;

    BitField<uint32_t, uint8_t, 0, 3> MDECin_priority;
    BitField<uint32_t, bool, 3, 1> MDECin_master_enable;
    BitField<uint32_t, uint8_t, 4, 3> MDECout_priority;
    BitField<uint32_t, bool, 7, 1> MDECout_master_enable;
    BitField<uint32_t, uint8_t, 8, 3> GPU_priority;
    BitField<uint32_t, bool, 10, 1> GPU_master_enable;
    BitField<uint32_t, uint8_t, 12, 3> CDROM_priority;
    BitField<uint32_t, bool, 15, 1> CDROM_master_enable;
    BitField<uint32_t, uint8_t, 16, 3> SPU_priority;
    BitField<uint32_t, bool, 19, 1> SPU_master_enable;
    BitField<uint32_t, uint8_t, 20, 3> PIO_priority;
    BitField<uint32_t, bool, 23, 1> PIO_master_enable;
    BitField<uint32_t, uint8_t, 24, 3> OTC_priority;
    BitField<uint32_t, bool, 27, 1> OTC_master_enable;
    BitField<uint32_t, uint8_t, 28, 3> priority_offset;
    BitField<uint32_t, bool, 31, 1> unused;

    bool GetMasterEnable(Channel channel) const
    {
      return static_cast<bool>((bits >> (static_cast<uint8_t>(channel) * 4 + 3)) & uint32_t(1));
    }
  } m_DPCR = {};

  static constexpr uint32_t DICR_WRITE_MASK = 0b00000000'11111111'10000000'00111111;
  static constexpr uint32_t DICR_RESET_MASK = 0b01111111'00000000'00000000'00000000;
  union DICR
  {
    uint32_t bits;

    BitField<uint32_t, bool, 15, 1> force_irq;
    BitField<uint32_t, bool, 16, 1> MDECin_irq_enable;
    BitField<uint32_t, bool, 17, 1> MDECout_irq_enable;
    BitField<uint32_t, bool, 18, 1> GPU_irq_enable;
    BitField<uint32_t, bool, 19, 1> CDROM_irq_enable;
    BitField<uint32_t, bool, 20, 1> SPU_irq_enable;
    BitField<uint32_t, bool, 21, 1> PIO_irq_enable;
    BitField<uint32_t, bool, 22, 1> OTC_irq_enable;
    BitField<uint32_t, bool, 23, 1> master_enable;
    BitField<uint32_t, bool, 24, 1> MDECin_irq_flag;
    BitField<uint32_t, bool, 25, 1> MDECout_irq_flag;
    BitField<uint32_t, bool, 26, 1> GPU_irq_flag;
    BitField<uint32_t, bool, 27, 1> CDROM_irq_flag;
    BitField<uint32_t, bool, 28, 1> SPU_irq_flag;
    BitField<uint32_t, bool, 29, 1> PIO_irq_flag;
    BitField<uint32_t, bool, 30, 1> OTC_irq_flag;
    BitField<uint32_t, bool, 31, 1> master_flag;

    bool IsIRQEnabled(Channel channel) const
    {
      return static_cast<bool>((bits >> (static_cast<uint8_t>(channel) + 16)) & uint32_t(1));
    }

    void SetIRQFlag(Channel channel) { bits |= (uint32_t(1) << (static_cast<uint8_t>(channel) + 24)); }

    void UpdateMasterFlag()
    {
      master_flag = master_enable && ((((bits >> 16) & uint32_t(0b1111111)) & ((bits >> 24) & uint32_t(0b1111111))) != 0);
    }
  } m_DICR = {};
};

extern DMA g_dma;

#pragma once
#include "common/bitfield.h"
#include "common/fifo_queue.h"
#include "system.h"
#include "types.h"
#include <array>
#include <memory>

class StateWrapper;

namespace Common {
}

class TimingEvent;
class LibretroAudioStream;

class SPU
{
public:
  static constexpr uint32_t RAM_SIZE = 512 * 1024, RAM_MASK = RAM_SIZE - 1;

  SPU();
  ~SPU();

  void Initialize();
  void CPUClockChanged();
  void Shutdown();
  void Reset();
  bool DoState(StateWrapper& sw);

  uint16_t ReadRegister(uint32_t offset);
  void WriteRegister(uint32_t offset, uint16_t value);

  void DMARead(uint32_t* words, uint32_t word_count);
  void DMAWrite(const uint32_t* words, uint32_t word_count);

  // Executes the SPU, generating any pending samples.
  void GeneratePendingSamples();

  /// Change output stream - used for runahead.
  ALWAYS_INLINE void SetAudioStream(LibretroAudioStream* stream) { m_audio_stream = stream; }

private:
  static constexpr uint32_t SPU_BASE = 0x1F801C00;
  static constexpr uint32_t NUM_VOICES = 24;
  static constexpr uint32_t NUM_VOICE_REGISTERS = 8;
  static constexpr uint32_t VOICE_ADDRESS_SHIFT = 3;
  static constexpr uint32_t NUM_SAMPLES_PER_ADPCM_BLOCK = 28;
  static constexpr uint32_t NUM_SAMPLES_FROM_LAST_ADPCM_BLOCK = 3;
  static constexpr uint32_t SAMPLE_RATE = 44100;
  static constexpr uint32_t SYSCLK_TICKS_PER_SPU_TICK = System::MASTER_CLOCK / SAMPLE_RATE; // 0x300
  static constexpr int16_t ENVELOPE_MIN_VOLUME = 0;
  static constexpr int16_t ENVELOPE_MAX_VOLUME = 0x7FFF;
  static constexpr uint32_t CAPTURE_BUFFER_SIZE_PER_CHANNEL = 0x400;
  static constexpr uint32_t MINIMUM_TICKS_BETWEEN_KEY_ON_OFF = 2;
  static constexpr uint32_t NUM_REVERB_REGS = 32;
  static constexpr uint32_t FIFO_SIZE_IN_HALFWORDS = 32;
  static constexpr TickCount TRANSFER_TICKS_PER_HALFWORD = 16;

  enum class RAMTransferMode : uint8_t
  {
    Stopped = 0,
    ManualWrite = 1,
    DMAWrite = 2,
    DMARead = 3
  };

  union SPUCNT
  {
    uint16_t bits;

    BitField<uint16_t, bool, 15, 1> enable;
    BitField<uint16_t, bool, 14, 1> mute_n;
    BitField<uint16_t, uint8_t, 8, 6> noise_clock;
    BitField<uint16_t, bool, 7, 1> reverb_master_enable;
    BitField<uint16_t, bool, 6, 1> irq9_enable;
    BitField<uint16_t, RAMTransferMode, 4, 2> ram_transfer_mode;
    BitField<uint16_t, bool, 3, 1> external_audio_reverb;
    BitField<uint16_t, bool, 2, 1> cd_audio_reverb;
    BitField<uint16_t, bool, 1, 1> external_audio_enable;
    BitField<uint16_t, bool, 0, 1> cd_audio_enable;

    BitField<uint16_t, uint8_t, 0, 6> mode;
  };

  union SPUSTAT
  {
    uint16_t bits;

    BitField<uint16_t, bool, 11, 1> second_half_capture_buffer;
    BitField<uint16_t, bool, 10, 1> transfer_busy;
    BitField<uint16_t, bool, 9, 1> dma_write_request;
    BitField<uint16_t, bool, 8, 1> dma_read_request;
    BitField<uint16_t, bool, 7, 1> dma_request;
    BitField<uint16_t, bool, 6, 1> irq9_flag;
    BitField<uint16_t, uint8_t, 0, 6> mode;
  };

  union TransferControl
  {
    uint16_t bits;

    BitField<uint8_t, uint8_t, 1, 3> mode;
  };

  union ADSRRegister
  {
    uint32_t bits;
    struct
    {
      uint16_t bits_low;
      uint16_t bits_high;
    };

    BitField<uint32_t, uint8_t, 0, 4> sustain_level;
    BitField<uint32_t, uint8_t, 4, 4> decay_rate_shr2;
    BitField<uint32_t, uint8_t, 8, 7> attack_rate;
    BitField<uint32_t, bool, 15, 1> attack_exponential;

    BitField<uint32_t, uint8_t, 16, 5> release_rate_shr2;
    BitField<uint32_t, bool, 21, 1> release_exponential;
    BitField<uint32_t, uint8_t, 22, 7> sustain_rate;
    BitField<uint32_t, bool, 30, 1> sustain_direction_decrease;
    BitField<uint32_t, bool, 31, 1> sustain_exponential;
  };

  union VolumeRegister
  {
    uint16_t bits;

    BitField<uint16_t, bool, 15, 1> sweep_mode;
    BitField<uint16_t, int16_t, 0, 15> fixed_volume_shr1; // divided by 2

    BitField<uint16_t, bool, 14, 1> sweep_exponential;
    BitField<uint16_t, bool, 13, 1> sweep_direction_decrease;
    BitField<uint16_t, bool, 12, 1> sweep_phase_negative;
    BitField<uint16_t, uint8_t, 0, 7> sweep_rate;
  };

  // organized so we can replace this with a uint16_t array in the future
  union VoiceRegisters
  {
    uint16_t index[NUM_VOICE_REGISTERS];

    struct
    {
      VolumeRegister volume_left;
      VolumeRegister volume_right;

      uint16_t adpcm_sample_rate;   // VxPitch
      uint16_t adpcm_start_address; // multiply by 8

      ADSRRegister adsr;
      int16_t adsr_volume;

      uint16_t adpcm_repeat_address; // multiply by 8
    };
  };

  union VoiceCounter
  {
    // promoted to uint32_t because of overflow
    uint32_t bits;

    BitField<uint32_t, uint8_t, 4, 8> interpolation_index;
    BitField<uint32_t, uint8_t, 12, 5> sample_index;
  };

  union ADPCMFlags
  {
    uint8_t bits;

    BitField<uint8_t, bool, 0, 1> loop_end;
    BitField<uint8_t, bool, 1, 1> loop_repeat;
    BitField<uint8_t, bool, 2, 1> loop_start;
  };

  struct ADPCMBlock
  {
    union
    {
      uint8_t bits;

      BitField<uint8_t, uint8_t, 0, 4> shift;
      BitField<uint8_t, uint8_t, 4, 3> filter;
    } shift_filter;
    ADPCMFlags flags;
    uint8_t data[NUM_SAMPLES_PER_ADPCM_BLOCK / 2];

    // For both 4bit and 8bit ADPCM, reserved shift values 13..15 will act same as shift=9).
    uint8_t GetShift() const
    {
      const uint8_t shift = shift_filter.shift;
      return (shift > 12) ? 9 : shift;
    }

    uint8_t GetFilter() const { return std::min<uint8_t>(shift_filter.filter, 4); }

    uint8_t GetNibble(uint32_t index) const { return (data[index / 2] >> ((index % 2) * 4)) & 0x0F; }
  };

  struct VolumeEnvelope
  {
    int32_t counter;
    uint8_t rate;
    bool decreasing;
    bool exponential;

    void Reset(uint8_t rate_, bool decreasing_, bool exponential_);
    int16_t Tick(int16_t current_level);
    bool IsValid() const;
  };

  struct VolumeSweep
  {
    VolumeEnvelope envelope;
    bool envelope_active;
    int16_t current_level;

    void Reset(VolumeRegister reg);
    void Tick();
    bool IsValid() const;
  };

  enum class ADSRPhase : uint8_t
  {
    Off = 0,
    Attack = 1,
    Decay = 2,
    Sustain = 3,
    Release = 4
  };

  struct Voice
  {
    uint16_t current_address;
    VoiceRegisters regs;
    VoiceCounter counter;
    ADPCMFlags current_block_flags;
    bool is_first_block;
    std::array<int16_t, NUM_SAMPLES_FROM_LAST_ADPCM_BLOCK + NUM_SAMPLES_PER_ADPCM_BLOCK> current_block_samples;
    std::array<int16_t, 2> adpcm_last_samples;
    int32_t last_volume;

    VolumeSweep left_volume;
    VolumeSweep right_volume;

    VolumeEnvelope adsr_envelope;
    ADSRPhase adsr_phase;
    int16_t adsr_target;
    bool has_samples;
    bool ignore_loop_address;

    bool IsOn() const { return adsr_phase != ADSRPhase::Off; }

    void KeyOn();
    void KeyOff();
    void ForceOff();

    void DecodeBlock(const ADPCMBlock& block);
    int32_t Interpolate() const;

    // Switches to the specified phase, filling in target.
    void UpdateADSREnvelope();

    // Updates the ADSR volume/phase.
    void TickADSR();
  };

  struct ReverbRegisters
  {
    int16_t vLOUT;
    int16_t vROUT;
    uint16_t mBASE;

    union
    {
      struct
      {
        uint16_t FB_SRC_A;
        uint16_t FB_SRC_B;
        int16_t IIR_ALPHA;
        int16_t ACC_COEF_A;
        int16_t ACC_COEF_B;
        int16_t ACC_COEF_C;
        int16_t ACC_COEF_D;
        int16_t IIR_COEF;
        int16_t FB_ALPHA;
        int16_t FB_X;
        uint16_t IIR_DEST_A[2];
        uint16_t ACC_SRC_A[2];
        uint16_t ACC_SRC_B[2];
        uint16_t IIR_SRC_A[2];
        uint16_t IIR_DEST_B[2];
        uint16_t ACC_SRC_C[2];
        uint16_t ACC_SRC_D[2];
        uint16_t IIR_SRC_B[2];
        uint16_t MIX_DEST_A[2];
        uint16_t MIX_DEST_B[2];
        int16_t IN_COEF[2];
      };

      uint16_t rev[NUM_REVERB_REGS];
    };
  };

  static constexpr int32_t Clamp16(int32_t value) { return (value < -0x8000) ? -0x8000 : (value > 0x7FFF) ? 0x7FFF : value; }

  static constexpr int32_t ApplyVolume(int32_t sample, int16_t volume) { return (sample * int32_t(volume)) >> 15; }

  static ADSRPhase GetNextADSRPhase(ADSRPhase phase);

  ALWAYS_INLINE bool IsVoiceReverbEnabled(uint32_t i) const
  {
    return static_cast<bool>((m_reverb_on_register >> i) & uint32_t(1));
  }
  ALWAYS_INLINE bool IsVoiceNoiseEnabled(uint32_t i) const
  {
    return static_cast<bool>((m_noise_mode_register >> i) & uint32_t(1));
  }
  ALWAYS_INLINE bool IsPitchModulationEnabled(uint32_t i) const
  {
    return ((i > 0) && static_cast<bool>((m_pitch_modulation_enable_register >> i) & uint32_t(1)));
  }
  ALWAYS_INLINE int16_t GetVoiceNoiseLevel() const { return static_cast<int16_t>(static_cast<uint16_t>(m_noise_level)); }

  uint16_t ReadVoiceRegister(uint32_t offset);
  void WriteVoiceRegister(uint32_t offset, uint16_t value);

  ALWAYS_INLINE bool IsRAMIRQTriggerable() const { return m_SPUCNT.irq9_enable && !m_SPUSTAT.irq9_flag; }
  ALWAYS_INLINE bool CheckRAMIRQ(uint32_t address) const { return ((static_cast<uint32_t>(m_irq_address) * 8) == address); }
  void CheckForLateRAMIRQs();

  void WriteToCaptureBuffer(uint32_t index, int16_t value);
  void IncrementCaptureBufferPosition();

  void ReadADPCMBlock(uint16_t address, ADPCMBlock* block);
  std::tuple<int32_t, int32_t> SampleVoice(uint32_t voice_index);

  void UpdateNoise();

  uint32_t ReverbMemoryAddress(uint32_t address) const;
  int16_t ReverbRead(uint32_t address, int32_t offset = 0);
  void ReverbWrite(uint32_t address, int16_t data);
  void ProcessReverb(int16_t left_in, int16_t right_in, int32_t* left_out, int32_t* right_out);

  void Execute(TickCount ticks);
  void UpdateEventInterval();

  void ExecuteFIFOWriteToRAM(TickCount& ticks);
  void ExecuteFIFOReadFromRAM(TickCount& ticks);
  void ExecuteTransfer(TickCount ticks);
  void ManualTransferWrite(uint16_t value);
  void UpdateTransferEvent();
  void UpdateDMARequest();
  void UpdateDMARequestInTransfer(TickCount* ticks);

  std::unique_ptr<TimingEvent> m_tick_event;
  std::unique_ptr<TimingEvent> m_transfer_event;
  LibretroAudioStream* m_audio_stream = nullptr;
  TickCount m_ticks_carry = 0;
  TickCount m_cpu_ticks_per_spu_tick = 0;
  TickCount m_cpu_tick_divider = 0;

  SPUCNT m_SPUCNT = {};
  SPUSTAT m_SPUSTAT = {};

  TransferControl m_transfer_control = {};
  uint16_t m_transfer_address_reg = 0;
  uint32_t m_transfer_address = 0;

  uint16_t m_irq_address = 0;
  uint16_t m_capture_buffer_position = 0;

  VolumeRegister m_main_volume_left_reg = {};
  VolumeRegister m_main_volume_right_reg = {};
  VolumeSweep m_main_volume_left = {};
  VolumeSweep m_main_volume_right = {};

  int16_t m_cd_audio_volume_left = 0;
  int16_t m_cd_audio_volume_right = 0;

  int16_t m_external_volume_left = 0;
  int16_t m_external_volume_right = 0;

  uint32_t m_key_on_register = 0;
  uint32_t m_key_off_register = 0;
  uint32_t m_endx_register = 0;
  uint32_t m_pitch_modulation_enable_register = 0;

  uint32_t m_noise_mode_register = 0;
  uint32_t m_noise_count = 0;
  uint32_t m_noise_level = 0;

  uint32_t m_reverb_on_register = 0;
  uint32_t m_reverb_base_address = 0;
  uint32_t m_reverb_current_address = 0;
  ReverbRegisters m_reverb_registers{};
  std::array<std::array<int16_t, 128>, 2> m_reverb_downsample_buffer;
  std::array<std::array<int16_t, 64>, 2> m_reverb_upsample_buffer;
  int32_t m_reverb_resample_buffer_position = 0;

  std::array<Voice, NUM_VOICES> m_voices{};

  InlineFIFOQueue<uint16_t, FIFO_SIZE_IN_HALFWORDS> m_transfer_fifo;

  std::array<uint8_t, RAM_SIZE> m_ram{};
};

extern SPU g_spu;

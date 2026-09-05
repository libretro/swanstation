#pragma once
#include "cdrom_async_reader.h"
#include "common/bitfield.h"
#include "common/cd_image.h"
#include "common/cd_xa.h"
#include "common/fifo_queue.h"
#include "common/heap_array.h"
#include "types.h"
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

class StateWrapper;
class TimingEvent;

class CDROM final
{
public:
  CDROM();
  ~CDROM();

  void Initialize();
  void Shutdown();
  void Reset();
  bool DoState(StateWrapper& sw);

  bool HasMedia() const { return m_reader.HasMedia(); }
  const std::string& GetMediaFileName() const { return m_reader.GetMediaFileName(); }
  const CDImage* GetMedia() const { return m_reader.GetMedia(); }

  void InsertMedia(std::unique_ptr<CDImage> media);
  std::unique_ptr<CDImage> RemoveMedia(bool for_disc_swap);
  void SetShellOpen(bool opened) { m_shell_open = opened; }

  void CPUClockChanged();

  // I/O
  uint8_t ReadRegister(uint32_t offset);
  void WriteRegister(uint32_t offset, uint8_t value);
  void DMARead(uint32_t* words, uint32_t word_count);

  void SetReadaheadSectors(uint32_t readahead_sectors);

  /// Reads a frame from the audio FIFO, used by the SPU.
  ALWAYS_INLINE std::tuple<int16_t, int16_t> GetAudioFrame()
  {
    const uint32_t frame = m_audio_fifo.IsEmpty() ? 0u : m_audio_fifo.Pop();
    const int16_t left = static_cast<int16_t>(static_cast<uint16_t>(frame));
    const int16_t right = static_cast<int16_t>(static_cast<uint16_t>(frame >> 16));
    const int16_t left_out = SaturateVolume(ApplyVolume(left, m_cd_audio_volume_matrix[0][0]) +
                                        ApplyVolume(right, m_cd_audio_volume_matrix[1][0]));
    const int16_t right_out = SaturateVolume(ApplyVolume(left, m_cd_audio_volume_matrix[0][1]) +
                                         ApplyVolume(right, m_cd_audio_volume_matrix[1][1]));
    return std::tuple<int16_t, int16_t>(left_out, right_out);
  }

private:
  bool IsMediaPS1Disc() const;
  bool DoesMediaRegionMatchConsole() const;

  static constexpr uint32_t RAW_SECTOR_OUTPUT_SIZE = CDImage::RAW_SECTOR_SIZE - CDImage::SECTOR_SYNC_SIZE,
                       DATA_SECTOR_OUTPUT_SIZE = CDImage::DATA_SECTOR_SIZE,
                       SECTOR_SYNC_SIZE = CDImage::SECTOR_SYNC_SIZE, SECTOR_HEADER_SIZE = CDImage::SECTOR_HEADER_SIZE,
                       XA_RESAMPLE_RING_BUFFER_SIZE = 32, XA_RESAMPLE_ZIGZAG_TABLE_SIZE = 29,
                       XA_RESAMPLE_NUM_ZIGZAG_TABLES = 7,

                       PARAM_FIFO_SIZE = 16, RESPONSE_FIFO_SIZE = 16, DATA_FIFO_SIZE = RAW_SECTOR_OUTPUT_SIZE,
                       NUM_SECTOR_BUFFERS = 8, AUDIO_FIFO_SIZE = 44100 * 2, AUDIO_FIFO_LOW_WATERMARK = 10,

                       RESET_TICKS = 400000, ID_READ_TICKS = 33868, MOTOR_ON_RESPONSE_TICKS = 400000,

                       MAX_FAST_FORWARD_RATE = 12, FAST_FORWARD_RATE_STEP = 4;

  static constexpr uint8_t INTERRUPT_REGISTER_MASK = 0x1F;

  enum class Interrupt : uint8_t
  {
    DataReady = 0x01,
    Complete = 0x02,
    ACK = 0x03,
    DataEnd = 0x04,
    Error = 0x05
  };

  enum class Command : uint16_t
  {
    Sync = 0x00,
    Getstat = 0x01,
    Setloc = 0x02,
    Play = 0x03,
    Forward = 0x04,
    Backward = 0x05,
    ReadN = 0x06,
    MotorOn = 0x07,
    Stop = 0x08,
    Pause = 0x09,
    Reset = 0x0A,
    Mute = 0x0B,
    Demute = 0x0C,
    Setfilter = 0x0D,
    Setmode = 0x0E,
    Getparam = 0x0F,
    GetlocL = 0x10,
    GetlocP = 0x11,
    SetSession = 0x12,
    GetTN = 0x13,
    GetTD = 0x14,
    SeekL = 0x15,
    SeekP = 0x16,
    SetClock = 0x17,
    GetClock = 0x18,
    Test = 0x19,
    GetID = 0x1A,
    ReadS = 0x1B,
    Init = 0x1C,
    GetQ = 0x1D,
    ReadTOC = 0x1E,
    VideoCD = 0x1F,

    None = 0xFFFF
  };

  enum class DriveState : uint8_t
  {
    Idle,
    ShellOpening,
    UNUSED_Resetting,
    SeekingPhysical,
    SeekingLogical,
    UNUSED_ReadingID,
    UNUSED_ReadingTOC,
    Reading,
    Playing,
    UNUSED_Pausing,
    UNUSED_Stopping,
    ChangingSession,
    SpinningUp,
    SeekingImplicit,
    ChangingSpeedOrTOCRead
  };

  union StatusRegister
  {
    uint8_t bits;
    BitField<uint8_t, uint8_t, 0, 2> index;
    BitField<uint8_t, bool, 2, 1> ADPBUSY;
    BitField<uint8_t, bool, 3, 1> PRMEMPTY;
    BitField<uint8_t, bool, 4, 1> PRMWRDY;
    BitField<uint8_t, bool, 5, 1> RSLRRDY;
    BitField<uint8_t, bool, 6, 1> DRQSTS;
    BitField<uint8_t, bool, 7, 1> BUSYSTS;
  };

  static constexpr uint8_t STAT_ERROR = (1 << 0), STAT_MOTOR_ON = (1 << 1), STAT_SEEK_ERROR = (1 << 2),
                      STAT_ID_ERROR = (1 << 3), STAT_SHELL_OPEN = (1 << 4), STAT_READING = (1 << 5),
                      STAT_SEEKING = (1 << 6), STAT_PLAYING_CDDA = (1 << 7);

  static constexpr uint8_t ERROR_REASON_INVALID_ARGUMENT = 0x10, ERROR_REASON_INCORRECT_NUMBER_OF_PARAMETERS = 0x20,
                      ERROR_REASON_INVALID_COMMAND = 0x40, ERROR_REASON_NOT_READY = 0x80;

  union SecondaryStatusRegister
  {
    uint8_t bits;
    BitField<uint8_t, bool, 0, 1> error;
    BitField<uint8_t, bool, 1, 1> motor_on;
    BitField<uint8_t, bool, 2, 1> seek_error;
    BitField<uint8_t, bool, 3, 1> id_error;
    BitField<uint8_t, bool, 4, 1> shell_open;
    BitField<uint8_t, bool, 5, 1> reading;
    BitField<uint8_t, bool, 6, 1> seeking;
    BitField<uint8_t, bool, 7, 1> playing_cdda;

    /// Clears the CDDA/seeking bits.
    ALWAYS_INLINE void ClearActiveBits() { bits &= ~(STAT_SEEKING | STAT_READING | STAT_PLAYING_CDDA); }

    /// Sets the bits for seeking.
    ALWAYS_INLINE void SetSeeking()
    {
      bits = (bits & ~(STAT_READING | STAT_PLAYING_CDDA)) | (STAT_MOTOR_ON | STAT_SEEKING);
    }

    /// Sets the bits for reading/playing.
    ALWAYS_INLINE void SetReadingBits(bool audio)
    {
      bits = (bits & ~(STAT_SEEKING | STAT_READING | STAT_PLAYING_CDDA)) |
             ((audio) ? (STAT_MOTOR_ON | STAT_PLAYING_CDDA) : (STAT_MOTOR_ON | STAT_READING));
    }
  };

  union ModeRegister
  {
    uint8_t bits;
    BitField<uint8_t, bool, 0, 1> cdda;
    BitField<uint8_t, bool, 1, 1> auto_pause;
    BitField<uint8_t, bool, 2, 1> report_audio;
    BitField<uint8_t, bool, 3, 1> xa_filter;
    BitField<uint8_t, bool, 4, 1> ignore_bit;
    BitField<uint8_t, bool, 5, 1> read_raw_sector;
    BitField<uint8_t, bool, 6, 1> xa_enable;
    BitField<uint8_t, bool, 7, 1> double_speed;
  };

  union RequestRegister
  {
    uint8_t bits;
    BitField<uint8_t, bool, 5, 1> SMEN;
    BitField<uint8_t, bool, 6, 1> BFWR;
    BitField<uint8_t, bool, 7, 1> BFRD;
  };

  void SoftReset(TickCount ticks_late);

  ALWAYS_INLINE bool IsDriveIdle() const { return m_drive_state == DriveState::Idle; }
  ALWAYS_INLINE bool IsMotorOn() const { return m_secondary_status.motor_on; }
  ALWAYS_INLINE bool IsSeeking() const
  {
    return (m_drive_state == DriveState::SeekingLogical || m_drive_state == DriveState::SeekingPhysical ||
            m_drive_state == DriveState::SeekingImplicit);
  }
  ALWAYS_INLINE bool IsReadingOrPlaying() const
  {
    return (m_drive_state == DriveState::Reading || m_drive_state == DriveState::Playing);
  }
  ALWAYS_INLINE bool CanReadMedia() const { return (m_drive_state != DriveState::ShellOpening && m_reader.HasMedia()); }
  ALWAYS_INLINE bool HasPendingCommand() const { return m_command != Command::None; }
  ALWAYS_INLINE bool HasPendingInterrupt() const { return m_interrupt_flag_register != 0; }
  ALWAYS_INLINE bool HasPendingAsyncInterrupt() const { return m_pending_async_interrupt != 0; }
  ALWAYS_INLINE void AddCDAudioFrame(int16_t left, int16_t right)
  {
    m_audio_fifo.Push(static_cast<uint32_t>(static_cast<uint16_t>(left)) | (static_cast<uint32_t>(static_cast<uint16_t>(right)) << 16));
  }
  ALWAYS_INLINE static constexpr int32_t ApplyVolume(int16_t sample, uint8_t volume)
  {
    return int32_t(sample) * static_cast<int32_t>(volume) >> 7;
  }

  ALWAYS_INLINE static constexpr int16_t SaturateVolume(int32_t volume)
  {
    return static_cast<int16_t>((volume < -0x8000) ? -0x8000 : ((volume > 0x7FFF) ? 0x7FFF : volume));
  }

  void SetInterrupt(Interrupt interrupt);
  void SetAsyncInterrupt(Interrupt interrupt);
  void ClearAsyncInterrupt();
  void DeliverAsyncInterrupt();
  void SendACKAndStat();
  void SendErrorResponse(uint8_t stat_bits = STAT_ERROR, uint8_t reason = 0x80);
  void SendAsyncErrorResponse(uint8_t stat_bits = STAT_ERROR, uint8_t reason = 0x80);
  void UpdateStatusRegister();
  void UpdateInterruptRequest();
  bool HasPendingDiscEvent() const;

  TickCount GetAckDelayForCommand(Command command);
  TickCount GetTicksForSpinUp();
  TickCount GetTicksForIDRead();
  TickCount GetTicksForRead();
  TickCount GetTicksForSeek(CDImage::LBA new_lba, bool ignore_speed_change = false);
  TickCount GetTicksForStop(bool motor_was_on);
  TickCount GetTicksForSpeedChange();
  TickCount GetTicksForTOCRead();
  CDImage::LBA GetNextSectorToBeRead();
  bool CompleteSeek();

  void BeginCommand(Command command); // also update status register
  void EndCommand();                  // also updates status register
  void AbortCommand();
  void ExecuteCommand(TickCount ticks_late);
  void ExecuteTestCommand(uint8_t subcommand);
  void ExecuteCommandSecondResponse(TickCount ticks_late);
  void QueueCommandSecondResponse(Command command, TickCount ticks);
  void ClearCommandSecondResponse();
  void UpdateCommandEvent();
  void ExecuteDrive(TickCount ticks_late);
  void ClearDriveState();
  void BeginReading(TickCount ticks_late = 0, bool after_seek = false);
  void BeginPlaying(uint8_t track, TickCount ticks_late = 0, bool after_seek = false);
  void DoShellOpenComplete(TickCount ticks_late);
  void DoSeekComplete(TickCount ticks_late);
  void DoStatSecondResponse();
  void DoChangeSessionComplete();
  void DoSpinUpComplete();
  void DoSpeedChangeOrImplicitTOCReadComplete();
  void DoIDRead();
  void DoSectorRead();
  void ProcessDataSectorHeader(const uint8_t* raw_sector);
  void ProcessDataSector(const uint8_t* raw_sector, const CDImage::SubChannelQ& subq);
  void ProcessXAADPCMSector(const uint8_t* raw_sector, const CDImage::SubChannelQ& subq);
  void ProcessCDDASector(const uint8_t* raw_sector, const CDImage::SubChannelQ& subq);
  void StopReadingWithDataEnd();
  void StartMotor();
  void StopMotor();
  void BeginSeeking(bool logical, bool read_after_seek, bool play_after_seek);
  void UpdatePositionWhileSeeking();
  void UpdatePhysicalPosition(bool update_logical);
  void SetHoldPosition(CDImage::LBA lba, bool update_subq);
  void ResetCurrentXAFile();
  void ResetAudioDecoder();
  void LoadDataFIFO();
  void ClearSectorBuffers();

  template<bool STEREO, bool SAMPLE_RATE>
  void ResampleXAADPCM(const int16_t* frames_in, uint32_t num_frames_in);

  std::unique_ptr<TimingEvent> m_command_event;
  std::unique_ptr<TimingEvent> m_command_second_response_event;
  std::unique_ptr<TimingEvent> m_drive_event;

  Command m_command = Command::None;
  Command m_command_second_response = Command::None;
  DriveState m_drive_state = DriveState::Idle;
  DiscRegion m_disc_region = DiscRegion::Other;

  StatusRegister m_status = {};
  SecondaryStatusRegister m_secondary_status = {};
  ModeRegister m_mode = {};

  uint8_t m_interrupt_enable_register = INTERRUPT_REGISTER_MASK;
  uint8_t m_interrupt_flag_register = 0;
  uint8_t m_pending_async_interrupt = 0;

  CDImage::Position m_setloc_position = {};
  CDImage::LBA m_requested_lba{};
  CDImage::LBA m_current_lba{}; // this is the hold position
  CDImage::LBA m_seek_start_lba{};
  CDImage::LBA m_seek_end_lba{};
  CDImage::LBA m_physical_lba{}; // current position of the disc with respect to time
  uint32_t m_physical_lba_update_tick = 0;
  uint32_t m_physical_lba_update_carry = 0;
  bool m_setloc_pending = false;
  bool m_read_after_seek = false;
  bool m_play_after_seek = false;

  bool m_muted = false;
  bool m_adpcm_muted = false;

  uint8_t m_xa_filter_file_number = 0;
  uint8_t m_xa_filter_channel_number = 0;
  uint8_t m_xa_current_file_number = 0;
  uint8_t m_xa_current_channel_number = 0;
  uint8_t m_xa_current_set = false;

  CDImage::SectorHeader m_last_sector_header{};
  CDXA::XASubHeader m_last_sector_subheader{};
  bool m_last_sector_header_valid = false;
  CDImage::SubChannelQ m_last_subq{};
  uint8_t m_last_cdda_report_frame_nibble = 0xFF;
  uint8_t m_play_track_number_bcd = 0xFF;
  uint8_t m_async_command_parameter = 0x00;
  int8_t m_fast_forward_rate = 0;

  std::array<std::array<uint8_t, 2>, 2> m_cd_audio_volume_matrix{};
  std::array<std::array<uint8_t, 2>, 2> m_next_cd_audio_volume_matrix{};

  std::array<int32_t, 4> m_xa_last_samples{};
  std::array<std::array<int16_t, XA_RESAMPLE_RING_BUFFER_SIZE>, 2> m_xa_resample_ring_buffer{};
  uint8_t m_xa_resample_p = 0;
  uint8_t m_xa_resample_sixstep = 6;

  InlineFIFOQueue<uint8_t, PARAM_FIFO_SIZE> m_param_fifo;
  InlineFIFOQueue<uint8_t, RESPONSE_FIFO_SIZE> m_response_fifo;
  InlineFIFOQueue<uint8_t, RESPONSE_FIFO_SIZE> m_async_response_fifo;
  HeapFIFOQueue<uint8_t, DATA_FIFO_SIZE> m_data_fifo;

  struct SectorBuffer
  {
    HeapArray<uint8_t, RAW_SECTOR_OUTPUT_SIZE> data;
    uint32_t size;
  };

  uint32_t m_current_read_sector_buffer = 0;
  uint32_t m_current_write_sector_buffer = 0;
  std::array<SectorBuffer, NUM_SECTOR_BUFFERS> m_sector_buffers;

  CDROMAsyncReader m_reader;
  // The status bit stays latched until Getstat acknowledges a closed shell.
  bool m_shell_open = false;

  // two 16-bit samples packed in 32-bits
  HeapFIFOQueue<uint32_t, AUDIO_FIFO_SIZE> m_audio_fifo;
};

extern CDROM g_cdrom;

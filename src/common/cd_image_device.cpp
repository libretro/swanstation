#include "assert.h"
#include "cd_image.h"
#include "error.h"
#include "string_util.h"
#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cmath>

#define ALL_SUBCODE_SIZE 96

#if defined(_WIN32) && !defined(_UWP)
static constexpr u32 MAX_TRACK_NUMBER = 99;

// Adapted from
// https://github.com/saramibreak/DiscImageCreator/blob/5a8fe21730872d67991211f1319c87f0780f2d0f/DiscImageCreator/convert.cpp
static void DeinterleaveSubcode(const u8* subcode_in, u8* subcode_out)
{
  std::memset(subcode_out, 0, ALL_SUBCODE_SIZE);

  int row = 0;
  for (int bitNum = 0; bitNum < 8; bitNum++)
  {
    for (int nColumn = 0; nColumn < ALL_SUBCODE_SIZE; row++)
    {
      u32 mask = 0x80;
      for (int nShift = 0; nShift < 8; nShift++, nColumn++)
      {
        const int n = nShift - bitNum;
        if (n > 0)
        {
          subcode_out[row] |= static_cast<u8>((subcode_in[nColumn] >> n) & mask);
        }
        else
        {
          subcode_out[row] |= static_cast<u8>((subcode_in[nColumn] << std::abs(n)) & mask);
        }
        mask >>= 1;
      }
    }
  }
}

static u32 BEToU32(const u8* val)
{
  return (static_cast<u32>(val[0]) << 24) | (static_cast<u32>(val[1]) << 16) | (static_cast<u32>(val[2]) << 8) |
         static_cast<u32>(val[3]);
}


static void U16ToBE(u8* beval, u16 leval)
{
  beval[0] = static_cast<u8>(leval >> 8);
  beval[1] = static_cast<u8>(leval);
}


// The include order here is critical.
// clang-format off
#include "windows_headers.h"
#include <winioctl.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
// clang-format on

class CDImageDeviceWin32 : public CDImage
{
public:
  CDImageDeviceWin32(OpenFlags open_flags);
  ~CDImageDeviceWin32() override;

  bool Open(const char* filename, Common::Error* error);

  bool ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index) override;
  bool HasNonStandardSubchannel() const override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  struct SPTDBuffer
  {
    SCSI_PASS_THROUGH_DIRECT cmd;
    u8 sense[20];
  };

  static void FillSPTD(SPTDBuffer* sptd, u32 sector_number, bool include_subq, void* buffer);

  bool ReadSectorToBuffer(u64 offset);
  bool DetermineReadMode();

  HANDLE m_hDevice = INVALID_HANDLE_VALUE;

  u64 m_buffer_offset = ~static_cast<u64>(0);

  bool m_use_sptd = true;
  bool m_read_subcode = false;

  std::array<u8, CD_RAW_SECTOR_WITH_SUBCODE_SIZE> m_buffer;
  std::array<u8, ALL_SUBCODE_SIZE> m_deinterleaved_subcode;
  std::array<u8, SUBCHANNEL_BYTES_PER_FRAME> m_subq;
};

CDImageDeviceWin32::CDImageDeviceWin32(OpenFlags open_flags) : CDImage(open_flags) {}

CDImageDeviceWin32::~CDImageDeviceWin32()
{
  if (m_hDevice != INVALID_HANDLE_VALUE)
    CloseHandle(m_hDevice);
}

bool CDImageDeviceWin32::Open(const char* filename, Common::Error* error)
{
  m_filename = filename;
  m_hDevice = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, NULL);
  if (m_hDevice == INVALID_HANDLE_VALUE)
  {
    m_hDevice = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, NULL);
    if (m_hDevice == INVALID_HANDLE_VALUE)
      return false;
    m_use_sptd = false;
  }

  // Set it to 4x speed. A good balance between readahead and spinning up way too high.
  static constexpr u32 READ_SPEED_MULTIPLIER = 4;
  static constexpr u32 READ_SPEED_KBS = (DATA_SECTOR_SIZE * FRAMES_PER_SECOND * 8) / 1024;
  CDROM_SET_SPEED set_speed = {CdromSetSpeed, READ_SPEED_KBS, 0, CdromDefaultRotation};
  DeviceIoControl(m_hDevice, IOCTL_CDROM_SET_SPEED, &set_speed, sizeof(set_speed), nullptr, 0, nullptr, nullptr);

  CDROM_READ_TOC_EX read_toc_ex = {};
  read_toc_ex.Format = CDROM_READ_TOC_EX_FORMAT_TOC;
  read_toc_ex.Msf = 0;
  read_toc_ex.SessionTrack = 1;

  CDROM_TOC toc = {};
  U16ToBE(toc.Length, sizeof(toc) - sizeof(UCHAR) * 2);

  DWORD bytes_returned;
  if (!DeviceIoControl(m_hDevice, IOCTL_CDROM_READ_TOC_EX, &read_toc_ex, sizeof(read_toc_ex), &toc, sizeof(toc),
                       &bytes_returned, nullptr) ||
      toc.LastTrack < toc.FirstTrack)
  {
    return false;
  }

  DWORD last_track_address = 0;
  LBA disc_lba = 0;

  const u32 num_tracks_to_check = (toc.LastTrack - toc.FirstTrack) + 1 + 1;
  for (u32 track_index = 0; track_index < num_tracks_to_check; track_index++)
  {
    const TRACK_DATA& td = toc.TrackData[track_index];
    const u8 track_num = td.TrackNumber;
    const DWORD track_address = BEToU32(td.Address);

    // fill in the previous track's length
    if (!m_tracks.empty())
    {
      if (track_num < m_tracks.back().track_number)
        return false;

      const LBA previous_track_length = static_cast<LBA>(track_address - last_track_address);
      m_tracks.back().length += previous_track_length;
      m_indices.back().length += previous_track_length;
      disc_lba += previous_track_length;
    }

    last_track_address = track_address;
    if (track_num == LEAD_OUT_TRACK_NUMBER)
    {
      AddLeadOutIndex();
      break;
    }

    // precompute subchannel q flags for the whole track
    SubChannelQ::Control control{};
    control.bits = td.Adr | (td.Control << 4);

    const LBA track_lba = static_cast<LBA>(track_address);
    const TrackMode track_mode = control.data ? CDImage::TrackMode::Mode2Raw : CDImage::TrackMode::Audio;

    // TODO: How the hell do we handle pregaps here?
    const u32 pregap_frames = (control.data && track_index == 0) ? 150 : 0;
    if (pregap_frames > 0)
    {
      Index pregap_index = {};
      pregap_index.start_lba_on_disc = disc_lba;
      pregap_index.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(pregap_frames));
      pregap_index.length = pregap_frames;
      pregap_index.track_number = track_num;
      pregap_index.index_number = 0;
      pregap_index.mode = track_mode;
      pregap_index.control.bits = control.bits;
      pregap_index.is_pregap = true;
      m_indices.push_back(pregap_index);
      disc_lba += pregap_frames;
    }

    // index 1, will be filled in next iteration
    if (track_num <= MAX_TRACK_NUMBER)
    {
      // add the track itself
      m_tracks.push_back(Track{track_num, disc_lba, static_cast<u32>(m_indices.size()), 0, track_mode, control});

      Index index1;
      index1.start_lba_on_disc = disc_lba;
      index1.start_lba_in_track = 0;
      index1.length = 0;
      index1.track_number = track_num;
      index1.index_number = 1;
      index1.file_index = 0;
      index1.file_sector_size = 2048;
      index1.file_offset = static_cast<u64>(track_address) * index1.file_sector_size;
      index1.mode = track_mode;
      index1.control.bits = control.bits;
      index1.is_pregap = false;
      m_indices.push_back(index1);
    }
  }

  if (m_tracks.empty())
  {
    if (error)
      error->SetFormattedMessage("File '%s' contains no tracks", filename);
    return false;
  }

  m_lba_count = disc_lba;

  if (!DetermineReadMode())
  {
    if (error)
      error->SetMessage("Could not determine read mode");

    return false;
  }

  return Seek(1, Position{0, 0, 0});
}

bool CDImageDeviceWin32::ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index)
{
  if (index.file_sector_size == 0 || !m_read_subcode)
    return CDImage::ReadSubChannelQ(subq, index, lba_in_index);

  const u64 offset = index.file_offset + static_cast<u64>(lba_in_index) * index.file_sector_size;
  if (m_buffer_offset != offset && !ReadSectorToBuffer(offset))
    return false;

  // P, Q, ...
  std::memcpy(subq->data.data(), m_subq.data(), SUBCHANNEL_BYTES_PER_FRAME);
  return true;
}

bool CDImageDeviceWin32::HasNonStandardSubchannel() const
{
  return true;
}

bool CDImageDeviceWin32::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  if (index.file_sector_size == 0)
    return false;

  const u64 offset = index.file_offset + static_cast<u64>(lba_in_index) * index.file_sector_size;
  if (m_buffer_offset != offset && !ReadSectorToBuffer(offset))
    return false;

  std::memcpy(buffer, m_buffer.data(), RAW_SECTOR_SIZE);
  return true;
}

void CDImageDeviceWin32::FillSPTD(SPTDBuffer* sptd, u32 sector_number, bool include_subq, void* buffer)
{
  std::memset(sptd, 0, sizeof(SPTDBuffer));

  sptd->cmd.Length = sizeof(sptd->cmd);
  sptd->cmd.CdbLength = 12;
  sptd->cmd.SenseInfoLength = sizeof(sptd->sense);
  sptd->cmd.DataIn = SCSI_IOCTL_DATA_IN;
  sptd->cmd.DataTransferLength = include_subq ? (RAW_SECTOR_SIZE + SUBCHANNEL_BYTES_PER_FRAME) : RAW_SECTOR_SIZE;
  sptd->cmd.TimeOutValue = 10;
  sptd->cmd.SenseInfoOffset = offsetof(SPTDBuffer, sense);
  sptd->cmd.DataBuffer = buffer;

  sptd->cmd.Cdb[0] = 0xBE;                           // READ CD
  sptd->cmd.Cdb[1] = 0x00;                           // sector type
  sptd->cmd.Cdb[2] = Truncate8(sector_number >> 24); // Starting LBA
  sptd->cmd.Cdb[3] = Truncate8(sector_number >> 16);
  sptd->cmd.Cdb[4] = Truncate8(sector_number >> 8);
  sptd->cmd.Cdb[5] = Truncate8(sector_number);
  sptd->cmd.Cdb[6] = 0x00; // Transfer Count
  sptd->cmd.Cdb[7] = 0x00;
  sptd->cmd.Cdb[8] = 0x01;
  sptd->cmd.Cdb[9] = (1 << 7) |                                     // include sync
                     (0b11 << 5) |                                  // include header codes
                     (1 << 4) |                                     // include user data
                     (1 << 3) |                                     // edc/ecc
                     (0 << 2);                                      // don't include C2 data
  sptd->cmd.Cdb[10] = (include_subq ? (0b010 << 0) : (0b000 << 0)); // subq selection
}

bool CDImageDeviceWin32::ReadSectorToBuffer(u64 offset)
{
  if (m_use_sptd)
  {
    const u32 sector_number = static_cast<u32>(offset / 2048);

    SPTDBuffer sptd = {};
    FillSPTD(&sptd, sector_number, m_read_subcode, m_buffer.data());

    const u32 expected_bytes = sptd.cmd.DataTransferLength;
    DWORD bytes_returned;
    if (!DeviceIoControl(m_hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd, sizeof(sptd), &sptd, sizeof(sptd),
                         &bytes_returned, nullptr) &&
        sptd.cmd.ScsiStatus == 0x00)
      return false;

    if (m_read_subcode)
      std::memcpy(m_subq.data(), &m_buffer[RAW_SECTOR_SIZE], SUBCHANNEL_BYTES_PER_FRAME);
  }
  else
  {
    RAW_READ_INFO rri;
    rri.DiskOffset.QuadPart = offset;
    rri.SectorCount = 1;
    rri.TrackMode = RawWithSubCode;

    DWORD bytes_returned;
    if (!DeviceIoControl(m_hDevice, IOCTL_CDROM_RAW_READ, &rri, sizeof(rri), m_buffer.data(),
                         static_cast<DWORD>(m_buffer.size()), &bytes_returned, nullptr))
      return false;

    // P, Q, ...
    DeinterleaveSubcode(&m_buffer[RAW_SECTOR_SIZE], m_deinterleaved_subcode.data());
    std::memcpy(m_subq.data(), &m_deinterleaved_subcode[SUBCHANNEL_BYTES_PER_FRAME], SUBCHANNEL_BYTES_PER_FRAME);
  }

  m_buffer_offset = offset;
  return true;
}

bool CDImageDeviceWin32::DetermineReadMode()
{
  // Prefer raw reads if we can use them
  RAW_READ_INFO rri;
  rri.DiskOffset.QuadPart = 0;
  rri.SectorCount = 1;
  rri.TrackMode = RawWithSubCode;

  DWORD bytes_returned;
  if (DeviceIoControl(m_hDevice, IOCTL_CDROM_RAW_READ, &rri, sizeof(rri), m_buffer.data(),
                      static_cast<DWORD>(m_buffer.size()), &bytes_returned, nullptr) &&
      bytes_returned == CD_RAW_SECTOR_WITH_SUBCODE_SIZE)
  {
    SubChannelQ subq;
    DeinterleaveSubcode(&m_buffer[RAW_SECTOR_SIZE], m_deinterleaved_subcode.data());
    std::memcpy(&subq, &m_deinterleaved_subcode[SUBCHANNEL_BYTES_PER_FRAME], SUBCHANNEL_BYTES_PER_FRAME);

    m_use_sptd = false;
    m_read_subcode = true;

    if (subq.IsCRCValid())
      m_read_subcode = false;

    return true;
  }

  SPTDBuffer sptd = {};
  FillSPTD(&sptd, 0, true, m_buffer.data());

  if (DeviceIoControl(m_hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd, sizeof(sptd), &sptd, sizeof(sptd),
                      &bytes_returned, nullptr) &&
      sptd.cmd.ScsiStatus == 0x00)
  {
    // check the validity of the subchannel data. this assumes that the first sector has a valid subq, which it should
    // in all PS1 games.
    SubChannelQ subq;
    std::memcpy(&subq, &m_buffer[RAW_SECTOR_SIZE], sizeof(subq));
    if (subq.IsCRCValid())
    {
      m_read_subcode = true;
      m_use_sptd = true;
      return true;
    }
  }

  // try without subcode
  FillSPTD(&sptd, 0, false, m_buffer.data());
  if (DeviceIoControl(m_hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd, sizeof(sptd), &sptd, sizeof(sptd),
                      &bytes_returned, nullptr) &&
      sptd.cmd.ScsiStatus == 0x00)
  {
    m_read_subcode = false;
    m_use_sptd = true;
    return true;
  }

  return false;
}

std::unique_ptr<CDImage> CDImage::OpenDeviceImage(const char* filename, OpenFlags open_flags, Common::Error* error)
{
  std::unique_ptr<CDImageDeviceWin32> image = std::make_unique<CDImageDeviceWin32>(open_flags);
  if (!image->Open(filename, error))
    return {};

  return image;
}

std::vector<std::pair<std::string, std::string>> CDImage::GetDeviceList()
{
  std::vector<std::pair<std::string, std::string>> ret;

  char buf[256];
  if (GetLogicalDriveStringsA(sizeof(buf), buf) != 0)
  {
    const char* ptr = buf;
    while (*ptr != '\0')
    {
      std::size_t len = std::strlen(ptr);
      const DWORD type = GetDriveTypeA(ptr);
      if (type != DRIVE_CDROM)
      {
        ptr += len + 1u;
        continue;
      }

      // Drop the trailing slash.
      const std::size_t append_len = (ptr[len - 1] == '\\') ? (len - 1) : len;

      std::string path;
      path.append("\\\\.\\");
      path.append(ptr, append_len);

      std::string name(ptr, append_len);

      ret.emplace_back(std::move(path), std::move(name));

      ptr += len + 1u;
    }
  }

  return ret;
}

bool CDImage::IsDeviceName(const char* filename)
{
  return StringUtil::StartsWith(filename, "\\\\.\\");
}

#else

std::unique_ptr<CDImage> CDImage::OpenDeviceImage(const char* filename, OpenFlags open_flags, Common::Error* error)
{
  return {};
}

std::vector<std::pair<std::string, std::string>> CDImage::GetDeviceList()
{
  return {};
}

bool CDImage::IsDeviceName(const char* filename)
{
  return false;
}

#endif

#include "libretro_audio_stream.h"
#include "common/assert.h"
#include "libretro_host_interface.h"
#include <algorithm>
#include <array>

LibretroAudioStream::LibretroAudioStream() = default;

LibretroAudioStream::~LibretroAudioStream() = default;

void LibretroAudioStream::UploadToFrontend()
{
  std::array<SampleType, MaxSamples> output_buffer;
  u32 total_samples = 0;

  while (const auto num_samples = m_buffer.GetContiguousSize()) {
    const auto write_pos = output_buffer.begin() + total_samples;
    Assert(write_pos + num_samples <= output_buffer.end());

    std::copy_n(m_buffer.GetReadPointer(), num_samples, write_pos);
    m_buffer.Remove(num_samples);
    total_samples += num_samples;
  }

  const auto total_frames = total_samples / m_channels;
  g_retro_audio_sample_batch_callback(output_buffer.data(), total_frames);
}

bool LibretroAudioStream::OpenDevice()
{
  return true;
}

void LibretroAudioStream::PauseDevice(bool paused) {}

void LibretroAudioStream::CloseDevice() {}

void LibretroAudioStream::FramesAvailable() {}

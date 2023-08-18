#include "libretro_audio_stream.h"
#include "libretro_host_interface.h"
#include <algorithm>
#include <array>

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

LibretroAudioStream::LibretroAudioStream() = default;

LibretroAudioStream::~LibretroAudioStream() = default;

void LibretroAudioStream::FramesAvailable() 
{
  for (;;)
  {
    const u32 num_samples = m_buffer.GetContiguousSize();
    if (num_samples == 0)
      break;

    const u32 num_frames = num_samples / AUDIO_CHANNELS;
    g_retro_audio_sample_batch_callback(m_buffer.GetReadPointer(), num_frames);
    m_buffer.Remove(num_samples);
  }
}
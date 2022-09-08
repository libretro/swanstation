#include "null_audio_stream.h"

#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

NullAudioStream::NullAudioStream() = default;

NullAudioStream::~NullAudioStream() = default;

void NullAudioStream::FramesAvailable()
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  u32 available_samples = m_buffer.GetSize() / AUDIO_CHANNELS;
  // Drop any buffer as soon as they're available
  m_buffer.Remove(available_samples);
}

std::unique_ptr<AudioStream> AudioStream::CreateNullAudioStream()
{
  return std::make_unique<NullAudioStream>();
}

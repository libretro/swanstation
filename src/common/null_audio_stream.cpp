#include "null_audio_stream.h"

NullAudioStream::NullAudioStream() = default;

NullAudioStream::~NullAudioStream() = default;

void NullAudioStream::FramesAvailable()
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  u32 available_samples = m_buffer.GetSize() / m_channels;
  // Drop any buffer as soon as they're available
  m_buffer.Remove(available_samples);
}

std::unique_ptr<AudioStream> AudioStream::CreateNullAudioStream()
{
  return std::make_unique<NullAudioStream>();
}

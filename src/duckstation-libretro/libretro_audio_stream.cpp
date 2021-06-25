#include "libretro_audio_stream.h"
#include "libretro_host_interface.h"

LibretroAudioStream::LibretroAudioStream() = default;

LibretroAudioStream::~LibretroAudioStream() = default;

bool LibretroAudioStream::OpenDevice()
{
  m_output_buffer.resize(m_buffer_size * m_channels);
  return true;
}

void LibretroAudioStream::PauseDevice(bool paused) {}

void LibretroAudioStream::CloseDevice() {}

void LibretroAudioStream::FramesAvailable() {}

void LibretroAudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
	*buffer_ptr = m_output_buffer.data();
	*num_frames = m_output_buffer.size() / m_channels;
}

void LibretroAudioStream::EndWrite(u32 num_frames)
{
	g_retro_audio_sample_batch_callback(m_output_buffer.data(), num_frames);
}

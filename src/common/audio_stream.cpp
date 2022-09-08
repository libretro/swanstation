#include "audio_stream.h"
#include "assert.h"
#include <algorithm>
#include <cstring>

AudioStream::AudioStream() = default;

AudioStream::~AudioStream()
{
}

bool AudioStream::Reconfigure(u32 input_sample_rate /* = DefaultInputSampleRate */,
                              u32 output_sample_rate /* = DefaultOutputSampleRate */, u32 channels /* = 1 */,
                              u32 buffer_size /* = DefaultBufferSize */)
{
  std::unique_lock<std::mutex> buffer_lock(m_buffer_mutex);

  m_output_sample_rate = output_sample_rate;
  m_channels = channels;
  m_buffer_size = buffer_size;
  m_buffer_filling.store(false);

  return SetBufferSize(buffer_size);
}

void AudioStream::Shutdown()
{
  if (m_output_sample_rate == 0)
    return;

  EmptyBuffers();
  m_buffer_size = 0;
  m_output_sample_rate = 0;
  m_channels = 0;
}

void AudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  m_buffer_mutex.lock();

  const u32 requested_frames = std::min(*num_frames, m_buffer_size);
  u32 size                   = requested_frames * m_channels;
  u32 buffer_space           = m_max_samples - m_buffer.GetSize();
  if (buffer_space < size)
  {
    std::unique_lock<std::mutex> lock(m_buffer_mutex, std::adopt_lock);
    m_buffer_draining_cv.wait(lock, [this, size]() 
		    {
		    u32 buffer_space = m_max_samples - m_buffer.GetSize();
		    return buffer_space >= size; 
		    }
		    );
    lock.release();
  }

  *buffer_ptr = m_buffer.GetWritePointer();
  *num_frames = std::min(m_buffer_size, m_buffer.GetContiguousSpace() / m_channels);
}

void AudioStream::EndWrite(u32 num_frames)
{
  m_buffer.AdvanceTail(num_frames * m_channels);
  if (m_buffer_filling.load())
  {
    if ((m_buffer.GetSize() / m_channels) >= m_buffer_size)
      m_buffer_filling.store(false);
  }
  m_buffer_mutex.unlock();
  FramesAvailable();
}

bool AudioStream::SetBufferSize(u32 buffer_size)
{
  const u32 buffer_size_in_samples = buffer_size * m_channels;
  const u32 max_samples = buffer_size_in_samples * 2u;
  if (max_samples > m_buffer.GetCapacity())
    return false;

  m_buffer_size = buffer_size;
  m_max_samples = max_samples;
  return true;
}

void AudioStream::EmptyBuffers()
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  m_buffer.Clear();
  m_buffer_filling.store(false);
}

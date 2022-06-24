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

  if (IsDeviceOpen())
    CloseDevice();

  m_output_sample_rate = output_sample_rate;
  m_channels = channels;
  m_buffer_size = buffer_size;
  m_buffer_filling.store(m_wait_for_buffer_fill);
  m_output_paused = true;

  if (!SetBufferSize(buffer_size))
    return false;

  if (!OpenDevice())
  {
    LockedEmptyBuffers();
    m_buffer_size = 0;
    m_output_sample_rate = 0;
    m_channels = 0;
    return false;
  }

  return true;
}

void AudioStream::PauseOutput(bool paused)
{
  if (m_output_paused == paused)
    return;

  PauseDevice(paused);
  m_output_paused = paused;

  // Empty buffers on pause.
  if (paused)
    EmptyBuffers();
}

void AudioStream::Shutdown()
{
  if (!IsDeviceOpen())
    return;

  CloseDevice();
  EmptyBuffers();
  m_buffer_size = 0;
  m_output_sample_rate = 0;
  m_channels = 0;
  m_output_paused = true;
}

void AudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  m_buffer_mutex.lock();

  const u32 requested_frames = std::min(*num_frames, m_buffer_size);
  EnsureBuffer(requested_frames * m_channels);

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

u32 AudioStream::GetSamplesAvailable() const
{
  // TODO: Use atomic loads
  u32 available_samples;
  {
    std::unique_lock<std::mutex> lock(m_buffer_mutex);
    available_samples = m_buffer.GetSize();
  }

  return available_samples / m_channels;
}

void AudioStream::EnsureBuffer(u32 size)
{
  DebugAssert(size <= (m_buffer_size * m_channels));
  if (GetBufferSpace() >= size)
    return;

  std::unique_lock<std::mutex> lock(m_buffer_mutex, std::adopt_lock);
  m_buffer_draining_cv.wait(lock, [this, size]() { return GetBufferSpace() >= size; });
  lock.release();
}

void AudioStream::DropFrames(u32 count)
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  m_buffer.Remove(count);
}

void AudioStream::EmptyBuffers()
{
  std::unique_lock<std::mutex> lock(m_buffer_mutex);
  LockedEmptyBuffers();
}

void AudioStream::LockedEmptyBuffers()
{
  m_buffer.Clear();
  m_buffer_filling.store(m_wait_for_buffer_fill);
}

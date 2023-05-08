#pragma once
#include "fifo_queue.h"
#include "types.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

// Uses signed 16-bits samples.

class AudioStream
{
public:
  using SampleType = s16;

  static constexpr u32 DefaultInputSampleRate = 44100, DefaultOutputSampleRate = 44100, DefaultBufferSize = 2048,
                       MaxSamples = 32768, FullVolume = 100;

  AudioStream();
  virtual ~AudioStream();

  u32 GetBufferSize() const { return m_buffer_size; }

  bool Reconfigure(u32 input_sample_rate, u32 output_sample_rate,
                   u32 channels, u32 buffer_size);

  void Shutdown();

  void BeginWrite(SampleType** buffer_ptr, u32* num_frames);
  void EndWrite(u32 num_frames);

  static std::unique_ptr<AudioStream> CreateNullAudioStream();

protected:
  virtual void FramesAvailable() = 0;

  ALWAYS_INLINE static SampleType ApplyVolume(SampleType sample, u32 volume)
  {
    return s16((s32(sample) * s32(volume)) / 100);
  }

  bool SetBufferSize(u32 buffer_size);

  u32 m_buffer_size = 0;

  HeapFIFOQueue<SampleType, MaxSamples> m_buffer;
  mutable std::mutex m_buffer_mutex;
  std::condition_variable m_buffer_draining_cv;

  u32 m_max_samples = 0;
};

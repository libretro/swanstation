#pragma once
#include "common/audio_stream.h"
#include <cstdint>
#include <vector>

class LibretroAudioStream final : public AudioStream
{
public:
  LibretroAudioStream();
  ~LibretroAudioStream();

  void BeginWrite(SampleType** buffer_ptr, u32* num_frames) override;
  void EndWrite(u32 num_frames) override;

protected:
  bool OpenDevice() override;
  void PauseDevice(bool paused) override;
  void CloseDevice() override;
  void FramesAvailable() override;

private:
  std::vector<SampleType> m_output_buffer;
};

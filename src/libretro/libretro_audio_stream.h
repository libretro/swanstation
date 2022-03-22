#pragma once
#include "common/audio_stream.h"
#include <cstdint>
#include <vector>

class LibretroAudioStream final : public AudioStream
{
public:
  LibretroAudioStream();
  ~LibretroAudioStream();

  void UploadToFrontend();

protected:
  bool OpenDevice() override;
  void PauseDevice(bool paused) override;
  void CloseDevice() override;
  void FramesAvailable() override;
};

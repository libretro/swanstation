#pragma once
#include "common/audio_stream.h"
#include <cstdint>
#include <vector>

class LibretroAudioStream final : public AudioStream
{
public:
  LibretroAudioStream();
  ~LibretroAudioStream();

protected:
  void FramesAvailable() override;
};

#pragma once
#include "audio_stream.h"

class NullAudioStream final : public AudioStream
{
public:
  NullAudioStream();
  ~NullAudioStream();

protected:
  void FramesAvailable() override;
};

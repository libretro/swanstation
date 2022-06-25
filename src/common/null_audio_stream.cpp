#include "null_audio_stream.h"

NullAudioStream::NullAudioStream() = default;

NullAudioStream::~NullAudioStream() = default;

void NullAudioStream::FramesAvailable()
{
  // drop any buffer as soon as they're available
  DropFrames(GetSamplesAvailable());
}

std::unique_ptr<AudioStream> AudioStream::CreateNullAudioStream()
{
  return std::make_unique<NullAudioStream>();
}

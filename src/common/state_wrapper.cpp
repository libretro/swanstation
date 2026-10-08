#include "state_wrapper.h"
#include "log.h"
#include "string.h"
#include <cinttypes>
#include <cstring>
Log_SetChannel(StateWrapper);

StateWrapper::StateWrapper(ByteStream* stream, Mode mode, uint32_t version)
  : m_stream(stream), m_mode(mode), m_version(version)
{
}

StateWrapper::~StateWrapper() = default;

void StateWrapper::DoBytes(void* data, size_t length)
{
  if (m_mode == Mode::Read)
  {
    if (m_error || (m_error |= !m_stream->Read2(data, static_cast<uint32_t>(length))) == true)
      std::memset(data, 0, length);
  }
  else
  {
    if (!m_error)
      m_error |= !m_stream->Write2(data, static_cast<uint32_t>(length));
  }
}

void StateWrapper::Do(bool* value_ptr)
{
  if (m_mode == Mode::Read)
  {
    uint8_t value = 0;
    if (!m_error)
      m_error |= !m_stream->ReadByte(&value);
    *value_ptr = (value != 0);
  }
  else
  {
    uint8_t value = static_cast<uint8_t>(*value_ptr);
    if (!m_error)
      m_error |= !m_stream->WriteByte(value);
  }
}

bool StateWrapper::CheckReadCount(uint32_t count)
{
  if (m_error || count > (m_stream->GetSize() - m_stream->GetPosition()))
  {
    m_error = true;
    return false;
  }
  return true;
}

void StateWrapper::Do(std::string* value_ptr)
{
  uint32_t length = static_cast<uint32_t>(value_ptr->length());
  Do(&length);
  if (m_mode == Mode::Read)
  {
    if (!CheckReadCount(length))
    {
      value_ptr->clear();
      return;
    }
    value_ptr->resize(length);
  }
  DoBytes(&(*value_ptr)[0], length);
  value_ptr->resize(std::strlen(&(*value_ptr)[0]));
}

void StateWrapper::Do(String* value_ptr)
{
  uint32_t length = static_cast<uint32_t>(value_ptr->GetLength());
  Do(&length);
  if (m_mode == Mode::Read)
  {
    if (!CheckReadCount(length))
    {
      value_ptr->Clear();
      return;
    }
    value_ptr->Resize(length);
  }
  DoBytes(value_ptr->GetWriteableCharArray(), length);
  value_ptr->UpdateSize();
}

void StateWrapper::DoCString(char* buffer, uint32_t buffer_size)
{
  uint32_t length = (m_mode == Mode::Write) ? static_cast<uint32_t>(std::strlen(buffer)) : 0;
  Do(&length);
  if (m_mode == Mode::Read)
  {
    if (length >= buffer_size)
      m_error = true;
    if (m_error)
    {
      buffer[0] = '\0';
      return;
    }
    DoBytes(buffer, length);
    buffer[length] = '\0';
  }
  else
  {
    DoBytes(buffer, length);
  }
}

bool StateWrapper::DoMarker(const char* marker)
{
  char file_value[64];
  if (m_mode == Mode::Write)
  {
    uint32_t length = static_cast<uint32_t>(std::strlen(marker));
    Do(&length);
    DoBytes(const_cast<char*>(marker), length);
    return !m_error;
  }

  file_value[0] = '\0';
  DoCString(file_value, sizeof(file_value));
  if (m_error)
    return false;

  if (std::strcmp(file_value, marker) == 0)
    return true;

  Log_ErrorPrintf("Marker mismatch at offset %" PRIu64 ": found '%s' expected '%s'", m_stream->GetPosition(),
                  file_value, marker);

  return false;
}

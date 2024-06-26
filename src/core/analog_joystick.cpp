#include "analog_joystick.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "common/string_util.h"
#include "host_interface.h"
#include "system.h"
#include <cmath>
Log_SetChannel(AnalogJoystick);

AnalogJoystick::AnalogJoystick(u32 index)
{
  m_index = index;
  m_axis_state.fill(0x80);
  Reset();
}

AnalogJoystick::~AnalogJoystick() = default;

ControllerType AnalogJoystick::GetType() const
{
  return ControllerType::AnalogJoystick;
}

void AnalogJoystick::Reset()
{
  m_transfer_state = TransferState::Idle;
}

bool AnalogJoystick::DoState(StateWrapper& sw, bool apply_input_state)
{
  if (!Controller::DoState(sw, apply_input_state))
    return false;

  const bool old_analog_mode = m_analog_mode;

  sw.Do(&m_analog_mode);

  u16 button_state = m_button_state;
  auto axis_state = m_axis_state;
  sw.Do(&button_state);
  sw.Do(&axis_state);

  if (apply_input_state)
  {
    m_button_state = button_state;
    m_axis_state = axis_state;
  }

  sw.Do(&m_transfer_state);

  if (sw.IsReading() && (old_analog_mode != m_analog_mode))
  {
    g_host_interface->AddFormattedOSDMessage(
      5.0f,
      m_analog_mode ? g_host_interface->TranslateString("AnalogJoystick", "Controller %u switched to analog mode.") :
                      g_host_interface->TranslateString("AnalogJoystick", "Controller %u switched to digital mode."),
      m_index + 1u);
  }
  return true;
}

void AnalogJoystick::SetAxisState(s32 axis_code, float value)
{
  if (axis_code < 0 || axis_code >= static_cast<s32>(Axis::Count))
    return;

  // -1..1 -> 0..255
  const float scaled_value = std::clamp(value * m_axis_scale, -1.0f, 1.0f);
  const u8 u8_value = static_cast<u8>(std::clamp(std::round(((scaled_value + 1.0f) / 2.0f) * 255.0f), 0.0f, 255.0f));

  SetAxisState(static_cast<Axis>(axis_code), u8_value);
}

void AnalogJoystick::SetAxisState(Axis axis, u8 value)
{
  if (m_axis_state[static_cast<u8>(axis)] != value)
    System::SetRunaheadReplayFlag();

  m_axis_state[static_cast<u8>(axis)] = value;
}

void AnalogJoystick::SetButtonState(Button button, bool pressed)
{
  if (button == Button::Mode)
  {
    if (pressed)
      ToggleAnalogMode();

    return;
  }

  const u16 bit = u16(1) << static_cast<u8>(button);

  if (pressed)
  {
    if (m_button_state & bit)
      System::SetRunaheadReplayFlag();

    m_button_state &= ~bit;
  }
  else
  {
    if (!(m_button_state & bit))
      System::SetRunaheadReplayFlag();

    m_button_state |= bit;
  }
}

void AnalogJoystick::SetButtonState(s32 button_code, bool pressed)
{
  if (button_code < 0 || button_code >= static_cast<s32>(Button::Count))
    return;

  SetButtonState(static_cast<Button>(button_code), pressed);
}

u32 AnalogJoystick::GetButtonStateBits() const
{
  return m_button_state ^ 0xFFFF;
}

std::optional<u32> AnalogJoystick::GetAnalogInputBytes() const
{
  return m_axis_state[static_cast<size_t>(Axis::LeftY)] << 24 | m_axis_state[static_cast<size_t>(Axis::LeftX)] << 16 |
         m_axis_state[static_cast<size_t>(Axis::RightY)] << 8 | m_axis_state[static_cast<size_t>(Axis::RightX)];
}

void AnalogJoystick::ResetTransferState()
{
  m_transfer_state = TransferState::Idle;
}

u16 AnalogJoystick::GetID() const
{
  static constexpr u16 DIGITAL_MODE_ID = 0x5A41;
  static constexpr u16 ANALOG_MODE_ID = 0x5A53;

  return m_analog_mode ? ANALOG_MODE_ID : DIGITAL_MODE_ID;
}

void AnalogJoystick::ToggleAnalogMode()
{
  m_analog_mode = !m_analog_mode;

  Log_InfoPrintf("Joystick %u switched to %s mode.", m_index + 1u, m_analog_mode ? "analog" : "digital");
  g_host_interface->AddFormattedOSDMessage(
    5.0f,
    m_analog_mode ? g_host_interface->TranslateString("AnalogJoystick", "Controller %u switched to analog mode.") :
                    g_host_interface->TranslateString("AnalogJoystick", "Controller %u switched to digital mode."),
    m_index + 1u);
}

bool AnalogJoystick::Transfer(const u8 data_in, u8* data_out)
{
  switch (m_transfer_state)
  {
    case TransferState::Idle:
    {
      *data_out = 0xFF;

      if (data_in == 0x01)
      {
        m_transfer_state = TransferState::Ready;
        return true;
      }
      return false;
    }

    case TransferState::Ready:
    {
      if (data_in == 0x42)
      {
        *data_out = Truncate8(GetID());
        m_transfer_state = TransferState::IDMSB;
        return true;
      }

      *data_out = 0xFF;
      return false;
    }

    case TransferState::IDMSB:
    {
      *data_out = Truncate8(GetID() >> 8);
      m_transfer_state = TransferState::ButtonsLSB;
      return true;
    }

    case TransferState::ButtonsLSB:
    {
      *data_out = Truncate8(m_button_state);
      m_transfer_state = TransferState::ButtonsMSB;
      return true;
    }

    case TransferState::ButtonsMSB:
    {
      *data_out = Truncate8(m_button_state >> 8);

      m_transfer_state = m_analog_mode ? TransferState::RightAxisX : TransferState::Idle;
      return m_analog_mode;
    }

    case TransferState::RightAxisX:
    {
      *data_out = Truncate8(m_axis_state[static_cast<u8>(Axis::RightX)]);
      m_transfer_state = TransferState::RightAxisY;
      return true;
    }

    case TransferState::RightAxisY:
    {
      *data_out = Truncate8(m_axis_state[static_cast<u8>(Axis::RightY)]);
      m_transfer_state = TransferState::LeftAxisX;
      return true;
    }

    case TransferState::LeftAxisX:
    {
      *data_out = Truncate8(m_axis_state[static_cast<u8>(Axis::LeftX)]);
      m_transfer_state = TransferState::LeftAxisY;
      return true;
    }

    case TransferState::LeftAxisY:
    {
      *data_out = Truncate8(m_axis_state[static_cast<u8>(Axis::LeftY)]);
      m_transfer_state = TransferState::Idle;
      return false;
    }

    default:
      return false;
  }
}

std::unique_ptr<AnalogJoystick> AnalogJoystick::Create(u32 index)
{
  return std::make_unique<AnalogJoystick>(index);
}

u32 AnalogJoystick::StaticGetVibrationMotorCount()
{
  return 0;
}

void AnalogJoystick::LoadSettings(const char* section)
{
  Controller::LoadSettings(section);
  m_axis_scale = std::clamp(g_host_interface->GetFloatSettingValue(section, "AxisScale", 1.00f), 0.01f, 1.50f);
}

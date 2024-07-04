#include "negcon_rumble.h"
#include "common/state_wrapper.h"
#include "common/string_util.h"
#include "host_interface.h"
#include "settings.h"
#include "system.h"
#include <cmath>

NeGconRumble::NeGconRumble(u32 index) : m_index(index)
{
  m_axis_state.fill(0x00);
  m_axis_state[static_cast<u8>(Axis::Steering)] = 0x80;
  Reset();
}

NeGconRumble::~NeGconRumble() = default;

ControllerType NeGconRumble::GetType() const
{
  return ControllerType::NeGconRumble;
}

void NeGconRumble::Reset()
{
  m_command = Command::Idle;
  m_command_step = 0;
  m_rx_buffer.fill(0x00);
  m_tx_buffer.fill(0x00);
  m_analog_mode = true;
  m_configuration_mode = false;
  m_motor_state.fill(0);

  m_dualshock_enabled = true;
  ResetRumbleConfig();

  m_status_byte = 0x5A;
}

bool NeGconRumble::DoState(StateWrapper& sw, bool apply_input_state)
{
  if (!Controller::DoState(sw, apply_input_state))
    return false;

  const bool old_analog_mode = m_analog_mode;

  sw.Do(&m_analog_mode);
  sw.Do(&m_dualshock_enabled);
  sw.Do(&m_configuration_mode);
  sw.DoEx(&m_status_byte, 55, static_cast<u8>(0x5A));

  u16 button_state = m_button_state;
  sw.DoEx(&button_state, 44, static_cast<u16>(0xFFFF));
  if (apply_input_state)
    m_button_state = button_state;
  else
    m_analog_mode = old_analog_mode;

  sw.Do(&m_command);

  sw.DoEx(&m_rumble_config, 45, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  sw.DoEx(&m_rumble_config_large_motor_index, 45, -1);
  sw.DoEx(&m_rumble_config_small_motor_index, 45, -1);
  sw.DoEx(&m_analog_toggle_queued, 45, false);

  MotorState motor_state = m_motor_state;
  sw.Do(&motor_state);

  if (sw.IsReading())
  {
    for (u8 i = 0; i < NUM_MOTORS; i++)
      SetMotorState(i, motor_state[i]);

    if (old_analog_mode != m_analog_mode)
    {
      g_host_interface->AddFormattedOSDMessage(
        5.0f,
        m_analog_mode ?
          g_host_interface->TranslateString("NeGconRumble", "Controller %u switched to analog mode.") :
          g_host_interface->TranslateString("NeGconRumble", "Controller %u switched to digital mode."),
        m_index + 1u);
    }
  }
  return true;
}

void NeGconRumble::SetAxisState(s32 axis_code, float value)
{
  if (axis_code < 0 || axis_code >= static_cast<s32>(Axis::Count))
    return;

  // Steering Axis: -1..1 -> 0..255
  if (axis_code == static_cast<s32>(Axis::Steering))
  {
    float float_value =
      (std::abs(value) < m_steering_deadzone) ?
        0.0f :
        std::copysign((std::abs(value) - m_steering_deadzone) / (1.0f - m_steering_deadzone), value);

    if (m_twist_response == "quadratic")
    {
        if (float_value < 0.0f)
            float_value = -(float_value * float_value);
        else
            float_value = float_value * float_value;
    }
    else if (m_twist_response == "cubic")
    {
        float_value = float_value * float_value * float_value;
    }

    const u8 u8_value = static_cast<u8>(std::clamp(std::round(((float_value + 1.0f) / 2.0f) * 255.0f), 0.0f, 255.0f));
    SetAxisState(static_cast<Axis>(axis_code), u8_value);

    return;
  }

  // I, II, L: -1..1 -> 0..255
  const u8 u8_value = static_cast<u8>(std::clamp(value * 255.0f, 0.0f, 255.0f));

  SetAxisState(static_cast<Axis>(axis_code), u8_value);
}

void NeGconRumble::SetAxisState(Axis axis, u8 value)
{
  if (value != m_axis_state[static_cast<u8>(axis)])
    System::SetRunaheadReplayFlag();

  m_axis_state[static_cast<u8>(axis)] = value;
}

void NeGconRumble::SetButtonState(Button button, bool pressed)
{
  // Mapping of Button to index of corresponding bit in m_button_state
  static constexpr std::array<u8, static_cast<size_t>(Button::Count)> indices = {3, 4, 5, 6, 7, 11, 12, 13};

  if (button == Button::Analog)
  {
    // analog toggle
    if (pressed)
    {
      if (m_command == Command::Idle)
        ProcessAnalogModeToggle();
      else
        m_analog_toggle_queued = true;
    }

    return;
  }

  const u16 bit = u16(1) << static_cast<u8>(button);

  if (pressed)
  {
    if (m_button_state & bit)
      System::SetRunaheadReplayFlag();

    m_button_state &= ~(u16(1) << indices[static_cast<u8>(button)]);
  }
  else
  {
    if (!(m_button_state & bit))
      System::SetRunaheadReplayFlag();

    m_button_state |= u16(1) << indices[static_cast<u8>(button)];
  }
}

void NeGconRumble::SetButtonState(s32 button_code, bool pressed)
{
  if (button_code < 0 || button_code >= static_cast<s32>(Button::Count))
    return;

  SetButtonState(static_cast<Button>(button_code), pressed);
}

u32 NeGconRumble::GetButtonStateBits() const
{
  // flip bits, native data is active low
  return m_button_state ^ 0xFFFF;
}

std::optional<u32> NeGconRumble::GetAnalogInputBytes() const
{
  return m_axis_state[static_cast<size_t>(Axis::L)] << 24 | m_axis_state[static_cast<size_t>(Axis::II)] << 16 |
         m_axis_state[static_cast<size_t>(Axis::I)] << 8 | m_axis_state[static_cast<size_t>(Axis::Steering)];
}

u32 NeGconRumble::GetVibrationMotorCount() const
{
  return NUM_MOTORS;
}

float NeGconRumble::GetVibrationMotorStrength(u32 motor)
{
  if (m_motor_state[motor] == 0)
    return 0.0f;

  // Curve from https://github.com/KrossX/Pokopom/blob/master/Pokopom/Input_XInput.cpp#L210
  const double x =
    static_cast<double>(std::min<u32>(static_cast<u32>(m_motor_state[motor]) + static_cast<u32>(m_rumble_bias), 255));
  const double strength = 0.006474549734772402 * std::pow(x, 3.0) - 1.258165252213538 * std::pow(x, 2.0) +
                          156.82454281087692 * x + 3.637978807091713e-11;

  return static_cast<float>(strength / 65535.0);
}

void NeGconRumble::ResetTransferState()
{
  if (m_analog_toggle_queued)
  {
    ProcessAnalogModeToggle();
    m_analog_toggle_queued = false;
  }

  m_command = Command::Idle;
  m_command_step = 0;
}

void NeGconRumble::SetAnalogMode(bool enabled)
{
  if (m_analog_mode == enabled)
    return;

  g_host_interface->AddFormattedOSDMessage(
    5.0f,
    enabled ? g_host_interface->TranslateString("NeGconRumble", "Controller %u switched to analog mode.") :
              g_host_interface->TranslateString("NeGconRumble", "Controller %u switched to digital mode."),
    m_index + 1u);
  m_analog_mode = enabled;
}

void NeGconRumble::ProcessAnalogModeToggle()
{
  if (m_analog_locked)
  {
    g_host_interface->AddFormattedOSDMessage(
      5.0f,
      m_analog_mode ?
        g_host_interface->TranslateString("NeGconRumble", "Controller %u is locked to analog mode by the game.") :
        g_host_interface->TranslateString("NeGconRumble", "Controller %u is locked to digital mode by the game."),
      m_index + 1u);
  }
  else
  {
    SetAnalogMode(!m_analog_mode);
    ResetRumbleConfig();

    if (m_dualshock_enabled)
      m_status_byte = 0x00;
  }
}

void NeGconRumble::SetMotorState(u8 motor, u8 value)
{
  m_motor_state[motor] = value;
}

u8 NeGconRumble::GetExtraButtonMaskLSB() const
{
  return 0xFF;
}

void NeGconRumble::ResetRumbleConfig()
{
  m_rumble_config.fill(0xFF);

  m_rumble_config_large_motor_index = -1;
  m_rumble_config_small_motor_index = -1;

  SetMotorState(LargeMotor, 0);
  SetMotorState(SmallMotor, 0);
}

void NeGconRumble::SetMotorStateForConfigIndex(int index, u8 value)
{
  if (m_rumble_config_small_motor_index == index)
    SetMotorState(SmallMotor, ((value & 0x01) != 0) ? 255 : 0);
  else if (m_rumble_config_large_motor_index == index)
    SetMotorState(LargeMotor, value);
}

u8 NeGconRumble::GetResponseNumHalfwords() const
{
  if (m_configuration_mode || m_analog_mode)
    return 0x3;

  return (0x1);
}

u8 NeGconRumble::GetModeID() const
{
  if (m_configuration_mode)
    return 0xF;

  if (m_analog_mode)
    return 0x2;

  return 0x4;
}

u8 NeGconRumble::GetIDByte() const
{
  return Truncate8((GetModeID() << 4) | GetResponseNumHalfwords());
}

bool NeGconRumble::Transfer(const u8 data_in, u8* data_out)
{
  bool ack;
  m_rx_buffer[m_command_step] = data_in;

  switch (m_command)
  {
    case Command::Idle:
    {
      *data_out = 0xFF;

      if (data_in == 0x01)
      {
        m_command = Command::Ready;
        return true;
      }

      return false;
    }
    break;

    case Command::Ready:
    {
      if (data_in == 0x42)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::ReadPad;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      }
      else if (data_in == 0x43)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::ConfigModeSetMode;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      }
      else if (m_configuration_mode && data_in == 0x44)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::SetAnalogMode;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

        ResetRumbleConfig();
      }
      else if (m_configuration_mode && data_in == 0x45)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::GetAnalogMode;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x01, 0x02, BoolToUInt8(m_analog_mode), 0x02, 0x01, 0x00};
      }
      else if (m_configuration_mode && data_in == 0x46)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::Command46;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      }
      else if (m_configuration_mode && data_in == 0x47)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::Command47;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
      }
      else if (m_configuration_mode && data_in == 0x4C)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::Command4C;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      }
      else if (m_configuration_mode && data_in == 0x4D)
      {
        m_response_length = (GetResponseNumHalfwords() + 1) * 2;
        m_command = Command::GetSetRumble;
        m_tx_buffer = {GetIDByte(), m_status_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

        m_rumble_config_large_motor_index = -1;
        m_rumble_config_small_motor_index = -1;
      }
      else
      {
        *data_out = 0xFF;
        return false;
      }
    }
    break;

    case Command::ReadPad:
    {
      const int rumble_index = m_command_step - 2;

      switch (m_command_step)
      {
        case 2:
        {
          m_tx_buffer[m_command_step] = Truncate8(m_button_state) & GetExtraButtonMaskLSB();

          if (m_dualshock_enabled)
            SetMotorStateForConfigIndex(rumble_index, data_in);
        }
        break;

        case 3:
        {
          m_tx_buffer[m_command_step] = Truncate8(m_button_state >> 8);

          if (m_dualshock_enabled)
          {
            SetMotorStateForConfigIndex(rumble_index, data_in);
          }
          else
          {
            bool legacy_rumble_on = (m_rx_buffer[2] & 0xC0) == 0x40 && (m_rx_buffer[3] & 0x01) != 0;
            SetMotorState(SmallMotor, legacy_rumble_on ? 255 : 0);
          }
        }
        break;

        case 4:
        {
          if (m_configuration_mode || m_analog_mode)
            m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::Steering)];

          if (m_dualshock_enabled)
            SetMotorStateForConfigIndex(rumble_index, data_in);
        }
        break;

        case 5:
        {
          if (m_configuration_mode || m_analog_mode)
            m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::I)];

          if (m_dualshock_enabled)
            SetMotorStateForConfigIndex(rumble_index, data_in);
        }
        break;

        case 6:
        {
          if (m_configuration_mode || m_analog_mode)
            m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::II)];

          if (m_dualshock_enabled)
            SetMotorStateForConfigIndex(rumble_index, data_in);
        }
        break;

        case 7:
        {
          if (m_configuration_mode || m_analog_mode)
            m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::L)];

          if (m_dualshock_enabled)
            SetMotorStateForConfigIndex(rumble_index, data_in);
        }
        break;

        default:
        {
        }
        break;
      }
    }
    break;

    case Command::ConfigModeSetMode:
    {
      if (!m_configuration_mode)
      {
        switch (m_command_step)
        {
          case 2:
          {
            m_tx_buffer[m_command_step] = Truncate8(m_button_state) & GetExtraButtonMaskLSB();
          }
          break;

          case 3:
          {
            m_tx_buffer[m_command_step] = Truncate8(m_button_state >> 8);
          }
          break;

          case 4:
          {
            if (m_configuration_mode || m_analog_mode)
              m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::Steering)];
          }
          break;

          case 5:
          {
            if (m_configuration_mode || m_analog_mode)
              m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::I)];
          }
          break;

          case 6:
          {
            if (m_configuration_mode || m_analog_mode)
              m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::II)];
          }
          break;

          case 7:
          {
            if (m_configuration_mode || m_analog_mode)
              m_tx_buffer[m_command_step] = m_axis_state[static_cast<u8>(Axis::L)];
          }
          break;

          default:
          {
          }
          break;
        }
      }

      if (m_command_step == (static_cast<s32>(m_response_length) - 1))
      {
        m_configuration_mode = (m_rx_buffer[2] == 1);

        if (m_configuration_mode)
        {
          m_dualshock_enabled = true;
          m_status_byte = 0x5A;
        }
      }
    }
    break;

    case Command::SetAnalogMode:
    {
      if (m_command_step == 3)
      {
        if (data_in == 0x02 || data_in == 0x03)
          m_analog_locked = (data_in == 0x03);
      }
    }
    break;

    case Command::GetAnalogMode:
    {
      // Intentionally empty, analog mode byte is set in reply buffer when command is first received
    }
    break;

    case Command::Command46:
    {
      if (m_command_step == 2)
      {
        if (data_in == 0x00)
        {
          m_tx_buffer[4] = 0x01;
          m_tx_buffer[5] = 0x02;
          m_tx_buffer[6] = 0x00;
          m_tx_buffer[7] = 0x0A;
        }
        else if (data_in == 0x01)
        {
          m_tx_buffer[4] = 0x01;
          m_tx_buffer[5] = 0x01;
          m_tx_buffer[6] = 0x01;
          m_tx_buffer[7] = 0x14;
        }
      }
    }
    break;

    case Command::Command47:
    {
      if (m_command_step == 2 && data_in != 0x00)
      {
        m_tx_buffer[4] = 0x00;
        m_tx_buffer[5] = 0x00;
        m_tx_buffer[6] = 0x00;
        m_tx_buffer[7] = 0x00;
      }
    }
    break;

    case Command::Command4C:
    {
      if (m_command_step == 2)
      {
        if (data_in == 0x00)
          m_tx_buffer[5] = 0x04;
        else if (data_in == 0x01)
          m_tx_buffer[5] = 0x07;
      }
    }
    break;

    case Command::GetSetRumble:
      {
        int rumble_index = m_command_step - 2;
        if (rumble_index >= 0)
        {
          m_tx_buffer[m_command_step] = m_rumble_config[rumble_index];
          m_rumble_config[rumble_index] = data_in;

          if (data_in == 0x00)
            m_rumble_config_small_motor_index = rumble_index;
          else if (data_in == 0x01)
            m_rumble_config_large_motor_index = rumble_index;
        }

        if (m_command_step == 7)
        {
          if (m_rumble_config_large_motor_index == -1)
            SetMotorState(LargeMotor, 0);

          if (m_rumble_config_small_motor_index == -1)
            SetMotorState(SmallMotor, 0);
        }
      }
      break;
    default:
      break;
  }

  *data_out = m_tx_buffer[m_command_step];

  m_command_step = (m_command_step + 1) % m_response_length;
  ack = (m_command_step == 0) ? false : true;

  if (m_command_step == 0)
  {
    m_command = Command::Idle;

    m_rx_buffer.fill(0x00);
    m_tx_buffer.fill(0x00);
  }

  return ack;
}

std::unique_ptr<NeGconRumble> NeGconRumble::Create(u32 index)
{
  return std::make_unique<NeGconRumble>(index);
}

u32 NeGconRumble::StaticGetVibrationMotorCount()
{
  return NUM_MOTORS;
}

void NeGconRumble::LoadSettings(const char* section)
{
  Controller::LoadSettings(section);
  m_steering_deadzone = g_host_interface->GetFloatSettingValue(section, "SteeringDeadzone", 0.10f);
  m_twist_response = g_host_interface->GetStringSettingValue(section, "TwistResponse");
  m_rumble_bias =
    static_cast<u8>(std::min<u32>(g_host_interface->GetIntSettingValue(section, "VibrationBias", 8), 255));
}

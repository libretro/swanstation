#include "controller.h"
#include "analog_controller.h"
#include "analog_joystick.h"
#include "common/state_wrapper.h"
#include "digital_controller.h"
#include "namco_guncon.h"
#include "negcon.h"
#include "negcon_rumble.h"
#include "playstation_mouse.h"

Controller::Controller() = default;

Controller::~Controller() = default;

void Controller::Reset() {}

bool Controller::DoState(StateWrapper& sw, bool apply_input_state)
{
  return !sw.HasError();
}

void Controller::ResetTransferState() {}

bool Controller::Transfer(const u8 data_in, u8* data_out)
{
  *data_out = 0xFF;
  return false;
}

void Controller::SetAxisState(s32 axis_code, float value) {}

void Controller::SetButtonState(s32 button_code, bool pressed) {}

u32 Controller::GetButtonStateBits() const
{
  return 0;
}

std::optional<u32> Controller::GetAnalogInputBytes() const
{
  return std::nullopt;
}

u32 Controller::GetVibrationMotorCount() const
{
  return 0;
}

float Controller::GetVibrationMotorStrength(u32 motor)
{
  return 0.0f;
}

void Controller::LoadSettings(const char* section) {}

bool Controller::GetSoftwareCursor(const Common::RGBA8Image** image, float* image_scale, bool* relative_mode)
{
  return false;
}

std::unique_ptr<Controller> Controller::Create(ControllerType type, u32 index)
{
  switch (type)
  {
    case ControllerType::DigitalController:
      return DigitalController::Create();

    case ControllerType::AnalogController:
      return AnalogController::Create(index);

    case ControllerType::AnalogJoystick:
      return AnalogJoystick::Create(index);

    case ControllerType::NamcoGunCon:
      return NamcoGunCon::Create();

    case ControllerType::PlayStationMouse:
      return PlayStationMouse::Create();

    case ControllerType::NeGcon:
      return NeGcon::Create();

    case ControllerType::NeGconRumble:
      return NeGconRumble::Create(index);

    case ControllerType::None:
    default:
      return {};
  }
}

u32 Controller::GetVibrationMotorCount(ControllerType type)
{
  switch (type)
  {
    case ControllerType::DigitalController:
      return DigitalController::StaticGetVibrationMotorCount();

    case ControllerType::AnalogController:
      return AnalogController::StaticGetVibrationMotorCount();

    case ControllerType::AnalogJoystick:
      return AnalogJoystick::StaticGetVibrationMotorCount();

    case ControllerType::NamcoGunCon:
      return NamcoGunCon::StaticGetVibrationMotorCount();

    case ControllerType::PlayStationMouse:
      return PlayStationMouse::StaticGetVibrationMotorCount();

    case ControllerType::NeGcon:
      return NeGcon::StaticGetVibrationMotorCount();

    case ControllerType::NeGconRumble:
      return NeGconRumble::StaticGetVibrationMotorCount();

    case ControllerType::None:
    default:
      return 0;
  }
}

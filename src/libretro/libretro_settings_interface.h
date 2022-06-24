#pragma once
#include "core/settings.h"

class LibretroSettingsInterface : public SettingsInterface
{
public:
  int GetIntValue(const char* section, const char* key, int default_value = 0) override;
  float GetFloatValue(const char* section, const char* key, float default_value = 0.0f) override;
  bool GetBoolValue(const char* section, const char* key, bool default_value = false) override;
  std::string GetStringValue(const char* section, const char* key, const char* default_value = "") override;

  std::vector<std::string> GetStringList(const char* section, const char* key) override;
};

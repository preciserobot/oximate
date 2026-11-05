#include "radio.h"

#include <Preferences.h>

static const char *const kNamespace = "oximate";
static const char *const kKey = "radio";

RadioConfig RadioSettings::load()
{
  RadioConfig config;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return config;
  RadioConfig stored;
  if (prefs.getBytesLength(kKey) == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == RadioConfig::kVersion) {
    config = stored;
  }
  prefs.end();
  return config;
}

void RadioSettings::save(const RadioConfig &config)
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKey, &config, sizeof(config));
    prefs.end();
  }
}

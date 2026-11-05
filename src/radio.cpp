#include "radio.h"

#include <Preferences.h>
#if defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#endif

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

#if defined(ESP_PLATFORM)
static const esp_partition_t *otherSlot()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = esp_ota_get_next_update_partition(running);
  return other && other != running ? other : nullptr;
}

bool RadioFirmware::otherInstalled()
{
  const esp_partition_t *other = otherSlot();
  esp_app_desc_t desc;
  return other && esp_ota_get_partition_description(other, &desc) == ESP_OK;
}

bool RadioFirmware::bootOther()
{
  const esp_partition_t *other = otherSlot();
  return other && otherInstalled() && esp_ota_set_boot_partition(other) == ESP_OK;
}
#else
bool RadioFirmware::otherInstalled() { return false; }
bool RadioFirmware::bootOther() { return false; }
#endif

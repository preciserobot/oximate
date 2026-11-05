#include "leds.h"

#include <utility/led/LED_Strip_Class.hpp>

static constexpr uint32_t kFrameMillis = 16;
static constexpr float kDecayPerFrame = 0.85f; // ~100 ms fade
static constexpr uint8_t kLevelBrightness[] = {0, 8, 32, 96};

RGBColor noteColor(uint8_t note)
{
  // HSV hue from pitch class, full saturation and value
  float h = (note % 12) / 12.0f * 6.0f;
  int sector = (int)h;
  uint8_t rise = (h - sector) * 255;
  uint8_t fall = 255 - rise;
  switch (sector) {
    case 0:  return RGBColor(255, rise, 0);
    case 1:  return RGBColor(fall, 255, 0);
    case 2:  return RGBColor(0, 255, rise);
    case 3:  return RGBColor(0, fall, 255);
    case 4:  return RGBColor(rise, 0, 255);
    default: return RGBColor(255, 0, fall);
  }
}

void Leds::begin()
{
  // M5Unified configures a single LED on the Core2; the GO Bottom2 has 10.
  // M5.begin() does not start the LED driver, so replacing the instance here is safe.
  auto bus = std::make_shared<m5::LedBus_RMT>();
  auto busConfig = bus->getConfig();
  busConfig.pin_data = M5.getPin(m5::pin_name_t::rgb_led);
  bus->setConfig(busConfig);

  auto strip = std::make_shared<m5::LED_Strip_Class>();
  auto stripConfig = strip->getConfig();
  stripConfig.led_count = kCount;
  strip->setBus(bus);
  strip->setConfig(stripConfig);

  M5.Led.setLedInstance(strip);
  M5.Led.setAutoDisplay(false);
  M5.Led.begin();
  M5.Led.setAllColor(RGBColor(0, 0, 0));
  M5.Led.display();
}

void Leds::setLevel(uint8_t level)
{
  M5.Led.setBrightness(kLevelBrightness[level < 4 ? level : 3]);
  _lit = true; // force a redraw with the new brightness
}

void Leds::activity(const MidiMessage &msg)
{
  // Two strips: radio input (BLE, network) flashes on the USB side
  Flash &flash = _flash[msg.source == MidiSource::Din ? 1 : 0];
  uint8_t type = msg.status() & 0xF0;
  if (type == 0x90 && msg.bytes[2] > 0) {
    flash.color = noteColor(msg.bytes[1]);
    flash.intensity = 0.2f + 0.8f * msg.bytes[2] / 127.0f;
  } else if (msg.isSysEx()) {
    flash.color = RGBColor(255, 0, 255);
    flash.intensity = 1;
  } else if (msg.isChannel() && type != 0x80 && flash.intensity < 0.3f) {
    flash.color = RGBColor(255, 255, 255); // CC, bend, pressure: dim white
    flash.intensity = 0.3f;
  }
}

void Leds::claim(uint8_t owner, int first, int count)
{
  for (int i = first; i < first + count && i < kCount; i++) {
    _owner[i] = owner;
    _pixel[i] = RGBColor(0, 0, 0);
  }
  _lit = true;
}

void Leds::release(uint8_t owner)
{
  for (int i = 0; i < kCount; i++) {
    if (_owner[i] == owner) _owner[i] = 0;
  }
  _lit = true;
}

void Leds::setPixel(uint8_t owner, int index, RGBColor color)
{
  if (index >= 0 && index < kCount && _owner[index] == owner) {
    _pixel[index] = color;
  }
}

void Leds::setOverlay(const RGBColor *colors)
{
  _overlayOn = colors != nullptr;
  if (colors) {
    for (int i = 0; i < kCount; i++) _overlay[i] = colors[i];
  }
  _lit = true;
}

void Leds::update()
{
  uint32_t now = millis();
  if (now - _lastFrame < kFrameMillis) {
    return;
  }
  _lastFrame = now;

  bool any = false;
  bool claimed = false;
  for (int side = 0; side < 2; side++) {
    Flash &flash = _flash[side];
    RGBColor c(flash.color.R8() * flash.intensity, flash.color.G8() * flash.intensity,
                   flash.color.B8() * flash.intensity);
    for (int i = 0; i < kPerSide; i++) {
      int index = side * kPerSide + i;
      if (_overlayOn) {
        M5.Led.setColor(index, _overlay[index]);
        claimed = true;
      } else if (_owner[index]) {
        M5.Led.setColor(index, _pixel[index]);
        claimed = true;
      } else {
        M5.Led.setColor(index, c);
      }
    }
    if (flash.intensity > 0.01f) {
      flash.intensity *= kDecayPerFrame;
      any = true;
    } else {
      flash.intensity = 0;
    }
  }
  // Keep pushing frames while fading or while a mode owns LEDs, plus one final frame to turn off
  if (any || claimed || _lit) {
    M5.Led.display();
  }
  _lit = any || claimed;
}

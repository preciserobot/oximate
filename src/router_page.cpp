#include "router_page.h"

using namespace Layout;

// Rows of the page, top to bottom
static constexpr int kRoutesY = kContentTop + 4;
static constexpr int kChipHeight = 26;
static constexpr int kUsbLabelY = kRoutesY + kChipHeight + 4;
static constexpr int kGridCellWidth = 40;
static constexpr int kGridCellHeight = 22;
static constexpr int kGridHeight = 2 * kGridCellHeight;
static constexpr int kUsbGridY = kUsbLabelY + kCharHeight + 2;
static constexpr int kDinLabelY = kUsbGridY + kGridHeight + 4;
static constexpr int kDinGridY = kDinLabelY + kCharHeight + 2;
static constexpr int kOptionsY = kDinGridY + kGridHeight + 4;

static constexpr int kRouteChipWidth = 320 / 3;
static constexpr int kOptionChipWidth = 320 / 4;

static const char *const kLedLevels[] = {"LEDs:off", "LEDs:low", "LEDs:med", "LEDs:high"};
static const char *const kClockNames[] = {"Clk INT", "Clk USB", "Clk DIN"};

static void drawChannelGrid(LovyanGFX &target, int y, uint16_t mask, uint16_t color)
{
  char text[3];
  for (int ch = 0; ch < 16; ch++) {
    snprintf(text, sizeof(text), "%d", ch + 1);
    drawChip(target, (ch % 8) * kGridCellWidth, y + (ch / 8) * kGridCellHeight, kGridCellWidth, kGridCellHeight, text,
             mask & (1 << ch), color);
  }
}

static void forceChannelText(const char *input, uint8_t force, char *out, size_t size)
{
  if (force) {
    snprintf(out, size, "%s>ch%d", input, force);
  } else {
    snprintf(out, size, "%s>keep", input);
  }
}

void RouterPage::draw()
{
  const RouterConfig &c = _router.config();
  canvas().fillRect(0, kContentTop, canvas().width(), kContentHeight, TFT_BLACK);

  drawChip(canvas(), 0 * kRouteChipWidth, kRoutesY, kRouteChipWidth, kChipHeight, "USB>DIN",
           c.routed(MidiSource::Usb, MidiOutput::Din));
  drawChip(canvas(), 1 * kRouteChipWidth, kRoutesY, kRouteChipWidth, kChipHeight, "DIN>DIN",
           c.routed(MidiSource::Din, MidiOutput::Din));
  drawChip(canvas(), 2 * kRouteChipWidth, kRoutesY, kRouteChipWidth, kChipHeight, "DIN>USB",
           c.routed(MidiSource::Din, MidiOutput::Usb));

  canvas().setTextColor(kUsbColor);
  canvas().drawString("USB in channels", 4, kUsbLabelY);
  drawChannelGrid(canvas(), kUsbGridY, c.channelMask[(int)MidiSource::Usb], kUsbColor);

  canvas().setTextColor(kDinColor);
  canvas().drawString("DIN in channels", 4, kDinLabelY);
  drawChannelGrid(canvas(), kDinGridY, c.channelMask[(int)MidiSource::Din], kDinColor);

  char usbForce[10], dinForce[10];
  forceChannelText("USB", c.forceChannel[(int)MidiSource::Usb], usbForce, sizeof(usbForce));
  forceChannelText("DIN", c.forceChannel[(int)MidiSource::Din], dinForce, sizeof(dinForce));
  drawChip(canvas(), 0 * kOptionChipWidth, kOptionsY, kOptionChipWidth, kChipHeight, kClockNames[(int)_clock.config().mode],
           true);
  drawChip(canvas(), 1 * kOptionChipWidth, kOptionsY, kOptionChipWidth, kChipHeight, c.sysex ? "SysEx on" : "SysEx off",
           c.sysex);
  drawChip(canvas(), 2 * kOptionChipWidth, kOptionsY, kOptionChipWidth, kChipHeight, usbForce,
           c.forceChannel[(int)MidiSource::Usb]);
  drawChip(canvas(), 3 * kOptionChipWidth, kOptionsY, kOptionChipWidth, kChipHeight, dinForce,
           c.forceChannel[(int)MidiSource::Din]);
}

void RouterPage::onTouch(int x, int y)
{
  RouterConfig &c = _router.config();
  auto inRow = [y](int top, int height) { return y >= top && y < top + height; };

  if (inRow(kRoutesY, kChipHeight)) {
    switch (x / kRouteChipWidth) {
      case 0: c.setRouted(MidiSource::Usb, MidiOutput::Din, !c.routed(MidiSource::Usb, MidiOutput::Din)); break;
      case 1: c.setRouted(MidiSource::Din, MidiOutput::Din, !c.routed(MidiSource::Din, MidiOutput::Din)); break;
      default: c.setRouted(MidiSource::Din, MidiOutput::Usb, !c.routed(MidiSource::Din, MidiOutput::Usb)); break;
    }
  } else if (inRow(kUsbGridY, kGridHeight) || inRow(kDinGridY, kGridHeight)) {
    MidiSource input = inRow(kUsbGridY, kGridHeight) ? MidiSource::Usb : MidiSource::Din;
    int top = input == MidiSource::Usb ? kUsbGridY : kDinGridY;
    int ch = ((y - top) / kGridCellHeight) * 8 + min(x / kGridCellWidth, 7);
    c.channelMask[(int)input] ^= (1 << ch);
  } else if (inRow(kOptionsY, kChipHeight)) {
    switch (x / kOptionChipWidth) {
      case 0: { // shortcut for the Clock page's source setting
        ClockConfig &clock = _clock.config();
        clock.mode = (ClockMode)(((int)clock.mode + 1) % 3);
        _clock.changed();
        break;
      }
      case 1: c.sysex = !c.sysex; break;
      case 2: c.forceChannel[(int)MidiSource::Usb] = (c.forceChannel[(int)MidiSource::Usb] + 1) % 17; break;
      default: c.forceChannel[(int)MidiSource::Din] = (c.forceChannel[(int)MidiSource::Din] + 1) % 17; break;
    }
  } else {
    return; // labels and gaps
  }
  changed();
}

void RouterPage::buttonLabels(const char *labels[3]) const
{
  labels[0] = "All ch";
  labels[1] = kLedLevels[_router.config().ledLevel & 3];
  labels[2] = "Defaults";
}

void RouterPage::onButton(int index)
{
  RouterConfig &c = _router.config();
  switch (index) {
    case 0:
      for (uint16_t &mask : c.channelMask) mask = 0xFFFF;
      break;
    case 1:
      c.ledLevel = (c.ledLevel + 1) % 4;
      _leds.setLevel(c.ledLevel);
      break;
    case 2:
      _router.resetDefaults();
      _leds.setLevel(_router.config().ledLevel);
      break;
  }
  changed();
}

void RouterPage::changed()
{
  _router.changed();
  markDirty();
}

// Stage 14: SysEx librarian (parser, USB packets, DIN streaming, send job, capture)
#include "test_support.h"
#include "input_observers.h"
#define private public
#include "sysex_capture.h"
#include "sysex_sender.h"
#undef private
#include "syx_file.h"

// The Moog Subharmonicon files from subH_sysex_user/ (inspected byte by byte)
static const uint8_t kSubhCh1[] = {0xF0, 0x04, 0x1B, 0x23, 0x01, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xF7};
static const uint8_t kSubhTransportOn[] = {0xF0, 0x04, 0x1B, 0x23, 0x02, 0x00, 0x01, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xF7};
static const uint8_t kSubhFirmware[] = {0xF0, 0x04, 0x1B, 0x30, 0xF7};

TEST(testSyxSplit)
{
  Syx::Message messages[8];
  Syx::Stats stats;
  CHECK(Syx::split(kSubhCh1, sizeof(kSubhCh1), messages, 8, stats) == 1);
  CHECK(stats.messages == 1 && stats.bytes == 17 && stats.stray == 0 && !stats.truncated);
  CHECK(messages[0].offset == 0 && messages[0].length == 17);
  CHECK(strcmp(Syx::manufacturer(kSubhCh1, 17), "Moog") == 0 && strcmp(Syx::slug(kSubhFirmware, 5), "moog") == 0);
  CHECK(!Syx::isBroadcast(kSubhCh1, 17));

  // Several messages with stray bytes between them, and a file ending inside a message
  std::vector<uint8_t> file = {0x00, 0x12};
  file.insert(file.end(), kSubhFirmware, kSubhFirmware + 5);
  file.push_back(0x55);
  file.insert(file.end(), kSubhTransportOn, kSubhTransportOn + sizeof(kSubhTransportOn));
  file.insert(file.end(), {0xF0, 0x00, 0x20, 0x3C, 0x01});
  int n = Syx::split(file.data(), file.size(), messages, 8, stats);
  CHECK(n == 2 && stats.messages == 2 && stats.bytes == 22 && stats.stray == 3 + 5 && stats.truncated);
  CHECK(messages[0].offset == 2 && messages[1].offset == 8 && messages[1].length == 17);

  // A status byte inside a message ends it unfinished; more messages than fit are still counted
  const uint8_t broken[] = {0xF0, 0x7E, 0x7F, 0x90, 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
  n = Syx::split(broken, sizeof(broken), messages, 8, stats);
  CHECK(n == 1 && messages[0].offset == 4 && Syx::isBroadcast(broken + 4, 6));
  CHECK(strcmp(Syx::manufacturer(broken + 4, 6), "Universal") == 0);
  const uint8_t many[] = {0xF0, 0x43, 0xF7, 0xF0, 0x43, 0xF7, 0xF0, 0x43, 0xF7};
  n = Syx::split(many, sizeof(many), messages, 2, stats);
  CHECK(n == 2 && stats.messages == 3);
  const uint8_t elektron[] = {0xF0, 0x00, 0x20, 0x3C, 0x0C, 0xF7};
  CHECK(strcmp(Syx::manufacturer(elektron, 6), "Elektron") == 0);
}

TEST(testSyxFileNames)
{
  char name[48];
  Syx::fileName(name, sizeof(name), "elektron", 2026, 11, 1, 21, 30, 45, 7);
  CHECK(strcmp(name, "elektron-20261101-213045.syx") == 0);
  Syx::fileName(name, sizeof(name), "elektron", 2000, 1, 1, 0, 0, 0, 7); // clock never set
  CHECK(strcmp(name, "elektron-0007.syx") == 0);
}

TEST(testUsbSysExPackets)
{
  // Lengths ending with 1, 2 and 3 bytes after the last full packet: CIN 5, 6, 7
  for (int extra = 1; extra <= 3; extra++) {
    Syx::UsbPacker packer;
    std::vector<uint8_t> message = {0xF0};
    while (message.size() < (size_t)(6 + extra - 1)) message.push_back(0x11);
    message.push_back(0xF7); // 6 + extra bytes: two full packets, then `extra`
    std::vector<uint8_t> packets;
    uint8_t packet[4];
    for (uint8_t b : message) {
      if (packer.push(b, 0, packet)) packets.insert(packets.end(), packet, packet + 4);
    }
    CHECK(packets.size() == 12);
    CHECK(packets[0] == 0x04 && packets[1] == 0xF0 && packets[4] == 0x04);
    CHECK(packets[8] == 4 + extra && packets[8 + extra] == 0xF7);
  }
}

TEST(testDinSendRawHoldsOtherOutput)
{
  OutputRig rig;
  DinMidi &din = rig.din;
  din.sendRaw(kSubhCh1, 6); // F0 ... (open)
  CHECK(din.rawOpen());
  uint8_t note[3] = {0x90, 60, 100}, clock = 0xF8;
  din.send({MidiSource::Usb, 0, note, 3});  // must wait for the F7
  din.send({MidiSource::Usb, 0, &clock, 1}); // realtime may go inside
  din.sendRaw(kSubhCh1 + 6, sizeof(kSubhCh1) - 6);
  CHECK(!din.rawOpen());
  std::vector<uint8_t> out = rig.dinBytes();
  std::vector<uint8_t> expected = {0xF8};
  expected.insert(expected.end(), kSubhCh1, kSubhCh1 + sizeof(kSubhCh1));
  expected.insert(expected.end(), {0x90, 60, 100});
  CHECK(out == expected); // clock first, the SysEx unbroken, the note after its F7
}

TEST(testSysExSender)
{
  OutputRig rig;
  rig.usb.poll(); // the fake USB device connects
  gUsbSent.clear();
  SysExCapture capture;
  SysExSender sender;
  sender.begin(rig.din, rig.usb, &capture);

  // Two messages to DIN and USB with a 20 ms gap
  std::vector<uint8_t> file(kSubhFirmware, kSubhFirmware + 5);
  file.insert(file.end(), kSubhCh1, kSubhCh1 + sizeof(kSubhCh1));
  Syx::Message messages[4];
  Syx::Stats stats;
  int count = Syx::split(file.data(), file.size(), messages, 4, stats);
  CHECK(sender.start(file.data(), messages, count, SysExSender::kDin | SysExSender::kUsb, 20));
  CHECK(!sender.start(file.data(), messages, count, SysExSender::kDin, 0)); // busy
  uint32_t now = 1000;
  sender.run(now);
  CHECK(sender.progress().message == 1 && sender.progress().sentBytes == 5); // first message, then the gap
  sender.run(now + 10);
  CHECK(sender.progress().sentBytes == 5);
  sender.run(now + 21);
  SysExSender::Progress p = sender.progress();
  CHECK(p.state == SysExSender::State::Done && p.sentBytes == 22 && p.totalBytes == 22);
  CHECK(rig.dinBytes() == file);
  rig.usb.poll(); // flush the USB send queue
  std::vector<uint8_t> packets;
  for (auto &transfer : gUsbSent) packets.insert(packets.end(), transfer.begin(), transfer.end());
  // 5 bytes -> 2 packets; 17 bytes -> 6 packets
  CHECK(packets.size() == 8 * 4 && packets[0] == 0x04 && packets[4] == 0x06 && packets[8] == 0x04);
  CHECK(capture._holdOffUntil > now); // our own send is not captured if it comes back

  // A long message streams through in chunks; cancel stops after its F7
  std::vector<uint8_t> big(5000, 0x22);
  big[0] = 0xF0;
  big.back() = 0xF7;
  big.insert(big.end(), kSubhFirmware, kSubhFirmware + 5);
  count = Syx::split(big.data(), big.size(), messages, 4, stats);
  CHECK(count == 2 && sender.start(big.data(), messages, count, SysExSender::kDin, 0));
  sender.run(2000);
  sender.cancel();
  std::vector<uint8_t> out;
  for (int i = 0; i < 200 && sender.busy(); i++) {
    sender.run(2001 + i);
    std::vector<uint8_t> part = rig.dinBytes();
    out.insert(out.end(), part.begin(), part.end());
  }
  std::vector<uint8_t> rest = rig.dinBytes();
  out.insert(out.end(), rest.begin(), rest.end());
  CHECK(sender.progress().state == SysExSender::State::Cancelled);
  CHECK(out.size() == 5000 && out.back() == 0xF7); // the whole first message, nothing after
}

TEST(testSysExCapture)
{
  static uint8_t buffer[64 * 1024];
  SysExCapture capture;
  capture.begin(buffer, sizeof(buffer));
  InputHooks::registry() = InputHooks::Registry();
  capture.registerHooks();
  capture.start(SysExCapture::kDin, true);

  // A 50 KB dump on DIN with clock bytes inside, through the real parser: captured byte-exact
  HardwareSerial serial;
  DinMidi din;
  din.begin(serial, 0, 0, record);
  std::vector<uint8_t> dump(50000, 0x33);
  dump[0] = 0xF0;
  dump[1] = 0x00, dump[2] = 0x20, dump[3] = 0x3C;
  dump.back() = 0xF7;
  gReceived.clear();
  for (size_t i = 0; i < dump.size(); i++) {
    din.parse(dump[i], 0);
    if (i % 1000 == 500) din.parse(0xF8, 0);
  }

  SysExCapture::Status s = capture.status();
  CHECK(s.bytes == 50000 && s.messages == 1 && memcmp(buffer, dump.data(), dump.size()) == 0);
  CHECK(strcmp(Syx::manufacturer(buffer, s.bytes), "Elektron") == 0);
  gReceived.clear(); // the too-long SysEx isn't routed; clock bytes are

  // Idle: the capture ends 2 s after the last byte
  uint32_t last = capture.status().lastByteMillis;
  CHECK(!capture.poll(last + 1000, 2000) && capture.status().active);
  CHECK(capture.poll(last + 2000, 2000) && !capture.status().active);

  // USB packets: split across packets, ending with CIN 6; an unselected input is ignored
  capture.start(SysExCapture::kUsb, true);
  UsbMidi usb;
  usb.begin(record);
  uint8_t p1[4] = {0x04, 0xF0, 0x43, 0x10}, p2[4] = {0x04, 0x01, 0x02, 0x03}, p3[4] = {0x06, 0x04, 0xF7, 0};
  usb.parsePacket(p1, 0);
  usb.parsePacket(p2, 0);
  usb.parsePacket(p3, 0);
  din.parse(0xF0, 0);
  din.parse(0xF7, 0);
  s = capture.status();
  const uint8_t expected[] = {0xF0, 0x43, 0x10, 0x01, 0x02, 0x03, 0x04, 0xF7};
  CHECK(s.bytes == 8 && s.messages == 1 && memcmp(buffer, expected, 8) == 0);

  // Capture only: SysEx from the capturing input isn't routed; other inputs and messages are
  uint8_t sx[3] = {0xF0, 0x43, 0xF7}, note[3] = {0x90, 60, 1};
  CHECK(InputHooks::intercept({MidiSource::Usb, 0, sx, 3}));
  CHECK(!InputHooks::intercept({MidiSource::Din, 0, sx, 3}));
  CHECK(!InputHooks::intercept({MidiSource::Usb, 0, note, 3}));

  // Own echo: bytes during the hold-off are ignored; a full buffer stops the capture
  capture.start(SysExCapture::kUsb | SysExCapture::kDin, false);
  capture.holdOff(5000);
  capture.tap(MidiSource::Din, sx, 3, 4000);
  CHECK(capture.status().bytes == 0);
  capture.tap(MidiSource::Din, sx, 3, 6000);
  CHECK(capture.status().bytes == 3);
  SysExCapture small;
  static uint8_t tiny[4];
  small.begin(tiny, sizeof(tiny));
  small.start(SysExCapture::kDin, false);
  small.tap(MidiSource::Din, dump.data(), 10, 1);
  CHECK(small.status().full && !small.status().active && small.status().bytes == 4);
  InputHooks::registry() = InputHooks::Registry();
}

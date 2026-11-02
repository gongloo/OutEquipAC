#include "ac_framer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class ACFramerTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
  ACFramer framer_;
};

TEST_F(ACFramerTest, EmptyFrame) {
  EXPECT_EQ(0, framer_.buffer_pos());
  EXPECT_FALSE(framer_.HasFullFrame());
  EXPECT_EQ(static_cast<ACFramer::Key>(0), framer_.GetKey());
  EXPECT_STREQ("invalid", framer_.GetKeyAsString());
  EXPECT_EQ(0, framer_.GetValue());
  EXPECT_STREQ("invalid", framer_.GetValueAsString());
}

TEST_F(ACFramerTest, NewPowerOnAndReset) {
  framer_.NewFrame(ACFramer::Key::Power,
                   static_cast<uint16_t>(ACFramer::OnOffValue::On));
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Power, framer_.GetKey());
  EXPECT_EQ(static_cast<uint16_t>(ACFramer::OnOffValue::On),
            framer_.GetValue());

  framer_.Reset();
  EXPECT_EQ(0, framer_.buffer_pos());
  EXPECT_FALSE(framer_.HasFullFrame());
  EXPECT_EQ(static_cast<ACFramer::Key>(0), framer_.GetKey());
  EXPECT_EQ(0, framer_.GetValue());
}

TEST_F(ACFramerTest, FramePowerOn) {
  const uint8_t kPowerOn[] = {0x5a, 0x5a, 0x06, 0x01, 0x01,
                              0x02, 0xbe, 0x0d, 0x0a};
  for (auto c : kPowerOn) {
    EXPECT_TRUE(framer_.FrameData(c));
  }
  EXPECT_EQ(sizeof(kPowerOn), framer_.buffer_pos());
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Power, framer_.GetKey());
  EXPECT_STREQ("power", framer_.GetKeyAsString());
  EXPECT_EQ(static_cast<uint16_t>(ACFramer::OnOffValue::On),
            framer_.GetValue());
  EXPECT_STREQ("on", framer_.GetValueAsString());
}

TEST_F(ACFramerTest, FrameSubZeroIntake) {
  const uint8_t kSubZero[] = {0x5a, 0x5a, 0x06, 0x01, 0x07,
                              0xff, 0xc1, 0x0d, 0x0a};
  for (auto c : kSubZero) {
    EXPECT_TRUE(framer_.FrameData(c));
  }
  EXPECT_EQ(sizeof(kSubZero), framer_.buffer_pos());
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::IntakeAirTemp, framer_.GetKey());
  EXPECT_STREQ("intakeTemp", framer_.GetKeyAsString());
  EXPECT_EQ(-1, static_cast<int8_t>(framer_.GetValue() & 0xFF));
  EXPECT_STREQ("-1", framer_.GetValueAsString());
}

TEST_F(ACFramerTest, FrameHighVoltage) {
  const uint8_t kHighVoltage[] = {0x5a, 0x5a, 0x07, 0x01, 0x12,
                                  0xff, 0xff, 0xcc, 0x0d, 0x0a};
  for (auto c : kHighVoltage) {
    EXPECT_TRUE(framer_.FrameData(c));
  }
  EXPECT_EQ(sizeof(kHighVoltage), framer_.buffer_pos());
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Voltage, framer_.GetKey());
  EXPECT_STREQ("voltage", framer_.GetKeyAsString());
  EXPECT_EQ(65535, framer_.GetValue());
  EXPECT_STREQ("6553.5", framer_.GetValueAsString());
}

TEST_F(ACFramerTest, FrameBadKey) {
  // Key 0x00 is invalid. Early header validation rejects it at byte index 4.
  const uint8_t kBadKey[] = {0x5a, 0x5a, 0x06, 0x01, 0x00,
                             0x02, 0xbd, 0x0d, 0x0a};
  EXPECT_TRUE(framer_.FrameData(kBadKey[0]));
  EXPECT_TRUE(framer_.FrameData(kBadKey[1]));
  EXPECT_TRUE(framer_.FrameData(kBadKey[2]));
  EXPECT_TRUE(framer_.FrameData(kBadKey[3]));
  // 5th byte (Key = 0x00) is rejected immediately!
  EXPECT_FALSE(framer_.FrameData(kBadKey[4]));
}

TEST_F(ACFramerTest, SetTemperatureValidation) {
  // Query frame (0) is valid
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::SetTemperature, 0));
  EXPECT_EQ(0, framer_.GetValue());

  // Celsius range (16 - 30) is valid
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::SetTemperature, 22));
  EXPECT_EQ(22, framer_.GetValue());
  EXPECT_STREQ("22", framer_.GetValueAsString());

  // Fahrenheit range (60 - 86) is valid
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::SetTemperature, 72));
  EXPECT_EQ(72, framer_.GetValue());
  EXPECT_STREQ("72", framer_.GetValueAsString());

  // Out of range (< 16 and not 0, e.g. 15) is invalid
  EXPECT_FALSE(framer_.NewFrame(ACFramer::Key::SetTemperature, 15));

  // Gap between Celsius and Fahrenheit (e.g. 45) is invalid
  EXPECT_FALSE(framer_.NewFrame(ACFramer::Key::SetTemperature, 45));

  // Above Fahrenheit max (> 86, e.g. 90) is invalid
  EXPECT_FALSE(framer_.NewFrame(ACFramer::Key::SetTemperature, 90));
}

TEST_F(ACFramerTest, BadDeviceTypeAndLength) {
  // Bad length (e.g. 0x05)
  framer_.Reset();
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_FALSE(framer_.FrameData(0x05));

  // Bad device type (e.g. 0x02)
  framer_.Reset();
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_TRUE(framer_.FrameData(0x06));
  EXPECT_FALSE(framer_.FrameData(0x02));
}

TEST_F(ACFramerTest, StreamResynchronization) {
  // Preamble noise before valid frame: 0x5a, 0x5a, 0x5a, 0x06, 0x01, 0x01, 0x02, 0xbe, 0x0d, 0x0a
  // Here, 0x5a 0x5a starts, the third 0x5a is invalid length (rejected), but suffix resync
  // preserves [0x5a, 0x5a] as the preamble prefix! Next byte 0x06 is valid length.
  framer_.Reset();
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_TRUE(framer_.FrameData(0x5a));
  // Third 0x5a rejected as length, but suffix resync preserves [0x5a, 0x5a]
  EXPECT_FALSE(framer_.FrameData(0x5a));
  EXPECT_EQ(2, framer_.buffer_pos());
  // Next byte is valid Length (0x06)
  EXPECT_TRUE(framer_.FrameData(0x06));
  EXPECT_TRUE(framer_.FrameData(0x01));
  EXPECT_TRUE(framer_.FrameData(0x01));
  EXPECT_TRUE(framer_.FrameData(0x02));
  EXPECT_TRUE(framer_.FrameData(0xbe));
  EXPECT_TRUE(framer_.FrameData(0x0d));
  EXPECT_TRUE(framer_.FrameData(0x0a));
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Power, framer_.GetKey());
  EXPECT_EQ(static_cast<uint16_t>(ACFramer::OnOffValue::On), framer_.GetValue());
}

TEST_F(ACFramerTest, LightAndLCDValidation) {
  // LCD values: 0 (on), 1 (off), 2 (on)
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::LCD, 0));
  EXPECT_STREQ("on", framer_.GetValueAsString());
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::LCD, 1));
  EXPECT_STREQ("off", framer_.GetValueAsString());
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::LCD, 2));
  EXPECT_STREQ("on", framer_.GetValueAsString());
  EXPECT_FALSE(framer_.NewFrame(ACFramer::Key::LCD, 3));

  // Light values: 0 (query), 1 (on), 2 (off)
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::Light, static_cast<uint16_t>(ACFramer::LightValue::Query)));
  EXPECT_STREQ("query", framer_.GetValueAsString());
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::Light, static_cast<uint16_t>(ACFramer::LightValue::On)));
  EXPECT_STREQ("on", framer_.GetValueAsString());
  EXPECT_TRUE(framer_.NewFrame(ACFramer::Key::Light, static_cast<uint16_t>(ACFramer::LightValue::Off)));
  EXPECT_STREQ("off", framer_.GetValueAsString());
  EXPECT_FALSE(framer_.NewFrame(ACFramer::Key::Light, 3));
}

TEST_F(ACFramerTest, PayloadLengthMismatch) {
  // Key::Power with a 2-byte value length (Length = 7) should fail validation
  const uint8_t kBadPowerLen[] = {0x5a, 0x5a, 0x07, 0x01, 0x01, 0x00, 0x02, 0xbe, 0x0d, 0x0a};
  framer_.Reset();
  for (size_t i = 0; i < sizeof(kBadPowerLen) - 1; ++i) {
    EXPECT_TRUE(framer_.FrameData(kBadPowerLen[i]));
  }
  // 10th byte finishes frame but fails validation because Power expects 1-byte value
  EXPECT_FALSE(framer_.FrameData(kBadPowerLen[sizeof(kBadPowerLen) - 1]));
}

TEST_F(ACFramerTest, ChecksumAndPostambleFailures) {
  // Checksum failure: valid frame with corrupt checksum
  const uint8_t kBadChecksum[] = {0x5a, 0x5a, 0x06, 0x01, 0x01, 0x02, 0x00 /* bad */, 0x0d, 0x0a};
  framer_.Reset();
  for (size_t i = 0; i < sizeof(kBadChecksum) - 1; ++i) {
    EXPECT_TRUE(framer_.FrameData(kBadChecksum[i]));
  }
  EXPECT_FALSE(framer_.FrameData(kBadChecksum[sizeof(kBadChecksum) - 1]));

  // Postamble failure: valid checksum but corrupt postamble
  const uint8_t kBadPostamble[] = {0x5a, 0x5a, 0x06, 0x01, 0x01, 0x02, 0xbe, 0x0d, 0x00 /* bad */};
  framer_.Reset();
  for (size_t i = 0; i < sizeof(kBadPostamble) - 1; ++i) {
    EXPECT_TRUE(framer_.FrameData(kBadPostamble[i]));
  }
  EXPECT_FALSE(framer_.FrameData(kBadPostamble[sizeof(kBadPostamble) - 1]));
}

TEST_F(ACFramerTest, StreamResynchronizationSinglePreambleAndRecovery) {
  // Single 0x5a followed by noise, then a valid frame
  framer_.Reset();
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_FALSE(framer_.FrameData(0x12)); // Not 0x5a -> resets to 0
  EXPECT_EQ(0, framer_.buffer_pos());

  // Repeated 0x5a bytes before valid frame (e.g. 0x5a, 0x5a, 0x5a)
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_TRUE(framer_.FrameData(0x5a));
  EXPECT_FALSE(framer_.FrameData(0x5a)); // rejected as length, but suffix resync preserves [0x5a, 0x5a]
  EXPECT_EQ(2, framer_.buffer_pos());

  const uint8_t kValidPower[] = {0x06, 0x01, 0x01, 0x02, 0xbe, 0x0d, 0x0a};
  for (auto c : kValidPower) {
    EXPECT_TRUE(framer_.FrameData(c));
  }
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Power, framer_.GetKey());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
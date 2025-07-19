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
  EXPECT_EQ(0, framer_.GetValue());
  EXPECT_EQ(0, framer_.GetUnknown());
}

TEST_F(ACFramerTest, NewPowerOnAndReset) {
  framer_.NewFrame(ACFramer::Key::Power,
                   static_cast<uint16_t>(ACFramer::OnOffValue::On));
  EXPECT_TRUE(framer_.HasFullFrame());
  EXPECT_EQ(ACFramer::Key::Power, framer_.GetKey());
  EXPECT_EQ(static_cast<uint16_t>(ACFramer::OnOffValue::On),
            framer_.GetValue());
  EXPECT_EQ(1, framer_.GetUnknown());

  framer_.Reset();
  EXPECT_EQ(0, framer_.buffer_pos());
  EXPECT_FALSE(framer_.HasFullFrame());
  EXPECT_EQ(static_cast<ACFramer::Key>(0), framer_.GetKey());
  EXPECT_EQ(0, framer_.GetValue());
  EXPECT_EQ(0, framer_.GetUnknown());
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
  EXPECT_EQ(static_cast<uint16_t>(ACFramer::OnOffValue::On),
            framer_.GetValue());
  EXPECT_EQ(1, framer_.GetUnknown());
}

TEST_F(ACFramerTest, FrameBadKey) {
  const uint8_t kBadKey[] = {0x5a, 0x5a, 0x06, 0x01, 0x00,
                             0x02, 0xbd, 0x0d, 0x0a};
  for (int i = 0; i < sizeof(kBadKey); ++i) {
    if (i < sizeof(kBadKey) - 1) {
      EXPECT_TRUE(framer_.FrameData(kBadKey[i]));
    } else {
      EXPECT_FALSE(framer_.FrameData(kBadKey[i]));
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // if you plan to use GMock, replace the line above with
  // ::testing::InitGoogleMock(&argc, argv);

  if (RUN_ALL_TESTS())
    ;

  // Always return zero-code and allow PlatformIO to parse results
  return 0;
}
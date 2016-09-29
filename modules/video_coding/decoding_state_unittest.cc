/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string.h>

#include "webrtc/test/gtest.h"
#include "webrtc/modules/include/module_common_types.h"
#include "webrtc/modules/video_coding/decoding_state.h"
#include "webrtc/modules/video_coding/frame_buffer.h"
#include "webrtc/modules/video_coding/jitter_buffer_common.h"
#include "webrtc/modules/video_coding/packet.h"

namespace webrtc {

TEST(TestDecodingState, Sanity) {
  VCMDecodingState dec_state;
  dec_state.Reset();
  EXPECT_TRUE(dec_state.in_initial_state());
  EXPECT_TRUE(dec_state.full_sync());
}

TEST(TestDecodingState, FrameContinuity) {
  VCMDecodingState dec_state;
  // Check that makes decision based on correct method.
  VCMFrameBuffer frame;
  VCMFrameBuffer frame_key;
  VCMPacket packet;
  packet.isFirstPacket = true;
  packet.timestamp = 1;
  packet.seqNum = 0xffff;
  packet.frameType = kVideoFrameDelta;
  packet.video_header.codec = kRtpVideoVp8;
  packet.video_header.codecHeader.VP8.pictureId = 0x007F;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  // Always start with a key frame.
  dec_state.Reset();
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  packet.frameType = kVideoFrameKey;
  EXPECT_LE(0, frame_key.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame_key));
  dec_state.SetState(&frame);
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  // Use pictureId
  packet.isFirstPacket = false;
  packet.video_header.codecHeader.VP8.pictureId = 0x0002;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  frame.Reset();
  packet.video_header.codecHeader.VP8.pictureId = 0;
  packet.seqNum = 10;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));

  // Use sequence numbers.
  packet.video_header.codecHeader.VP8.pictureId = kNoPictureId;
  frame.Reset();
  packet.seqNum = dec_state.sequence_num() - 1u;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  frame.Reset();
  packet.seqNum = dec_state.sequence_num() + 1u;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  // Insert another packet to this frame
  packet.seqNum++;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  // Verify wrap.
  EXPECT_LE(dec_state.sequence_num(), 0xffff);
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Insert packet with temporal info.
  dec_state.Reset();
  frame.Reset();
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 0;
  packet.seqNum = 1;
  packet.timestamp = 1;
  EXPECT_TRUE(dec_state.full_sync());
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());
  frame.Reset();
  // 1 layer up - still good.
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 1;
  packet.video_header.codecHeader.VP8.pictureId = 1;
  packet.seqNum = 2;
  packet.timestamp = 2;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());
  frame.Reset();
  // Lost non-base layer packet => should update sync parameter.
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 3;
  packet.video_header.codecHeader.VP8.pictureId = 3;
  packet.seqNum = 4;
  packet.timestamp = 4;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  // Now insert the next non-base layer (belonging to a next tl0PicId).
  frame.Reset();
  packet.video_header.codecHeader.VP8.tl0PicIdx = 1;
  packet.video_header.codecHeader.VP8.temporalIdx = 2;
  packet.video_header.codecHeader.VP8.pictureId = 4;
  packet.seqNum = 5;
  packet.timestamp = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  // Checking continuity and not updating the state - this should not trigger
  // an update of sync state.
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  EXPECT_TRUE(dec_state.full_sync());
  // Next base layer (dropped interim non-base layers) - should update sync.
  frame.Reset();
  packet.video_header.codecHeader.VP8.tl0PicIdx = 1;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 5;
  packet.seqNum = 6;
  packet.timestamp = 6;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());

  // Check wrap for temporal layers.
  frame.Reset();
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0x00FF;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 6;
  packet.seqNum = 7;
  packet.timestamp = 7;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());
  frame.Reset();
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0x0000;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 7;
  packet.seqNum = 8;
  packet.timestamp = 8;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  // The current frame is not continuous
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
}

TEST(TestDecodingState, UpdateOldPacket) {
  VCMDecodingState dec_state;
  // Update only if zero size and newer than previous.
  // Should only update if the timeStamp match.
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.timestamp = 1;
  packet.seqNum = 1;
  packet.frameType = kVideoFrameDelta;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_EQ(dec_state.sequence_num(), 1);
  // Insert an empty packet that does not belong to the same frame.
  // => Sequence num should be the same.
  packet.timestamp = 2;
  dec_state.UpdateOldPacket(&packet);
  EXPECT_EQ(dec_state.sequence_num(), 1);
  // Now insert empty packet belonging to the same frame.
  packet.timestamp = 1;
  packet.seqNum = 2;
  packet.frameType = kEmptyFrame;
  packet.sizeBytes = 0;
  dec_state.UpdateOldPacket(&packet);
  EXPECT_EQ(dec_state.sequence_num(), 2);
  // Now insert delta packet belonging to the same frame.
  packet.timestamp = 1;
  packet.seqNum = 3;
  packet.frameType = kVideoFrameDelta;
  packet.sizeBytes = 1400;
  dec_state.UpdateOldPacket(&packet);
  EXPECT_EQ(dec_state.sequence_num(), 3);
  // Insert a packet belonging to an older timestamp - should not update the
  // sequence number.
  packet.timestamp = 0;
  packet.seqNum = 4;
  packet.frameType = kEmptyFrame;
  packet.sizeBytes = 0;
  dec_state.UpdateOldPacket(&packet);
  EXPECT_EQ(dec_state.sequence_num(), 3);
}

TEST(TestDecodingState, MultiLayerBehavior) {
  // Identify sync/non-sync when more than one layer.
  VCMDecodingState dec_state;
  // Identify packets belonging to old frames/packets.
  // Set state for current frames.
  // tl0PicIdx 0, temporal id 0.
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.frameType = kVideoFrameDelta;
  packet.video_header.codec = kRtpVideoVp8;
  packet.timestamp = 0;
  packet.seqNum = 0;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 0;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  // tl0PicIdx 0, temporal id 1.
  frame.Reset();
  packet.timestamp = 1;
  packet.seqNum = 1;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 1;
  packet.video_header.codecHeader.VP8.pictureId = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());
  // Lost tl0PicIdx 0, temporal id 2.
  // Insert tl0PicIdx 0, temporal id 3.
  frame.Reset();
  packet.timestamp = 3;
  packet.seqNum = 3;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 3;
  packet.video_header.codecHeader.VP8.pictureId = 3;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());
  // Insert next base layer
  frame.Reset();
  packet.timestamp = 4;
  packet.seqNum = 4;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 1;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 4;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());
  // Insert key frame - should update sync value.
  // A key frame is always a base layer.
  frame.Reset();
  packet.frameType = kVideoFrameKey;
  packet.isFirstPacket = 1;
  packet.timestamp = 5;
  packet.seqNum = 5;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 2;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());
  // After sync, a continuous PictureId is required
  // (continuous base layer is not enough )
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.timestamp = 6;
  packet.seqNum = 6;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 3;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 6;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  EXPECT_TRUE(dec_state.full_sync());
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.isFirstPacket = 1;
  packet.timestamp = 8;
  packet.seqNum = 8;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 4;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 8;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  EXPECT_TRUE(dec_state.full_sync());
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());

  // Insert a non-ref frame - should update sync value.
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.isFirstPacket = 1;
  packet.timestamp = 9;
  packet.seqNum = 9;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 4;
  packet.video_header.codecHeader.VP8.temporalIdx = 2;
  packet.video_header.codecHeader.VP8.pictureId = 9;
  packet.video_header.codecHeader.VP8.layerSync = true;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());

  // The following test will verify the sync flag behavior after a loss.
  // Create the following pattern:
  // Update base layer, lose packet 1 (sync flag on, layer 2), insert packet 3
  // (sync flag on, layer 2) check continuity and sync flag after inserting
  // packet 2 (sync flag on, layer 1).
  // Base layer.
  frame.Reset();
  dec_state.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.isFirstPacket = 1;
  packet.markerBit = 1;
  packet.timestamp = 0;
  packet.seqNum = 0;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 0;
  packet.video_header.codecHeader.VP8.layerSync = false;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());
  // Layer 2 - 2 packets (insert one, lose one).
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.isFirstPacket = 1;
  packet.markerBit = 0;
  packet.timestamp = 1;
  packet.seqNum = 1;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 2;
  packet.video_header.codecHeader.VP8.pictureId = 1;
  packet.video_header.codecHeader.VP8.layerSync = true;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  // Layer 1
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.isFirstPacket = 1;
  packet.markerBit = 1;
  packet.timestamp = 2;
  packet.seqNum = 3;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 1;
  packet.video_header.codecHeader.VP8.pictureId = 2;
  packet.video_header.codecHeader.VP8.layerSync = true;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  EXPECT_TRUE(dec_state.full_sync());
}

TEST(TestDecodingState, DiscontinuousPicIdContinuousSeqNum) {
  VCMDecodingState dec_state;
  VCMFrameBuffer frame;
  VCMPacket packet;
  frame.Reset();
  packet.frameType = kVideoFrameKey;
  packet.video_header.codec = kRtpVideoVp8;
  packet.timestamp = 0;
  packet.seqNum = 0;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 0;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  EXPECT_TRUE(dec_state.full_sync());

  // Continuous sequence number but discontinuous picture id. This implies a
  // a loss and we have to fall back to only decoding the base layer.
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  packet.timestamp += 3000;
  ++packet.seqNum;
  packet.video_header.codecHeader.VP8.temporalIdx = 1;
  packet.video_header.codecHeader.VP8.pictureId = 2;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);
  EXPECT_FALSE(dec_state.full_sync());
}

TEST(TestDecodingState, OldInput) {
  VCMDecodingState dec_state;
  // Identify packets belonging to old frames/packets.
  // Set state for current frames.
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.timestamp = 10;
  packet.seqNum = 1;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  packet.timestamp = 9;
  EXPECT_TRUE(dec_state.IsOldPacket(&packet));
  // Check for old frame
  frame.Reset();
  frame.InsertPacket(packet, 0, kNoErrors, frame_data);
  EXPECT_TRUE(dec_state.IsOldFrame(&frame));
}

TEST(TestDecodingState, PictureIdRepeat) {
  VCMDecodingState dec_state;
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.frameType = kVideoFrameDelta;
  packet.video_header.codec = kRtpVideoVp8;
  packet.timestamp = 0;
  packet.seqNum = 0;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 0;
  packet.video_header.codecHeader.VP8.temporalIdx = 0;
  packet.video_header.codecHeader.VP8.pictureId = 0;
  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  // tl0PicIdx 0, temporal id 1.
  frame.Reset();
  ++packet.timestamp;
  ++packet.seqNum;
  packet.video_header.codecHeader.VP8.temporalIdx++;
  packet.video_header.codecHeader.VP8.pictureId++;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  frame.Reset();
  // Testing only gap in tl0PicIdx when tl0PicIdx in continuous.
  packet.video_header.codecHeader.VP8.tl0PicIdx += 3;
  packet.video_header.codecHeader.VP8.temporalIdx++;
  packet.video_header.codecHeader.VP8.tl0PicIdx = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
}

TEST(TestDecodingState, FrameContinuityFlexibleModeKeyFrame) {
  VCMDecodingState dec_state;
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.isFirstPacket = true;
  packet.timestamp = 1;
  packet.seqNum = 0xffff;
  uint8_t data[] = "I need a data pointer for this test!";
  packet.sizeBytes = sizeof(data);
  packet.dataPtr = data;
  packet.video_header.codec = kRtpVideoVp9;

  RTPVideoHeaderVP9& vp9_hdr = packet.video_header.codecHeader.VP9;
  vp9_hdr.picture_id = 10;
  vp9_hdr.flexible_mode = true;

  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;

  // Key frame as first frame
  packet.frameType = kVideoFrameKey;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Key frame again
  vp9_hdr.picture_id = 11;
  frame.Reset();
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to 11, continuous
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  vp9_hdr.picture_id = 12;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
}

TEST(TestDecodingState, FrameContinuityFlexibleModeOutOfOrderFrames) {
  VCMDecodingState dec_state;
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.isFirstPacket = true;
  packet.timestamp = 1;
  packet.seqNum = 0xffff;
  uint8_t data[] = "I need a data pointer for this test!";
  packet.sizeBytes = sizeof(data);
  packet.dataPtr = data;
  packet.video_header.codec = kRtpVideoVp9;

  RTPVideoHeaderVP9& vp9_hdr = packet.video_header.codecHeader.VP9;
  vp9_hdr.picture_id = 10;
  vp9_hdr.flexible_mode = true;

  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;

  // Key frame as first frame
  packet.frameType = kVideoFrameKey;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to 10, continuous
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  vp9_hdr.picture_id = 15;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Out of order, last id 15, this id 12, ref to 10, continuous
  frame.Reset();
  vp9_hdr.picture_id = 12;
  vp9_hdr.pid_diff[0] = 2;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref 10, 12, 15, continuous
  frame.Reset();
  vp9_hdr.picture_id = 20;
  vp9_hdr.num_ref_pics = 3;
  vp9_hdr.pid_diff[0] = 10;
  vp9_hdr.pid_diff[1] = 8;
  vp9_hdr.pid_diff[2] = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
}

TEST(TestDecodingState, FrameContinuityFlexibleModeGeneral) {
  VCMDecodingState dec_state;
  VCMFrameBuffer frame;
  VCMPacket packet;
  packet.isFirstPacket = true;
  packet.timestamp = 1;
  packet.seqNum = 0xffff;
  uint8_t data[] = "I need a data pointer for this test!";
  packet.sizeBytes = sizeof(data);
  packet.dataPtr = data;
  packet.video_header.codec = kRtpVideoVp9;

  RTPVideoHeaderVP9& vp9_hdr = packet.video_header.codecHeader.VP9;
  vp9_hdr.picture_id = 10;
  vp9_hdr.flexible_mode = true;

  FrameData frame_data;
  frame_data.rtt_ms = 0;
  frame_data.rolling_average_packets_per_frame = -1;

  // Key frame as first frame
  packet.frameType = kVideoFrameKey;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));

  // Delta frame as first frame
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));

  // Key frame then delta frame
  frame.Reset();
  packet.frameType = kVideoFrameKey;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  dec_state.SetState(&frame);
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.picture_id = 15;
  vp9_hdr.pid_diff[0] = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to 11, not continuous
  frame.Reset();
  vp9_hdr.picture_id = 16;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));

  // Ref to 15, continuous
  frame.Reset();
  vp9_hdr.picture_id = 16;
  vp9_hdr.pid_diff[0] = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to 11 and 15, not continuous
  frame.Reset();
  vp9_hdr.picture_id = 20;
  vp9_hdr.num_ref_pics = 2;
  vp9_hdr.pid_diff[0] = 9;
  vp9_hdr.pid_diff[1] = 5;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));

  // Ref to 10, 15 and 16, continuous
  frame.Reset();
  vp9_hdr.picture_id = 22;
  vp9_hdr.num_ref_pics = 3;
  vp9_hdr.pid_diff[0] = 12;
  vp9_hdr.pid_diff[1] = 7;
  vp9_hdr.pid_diff[2] = 6;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Key Frame, continuous
  frame.Reset();
  packet.frameType = kVideoFrameKey;
  vp9_hdr.picture_id = VCMDecodingState::kFrameDecodedLength - 2;
  vp9_hdr.num_ref_pics = 0;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Frame at last index, ref to KF, continuous
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  vp9_hdr.picture_id = VCMDecodingState::kFrameDecodedLength - 1;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Frame after wrapping buffer length, ref to last index, continuous
  frame.Reset();
  vp9_hdr.picture_id = 0;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Frame after wrapping start frame, ref to 0, continuous
  frame.Reset();
  vp9_hdr.picture_id = 20;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 20;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Frame after wrapping start frame, ref to 10, not continuous
  frame.Reset();
  vp9_hdr.picture_id = 23;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 13;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));

  // Key frame, continuous
  frame.Reset();
  packet.frameType = kVideoFrameKey;
  vp9_hdr.picture_id = 25;
  vp9_hdr.num_ref_pics = 0;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to KF, continuous
  frame.Reset();
  packet.frameType = kVideoFrameDelta;
  vp9_hdr.picture_id = 26;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 1;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_TRUE(dec_state.ContinuousFrame(&frame));
  dec_state.SetState(&frame);

  // Ref to frame previous to KF, not continuous
  frame.Reset();
  vp9_hdr.picture_id = 30;
  vp9_hdr.num_ref_pics = 1;
  vp9_hdr.pid_diff[0] = 30;
  EXPECT_LE(0, frame.InsertPacket(packet, 0, kNoErrors, frame_data));
  EXPECT_FALSE(dec_state.ContinuousFrame(&frame));
}

}  // namespace webrtc

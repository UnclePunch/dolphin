// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_Channel.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "Core/Config/MainSettings.h" // starpole UI pane config settings
#include "Core/NetPlayProto.h"        // needed to get netplay player index
#include "Core/NetPlayClient.h"       // needed to send data over netplay
#include "VideoCommon/VideoConfig.h"  // aspect ratio

namespace ExpansionInterface
{
// Dolphin
void CEXIStarpole::DolphinData_Create(StarpoleDataNetplay* netplay)
{
  memset(netplay, 0, sizeof(*netplay));

  float expected_aspect;
  switch (g_Config.aspect_mode)
  {
  default:
  case AspectMode::Auto:
  case AspectMode::ForceStandard:
    expected_aspect = 4.0f / 3.0f;
    break;
  case AspectMode::ForceWide:
    expected_aspect = 16.0f / 9.0f;
    break;
  // For the custom (relative) case, we want to crop from the native aspect ratio
  // to the specific target one, as they likely have a small difference
  case AspectMode::Custom:
  // There should be no cropping needed in the custom strech case,
  // as output should always exactly match the target aspect ratio
  case AspectMode::CustomStretch:
    expected_aspect = g_ActiveConfig.GetCustomAspectRatio();
    break;
  }

  be_float aspect_mult;
  aspect_mult.FromHost(expected_aspect / (4.0f / 3.0f));
  netplay->aspect_mult = aspect_mult;

  netplay->is_netplay = be_u32::FromHostValue(NetPlay::IsNetPlayRunning());
  netplay->rng_seed = be_u32::FromHostValue(NetPlay_GetGameRNG());
  netplay->ply = be_s32::FromHostValue(GetLocalNetplayIndex());

  if (NetPlay::IsNetPlayRunning())
  {
    // populate name array
    for (int i = 0; i < 4; i++)
    {
      NetPlay::PadDetails pad = NetPlay::GetPadDetails(i);
      if (!pad.player_name.empty())
        strncpy(netplay->usernames[i], pad.player_name.c_str(), sizeof(pad.player_name));
    }
  }

  return;
}

void CEXIStarpole::Netsync_ReceiveGameState(u8* read_ptr, u32 size)
{
  StarpoleDataGameState* state = (StarpoleDataGameState*)read_ptr;

  // index hash
  int frame_idx = state->frame.ToHost() + m_instance_read_start;
  m_gamestate_hash_buffer[frame_idx % PAD_BUFFER_SIZE] = state->hash.ToHost();

  // compare hashes with players
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Checking for desyncs...");
  for (int ply = 0; ply < 4; ply++)
  {
    if (m_player_pad_map[ply] == 0 || m_player_pad_map[ply] == m_local_pid)
      continue;

    // get their most recent hash
    u32 their_hash = m_player_gamestate_hash[ply];
    u32 their_frame = m_player_gamestate_frame[ply];

    // ensure i have a hash for this frame
    if (their_frame >= m_forward_frame)
      continue;

    u32 my_hash = m_gamestate_hash_buffer[their_frame % PAD_BUFFER_SIZE];

    INFO_LOG_FMT(EXPANSIONINTERFACE, "  frame {}: local {} vs p{} {}", their_frame, my_hash, ply,
                 their_hash);

    if (my_hash != their_hash)
    {
      ERROR_LOG_FMT(EXPANSIONINTERFACE, "Desync detected from player {} in port {} on frame {}",
                    ply, m_player_pad_map[ply], their_frame);
    }
  }

  INFO_LOG_FMT(EXPANSIONINTERFACE, "");
  INFO_LOG_FMT(EXPANSIONINTERFACE, "");
}

void CEXIStarpole::Netsync_ReceiveInputs(u8* read_ptr, u32 size)
{
  StarpoleDataInputs* inputs = (StarpoleDataInputs*)read_ptr;

  if (0)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "");
    INFO_LOG_FMT(EXPANSIONINTERFACE, "FRAME {}\n", m_forward_frame);

    INFO_LOG_FMT(EXPANSIONINTERFACE, "received from game:");
    for (int i = 0; i < 4; i++)
    {
      if (inputs->status[i].isConnected)
        continue;

      INFO_LOG_FMT(EXPANSIONINTERFACE, " port {} ({}:{}) 0x{:04X}", i, (s8)inputs->status[i].stickX,
                   (s8)inputs->status[i].stickY, inputs->status[i].button);
    }
  }

  /*
    Reminder:
      1. Dolphin receives local inputs (here)
      2. Dolphin sends all inputs + simulation frames to game
  */

  // send to netplay clients
  NetPlay_SendGameInput(inputs->status);
}
void CEXIStarpole::Netsync_SendInputs(u8* write_ptr)
{
  if (m_sim_frames == 0)
    return;

  GCPadStatus(*local_status)[4] = (GCPadStatus(*)[4])write_ptr;
  NetPad(*pad_buffer)[4] = (m_is_rollback_active) ? m_rollback_buffer : m_delay_buffer;
  int read_frame = (m_forward_frame - m_rollback_num);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Sending to game:");

  for (int i = 0; i < m_sim_frames; i++)
  {
    int arr_idx = ((read_frame + i) + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    int cur_frame = (read_frame + i) - m_instance_read_start;

    INFO_LOG_FMT(EXPANSIONINTERFACE, " Frame {} ({}):", read_frame + i, cur_frame);

    for (int j = 0; j < 4; j++)
    {
      NetPad* this_pad;
      NetPad spectate_pad;

      // get pad
      if (m_is_spectator)
      {
        memset(&spectate_pad, 0, sizeof(spectate_pad));

        if (m_spectate_queue[j].Size() > 0)
          m_spectate_queue[j].Pop(spectate_pad);

        this_pad = &spectate_pad;
      }
      else
        this_pad = &pad_buffer[arr_idx][j];

      // if pad does not exist, set flag before sending
      if (m_player_pad_map[j] == 0)
        this_pad->status = {.isConnected = 1};

      memcpy(&local_status[i][j], &this_pad->status, sizeof(GCPadStatus));

      if (m_player_pad_map[j] != 0)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, "  port {} ({}:{}) 0x{:04X} (arr_idx {}) state {}", j,
                     (s8)local_status[i][j].stickX, (s8)local_status[i][j].stickY,
                     local_status[i][j].button, arr_idx, (int)this_pad->state);
      }
    }
  }
}

void CEXIStarpole::Netsync_UpdateTimeSync()
{
  if (!NetPlay::IsNetPlayRunning() || m_is_spectator)
    return;

  // handle time sync
  if (m_forward_frame % TIME_SYNC_INTERVAL == 0)
  {
    auto offset = NetPlay_GetTimeOffset();

    // Dynamically adjust emulation speed in order to fine-tune time sync to reduce one sided
    // rollbacks even more Modify emulation speed up to a max of 1% at 3 frames offset or more.
    // Don't slow down the front instance as much because we want to prioritize performance for the
    // fast PC
    float deviation = 0;
    float maxSlowDownAmount = 0.005f;
    float maxSpeedUpAmount = 0.01f;
    int slowDownFrameWindow = 3;
    int speedUpFrameWindow = 3;
    if (offset > -250 && offset < 8000)
    {
      // Do nothing, leave deviation at 0 for 100% emulation speed when ahead by 8 ms or less
    }
    else if (offset < 0)
    {
      // Here we are behind, so let's speed up our instance
      float frameWindowMultiplier = std::min(-offset / (speedUpFrameWindow * 16683.0f), 1.0f);
      deviation = frameWindowMultiplier * maxSpeedUpAmount;
    }
    else
    {
      // Here we are ahead, so let's slow down our instance
      float frameWindowMultiplier = std::min(offset / (slowDownFrameWindow * 16683.0f), 1.0f);
      deviation = frameWindowMultiplier * -maxSlowDownAmount;
    }

    auto dynamicEmulationSpeed = 1.0f + deviation;
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, dynamicEmulationSpeed);
    // SConfig::GetInstance().m_EmulationSpeed = 0.97f; // used for testing

    WARN_LOG_FMT(EXPANSIONINTERFACE, "[Frame {}] Offset for advance is: {} us. New speed: {}%",
                 m_forward_frame, offset, dynamicEmulationSpeed * 100.0f);
  }
}

void CEXIStarpole::Netsync_Init(bool is_rollback_active, u32 input_delay)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "setting rollback to {}", is_rollback_active);

  m_is_rollback_active = is_rollback_active;
  m_input_delay = input_delay;

  // m_forward_frame = 0;

  m_confirm_frame = m_forward_frame - 1;
  m_instance_read_start = m_forward_frame;
  m_inputs_sent = m_forward_frame;
  m_instance_idx++;

  for (int i = 0; i < 4; i++)
  {
    m_player_drain_num[i] = m_forward_frame;
    m_player_confirm_num[i] = m_forward_frame;
  }

  NetPlay_ClearTimeOffsets();

  // for (int i = 0; i < 4; i++)
  //   m_player_confirm_num[i] = m_player_drain_num[i];

  // memset(m_player_drain_num, 0, sizeof(m_player_drain_num));
  // memset(m_player_confirm_num, 0, sizeof(m_player_confirm_num));

  // send delay inputs
  for (int i = 0; i < m_input_delay; i++)
  {
    GCPadStatus pad[4];
    memset(pad, 0, sizeof(pad));
    NetPlay_SendGameInput(pad);
  }
}

int CEXIStarpole::Netsync_GetConfirmedInputNum()
{
  int input_num = 0;
  int frames_ahead =
      m_forward_frame - m_confirm_frame;  // yes m_confirm_frame is a u32 set to -1 on the first
                                          // frame, but frames_ahead does resolve to 1 lol
  int read_frame = (m_confirm_frame + 1);

  NetPad(*pad_buffer)[4] = (m_is_rollback_active) ? m_rollback_buffer : m_delay_buffer;

  for (int i = 0; i < frames_ahead; i++)
  {
    int arr_idx = (read_frame + i + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    bool frame_ready = true;

    for (int j = 0; j < 4; j++)
    {
      if (m_player_pad_map[j] == 0)
        continue;

      bool is_player_frame_ready =
          (pad_buffer[arr_idx][j].frame ==
               (u32)(m_confirm_frame + 1 + i) &&                        // input is for this frame
           pad_buffer[arr_idx][j].state >= StarpoleNetPadState::CORRECTED);  // input is confirmed

      if (!is_player_frame_ready)
      {
        frame_ready = false;
        break;
      }
    }

    if (!frame_ready)
      break;

    input_num++;
  }

  return input_num;
}

u32 CEXIStarpole::Netsync_ValidatePrediction(int ply)
{
  // returns number of frames required to rollback

  // skip if not present or is a local player
  if (m_player_pad_map[ply] == 0 || m_player_pad_map[ply] == m_local_pid)
    return 0;

  // if every drained input has been confirmed, its probably not worth validating anything
  if (m_player_confirm_num[ply] == m_player_drain_num[ply])
    return 0;

  // get the last confirmed frame we should check
  u32 confirm_end = (m_player_drain_num[ply] > m_forward_frame + 1) ? m_forward_frame + 1 :
                                                                      m_player_drain_num[ply];

  INFO_LOG_FMT(EXPANSIONINTERFACE, " prediction: validating player {} frames {} to {}...", ply,
               m_player_confirm_num[ply], confirm_end);

  // we are in a prediction branch and received a past input
  // lets validate the predicted input against the one received and determine if we should
  // rollback
  u32 rollback_num = 0;
  for (u32 i = m_player_confirm_num[ply]; i < confirm_end; i++)
  {
    int pad_idx = (i + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;

    // check predicted hash against real hash
    if (m_rollback_buffer[pad_idx][ply].state == StarpoleNetPadState::CORRECTED)
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE, " frame {} port {}. real {:08x} vs predicted {:08x}",
                   m_rollback_buffer[pad_idx][ply].frame, ply,
                   m_rollback_buffer[pad_idx][ply].hash_real,
                   m_rollback_buffer[pad_idx][ply].hash_predict);
      INFO_LOG_FMT(
          EXPANSIONINTERFACE,
          "    real: buttons: 0x{:04X} lstick ({:+04d}, {:+04d}) rstick ({:+04d}, {:+04d}) "
          "triggers ({:04d}, {:04d}) analog AB ({:04d}, {:04d}) isConnected: {} hash: {:08x}",
          m_rollback_buffer[pad_idx][ply].status.button,
          (s8)m_rollback_buffer[pad_idx][ply].status.stickX,
          (s8)m_rollback_buffer[pad_idx][ply].status.stickY,
          (s8)m_rollback_buffer[pad_idx][ply].status.substickX,
          (s8)m_rollback_buffer[pad_idx][ply].status.substickY,
          m_rollback_buffer[pad_idx][ply].status.triggerLeft,
          m_rollback_buffer[pad_idx][ply].status.triggerRight,
          m_rollback_buffer[pad_idx][ply].status.analogA,
          m_rollback_buffer[pad_idx][ply].status.analogB,
          (u8)m_rollback_buffer[pad_idx][ply].status.isConnected,
          m_rollback_buffer[pad_idx][ply].hash_real);

      INFO_LOG_FMT(
          EXPANSIONINTERFACE,
          " predict: buttons: 0x{:04X} lstick ({:+04d}, {:+04d}) rstick ({:+04d}, {:+04d}) "
          "triggers ({:04d}, {:04d}) analog AB ({:04d}, {:04d}) isConnected: {} hash: {:08x}",
          m_rollback_buffer[pad_idx][ply].status_predict.button,
          (s8)m_rollback_buffer[pad_idx][ply].status_predict.stickX,
          (s8)m_rollback_buffer[pad_idx][ply].status_predict.stickY,
          (s8)m_rollback_buffer[pad_idx][ply].status_predict.substickX,
          (s8)m_rollback_buffer[pad_idx][ply].status_predict.substickY,
          m_rollback_buffer[pad_idx][ply].status_predict.triggerLeft,
          m_rollback_buffer[pad_idx][ply].status_predict.triggerRight,
          m_rollback_buffer[pad_idx][ply].status_predict.analogA,
          m_rollback_buffer[pad_idx][ply].status_predict.analogB,
          (u8)m_rollback_buffer[pad_idx][ply].status_predict.isConnected,
          m_rollback_buffer[pad_idx][ply].hash_predict);

      if (m_rollback_buffer[pad_idx][ply].hash_real != m_rollback_buffer[pad_idx][ply].hash_predict)
      {
        // prediction was incorrect, should rollback to here
        INFO_LOG_FMT(EXPANSIONINTERFACE, " frame {} was invalid!", i);

        rollback_num = (m_forward_frame - i);

        INFO_LOG_FMT(EXPANSIONINTERFACE, " rolling back {} frames from forward_frame {}!",
                     rollback_num, m_forward_frame);

        // break out and lets repredict using the last correct input
        break;
      }
    }
  }

  // predicted correctly
  if (rollback_num == 0)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, " frames {} to {} were valid!", m_player_confirm_num[ply],
                 m_player_drain_num[ply]);
  }

  // update confirm num
  INFO_LOG_FMT(EXPANSIONINTERFACE, " updating port {} confirm from {} to {}. drain_num: {}", ply,
               m_player_confirm_num[ply], confirm_end, m_player_drain_num[ply]);
  m_player_confirm_num[ply] = confirm_end;

  return rollback_num;
}

bool CEXIStarpole::Netsync_CheckSimForward()
{
  if (m_is_spectator)
  {
    int present_num = 0;

    // only advance if we are enough frames behind
    for (int i = 0; i < 4; i++)
    {
      // skip if not present
      if (m_player_pad_map[i] == 0)
        continue;

      present_num++;

      if (m_spectate_queue[i].Size() == 0)
        return false;
    }

    if (present_num > 0)
      return true;
    else
      return false;
  }

  if (m_is_rollback_active)
  {
    // debug case to force a large rollback every N frames
    if (FORCE_ROLLBACK)
    {
      m_confirm_frame = m_forward_frame;
      return true;
    }

    // handle replay rollbacks first
    if (replay_state == StarpoleReplayState::PLAYBACK)
      return Playback_CheckSimForward();

    INFO_LOG_FMT(EXPANSIONINTERFACE, "Netsync_CheckSimForward:");
    INFO_LOG_FMT(EXPANSIONINTERFACE, " determining sim_frames for forward_frame {}",
                 m_forward_frame);

    // if (NetPlay::IsNetPlayRunning() && m_inputs_sent - m_input_delay <= m_forward_frame)
    //{
    //   INFO_LOG_FMT(EXPANSIONINTERFACE, " stalling to create time offset due to input delay");
    //   return false;
    // }

    u32 is_sim_forward = false;
    bool is_in_prediction = ((m_forward_frame - m_confirm_frame) > 1);

    // check how many frames of confirmed inputs we have between confirm_frame and forward_frame
    int confirm_num = Netsync_GetConfirmedInputNum();

    INFO_LOG_FMT(EXPANSIONINTERFACE, " confirm_num {}", confirm_num);

    if (confirm_num > 0)
    {
      // update confirm frame
      INFO_LOG_FMT(EXPANSIONINTERFACE, " advancing m_confirm_frame from {} to {}", m_confirm_frame,
                   m_confirm_frame + confirm_num);
      m_confirm_frame += confirm_num;

      if (!is_in_prediction)
      {
        // we havent predicted any inputs, meaning the delay buffer has accounted for all lag.
        INFO_LOG_FMT(EXPANSIONINTERFACE, " got all inputs in time, moving forward");
      }

      is_sim_forward = true;  // simulate forward
    }
    else
    {
      // stall if rollback not enabled
      if (!ROLLBACK_ENABLE)
        return false;
    }

    if (is_in_prediction)
    {
      // we are in a prediction branch
      if ((m_forward_frame - m_confirm_frame) > MAX_ROLLBACK_NUM)
      {
        // we've already predicted the max amount of times, halt simulation until we receive more
        // confirmed frames
        is_sim_forward = false;

        INFO_LOG_FMT(EXPANSIONINTERFACE, " STALLING. max prediction frames reached.");
      }
      else
      {
        is_sim_forward = true;

        INFO_LOG_FMT(EXPANSIONINTERFACE, " input missing. advancing to prediction #{}",
                     m_forward_frame - m_confirm_frame);
      }
    }
    else if (confirm_num == 0)
    {
      // lets branch off to a prediction
      INFO_LOG_FMT(EXPANSIONINTERFACE, " input missing. starting a prediction branch!");

      is_sim_forward = true;
    }

    INFO_LOG_FMT(EXPANSIONINTERFACE,
                 " performing {} sim_frames at forward_frame {} | confirm_frame {}", is_sim_forward,
                 m_forward_frame, m_confirm_frame);

    return is_sim_forward;
  }
  else
  {
    // delay based logic
    if (Netsync_GetConfirmedInputNum())
    {
      m_confirm_frame = m_forward_frame;
      return 1;
    }
    else
      return 0;
  }
}

u32 CEXIStarpole::Netsync_GetRollbackNum()
{
  u32 rollback_num = 0;

  if (!m_is_rollback_active)
    return 0;

  if (m_is_spectator)
    return 0;

  // handle replay rollbacks first
  if (replay_state == StarpoleReplayState::PLAYBACK)
    return Playback_GetRollbackNum();

  if (FORCE_ROLLBACK)
  {
    if (m_forward_frame >= (MAX_ROLLBACK_NUM) && m_forward_frame % MAX_ROLLBACK_NUM == 0)
      return MAX_ROLLBACK_NUM;
    else
      return 0;
  }

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Netsync_GetRollbackNum:");

  // validate newly received player inputs
  for (int i = 0; i < 4; i++)
  {
    u32 ply_rollback_num = Netsync_ValidatePrediction(i);

    if (ply_rollback_num > rollback_num)
      rollback_num = ply_rollback_num;
  }

  // predict missing inputs
  // WARNING: i cannot repredict inputs that are not being resimulated
  for (int i = 0; i < 4; i++)
    Netsync_PredictInputs(i);

  return rollback_num;
}

void CEXIStarpole::Netsync_PredictInputs(int ply)
{
  // skip if not present or is a local player
  if (m_player_pad_map[ply] == 0 || m_player_pad_map[ply] == m_local_pid)
    return;

  // we need at least one confirmed input before predicting
  if (m_player_confirm_num[ply] < 1)
    return;

  // skip if we're not missing any inputs from this player
  if (m_player_confirm_num[ply] > m_forward_frame)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "skipping predictions for port {}, confirm_num {}", ply,
                 m_player_confirm_num[ply]);
    return;
  }

  INFO_LOG_FMT(EXPANSIONINTERFACE, "updating predictions for port {} from frames {} to {}", ply,
               m_player_confirm_num[ply], m_forward_frame);

  for (u32 this_predict_frame = m_player_confirm_num[ply]; this_predict_frame <= m_forward_frame;
       this_predict_frame++)
  {
    int current_idx = (this_predict_frame + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    int last_confirmed_idx = ((m_player_confirm_num[ply] - 1) + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    // int previous_idx = (this_predict_frame - 1 + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;

    // use previous inputs for inputs not received
    m_rollback_buffer[current_idx][ply].frame = this_predict_frame;
    m_rollback_buffer[current_idx][ply].state = StarpoleNetPadState::PREDICTED;
    m_rollback_buffer[current_idx][ply].status = m_rollback_buffer[last_confirmed_idx][ply].status;
    m_rollback_buffer[current_idx][ply].hash_predict =
        m_rollback_buffer[last_confirmed_idx][ply].hash_real;

    INFO_LOG_FMT(EXPANSIONINTERFACE, " predicted frame {} using frame {}'s input!",
                 this_predict_frame, m_player_confirm_num[ply] - 1);
  }
}

u32 ExpansionInterface::CEXIStarpole::NetPlay_HashPadStatus(GCPadStatus* status)
{
  u32 h = 0;
  auto mix = [](u32 h, u32 v) {
    h ^= v;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
  };

  h = mix(h, status->button);
  h = mix(h, (u32)status->stickX << 8 | (u8)status->stickY);
  h = mix(h, (u32)status->substickX << 8 | (u8)status->substickY);
  h = mix(h, (u32)status->triggerLeft << 8 | status->triggerRight);
  return h;
}

u8 ExpansionInterface::CEXIStarpole::NetPlay_ClampStick(u8 val)
{
  s8 sval = (s8)val;

  if (sval < 28 && sval > -28)
    return 0;

  return val;
}

u8 ExpansionInterface::CEXIStarpole::NetPlay_ClampTrigger(u8 val)
{
  if (val < 70)
    return 0;

  return val;
}
}

// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_Starpole.h"

#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_Channel.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/ChunkFile.h"         // save states

#include "Core/Core.h"                // needed to exec code from UI thread on main thread
#include "Core/HW/Memmap.h"           // needed to write directly to game memory using DMA
#include "Core/System.h"              // needed to write directly to game memory using DMA
#include "Core/PowerPC/PowerPC.h"     // 
#include "Core/State.h"               // save states
#include "Core/NetPlayProto.h"        // needed to get netplay player index
#include "Core/NetPlayClient.h"       // needed to send data over netplay
#include "VideoCommon/VideoConfig.h"  // aspect ratio

namespace ExpansionInterface
{
CEXIStarpole::CEXIStarpole(Core::System& system, const std::string& name)
    : IEXIDevice(system), m_name{name}
{
  replay_state = STARPOLE_REPLAYSTATE_NONE;
  SaveState_End();
  NetPlay_InitData();

  m_global_timer = 0;
  m_instance_idx = 0;
  m_instance_read_start = 0;

  memset(m_player_input_num, 0, sizeof(m_player_input_num));
  memset(m_pad_buffer, -1, sizeof(m_pad_buffer));

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI Starpole Init");
}

void CEXIStarpole::DoState(PointerWrap& p)
{
  p.Do(cur_cmd);
  p.Do(cur_args);

  p.Do(m_file_frame_idx);
  p.Do(m_game_frame_idx);
  p.Do(replay_state);

  p.Do(m_is_rollback_active);
  p.Do(m_req_load);
  p.Do(m_savestate_size);
  p.Do(m_is_sim_forward);
  p.Do(m_savestate_num);
  p.DoArray(m_pad_buffer, sizeof(m_pad_buffer) / sizeof(m_pad_buffer[0]));
  p.DoArray(m_player_input_num, sizeof(m_player_input_num) / sizeof(m_player_input_num[0]));
  p.DoArray(m_player_drain_num, sizeof(m_player_drain_num) / sizeof(m_player_drain_num[0]));
  p.DoArray(m_player_confirm_num, sizeof(m_player_confirm_num) / sizeof(m_player_confirm_num[0]));
  p.Do(m_sim_frames);
  p.Do(m_confirm_frame);
  p.Do(m_forward_frame);

  u32 buffer_size = (u32)m_savestate_size * MAX_SAVESTATES;

  bool allocated = m_savestate_alloc != nullptr;
  p.Do(allocated);
  if (allocated)
  {
    if (p.IsReadMode() && !m_savestate_alloc)
      m_savestate_alloc = std::make_unique<u8[]>(buffer_size);
    p.DoArray(m_savestate_alloc.get(), buffer_size);
  }
}

void CEXIStarpole::ImmWrite(u32 data, u32 size)
{
  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE ImmWrite: data {:08x} size {}", data, size);

  // receive an Imm transfer from the game
  cur_cmd = (StarpoleCmd)(data & 0xFFFF); // remember which data the game is requesting
  cur_args = (data & 0xFFFF0000) >> 16;   // pull out args

  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE Imm Received cmd {} args {}", (u32)cur_cmd, cur_args);
}

u32 CEXIStarpole::ImmRead(u32 size)
{
  int response = 0;

  // respond with the appropriate data
  switch (cur_cmd)
  {
  case STARPOLE_CMD_ID:
    response = STARPOLE_DEVICE_ID;          // id for Starpole
    cur_cmd = STARPOLE_CMD_NUM;             // no follow up DMA, null cur_cmd
    break;

  case STARPOLE_CMD_TEST:
    response = sizeof(StarpoleDataTest);    // size of follow-up DMA response
    break;

  case STARPOLE_CMD_REQMATCH:
    response = Match_Prepare();
    break;

  case STARPOLE_CMD_REQFRAME:
    response = Frame_Prepare(cur_args);
    break;

  case STARPOLE_CMD_END:
    End_Receive();
    response = 0;
    cur_cmd = STARPOLE_CMD_NUM;             // no follow up DMA, null cur_cmd
    break;

  case STARPOLE_CMD_CHECKPLAYBACK:
    if (is_playback_queued)
    {
      is_playback_queued = 0;
      response = 1;
    }
    else
      response = 0;
    break;

  case STARPOLE_CMD_DOLPHIN:
    response = 1; // NetPlay::IsNetPlayRunning()
    break;

  case STARPOLE_CMD_NETSTART:     // game is sending preserve sections
    response = 1;
    break;

  case STARPOLE_CMD_NETPADSEND:   // game is sending its inputs
    // to-do ensure the buffer isnt full?
    if (1)
    {
      // m_forward_frame = cur_args;
      response = 1;  // signal we are ready to receive the inputs
    }
    else
      response = 0;    // signal we are ready to receive the inputs

    break;

  case STARPOLE_CMD_NETPADRECV:   // game is requesting inputs
  {
    m_global_timer++;

    // check for remote inputs, copy them to our pad buffer and update
    NetPlay_DrainPadQueue();

    m_is_sim_forward = Netsync_CheckSimForward();
    m_rollback_num = Netsync_GetRollbackNum();

    // request a load state
    if (m_rollback_num > 0)
      m_req_load = m_rollback_num;

    // determine how many frames to simulate
    m_sim_frames = m_is_sim_forward ? (m_rollback_num + 1) : (m_rollback_num);

    // tell game how many frames to simulate
    response = m_sim_frames;

    break;
  }

  case STARPOLE_CMD_NETSAVE:
    if (m_is_rollback_active)
    {
      // load state if needed
      if (m_req_load)
      {
        u32 load_idx = cur_args - m_req_load;
        INFO_LOG_FMT(EXPANSIONINTERFACE, "Loading frame {} ({} - {})", load_idx, cur_args, m_req_load);
        LoadState(load_idx);
        m_req_load = 0;
      }
      else
        SaveState(cur_args);
    }

    response = 1;

    break;

  case STARPOLE_CMD_NETGETCONFIRM:
    response = m_confirm_frame;
    break;

  case STARPOLE_CMD_NETEND:
    SaveState_End();
    response = 1;
    break;

  default:
    response = 0;
  }

  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE Imm Response {:08x}", response);

  return response;
}

void CEXIStarpole::DMAWrite(u32 address, u32 size)
{

  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Receive: {:08x} bytes, from {:08x} to EXI device",
  //              size, address);

  // get pointer to address we will read from
  u8* read_ptr = m_system.GetMemory().GetPointerForRange(address, size);

  // receive the data
  switch (cur_cmd)
  {
  case STARPOLE_CMD_MATCH:
    Match_Receive(read_ptr, size);
    break;
  case STARPOLE_CMD_FRAME:
    Frame_Receive(read_ptr, size);
    break;
  case STARPOLE_CMD_NETSTART:
    SaveState_Init((DolDataSection *)read_ptr, cur_args);
    break;
  case STARPOLE_CMD_NETPADSEND:
    Netsync_ReceiveInputs(read_ptr, size);
    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "DMA Receive not handled!");
    break;
  }

  cur_cmd = STARPOLE_CMD_NUM;  // data has been written to memory, end the current command operation
}

void CEXIStarpole::DMARead(u32 address, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Response to cmd {}: {:08x} bytes, from EXI device to {:08x}",
               (int)cur_cmd, size, address);

  // get pointer to address we will write to
  u8* write_ptr = m_system.GetMemory().GetPointerForRange(address, size);

  // perform the current command's operation
  switch (cur_cmd)
  {
  case STARPOLE_CMD_TEST:
    File::GetUserPath(D_KAR_REPLAY_IDX).copy((char *)write_ptr, sizeof(StarpoleDataTest), 0);
    break;
  case STARPOLE_CMD_REQMATCH:
    Match_Send(write_ptr);
    break;
  case STARPOLE_CMD_REQFRAME:
    Frame_Send(write_ptr, cur_args);
    break;
  case STARPOLE_CMD_DOLPHIN:
    Dolphin_SendInfo(write_ptr);
    break;
  case STARPOLE_CMD_NETPADRECV:
    Netsync_SendInputs(write_ptr);

    if (m_is_sim_forward)
      m_forward_frame++;

    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "DMA Reponse not handled!");
    break;
  }

  cur_cmd = STARPOLE_CMD_NUM;   // data has been written to memory, end the current command operation
}

bool CEXIStarpole::IsPresent() const
{
  return true;
}

void CEXIStarpole::TransferByte(u8& byte)
{
}

bool CEXIStarpole::CheckActive()
{
  return is_active;
}

// Dolphin
void CEXIStarpole::Dolphin_SendInfo(u8* write_ptr)
{
  StarpoleDataNetplay netplay_info;
  memset(&netplay_info, 0, sizeof(netplay_info));

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
  netplay_info.aspect_mult = aspect_mult;

  netplay_info.is_netplay = be_u32::FromHostValue(NetPlay::IsNetPlayRunning());
  netplay_info.rng_seed = be_u32::FromHostValue(NetPlay_GetGameRNG());
  netplay_info.ply = be_s32::FromHostValue(GetLocalNetplayIndex());

  if (NetPlay::IsNetPlayRunning())
  {
    // populate name array
    for (int i = 0; i < 4; i++)
    {
      NetPlay::PadDetails pad = NetPlay::GetPadDetails(i);
      if (!pad.player_name.empty())
        strncpy(netplay_info.usernames[i], pad.player_name.c_str(), sizeof(pad.player_name));
    }
  }

  // write to game memory
  memcpy(write_ptr, (void*)&netplay_info, sizeof(netplay_info));
}
void CEXIStarpole::Netsync_ReceiveInputs(u8* read_ptr, u32 size)
{
  GCPadStatus* status = (GCPadStatus*)read_ptr;
  //NetPad *frame_pads = m_pad_buffer[m_pad_head];

  // to-do: check if the buffer is full and skip receiving game frame and sending inputs

  if (0)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "");
    INFO_LOG_FMT(EXPANSIONINTERFACE, "FRAME {}\n", m_forward_frame);

    INFO_LOG_FMT(EXPANSIONINTERFACE, "received from game:");
    for (int i = 0; i < 4; i++)
    {
      if (status[i].isConnected)
        continue;

      INFO_LOG_FMT(EXPANSIONINTERFACE, " port {} ({}:{}) 0x{:04X}", i, (s8)status[i].stickX,
                   (s8)status[i].stickY, status[i].button);
    }
  }

  /*
    Reminder:
      1. Dolphin receives local inputs (here)
      2. Dolphin sends all inputs + simulation frames to game
  */

  // send to netplay clients
  NetPlay_SendGameInput((GCPadStatus*)status);
}
void CEXIStarpole::Netsync_SendInputs(u8* write_ptr)
{
  if (m_sim_frames == 0)
    return;

  GCPadStatus(*local_status)[4] = (GCPadStatus(*)[4])write_ptr;
  int read_frame = m_instance_read_start + (m_forward_frame - m_rollback_num);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Netsync_SendInputs read_frame: {}", read_frame);
  INFO_LOG_FMT(EXPANSIONINTERFACE, "sending to game:");
  
  for (int i = 0; i < m_sim_frames; i++)
  {
    int arr_idx = ((read_frame + i) + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    int cur_frame = (read_frame + i) - m_instance_read_start;

    for (int j = 0; j < 4; j++)
    {
      if (m_player_pad_map[j] == 0)
        m_pad_buffer[arr_idx][j].status = {.isConnected = 1};

      memcpy(&local_status[i][j], &m_pad_buffer[arr_idx][j].status, sizeof(GCPadStatus));

      if (m_player_pad_map[j] != 0)
      {
        INFO_LOG_FMT(EXPANSIONINTERFACE, " port {} frame {} ({}:{}) 0x{:04X} (arr_idx {}) state {}", j, cur_frame,
                     (s8) local_status[i][j].stickX,
                     (s8)local_status[i][j].stickY,
                     local_status[i][j].button, arr_idx, (int)m_pad_buffer[arr_idx][j].state);
      }
    }
  }
}

void CEXIStarpole::Netsync_Init(u32 input_delay)
{
  m_input_delay = input_delay;

  for (int i = 0; i < 4; i++)
  {
    if (m_player_pad_map[i] == m_local_pid)
    {
      m_instance_read_start = m_player_drain_num[i];
      INFO_LOG_FMT(EXPANSIONINTERFACE, "setting read_start to {}", m_instance_read_start);

      break;
    }
  }

  memset(m_player_drain_num, 0, sizeof(m_player_drain_num));
  memset(m_player_confirm_num, 0, sizeof(m_player_confirm_num));
  // memset(m_player_input_num, 0, sizeof(m_player_input_num));
  // memset(m_pad_buffer, 0, sizeof(m_pad_buffer));
  m_confirm_frame = -1;
  m_forward_frame = 0;
  m_inputs_sent = 0;
  m_instance_idx++;

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
  // need a function to check all inputs from m_last_confirm_idx to (head-1)
  int input_num = 0;
  int frames_ahead = m_forward_frame - m_confirm_frame;
  int read_frame = m_instance_read_start + (m_confirm_frame + 1);

  for (int i = 0; i < frames_ahead; i++)
  {
    int arr_idx = (read_frame + i + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    bool frame_ready = true;

    for (int j = 0; j < 4; j++)
    {
      if (m_player_pad_map[j] == 0)
        continue;

      bool is_player_frame_ready =
          (m_pad_buffer[arr_idx][j].frame == (u32)(m_confirm_frame + 1 + i) &&  // input is for this frame
           m_pad_buffer[arr_idx][j].state >= STARPOLE_NETPAD_CORRECTED);         // input is confirmed

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

  INFO_LOG_FMT(EXPANSIONINTERFACE, "prediction: validating player {} from frame {} to {}...", ply,
               m_player_confirm_num[ply], m_player_drain_num[ply]);

  // we are in a prediction branch and received a past input
  // lets validate the predicted input against the one received and determine if we should
  // rollback
  u32 rollback_num = 0;
  for (u32 i = m_player_confirm_num[ply]; i < m_player_drain_num[ply]; i++)
  {
    int pad_idx = (m_instance_read_start + i + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;

    // check predicted hash against real hash
    if (m_pad_buffer[pad_idx][ply].state == STARPOLE_NETPAD_CORRECTED)
    {
      INFO_LOG_FMT(EXPANSIONINTERFACE,
                   "prediction: validating frame {} port {}. real {:08x} vs predicted {:08x}",
                   m_pad_buffer[pad_idx][ply].frame, ply, m_pad_buffer[pad_idx][ply].hash_real,
                   m_pad_buffer[pad_idx][ply].hash_predict);
      INFO_LOG_FMT(
          EXPANSIONINTERFACE,
          "    real: buttons: 0x{:04X} lstick ({:+04d}, {:+04d}) rstick ({:+04d}, {:+04d}) "
          "triggers ({:04d}, {:04d}) analog AB ({:04d}, {:04d}) isConnected: {} hash: {:08x}",
          m_pad_buffer[pad_idx][ply].status.button, (s8)m_pad_buffer[pad_idx][ply].status.stickX,
          (s8)m_pad_buffer[pad_idx][ply].status.stickY,
          (s8)m_pad_buffer[pad_idx][ply].status.substickX,
          (s8)m_pad_buffer[pad_idx][ply].status.substickY,
          m_pad_buffer[pad_idx][ply].status.triggerLeft,
          m_pad_buffer[pad_idx][ply].status.triggerRight, m_pad_buffer[pad_idx][ply].status.analogA,
          m_pad_buffer[pad_idx][ply].status.analogB,
          (u8)m_pad_buffer[pad_idx][ply].status.isConnected, m_pad_buffer[pad_idx][ply].hash_real);
      u8* data = (u8*)&m_pad_buffer[pad_idx][ply].status;
      for (size_t cur = 0; cur < sizeof(m_pad_buffer[pad_idx][cur].status); cur++)
        printf("%02x ", data[cur]);
      printf("\n");

      INFO_LOG_FMT(
          EXPANSIONINTERFACE,
          " predict: buttons: 0x{:04X} lstick ({:+04d}, {:+04d}) rstick ({:+04d}, {:+04d}) "
          "triggers ({:04d}, {:04d}) analog AB ({:04d}, {:04d}) isConnected: {} hash: {:08x}",
          m_pad_buffer[pad_idx][ply].status_predict.button,
          (s8)m_pad_buffer[pad_idx][ply].status_predict.stickX,
          (s8)m_pad_buffer[pad_idx][ply].status_predict.stickY,
          (s8)m_pad_buffer[pad_idx][ply].status_predict.substickX,
          (s8)m_pad_buffer[pad_idx][ply].status_predict.substickY,
          m_pad_buffer[pad_idx][ply].status_predict.triggerLeft,
          m_pad_buffer[pad_idx][ply].status_predict.triggerRight,
          m_pad_buffer[pad_idx][ply].status_predict.analogA,
          m_pad_buffer[pad_idx][ply].status_predict.analogB,
          (u8)m_pad_buffer[pad_idx][ply].status_predict.isConnected,
          m_pad_buffer[pad_idx][ply].hash_predict);
      data = (u8*)&m_pad_buffer[pad_idx][ply].status_predict;
      for (size_t cur = 0; cur < sizeof(m_pad_buffer[pad_idx][cur].status_predict); cur++)
        printf("%02x ", data[cur]);
      printf("\n");

      if (m_pad_buffer[pad_idx][ply].hash_real != m_pad_buffer[pad_idx][ply].hash_predict)
      {
        // prediction was incorrect, should rollback to here
        INFO_LOG_FMT(EXPANSIONINTERFACE, " frame {} was invalid!", i);

        rollback_num = (m_forward_frame - i);

        // break out and lets repredict using the last correct input
        break;
      }
    }
  }

  // predicted correctly
  if (rollback_num == 0)
  {
    ;
    INFO_LOG_FMT(EXPANSIONINTERFACE, " frames {} to {} were valid!", m_player_confirm_num[ply],
                 m_player_drain_num[ply]);
  }

  return rollback_num;
}

bool CEXIStarpole::Netsync_CheckSimForward()
{
  if (m_is_rollback_active)
  {
    // debug case to force a large rollback every N frames
    if (FORCE_ROLLBACK)
    {
      m_confirm_frame = m_forward_frame;
      return true;
    }

    // handle replay rollbacks first
    if (replay_state == STARPOLE_REPLAYSTATE_PLAYBACK)
      return Playback_CheckSimForward();

    INFO_LOG_FMT(EXPANSIONINTERFACE, "");
    INFO_LOG_FMT(EXPANSIONINTERFACE, "determining sim_frames for forward_frame {}",
                 m_forward_frame);

    u32 is_sim_forward = false;
    bool is_in_prediction = ((m_forward_frame - m_confirm_frame) > 1);

    // check how many frames of confirmed inputs we have between confirm_frame and forward_frame
    int confirm_num = Netsync_GetConfirmedInputNum();

    INFO_LOG_FMT(EXPANSIONINTERFACE, "confirm_num {}", confirm_num);

    if (confirm_num > 0)
    {
      // update confirm frame
      INFO_LOG_FMT(EXPANSIONINTERFACE, " advancing m_confirm_frame from {} to {}", m_confirm_frame,
                   m_confirm_frame + confirm_num);
      m_confirm_frame += confirm_num;

      if (!is_in_prediction)
      {
        // we havent predicted any inputs, meaning the delay buffer has accounted for all lag.
        INFO_LOG_FMT(EXPANSIONINTERFACE, "rollback: got all inputs in time, moving forward");
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
      }
      else
      {
        is_sim_forward = true;

        INFO_LOG_FMT(EXPANSIONINTERFACE, "rollback: input missing. advancing to prediction #{}",
                     m_forward_frame - m_confirm_frame);
      }
    }
    else if (confirm_num == 0)
    {
      // lets branch off to a prediction
      INFO_LOG_FMT(EXPANSIONINTERFACE,
                   "rollback: input missing. starting a prediction branch!");

      is_sim_forward = true;
    }

    INFO_LOG_FMT(EXPANSIONINTERFACE,
                 "rollback: performing {} sim_frames at forward_frame {} | confirm_frame {}",
                 is_sim_forward, m_forward_frame, m_confirm_frame);

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

  // handle replay rollbacks first
  if (replay_state == STARPOLE_REPLAYSTATE_PLAYBACK)
    return Playback_GetRollbackNum();

  if (FORCE_ROLLBACK)
  {
    if (m_forward_frame >= (MAX_ROLLBACK_NUM) && m_forward_frame % MAX_ROLLBACK_NUM == 0)
      return MAX_ROLLBACK_NUM;
    else
      return 0;
  }

  // validate newly received player inputs
  for (int i = 0; i < 4; i++)
  {
    u32 ply_rollback_num = Netsync_ValidatePrediction(i);

    if (ply_rollback_num > rollback_num)
      rollback_num = ply_rollback_num;
  }

  // confirm new inputs
  for (int i = 0; i < 4; i++)
  {
    // skip
    if (m_player_pad_map[i] == 0)
      continue;

    u32 new_confirm =
        (m_player_drain_num[i] > m_forward_frame + 1) ? m_forward_frame + 1 : m_player_drain_num[i];

    INFO_LOG_FMT(EXPANSIONINTERFACE, "updating port {} confirm from {} to {}. drain_num: {}", i,
                 m_player_confirm_num[i], new_confirm, m_player_confirm_num[i]);

    m_player_confirm_num[i] = new_confirm;
  }

  // predict missing inputs
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

  INFO_LOG_FMT(EXPANSIONINTERFACE,
               "updating predictions for port {}... m_confirm_num: {}, m_drain_num: {}", ply,
               m_player_confirm_num[ply], m_player_drain_num[ply]);

  for (u32 this_predict_frame = m_player_confirm_num[ply]; this_predict_frame <= m_forward_frame; this_predict_frame++)
  {
    int current_idx = (m_instance_read_start + this_predict_frame + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;
    int previous_idx = (m_instance_read_start + this_predict_frame - 1 + PAD_BUFFER_SIZE) % PAD_BUFFER_SIZE;

    // use previous inputs for inputs not received
    m_pad_buffer[current_idx][ply].status = m_pad_buffer[previous_idx][ply].status;
    m_pad_buffer[current_idx][ply].frame = this_predict_frame;
    m_pad_buffer[current_idx][ply].state = STARPOLE_NETPAD_PREDICTED;

    if (m_pad_buffer[previous_idx][ply].state == STARPOLE_NETPAD_PREDICTED)
      m_pad_buffer[current_idx][ply].hash_predict = m_pad_buffer[previous_idx][ply].hash_predict;
    else
      m_pad_buffer[current_idx][ply].hash_predict = m_pad_buffer[previous_idx][ply].hash_real;

    INFO_LOG_FMT(EXPANSIONINTERFACE, " predicted frame {}!", this_predict_frame);
  }
}

u32 ExpansionInterface::CEXIStarpole::NetPlay_HashPadStatus(GCPadStatus* status)
{
  u32 h = 0;
  h = h * 131 + status->button;
  h = h * 131 + status->stickX;
  h = h * 131 + status->stickY;
  h = h * 131 + status->substickX;
  h = h * 131 + status->substickY;
  h = h * 131 + status->triggerLeft;
  h = h * 131 + status->triggerRight;
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

u32 CEXIStarpole::Netsync_GetLocalInputNum()
{
  for (int i = 0; i < 4; i++)
  {
    if (m_player_pad_map[i] == m_local_pid)
      return m_player_input_num[i];
  }

  return 0;
}

// Recording
void CEXIStarpole::Match_Receive(u8 *read_ptr, u32 size)
{
  memcpy((void*)&match_data, read_ptr, size);

  int active_ply_num = 0;
  for (int i = 0; i < 4; i++)
  {
    if (match_data.ply_desc[i].p_kind != 4)
      active_ply_num++;
  }

  //INFO_LOG_FMT(EXPANSIONINTERFACE,
  //             "Received {}p match being played on gr_kind {} with stadium {}. RNG Seed: {:08x}",
  //             active_ply_num, match_data.stage_kind.ToHost(), match_data.stadium_kind,
  //             match_data.rng_seed.ToHost());

  // create a file
  CreateFile(GenerateReplayFilename());
  WriteFile((uint8_t*)&match_data, size);
  replay_state = STARPOLE_REPLAYSTATE_RECORD;
}
void CEXIStarpole::Frame_Receive(u8* read_ptr, u32 size)
{
  StarpoleDataFrame frame;

  memcpy((void*)&frame, read_ptr, size);

  WriteFile((uint8_t*)&frame, size);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: wrote game frame {}",
               frame.frame_idx.ToHost());

  /*
  const u32 ply_count = std::min<u32>(frame.ply_num, 4);

  // Per-player data
  for (u32 i = 0; i < ply_count; i++)
  {
     const auto& ply = frame.ply[i];

    INFO_LOG_FMT(EXPANSIONINTERFACE, "Ply {}", ply.idx);

    INFO_LOG_FMT(EXPANSIONINTERFACE,
                 "  Inputs: LStick({}, {}) RStick({}, {}) Buttons: {:08x}",
                 ply.input.stickX, ply.input.stickY,
                 ply.input.substickX, ply.input.substickY,
                 (int)ply.input.down << 4);

    INFO_LOG_FMT(EXPANSIONINTERFACE, "");

  }
  */

}
void CEXIStarpole::End_Receive()
{
  int terminator = -1;
  WriteFile((uint8_t*)&terminator, sizeof(terminator));
  CloseFile();


  INFO_LOG_FMT(EXPANSIONINTERFACE, "Match end.");
}

// Playback
int CEXIStarpole::Match_Prepare()
{
  try
  {
    OpenFile(replay_file_path);
    return 1;
  }
  catch (const std::exception& e)
  {
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "{}", e.what());
    return 0;
  }
}
void CEXIStarpole::Match_Send(u8* write_ptr)
{
  // read match data
  ReadFileOffset((uint8_t*)&match_data, 0, sizeof(match_data));

  // write to game memory
  memcpy(write_ptr, (void*)&match_data, sizeof(match_data));
  
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Frame Size 0x{:X}", match_data.frame_size.ToHost());

  m_file_frame_idx = 0;
  m_game_frame_idx = 0;
  replay_state = STARPOLE_REPLAYSTATE_PLAYBACK;
}
int CEXIStarpole::Frame_Prepare(int index)
{
  int frame_size = match_data.frame_size.ToHost();
  int offset = sizeof(StarpoleDataMatch) + m_file_frame_idx * frame_size;
  int file_size = ReadFileSize();

  if (offset + frame_size > file_size)
    return 0;

  return frame_size;
}
void CEXIStarpole::Frame_Send(u8* write_ptr, u32 index)
{
  // read match data
  StarpoleDataFrame frame;

  int frame_size = match_data.frame_size.ToHost();
  int offset = sizeof(StarpoleDataMatch) + m_file_frame_idx * frame_size;

  ReadFileOffset((uint8_t*)&frame, offset, frame_size);
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: game requested frame {}, sending frame {}. file_frame: {}", index, frame.frame_idx.ToHost(), m_file_frame_idx);

  // write to game memory
  memcpy(write_ptr, (void*)&frame, sizeof(frame));

  m_game_frame_idx = index; // update the game frame we are on
  m_file_frame_idx++;       // update the file frame we are on
}

bool CEXIStarpole::Playback_CheckSimForward()
{
  //m_confirm_frame++;
  //return true;

  // does the next frame in the file correspond to the next game frame? if yes sim forward
  // does the next frame in the file come before the next game frame? does the next frame 

  int frame_size = match_data.frame_size.ToHost();
  int file_size = ReadFileSize();
  StarpoleDataFrame frame;
  int offset;
  u32 replay_frame;
  u32 game_frame = m_game_frame_idx + 1;

  bool is_sim_forward = false;
  offset = sizeof(StarpoleDataMatch) + (m_file_frame_idx)*frame_size;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: checking sim forward for game_frame {}...", game_frame);

  // first frame
  if (m_file_frame_idx == 0)
    is_sim_forward = true;
  else
  {
    if (offset + frame_size >= file_size)
      is_sim_forward = true;
    else
    {
      ReadFileOffset((uint8_t*)&frame, offset, frame_size);
      replay_frame = frame.frame_idx.ToHost();

      INFO_LOG_FMT(EXPANSIONINTERFACE, " next frame in replay is for frame {} (file_frame {})",
                   replay_frame, m_file_frame_idx);

      // is the next frame in the replay game_frame?
      if (replay_frame == game_frame)
        is_sim_forward = true;

      // rollback impending
      else if (replay_frame < game_frame)
      {
        u32 rollback_num = game_frame - replay_frame;
        INFO_LOG_FMT(EXPANSIONINTERFACE, " detected {} rollbacks", rollback_num);

        // does game_frame proceed the rollback?
        offset = sizeof(StarpoleDataMatch) + (m_file_frame_idx + rollback_num) * frame_size;
        if (offset + frame_size >= file_size)
          is_sim_forward = true;
        else
        {
          ReadFileOffset((uint8_t*)&frame, offset, frame_size);
          replay_frame = frame.frame_idx.ToHost();

          INFO_LOG_FMT(EXPANSIONINTERFACE, " frame after rollbacks is for frame {} (file_frame {})",
                       replay_frame, m_file_frame_idx + rollback_num);

          if (replay_frame == game_frame)
            is_sim_forward = true;
        }
      }
    }
  }

  if (is_sim_forward)
    m_confirm_frame++;

  INFO_LOG_FMT(EXPANSIONINTERFACE, " is_sim_forward: {}", is_sim_forward);

  return is_sim_forward;
}
u32 CEXIStarpole::Playback_GetRollbackNum()
{
  // peek at next frame, see if we need to rollback
  int frame_size = match_data.frame_size.ToHost();
  int offset = sizeof(StarpoleDataMatch) + m_file_frame_idx * frame_size;
  int file_size = ReadFileSize();

  if (offset + frame_size > file_size)
    return 0;


  // read frame
  StarpoleDataFrame frame;
  ReadFileOffset((uint8_t*)&frame, offset, frame_size);
  u32 replay_frame = frame.frame_idx.ToHost();
  u32 game_frame = m_game_frame_idx + 1;                // need to + 1 here because the netsync code runs before the replay stuff...

  // idk...
  if (replay_frame == 0)
    return 0;    

  // if next frame is earlier, rollback game state
  if (replay_frame < game_frame)
  {
    u32 rollback_num = game_frame - replay_frame;   
    INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: performing {} rollbacks on game_frame {}, file_frame {}, replay_frame {}",
                 rollback_num, game_frame, m_file_frame_idx, replay_frame);
    return rollback_num;
  }

  return 0;
}

std::string CEXIStarpole::GenerateReplayFilename()
{
  using namespace std::chrono;

  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);  // must use localtime_r on POSIX

  // generate unique filename based on current time and date
  std::ostringstream filename;
  filename << "replay_"
     << std::put_time(&tm, "%Y%m%d_%H%M%S")
     << ".krf";

  // // remember last created replay
  // std::ofstream out(recent_file_path);
  // out << filename.str() << '\n';
  // out.close();

  return File::GetUserPath(D_KAR_REPLAY_IDX) + filename.str();
}

int CEXIStarpole::GetLocalNetplayIndex()
{
  if (!NetPlay::IsNetPlayRunning())
    return -1;

  for (int i = 0; i < 4; i++)
  {
    NetPlay::PadDetails pad = NetPlay::GetPadDetails(i);
    if (pad.is_local)
      return i;
  }

  return -1;
}

void CEXIStarpole::SetReplay(std::string path)
{
  replay_file_path = path;
  is_playback_queued = 1;
  INFO_LOG_FMT(EXPANSIONINTERFACE,
               "Set replay file to {}", replay_file_path);
}

static DolDataSection m_preserve_sections[] = {
    // {0, 0},
    {0x80003100, 0x2500},     // dol text section 1
    {0x80005800, 0x483C40},   // dol text section 2
    {0, 0},                   // stay
    {0, 0},                   // AllM
    {0x00000000, 0x96000},    // XFB buffer 1 80589a48
    {0x00000000, 0x96000},    // XFB buffer 2 80589a4c
    {0x00000000, 0x80000},    // gx init alloc in arena lo, performed at 8040fc3c
    {0, 0},                   // audio heap
    {0, 0},                   // hoshi + mods
    {0x80550f68, 0x1008},     // file preload table

    // 
    // 0x1358 - 0x146C inclusive
    // audio heap allocs
    // ptr @ 0x1360 - size is 0x300
    // ptr @ 0x13ac - size is 0x4800
    // ptr @ 0x13c0 - size is 0x240. this is a disc read struct
    // 80535994 - size 0x4000. AR region. also 0xF30 -> 0xF38 inclusive
    // ARQ region. 0xF40 -> 0xF64 inclusive
    // 0x8058e7d8, size 340. AxFxUnk1
    // 0x8058ec18 size 96. AxFxUnk2
    // 
    // 80599c60 -> 8059a818
    // HSD ID data is at 0x8058bc94, size 404. must back this up
    // 8056d958 - thread data? unsure of size, referenced @ 803d9e8c. also some r13 variables, E48 - E50 inclusive
    // interrupt data. 0xD80 -> 0xE0C

    {0x80508bc8, 0x4 * 3},                      // BGM PID's. needed to stop a song from playing
    // {0x80535994, 16 * 4},                     // AR region, actual size is 16 * 4
    // {0x80538088, 0x17a28},                    // AudioSourceTable
    // {0x805383c4, 0xB8 * 512},                 // just audio emitters?
    {0x805dd0e0 + 0xF20, 0xF68 - 0xF20},      // ARQ and hsd audio sbss

    {0x805dd0e0 + 0xAC, 0x8},                 // 64 bitfield that is raised when the corresponding sg has its volume changed, 0x8044c450

    {0x80599c60, 0x8059a818 - 0x80599c60},    // more audio stuff. sg indexed audio data in here @ 8059a178 and 8059a160?
    {0x805dd0e0 + 0x1358, 0x1470 - 0x1358},   // hsd audio sbss

    {0x8056ccb4, 0x24},                       // DVD Waiting Queue
    {0x8056cb40, 0xE0},                       // DVD Interrupt stuff @ 803c40b4. includes alarm
    {0x8056cc20, 0x94},                       // DVD state stuff @ 803c67f0. another alarm at 0x70 of this?

    //{0x8056CCB4, 0x1D34},                    // lots of stuff, VI, SI, etc. just testing
    //{0x8056e3a0, 0x144},                      // VI Frame Buffer stuff @ 803df32c
    //{0x805dd0e0 + 0xED8, 0xEDC - 0xEB0},      // VI Frame Buffer variables

    {0x805dd0e0 + 0xC60, 0xCF4 - 0xC60},      // disc read variables

    {0x805dd0e0 + 0xDC8, 0xDD0 - 0xDC8},      // OSAlarm variables

    {0x805dd0e0 + 0x4C8, 0x4},                // file async load flag

    {0x8056e9e8, 0x80587A60 - 0x8056e9e8},     // all the AX data i know of, AXStack head -> end of __AXVPB
    {0x805dd0e0 + 0xF30, 0x1054 - 0xF30},      // AX region sbss

    {0x8058e298, 64 * 0x4},                   // array of VPB pointers? indexed by FGMInstance index
    {0x8058E398, 0x8F8},                      // unknown in between chunks, part of this is the fgm_kind struct, referenced @ 80442a24
    {0x8058ec90, 0x90},                       // hps stream unk struct @ 804464bc
    {0x8058ed20, 2 * 0x4000},                 // hps double buffer?
    {0x80596d20, 0x40},                       // hps streaming @ 80446a74
    {0x80596d60, 0x50},                       // hps streaming stuff
    {0x80596da0, 160 + (512*3)},              // FGM region, multiple offsets of this loaded around 80447ee4. also includes some HPS streaming stuff
    {0x80597440, 0x220},                      // unknown in between chunks

    {0x80597660, 64 * 0x98},                  // AXLive voice array. (8044ccf0)
    {0x80597F20, 64 * 152},                   // static audio lookup 0X8c0 (8044ccf0)
    // above ends at 0x8059A520 for reference

    {0x8059a880, 0x618},                      // memcard thread data? referenced by the function 8045b848 in the thread func
    {0x805b4698, 0x35C},                      // memcard thread data

    //{0x805dd0e0 + 0x13A0, 3 * 0x4},   // unk at 804422c8

    //// disc reads
    //{0x805dd0e0 + 0x13C0, 8 * 0x4},  // unk at 804422c8
    //
    // // AXAlloc
    //{0x8056e9e8, 128},                // AXStack head
    //{0x8056ea68, 128},                // AXStack tail
    //{0x805dd0e0 + 0xFA0, 0x4},        // __AXCallbackStack
    //
    // // AXAux
    //{0x805dd0e0 + 0xFA8, 0x4},        // __AXCallbackAuxA
    //{0x805dd0e0 + 0xFAC, 0x4},        // __AXCallbackAuxB
    //{0x805dd0e0 + 0xFB0, 0x4},        // __AXContextAuxA
    //{0x805dd0e0 + 0xFB4, 0x4},        // Unk
    //{0x805dd0e0 + 0xFB8, 0x4},        // __AXAuxADspWrite
    //{0x805dd0e0 + 0xFBC, 0x4},        // __AXAuxADspRead
    //{0x805dd0e0 + 0xFB4, 0x4},        // __AXContextAuxB
    //{0x805dd0e0 + 0xFC0, 0x4},        // __AXAuxBDspWrite
    //{0x805dd0e0 + 0xFC4, 0x4},        // __AXAuxBDspRead
    //{0x805dd0e0 + 0xFC8, 0x4},        // __AXAuxDspWritePosition
    //{0x805dd0e0 + 0xFCC, 0x4},        // __AXAuxDspReadPosition
    //{0x805dd0e0 + 0xFD8, 0x4},        // __AXAuxCpuReadWritePosition
    //{0x8056eb00, 11520},              // __AXBufferAuxA
    //{0x80570180, 11520},              // __AXBufferAuxB
    //
    // // AXCl
    //{0x805dd0e0 + 0xFF0, 0x4},        // __AXClMode
    //{0x805dd0e0 + 0xFE0, 0x4},        // __AXCommandListPosition
    //{0x805dd0e0 + 0xFE4, 0x4},        // __AXClWrite
    //{0x805dd0e0 + 0xFEC, 0x4},        // unk
    //{0x80571800, 1536},               // __AXCommandList
    // 
    // // AXOut
    //{0x805dd0e0 + 0xFF8, 0x4},        // __AXOutDspReady
    //{0x805dd0e0 + 0x1014, 0x4},       // 
    //{0x805dd0e0 + 0x1018, 0x4},       // __AXUserFrameCallback
    //{0x80571e00, 1280},               // __AXOutBuffer
    //{0x80572300, 640},                // __AXOutBuffer
    //
    // // AXSPB
    //{0x805dd0e0 + 0x1020, 9 * 4},    // 
    //
    // // AXVPB
    //{0x805dd0e0 + 0x1048, 0x4},           // __AXMaxDspCycles
    //{0x805dd0e0 + 0x104C, 0x4},           // __AXRecDspCycles
    //{0x80576620, 0x40},                   // AXStudio
    //{0x80576660, 0x138 + (0xEC * 64)},    // __AXServiceVPB related function data @ 803edc48
    //{0x8057a160, 0x1000},                 // __AXITD
    //{0x8057b160, 0x4000},                 // __AXUpdates
    //{0x8057f160, 0x8900},                 // __AXVPB
    //{0x805dd0e0 + 0x1050, 0x4},           // __AXNumVoices
    };

void CEXIStarpole::SaveState_GetChunkSizes(std::vector<DolDataSection> sections, u32 section_num,
                                           std::vector<std::pair<u32, u32>>& chunks)
{
  u32 current = 0x80000000;
  const u32 section_end = current + (24 * 1024 * 1024);

  // sort sections
  std::sort(sections.begin(), sections.end(),
            [](auto& a, auto& b) { return a.address < b.address; });

  for (u32 i = 0; i < section_num; i++)
  {
    DolDataSection ex = sections[i];
    u32 ex_start = ex.address;
    u32 ex_end = ex.address + ex.size;

    // No overlap
    if (ex_end <= current || ex_start >= section_end)
      continue;

    // Copy region before exclusion
    if (ex_start > current)
    {
      u32 chunk_size = ex_start - current;
      u8* ptr = m_system.GetMemory().GetPointerForRange(current, chunk_size);
      if (ptr)
        chunks.push_back({current, chunk_size});
    }

    current = std::max(current, ex_end);
  }

  // Copy tail after last exclusion
  if (current < section_end)
  {
    u32 chunk_size = section_end - current;
    u8* ptr = m_system.GetMemory().GetPointerForRange(current, chunk_size);
    if (ptr)
      chunks.push_back({current, chunk_size});
  }
}

void CEXIStarpole::SaveState_Init(DolDataSection* read_ptr, u32 section_num)
{
  if (ALWAYS_DELAY)
    return;

  if (m_savestate_alloc != nullptr)
    SaveState_End();

  // swap byte order cause its coming from PPC
  std::vector<DolDataSection> sections(section_num);
  for (size_t i = 0; i < section_num; i++)
  {
    sections[i].address = std::byteswap(read_ptr[i].address);
    sections[i].size    = std::byteswap(read_ptr[i].size);
  }

  // determine chunk info
  std::vector<std::pair<u32, u32>> chunks;
  SaveState_GetChunkSizes(sections, section_num, chunks);
  size_t chunk_num = chunks.size();

  // determine size of the raw data to backup
  size_t data_size = 0;
  for (const auto& [addr, size] : chunks)
  {
    data_size += size;
  }

  // determine alloc size
  size_t savestate_size =
      sizeof(SavestateHeader) + (sizeof(SavestateChunk) * chunk_num) + data_size;

  // alloc
  m_savestate_alloc = std::make_unique<u8[]>(savestate_size * MAX_SAVESTATES);

  // init savestates
  for (int save_idx = 0; save_idx < MAX_SAVESTATES; save_idx++)
  {
    auto* header = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() + (savestate_size * save_idx));

    header->chunk_num = chunk_num;

    u8* this_chunk_data_ptr = (u8*)header + sizeof(SavestateHeader) + (sizeof(SavestateChunk) * chunk_num);

    // init chunks
    for (int chunk_idx = 0; chunk_idx < chunk_num; chunk_idx++)
    {
      auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)header + sizeof(SavestateHeader) +
                                                      (sizeof(SavestateChunk) * chunk_idx));
      chunk->address = chunks[chunk_idx].first;
      chunk->size = chunks[chunk_idx].second;
      chunk->data_ptr = this_chunk_data_ptr;

      this_chunk_data_ptr += chunk->size;
    }
  }

  m_savestate_num = 0;
  m_savestate_size = savestate_size;

  m_is_rollback_active = true;
  Netsync_Init(2);

}

void CEXIStarpole::SaveState_End()
{
  m_savestate_alloc.reset();        // streets are saying this is safe to call on a nullptr

  m_savestate_num = 0;
  m_savestate_size = 0;

  m_is_rollback_active = false;
  Netsync_Init(0);
}

SavestateHeader* CEXIStarpole::SaveState_Get(u32 frame_idx)
{
  u32 save_idx = frame_idx % MAX_SAVESTATES;
  auto* savestate = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() + (m_savestate_size * save_idx));
  return savestate;
}

void CEXIStarpole::SaveState(u32 frame_idx)
{
  if (m_savestate_alloc == nullptr)
  {
    WARN_LOG_FMT(EXPANSIONINTERFACE, "Attempted to savestate before initializing!");
    return;
  }

  auto start = std::chrono::high_resolution_clock::now();

  //u32 heap_start = m_system.GetMemory().Read_U32(0x80537f58);
  //u32 heap_size = m_system.GetMemory().Read_U32(0x80537f5c);

  auto* savestate = SaveState_Get(frame_idx);

  savestate->frame_idx = frame_idx;

  auto& power_pc = m_system.GetPowerPC();
  auto& cpu = power_pc.GetPPCState();

  for (int i = 0; i < 32; i++)
  {
    savestate->cpu.gpr[i] = cpu.gpr[i];
    savestate->cpu.fpr[i] = cpu.ps[i];
  }
  savestate->cpu.pc = cpu.pc;
  savestate->cpu.npc = cpu.npc;
  savestate->cpu.cr = cpu.cr;
  savestate->cpu.msr.Hex = cpu.msr.Hex;
  savestate->cpu.fpscr.Hex = cpu.fpscr.Hex;
  savestate->cpu.xer_ca = cpu.xer_ca;
  savestate->cpu.xer_so_ov = cpu.xer_so_ov;
  savestate->cpu.xer_stringctrl = cpu.xer_stringctrl;

  // save chunks
  for (int chunk_idx = 0; chunk_idx < savestate->chunk_num; chunk_idx++)
  {
    auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)savestate + sizeof(SavestateHeader) +
                                                    (sizeof(SavestateChunk) * chunk_idx));

    u8* ptr = m_system.GetMemory().GetPointerForRange(chunk->address, chunk->size);
    if (ptr)
    {
      auto copy_start = std::chrono::high_resolution_clock::now();
      memcpy(chunk->data_ptr, ptr, chunk->size);
      auto copy_end = std::chrono::high_resolution_clock::now();

      auto copy_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start);
      // INFO_LOG_FMT(EXPANSIONINTERFACE, " memcpy'd 0x{:08X} (0x{:X}) section in {:.4f} ms",
      //              chunk->address, chunk->size,
      //              copy_duration.count() / 1000.0);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "frame {} savestate created: {:.3f} MB in {:.3f} ms",
               savestate->frame_idx, m_savestate_size / (1024.0f * 1024.0f), duration.count() / 1000.0);

  if (m_savestate_num < MAX_SAVESTATES)
    m_savestate_num++;

  //for (u32 i = 0; i < m_savestate_num; i++)
  //{
  //  int target_idx = ((int)i + MAX_SAVESTATES) % MAX_SAVESTATES;
  //  savestate = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() +
  //                                                 (m_savestate_size * target_idx));

  //  INFO_LOG_FMT(EXPANSIONINTERFACE, "savestate index {}: frame {}", i, savestate->frame_idx);
  //}
}

void CEXIStarpole::LoadState(u32 frame_idx)
{
  auto start = std::chrono::high_resolution_clock::now();

  auto* savestate = SaveState_Get(frame_idx);

  // ensure its for the frame we want
  if (savestate->frame_idx != frame_idx)
  {
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Error loading savestate for frame {}. Does not exist.", frame_idx);
    return;
  }

  if (1)
  {
    auto& power_pc = m_system.GetPowerPC();
    auto& cpu = power_pc.GetPPCState();

    for (int i = 0; i < 32; i++)
    {
      cpu.gpr[i] = savestate->cpu.gpr[i];
      cpu.ps[i] = savestate->cpu.fpr[i];
    }

    cpu.pc = savestate->cpu.pc;
    cpu.npc = savestate->cpu.npc;
    cpu.cr = savestate->cpu.cr;
    cpu.msr.Hex = savestate->cpu.msr.Hex;
    cpu.fpscr.Hex = savestate->cpu.fpscr.Hex;
    cpu.xer_ca = savestate->cpu.xer_ca;
    cpu.xer_so_ov = savestate->cpu.xer_so_ov;
    cpu.xer_stringctrl = savestate->cpu.xer_stringctrl;
  }

  // restore chunks
  for (int chunk_idx = 0; chunk_idx < savestate->chunk_num; chunk_idx++)
  {
    auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)savestate + sizeof(SavestateHeader) +
                                                    (sizeof(SavestateChunk) * chunk_idx));

    u8* ptr = m_system.GetMemory().GetPointerForRange(chunk->address, chunk->size);
    if (ptr)
    {
      // auto copy_start = std::chrono::high_resolution_clock::now();
      memcpy(ptr, chunk->data_ptr, chunk->size);
      //auto copy_end = std::chrono::high_resolution_clock::now();

      //auto copy_duration =
      //    std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start);
      //INFO_LOG_FMT(EXPANSIONINTERFACE, " memcpy'd {:08X} section in {:.4f} ms", chunk->size,
      //             copy_duration.count() / 1000.0);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  INFO_LOG_FMT(EXPANSIONINTERFACE,
               "frame {} savestate loaded: {:.2f} MB in {:.2f} ms",
               savestate->frame_idx, (m_savestate_size / (1024.0 * 1024.0)),
               duration.count() / 1000.0);

  //for (u32 i = 0; i < m_savestate_num; i++)
  //{
  //  int target_idx = ((int)i + MAX_SAVESTATES) % MAX_SAVESTATES;
  //  savestate = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() +
  //                                                 (m_savestate_size * target_idx));

  //  INFO_LOG_FMT(EXPANSIONINTERFACE, "savestate index {}: frame {}", i, savestate->frame_idx);
  //}

}

ExpansionInterface::CEXIStarpole* Starpole_Get()
{
  auto& system = Core::System::GetInstance();
  auto& exi = system.GetExpansionInterface();
  auto* channel = exi.GetChannel(1);
  auto* device = channel->GetDevice(1);
  if (device && device->m_device_type == ExpansionInterface::EXIDeviceType::Starpole)
  {
    auto starpole = static_cast<ExpansionInterface::CEXIStarpole*>(device);
    return starpole;
  }

  return NULL;
}
}  // namespace ExpansionInterface


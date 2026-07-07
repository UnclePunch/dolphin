// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_Channel.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/ChunkFile.h"         // save states

#include "Core/Config/MainSettings.h" // starpole UI pane config settings
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
  replay_state = StarpoleReplayState::NONE;

  m_forward_frame = 0;
  m_inputs_sent = 0;
  m_instance_idx = 0;
  m_instance_read_start = 0;

  NetPlay_InitData();

  SaveState_End();

  memset(m_gamestate_hash_buffer, 0, sizeof(m_gamestate_hash_buffer));
  memset(m_delay_buffer, -1, sizeof(m_delay_buffer));
  memset(m_rollback_buffer, -1, sizeof(m_rollback_buffer));
  
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI Starpole Init");
}

void CEXIStarpole::DoState(PointerWrap& p)
{
  p.Do(cur_cmd);
  p.Do(cur_args);

  p.Do(m_is_rollback_active);
  p.Do(m_req_rollback);
  p.Do(m_is_sim_forward);
  //p.Do(m_savestate_size);
  //p.Do(m_savestate_num);

  p.DoArray(m_delay_buffer, sizeof(m_delay_buffer) / sizeof(m_delay_buffer[0]));
  p.DoArray(m_rollback_buffer, sizeof(m_rollback_buffer) / sizeof(m_rollback_buffer[0]));
  p.DoArray(m_player_gamestate_frame, sizeof(m_player_gamestate_frame) / sizeof(m_player_gamestate_frame[0]));
  p.DoArray(m_player_drain_num, sizeof(m_player_drain_num) / sizeof(m_player_drain_num[0]));
  p.DoArray(m_player_confirm_num, sizeof(m_player_confirm_num) / sizeof(m_player_confirm_num[0]));
  p.DoArray(m_player_gamestate_hash, sizeof(m_player_gamestate_hash) / sizeof(m_player_gamestate_hash[0]));

  p.Do(m_sim_frames);
  p.Do(m_confirm_frame);
  p.Do(m_forward_frame);

  // replay stuff
  p.Do(m_replay_header);
  p.Do(m_match_data);
  p.Do(m_file_frame_idx);
  p.Do(m_game_frame_idx);
  p.Do(replay_state);
  p.Do(is_active);
  p.Do(replay_file_path);

  size_t tell;
  if (replay_state == StarpoleReplayState::PLAYBACK)
  {
    if (p.IsWriteMode())
    {
      // backup tell
      tell = reader->Tell();
      p.Do(tell);
    }
    else
    {
      // restore tell
      OpenFile(replay_file_path);
      p.Do(tell);
      reader->Seek(tell);
    }
  }
  else if (replay_state == StarpoleReplayState::RECORD)
  {
    if (p.IsReadMode())
    {
      replay_state = StarpoleReplayState::NONE;
      CloseWriter();
    }

  //  if (p.IsWriteMode())
  //  {
  //    // backup tell
  //    tell = writer->Tell();
  //    p.Do(tell);
  //  }
  //  else
  //  {
  //    // restore tell
  //    OpenFile(replay_file_path);
  //    p.Do(tell);
  //    writer->Seek(tell);
  //  }
  }

  //// restore savestates
  //u32 buffer_size = (u32)m_savestate_size * MAX_SAVESTATES;
  //bool allocated = m_savestate_alloc != nullptr;
  //p.Do(allocated);
  //if (allocated)
  //{
  //  if (p.IsReadMode() && !m_savestate_alloc)
  //    m_savestate_alloc = std::make_unique<u8[]>(buffer_size);
  //  p.DoArray(m_savestate_alloc.get(), buffer_size);
  //}
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
  auto start = std::chrono::high_resolution_clock::now();

  // respond with the appropriate data
  switch (cur_cmd)
  {
  case StarpoleCmd::ID:
    response = STARPOLE_DEVICE_ID;          // id for Starpole
    cur_cmd = StarpoleCmd::NUM;             // no follow up DMA, null cur_cmd
    break;

  case StarpoleCmd::TEST:
    response = sizeof(StarpoleDataTest);    // size of follow-up DMA response
    break;

  case StarpoleCmd::MODSAVE:
  case StarpoleCmd::MATCH:
  case StarpoleCmd::FRAME:
    response = 0;
    break;

  case StarpoleCmd::REQMODSAVE:
    response = 1;
    break;

  case StarpoleCmd::REQMATCH:
    response = Match_Prepare();
    break;

  case StarpoleCmd::REQFRAME:
    response = Frame_Prepare(cur_args);
    break;

  case StarpoleCmd::MATCHEND:
    MatchEnd_Receive();
    response = 0;
    cur_cmd = StarpoleCmd::NUM;             // no follow up DMA, null cur_cmd
    break;

  case StarpoleCmd::SEQEND:
    SeqEnd_Receive();
    response = 0;
    cur_cmd = StarpoleCmd::NUM;             // no follow up DMA, null cur_cmd
    break;

  case StarpoleCmd::CHECKPLAYBACK:
    if (is_playback_queued)
    {
      // open file
      OpenFile(replay_file_path);

      // read in header
      ReadFileOffset((uint8_t*)&m_replay_header, 0, sizeof(StarpoleReplayHeader));

      response = 1;

      is_playback_queued = 0;
    }
    else
      response = 0;
      
    break;

  case StarpoleCmd::DOLPHIN:
    response = DolphinData_Prepare();
    break;

  case StarpoleCmd::NETSTART:     // game is sending preserve sections
    response = 1;
    break;

  case StarpoleCmd::NETPADSEND:   // game is sending its inputs
    response = 1;  // signal we are ready to receive the inputs
    break;

  case StarpoleCmd::NETPADRECV:   // game is requesting inputs
  {
    int is_render = 1;
    int is_rollback = 0;

    // check for remote inputs, copy them to our pad buffer and update
    NetPlay_DrainPadQueue();

    m_is_sim_forward = Netsync_CheckSimForward(); // this is advancing confirm frame
    m_rollback_num = Netsync_GetRollbackNum();    // this references confirm frame when validating inputs

    // request a load state
    if (m_rollback_num > 0)
    {
      m_req_rollback = m_rollback_num;
      is_rollback = 1;
    }

    // determine how many frames to simulate
    m_sim_frames = m_is_sim_forward ? (m_rollback_num + 1) : (m_rollback_num);

    // placeholder seek code
    if (replay_state == StarpoleReplayState::PLAYBACK)
    {
      #include <windows.h>

      u32 cur_frame = cur_args;

      if (GetAsyncKeyState(VK_RIGHT) & 0x1)
      {
        // to-do: improve this, check if i have a savestate i can jump to first
        m_playback_desired_frame = cur_args + (5 * 60);
        cur_frame = ((m_playback_desired_frame.value() / 60) / 5) * (5 * 60);
      }
      else if (GetAsyncKeyState(VK_LEFT) & 0x1)
      {
        m_playback_desired_frame = cur_args - (5 * 60);
        cur_frame = ((m_playback_desired_frame.value() / 60) / 5) * (5 * 60);
      }

      if (m_playback_desired_frame.has_value() &&
        cur_frame < m_playback_desired_frame.value())
      {
        m_sim_frames = m_playback_desired_frame.value() - cur_frame;
        if (m_sim_frames > MAX_ROLLBACK_NUM + 1)
        {
          is_render = 0;
          m_sim_frames = MAX_ROLLBACK_NUM + 1;
        }
        else
          m_playback_desired_frame.reset();
      }
    }


    // tell game how many frames to simulate
    response = (is_rollback << 31) | (is_render << 30) | m_sim_frames;

    break;
  }

  case StarpoleCmd::NETSAVE:
    if (m_is_rollback_active)
    {
      // placeholder seek code
      if (replay_state == StarpoleReplayState::PLAYBACK)
      {
        // save state every 5 seconds
        if (cur_args % (5 * 60) == 0)
        {
          u32 save_idx = (cur_args / 60) / 5;
          m_playback_savestates->Save(save_idx, m_file_frame_idx, m_game_frame_idx);
        }

        if (m_playback_desired_frame.has_value() && cur_args > m_playback_desired_frame.value())
        {
          u32 load_idx = (m_playback_desired_frame.value() / 60) / 5;
          m_playback_savestates->Load(load_idx, &m_file_frame_idx,
                                      &m_game_frame_idx);
        }
      }
      else
      {
        // load state if needed
        if (m_req_rollback)
        {
          u32 load_idx = cur_args - m_req_rollback;
          INFO_LOG_FMT(EXPANSIONINTERFACE, "Loading frame {} ({} - {})", load_idx, cur_args,
                       m_req_rollback);
          m_rollback_savestates->Load(load_idx);
          m_req_rollback = 0;
        }
        else
          m_rollback_savestates->Save(cur_args);
      }
    }

    response = 1;

    break;

  case StarpoleCmd::NETGETCONFIRM:
    response = m_confirm_frame - m_instance_read_start;
    break;

  case StarpoleCmd::NETGAMESTATE:
    response = 1;
    break;

  case StarpoleCmd::NETEND:
    if (m_forward_frame != m_instance_read_start + cur_args)
    {
      WARN_LOG_FMT(EXPANSIONINTERFACE,
                   "detected a runahead when switching back to delay ({} vs {} ({} + {})", m_forward_frame,
                    m_instance_read_start + cur_args,
                    m_instance_read_start, cur_args);

      m_forward_frame = m_instance_read_start + cur_args;
    }

    SaveState_End();
    response = 1;
    break;

  default:
    response = -1;
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  //WARN_LOG_FMT(EXPANSIONINTERFACE, "Imm Read {} in {:.2f} ms", (int)cur_cmd,
  //             duration.count() / 1000.0);

  return response;
}

void CEXIStarpole::DMAWrite(u32 address, u32 size)
{

  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Receive: {:08x} bytes, from {:08x} to EXI device",
  //              size, address);

  // get pointer to address we will read from
  u8* read_ptr = m_system.GetMemory().GetPointerForRange(address, size);

  auto start = std::chrono::high_resolution_clock::now();

  // receive the data
  switch (cur_cmd)
  {
  case StarpoleCmd::MODSAVE:
    ModSave_Receive(read_ptr, size);
    break;
  case StarpoleCmd::MATCH:
    Match_Receive(read_ptr, size);
    break;
  case StarpoleCmd::FRAME:
    Frame_Receive(read_ptr, size);
    break;
  case StarpoleCmd::NETSTART:
    SaveState_Init((Starpole::DolDataSection *)read_ptr, cur_args);
    break;
  case StarpoleCmd::NETPADSEND:
    Netsync_ReceiveInputs(read_ptr, size);
    break;
  case StarpoleCmd::NETGAMESTATE:
    Netsync_ReceiveGameState(read_ptr, size);
    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "DMA Receive not handled!");
    break;
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  //WARN_LOG_FMT(EXPANSIONINTERFACE, "DMA Write {} in {:.2f} ms", (int)cur_cmd, duration.count() / 1000.0);

  cur_cmd = StarpoleCmd::NUM;  // data has been written to memory, end the current command operation
}

void CEXIStarpole::DMARead(u32 address, u32 size)
{
  //INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Response to cmd {}: {:08x} bytes, from EXI device to {:08x}",
  //             (int)cur_cmd, size, address);

  // get pointer to address we will write to
  u8* write_ptr = m_system.GetMemory().GetPointerForRange(address, size);

  auto start = std::chrono::high_resolution_clock::now();

  // perform the current command's operation
  switch (cur_cmd)
  {
  case StarpoleCmd::TEST:
    File::GetUserPath(D_KAR_REPLAY_IDX).copy((char *)write_ptr, sizeof(StarpoleDataTest), 0);
    break;
  case StarpoleCmd::REQMODSAVE:
    ModSave_Send(write_ptr);
    break;
  case StarpoleCmd::REQMATCH:
    Match_Send(write_ptr);
    break;
  case StarpoleCmd::REQFRAME:
    Frame_Send(write_ptr, cur_args);
    break;
  case StarpoleCmd::DOLPHIN:
    DolphinData_Send(write_ptr);
    break;
  case StarpoleCmd::NETPADRECV:
    Netsync_SendInputs(write_ptr);

    if (m_is_sim_forward)
    {
      Netsync_UpdateTimeSync();
      m_forward_frame++;
    }

    break;

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "DMA Reponse not handled!");
    break;
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  //WARN_LOG_FMT(EXPANSIONINTERFACE, "DMA Read {} in {:.2f} ms", (int)cur_cmd,
  //             duration.count() / 1000.0);

  cur_cmd = StarpoleCmd::NUM;   // data has been written to memory, end the current command operation
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


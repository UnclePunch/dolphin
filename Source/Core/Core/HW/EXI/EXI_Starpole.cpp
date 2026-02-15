// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_Starpole.h"

#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_Channel.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "Core/Core.h"                // needed to exec code from UI thread on main thread
#include "Core/HW/Memmap.h"           // needed to write directly to game memory using DMA
#include "Core/System.h"              // needed to write directly to game memory using DMA
#include "Core/NetPlayProto.h"        // needed to get netplay player index
#include "Core/NetPlayClient.h"       // needed to send data over netplay
#include "VideoCommon/VideoConfig.h"  // aspect ratio

namespace ExpansionInterface
{
CEXIStarpole::CEXIStarpole(Core::System& system, const std::string& name)
    : IEXIDevice(system), m_name{name}
{
  replay_state = STARPOLE_REPLAYSTATE_NONE;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI Starpole Init");
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
  int response;

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

  case STARPOLE_CMD_NETPADSEND:
    response = 1;       // signal we are ready to receive the inputs
    break;

  case STARPOLE_CMD_NETPADRECV:
    // check if we have all the inputs for the frame
    response = NetPlay_GetGameInput(pad_status);
    INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE Requested inputs, returned {}", response);
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
  GCPadStatus status[4];
  memcpy((void*)status, read_ptr, size);

  // send to netplay clients
  NetPlay_SendGameInput((GCPadStatus*)status);
}
void CEXIStarpole::Netsync_SendInputs(u8* write_ptr)
{
  // write to game memory
  memcpy(write_ptr, (void*)&pad_status, sizeof(pad_status));
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

  INFO_LOG_FMT(EXPANSIONINTERFACE,
               "Received {}p match being played on gr_kind {} with stadium {}. RNG Seed: {:08x}",
               active_ply_num, match_data.stage_kind.ToHost(), match_data.stadium_kind,
               match_data.rng_seed.ToHost());

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

  // Header
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Frame {}: RNG Seed: {:08x}, Player Count: {}",
               frame.frame_idx.ToHost(), frame.rng_seed.ToHost(), frame.ply_num);

  const u32 ply_count = std::min<u32>(frame.ply_num, 4);

  // Per-player data
  for (u32 i = 0; i < ply_count; i++)
  {
    const auto& ply = frame.ply[i];

    INFO_LOG_FMT(EXPANSIONINTERFACE, "Ply {}", ply.idx);

    //INFO_LOG_FMT(EXPANSIONINTERFACE, "  Machine: {} | State: {}", ply.machine_kind.ToHost(),
    //             ply.rd_state.ToHost());

    //INFO_LOG_FMT(EXPANSIONINTERFACE, "  Pos : ({:.3f}, {:.3f}, {:.3f})", ply.pos.x.ToHost(),
    //             ply.pos.y.ToHost(), ply.pos.z.ToHost());

    INFO_LOG_FMT(EXPANSIONINTERFACE,
                 "  Inputs: LStick({}, {}) RStick({}, {}) Buttons: {:08x}",
                 ply.input.stickX, ply.input.stickY,
                 ply.input.substickX, ply.input.substickY,
                 (int)ply.input.down << 4);

    INFO_LOG_FMT(EXPANSIONINTERFACE, "");

  }

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

  frame_idx = 0;
  replay_state = STARPOLE_REPLAYSTATE_PLAYBACK;
}
int CEXIStarpole::Frame_Prepare(int index)
{
  int frame_size = match_data.frame_size.ToHost();
  int offset = sizeof(StarpoleDataMatch) + index * frame_size;
  int file_size = ReadFileSize();

  if (offset + frame_size > file_size)
    return 0;

  return frame_size;
}
void CEXIStarpole::Frame_Send(u8* write_ptr, u32 index)
{
  // read match data
  StarpoleDataMatch frame;

  int offset = sizeof(StarpoleDataMatch) + index * match_data.frame_size.ToHost();
  int frame_size = match_data.frame_size.ToHost();

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Sending frame {} of size 0x{:x}", index, frame_size);
  ReadFileOffset((uint8_t*)&frame, offset, frame_size);

  // write to game memory
  memcpy(write_ptr, (void*)&frame, sizeof(frame));

  frame_idx++;
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
void CEXIStarpole::SetRNGSeed(u32 seed)
{
  m_initial_rng_seed = seed;
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Set m_initial_rng_seed to 0x{:08X}", m_initial_rng_seed);
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


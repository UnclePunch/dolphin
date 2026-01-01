// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_Starpole.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "Core/HW/Memmap.h"   // needed to write directly to game memory using DMA
#include "Core/System.h"      // needed to write directly to game memory using DMA

#include "Common/FileUtil.h"      // needed to access user directory
#include <filesystem>
#include <iostream>

namespace ExpansionInterface
{
CEXIStarpole::CEXIStarpole(Core::System& system, const std::string& name)
    : IEXIDevice(system), m_name{name}
{
  replay_state = STARPOLE_REPLAYSTATE_NONE;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE INIT");
}

void CEXIStarpole::ImmWrite(u32 data, u32 size)
{
  // INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE ImmWrite: data {:08x} size {}", data, size);

  // receive an Imm transfer from the game
  cur_cmd = (StarpoleCmd)(data & 0xFFFF); // remember which data the game is requesting
  cur_args = (data & 0xFFFF0000) >> 16;   // pull out args

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE Imm Received cmd {} args {}", (u32)cur_cmd, cur_args);
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

  case STARPOLE_CMD_END:
    End_Receive();
    response = 0;
    cur_cmd = STARPOLE_CMD_NUM;             // no follow up DMA, null cur_cmd
    break;

  default:
    response = 0;
  }

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE Imm Response {:08x}", response);

  return response;
}

void CEXIStarpole::DMAWrite(u32 address, u32 size)
{

  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Receive: {:08x} bytes, from {:08x} to EXI device",
               size, address);

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
  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "DMA Receive not handled!");
    break;
  }

  cur_cmd = STARPOLE_CMD_NUM;  // data has been written to memory, end the current command operation
}

void CEXIStarpole::DMARead(u32 address, u32 size)
{
  INFO_LOG_FMT(EXPANSIONINTERFACE, "EXI STARPOLE DMA Response: {:08x} bytes, from EXI device to {:08x}",
               size, address);

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
               active_ply_num, match_data.misc[0x3], match_data.stadium_kind.ToHost(),
               match_data.rng_seed.ToHost());

  // create a file
  recent_file = GenerateReplayFilename();
  CreateFile(recent_file);
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
    OpenFile(recent_file);
    return 1;
  }
  catch (const std::exception& e)
  {
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to open file: {}", e.what());
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
void CEXIStarpole::Frame_Send(u8* write_ptr, u32 index)
{
  // read match data
  StarpoleDataMatch frame;

  int offset = sizeof(StarpoleDataMatch) + index * match_data.frame_size.ToHost();
  ReadFileOffset((uint8_t*)&frame, offset, sizeof(StarpoleDataFrame));
  
  INFO_LOG_FMT(EXPANSIONINTERFACE, "Read file offset 0x{:X}", offset);

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
  localtime_s(&tm, &t);  // use localtime_r on POSIX

  std::ostringstream ss;
  ss << File::GetUserPath(D_KAR_REPLAY_IDX) << "replay_"
     << std::put_time(&tm, "%Y%m%d_%H%M%S")
     << ".krf";

  return ss.str();
}

}  // namespace ExpansionInterface


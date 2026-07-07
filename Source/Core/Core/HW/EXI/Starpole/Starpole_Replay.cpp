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
// Recording
void CEXIStarpole::Replay_Create(u32 modsave_size)
{
  // create replay file
  CreateFile(GenerateReplayFilename());

  // create header
  memset(&m_replay_header, -1, sizeof(m_replay_header));

  memcpy(&m_replay_header.magic, "KBRP", sizeof(m_replay_header.magic));
  m_replay_header.version.major = 0;
  m_replay_header.version.minor = 0;

  // set pointers
  int offset = sizeof(m_replay_header);
  m_replay_header.offset.mod_save = offset;
  offset += modsave_size;
  m_replay_header.offset.match = offset;
  offset += sizeof(StarpoleDataMatch);
  m_replay_header.offset.netplay = offset;
  offset += sizeof(StarpoleDataNetplay);
  m_replay_header.offset.frame = offset;

  // write header to replay
  WriteFile((u8*)&m_replay_header, sizeof(m_replay_header));
}
void CEXIStarpole::ModSave_Receive(u8* read_ptr, u32 size)
{
  // this gets sent to us first, save it for writing to file later
  m_mod_save_alloc = std::make_unique<u8[]>(size);
  memcpy((void*)m_mod_save_alloc.get(), read_ptr, size);
  m_mod_save_size = size;
}
void CEXIStarpole::Match_Receive(u8* read_ptr, u32 size)
{
  // read match data
  memcpy((void*)&m_match_data, read_ptr, size);

  // create netplay data
  DolphinData_Create(&m_dolphin_data);

  // create replay file
  Replay_Create(m_mod_save_size);

  // begin writing data
  WriteFile(m_mod_save_alloc.get(), m_mod_save_size);            // write mod save data
  WriteFile((uint8_t*)&m_match_data, size);                      // write match data
  WriteFile((uint8_t*)&m_dolphin_data, sizeof(m_dolphin_data));  // write dolphin data

  replay_state = StarpoleReplayState::RECORD;

  // int active_ply_num = 0;
  // for (int i = 0; i < 4; i++)
  //{
  //   if (m_match_data.ply_desc[i].p_kind != 4)
  //     active_ply_num++;
  // }
  // INFO_LOG_FMT(EXPANSIONINTERFACE,
  //              "Received {}p match being played on gr_kind {} with stadium {}. RNG Seed: {:08x}",
  //              active_ply_num, m_match_data.stage_kind.ToHost(), m_match_data.stadium_kind,
  //              m_match_data.rng_seed.ToHost());
}
void CEXIStarpole::Frame_Receive(u8* read_ptr, u32 size)
{
  if (replay_state != StarpoleReplayState::RECORD)
    return;

  StarpoleDataFrame frame;

  memcpy((void*)&frame, read_ptr, size);

  WriteFile((uint8_t*)&frame, size);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: wrote game frame {}", frame.frame_idx.ToHost());
}
void CEXIStarpole::MatchEnd_Receive()
{
  if (replay_state != StarpoleReplayState::RECORD)
    return;

  int terminator = -1;
  WriteFile((uint8_t*)&terminator, sizeof(terminator));

  // go back and write results i guess

  CloseWriter();
  replay_state = StarpoleReplayState::NONE;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Match end.");
}
void CEXIStarpole::SeqEnd_Receive()
{
  m_replay_seq_end = true;

  // zip up the folder?

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Sequence end.");
}

std::string CEXIStarpole::Replay_GetStageName(GroundKind kind, int stadium_round)
{
  std::string stage_name;

  switch (kind)
  {
  case (GroundKind::CITY1):
    stage_name = "City";
    break;
  case (GroundKind::DRAG1):
  case (GroundKind::DRAG2):
  case (GroundKind::DRAG3):
  case (GroundKind::DRAG4):
    stage_name = "Drag" + std::format("{}", ((int)kind - (int)GroundKind::DRAG1) + 1);
    break;
  case (GroundKind::AIRGLIDER):
    stage_name = "Glider";
    break;
  case (GroundKind::TARGETFLIGHT):
    stage_name = "Target";
    break;
  case (GroundKind::HIGHJUMP):
    stage_name = "Jump";
    break;
  case (GroundKind::KIRBYMELEE1):
  case (GroundKind::KIRBYMELEE2):
    stage_name = "Melee" + std::format("{}", ((int)kind - (int)GroundKind::KIRBYMELEE1) + 1);
    break;
  case (GroundKind::DESTRUCTIONDERBY1):
  case (GroundKind::DESTRUCTIONDERBY2):
  case (GroundKind::DESTRUCTIONDERBY3):
  case (GroundKind::DESTRUCTIONDERBY4):
  case (GroundKind::DESTRUCTIONDERBY5):
    stage_name = "Derby" + std::format("{}", ((int)kind - (int)GroundKind::DESTRUCTIONDERBY1) + 1);
    break;
  case (GroundKind::SINGLERACE1):
  case (GroundKind::SINGLERACE2):
  case (GroundKind::SINGLERACE3):
  case (GroundKind::SINGLERACE4):
  case (GroundKind::SINGLERACE5):
  case (GroundKind::SINGLERACE6):
  case (GroundKind::SINGLERACE7):
  case (GroundKind::SINGLERACE8):
  case (GroundKind::SINGLERACE9):
    stage_name = "Race" + std::format("{}", ((int)kind - (int)GroundKind::SINGLERACE1) + 1);
    break;
  case (GroundKind::VSKINGDEDEDE):
    stage_name = "VSKing";
    break;
  default:
    stage_name = "Stage" + std::format("{}", (int)kind);
    break;
  }

  if (stadium_round > 0)
    stage_name += std::format("{}", (int)stadium_round + 1);

  return stage_name;
}

// Playback
int CEXIStarpole::Match_Prepare()
{
  return 1;
}
void CEXIStarpole::ModSave_Send(u8* write_ptr)
{
  StarpoleDataModSave mod_save;
  ReadFileOffset((uint8_t*)&mod_save, m_replay_header.offset.mod_save, sizeof(StarpoleDataModSave));

  // read mod save
  u32 mod_save_size = mod_save.size.ToHost();
  std::vector<uint8_t> mod_save_buffer(mod_save_size);
  ReadFileOffset((uint8_t*)mod_save_buffer.data(), m_replay_header.offset.mod_save, mod_save_size);

  // write to game memory
  memcpy(write_ptr, (void*)mod_save_buffer.data(), mod_save_size);
}
void CEXIStarpole::Match_Send(u8* write_ptr)
{
  // read match data
  ReadFileOffset((uint8_t*)&m_match_data, m_replay_header.offset.match, sizeof(m_match_data));

  // write to game memory
  memcpy(write_ptr, (void*)&m_match_data, sizeof(m_match_data));

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Frame Size 0x{:X}", m_match_data.frame_size.ToHost());

  m_file_frame_idx = 0;
  m_game_frame_idx = 0;
  replay_state = StarpoleReplayState::PLAYBACK;
}

int CEXIStarpole::DolphinData_Prepare()
{
  // first handle what we can assume to be the game requesting dolphin data on bootup
  if (replay_state != StarpoleReplayState::PLAYBACK)
    return 1;

  // next handle a replay requesting the dolphin data in the replay
  if (replay_state == StarpoleReplayState::PLAYBACK &&
      Config::Get(Config::MAIN_STARPOLE_REPLAY_USERNAMES) &&
      m_replay_header.offset.netplay != -1)  // check if netplay data exists in the replay
  {
    return 1;
  }

  return 0;
}
void CEXIStarpole::DolphinData_Send(u8* write_ptr)
{
  StarpoleDataNetplay netplay;

  if (replay_state == StarpoleReplayState::PLAYBACK)
  {
    // read in
    ReadFileOffset((uint8_t*)&netplay, m_replay_header.offset.netplay, sizeof(netplay));
  }
  else
    DolphinData_Create(&netplay);

  // write to game memory
  memcpy(write_ptr, (void*)&netplay, sizeof(netplay));
}

int CEXIStarpole::Frame_Prepare(int index)
{
  int frame_size = m_match_data.frame_size.ToHost();
  int offset = m_replay_header.offset.frame + m_file_frame_idx * frame_size;
  int file_size = ReadFileSize();

  if (offset + frame_size > file_size)
  {
    CloseReader();
    replay_state = StarpoleReplayState::NONE;
    return 0;
  }

  return frame_size;
}
void CEXIStarpole::Frame_Send(u8* write_ptr, u32 index)
{
  // read match data
  StarpoleDataFrame frame;
  Frame_Get(&frame, index);

  // write to game memory
  memcpy(write_ptr, (void*)&frame, sizeof(frame));

  m_game_frame_idx = index;  // update the game frame we are on
}

bool CEXIStarpole::Frame_Read(StarpoleDataFrame* frame, u32 file_frame_index)
{
  u32 frame_size = m_match_data.frame_size.ToHost();
  u32 file_size = ReadFileSize();

  int offset = m_replay_header.offset.frame + (file_frame_index * frame_size);
  if (offset + frame_size > file_size)
    return false;

  ReadFileOffset((uint8_t*)frame, offset, frame_size);
  return true;
}

bool CEXIStarpole::Frame_Get(StarpoleDataFrame* frame, u32 index)
{
  if (!Config::Get(Config::MAIN_STARPOLE_REPLAY_ROLLBACK))
  {
    u32 rollback_file_offset = 0;
    int target_file_frame_idx = -1;
    StarpoleDataFrame frame_temp;

    u32 forward_frame = index;
    while (forward_frame <= index + MAX_ROLLBACK_NUM)
    {
      u32 this_file_frame_idx = m_file_frame_idx + rollback_file_offset + (forward_frame - index);

      if (!Frame_Read(&frame_temp, this_file_frame_idx))
        break;

      u32 this_frame_idx = frame_temp.frame_idx.ToHost();

      // check for a rollback
      if (this_frame_idx < forward_frame)
      {
        // does the desired frame exist in this rollback?
        if (this_frame_idx <= index)
          target_file_frame_idx = this_file_frame_idx + (index - this_frame_idx);

        // get to the end of this rollback sequence
        rollback_file_offset += (forward_frame - this_frame_idx);
      }
      else if (this_frame_idx == forward_frame)
      {
        // the frame we are looking for
        if (this_frame_idx == (index))
          target_file_frame_idx = this_file_frame_idx;

        forward_frame++;
      }
      else
      {
        ERROR_LOG_FMT(EXPANSIONINTERFACE, "Replay: this_frame_idx {} > forward_frame {}",
                      this_frame_idx, forward_frame);

        return false;
      }
    }

    if (target_file_frame_idx == -1)
      return false;

    // read in final frame data
    if (!Frame_Read(frame, target_file_frame_idx))
      return false;

    if (frame->frame_idx.ToHost() != index)
    {
      ERROR_LOG_FMT(EXPANSIONINTERFACE, "Replay: Expected frame {} but found frame {}", index,
                    frame->frame_idx.ToHost());
    }

    m_file_frame_idx = target_file_frame_idx + 1;
    return true;
  }
  else
  {
    if (!Frame_Read(frame, m_file_frame_idx))
      return false;

    INFO_LOG_FMT(EXPANSIONINTERFACE,
                 "Replay: game requested frame {}, sending frame {}. file_frame: {}", index,
                 frame->frame_idx.ToHost(), m_file_frame_idx);
    m_file_frame_idx++;  // update the file frame we are on

    return true;
  }
}

bool CEXIStarpole::Playback_CheckSimForward()
{
  u32 game_frame = m_game_frame_idx + 1;

  if (Config::Get(Config::MAIN_STARPOLE_REPLAY_ROLLBACK))
  {
    StarpoleDataFrame frame;
    u32 replay_frame;

    bool is_sim_forward = false;

    INFO_LOG_FMT(EXPANSIONINTERFACE, "Replay: checking sim forward for game_frame {}...",
                 game_frame);

    // first frame
    if (m_file_frame_idx == 0)
      is_sim_forward = true;
    else
    {
      if (!Frame_Read(&frame, (m_file_frame_idx)))
        is_sim_forward = true;
      else
      {
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
          if (!Frame_Read(&frame, (m_file_frame_idx + rollback_num)))
            is_sim_forward = true;
          else
          {
            replay_frame = frame.frame_idx.ToHost();

            INFO_LOG_FMT(EXPANSIONINTERFACE,
                         " frame after rollbacks is for frame {} (file_frame {})", replay_frame,
                         m_file_frame_idx + rollback_num);

            if (replay_frame == game_frame)
              is_sim_forward = true;
          }
        }
      }
    }

    if ((game_frame - m_confirm_frame) > MAX_ROLLBACK_NUM)
      m_confirm_frame = game_frame - MAX_ROLLBACK_NUM;

    INFO_LOG_FMT(EXPANSIONINTERFACE, " is_sim_forward: {}", is_sim_forward);

    return is_sim_forward;
  }
  else
  {
    m_confirm_frame = game_frame;
    return true;
  }
}
u32 CEXIStarpole::Playback_GetRollbackNum()
{
  if (Config::Get(Config::MAIN_STARPOLE_REPLAY_ROLLBACK))
  {
    // peek at next frame, see if we need to rollback
    StarpoleDataFrame frame;
    if (!Frame_Read(&frame, m_file_frame_idx))
      return 0;

    u32 replay_frame = frame.frame_idx.ToHost();
    u32 game_frame =
        m_game_frame_idx +
        1;  // need to + 1 here because the netsync code runs before the replay stuff...

    // idk...
    if (replay_frame == 0)
      return 0;

    // if next frame is earlier, rollback game state
    if (replay_frame < game_frame)
    {
      u32 rollback_num = game_frame - replay_frame;
      INFO_LOG_FMT(
          EXPANSIONINTERFACE,
          "Replay: performing {} rollbacks on game_frame {}, file_frame {}, replay_frame {}",
          rollback_num, game_frame, m_file_frame_idx, replay_frame);

      // new confirm frame
      m_confirm_frame = replay_frame;

      return rollback_num;
    }

    return 0;
  }
  else
    return 0;
}

std::string CEXIStarpole::GenerateReplayFilename()
{
  using namespace std::chrono;

  std::time_t t = std::time(nullptr);
  std::tm tm{};

#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  // folder name = 20260626_135549_Uncl_Poyo_Taco
  // rp_20260626_135549_airride
  // rp_20260626_135549_city
  // rp_20260626_135549_target
  // rp_20260626_135549_target2
  // rp_20260626_135549_race
  // rp_20260626_135549_derby
  // rp_20260626_135549_glider
  // rp_20260626_135549_glider2

  // check to generate a new folder name
  if (m_replay_seq_end)
  {
    m_replay_seq_end = false;
    m_replay_folder_name.clear();

    // generate folder name if tickbox is enabled and if this is a city trial match
    if (Config::Get(Config::MAIN_STARPOLE_REPLAY_FOLDERS) &&
        m_match_data.stage_kind.ToHost() == (int)GroundKind::CITY1)
    {
      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y%m%d_%H%M%S");

      if (m_dolphin_data.is_netplay.ToHost())
      {
        // build new
        for (int i = 0; i < 4; i++)
        {
          // player is a human
          if (m_match_data.ply_desc[i].p_kind == 0)
          {
            oss << "_";
            oss.write(m_dolphin_data.usernames[i], 4);
          }
        }
      }

      oss << "/";
      m_replay_folder_name = oss.str();

      // ensure folder exists
      File::CreateDir(File::GetUserPath(D_KAR_REPLAY_IDX) + m_replay_folder_name.c_str());
    }
  }

  // get stage info
  std::string stage_name =
      Replay_GetStageName((GroundKind)m_match_data.stage_kind.ToHost(), m_match_data.stadium_round);

  // generate unique filename based on current time and date
  std::ostringstream filename;
  filename << std::put_time(&tm, "%Y%m%d_%H%M%S_") << stage_name << ".krf";
  // filename << stage_name << ".krf";

  return File::GetUserPath(D_KAR_REPLAY_IDX) + m_replay_folder_name + filename.str();
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
  // set paths
  replay_file_path = path;
  is_playback_queued = 1;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "Set replay file to {}", replay_file_path);

  return;
}

// write file
void CEXIStarpole::CreateFile(const std::string& path)
{
  writer = std::make_unique<StreamWriter>(path);
}
void CEXIStarpole::WriteFile(const uint8_t* data, u32 size)
{
  writer->WriteChunk(data, size);
}
void CEXIStarpole::CloseWriter()
{
  if (writer)
  {
    writer->Flush();
    writer.reset();
  }
}

// read file
void CEXIStarpole::OpenFile(const std::string& path)
{
  reader = std::make_unique<StreamReader>(path);
}
void CEXIStarpole::ReadFile(uint8_t* buffer, u32 size)
{
  reader->ReadChunk(buffer, size);
}
void CEXIStarpole::ReadFileOffset(uint8_t* buffer, u32 offset, u32 size)
{
  reader->ReadChunkOffset(buffer, offset, size);
}
std::streamsize CEXIStarpole::ReadFileSize()
{
  return reader->GetFileSize();
}
void CEXIStarpole::CloseReader()
{
  if (reader)
  {
    reader.reset();
  }
}
}

// file write/read. dolphin probably already has similar classes i can leverage...
StreamWriter::StreamWriter(const std::string& filename) : file(filename, std::ios::binary)
{
  if (!file)
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to open file");
}

StreamWriter::~StreamWriter()
{
  file.close();
}

// Write a chunk of data
void StreamWriter::WriteChunk(const uint8_t* data, size_t size)
{
  file.write(reinterpret_cast<const char*>(data), size);

  if (!file)
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to write chunk to file");
}

std::streampos StreamWriter::Tell()
{
  return file.tellp();
}

void StreamWriter::Seek(std::streampos pos)
{
  file.clear();
  file.seekp(pos);
}

// flush to disk
void StreamWriter::Flush()
{
  file.flush();
}

StreamReader::StreamReader(const std::string& filename) : file(filename, std::ios::binary)
{
  if (!file)
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to open file for reading");
}

// Read the entire file into a vector
std::vector<uint8_t> StreamReader::ReadAll()
{
  file.seekg(0, std::ios::end);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (size < 0)
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to determine file size");

  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to read file");

  return buffer;
}

void StreamReader::ReadChunk(uint8_t* buffer, size_t size)
{
  if (!file.read(reinterpret_cast<char*>(buffer), size))
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to read chunk");
}

void StreamReader::ReadChunkOffset(uint8_t* buffer, std::streampos offset, size_t size)
{
  file.seekg(offset);
  if (!file)
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to seek to offset");

  if (!file.read(reinterpret_cast<char*>(buffer), size))
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Failed to read bytes");
}

std::streamsize StreamReader::GetFileSize()
{
  file.seekg(0, std::ios::end);
  return file.tellg();
}

void StreamReader::Seek(std::streampos pos)
{
  file.clear();
  file.seekg(pos);
}

std::streampos StreamReader::Tell()
{
  return file.tellg();
}

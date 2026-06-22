// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Buffer.h"
#include "Common/Swap.h"

#include "Core/HW/EXI/EXI_Device.h"
#include "Core/PowerPC/PowerPC.h"     //
#include "Core/NetPlayProto.h"

#include "InputCommon/GCPadStatus.h"

#include <cstdint>
#include <fstream>

#include "Common/FileUtil.h"      // needed to access user directory
#include <filesystem>
#include <iostream>

#define STARPOLE_DEVICE_ID 0x0A000000

// big endian conversion
struct be_u32
{
  u32 val;  // stored big endian

  be_u32() : val(0) {}

  // construct from host uint32
  explicit be_u32(u32 host_val) { FromHost(host_val); }

  // convert stored big endian to host
  u32 ToHost() const
  {
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
  }

  // store host value in big endian format
  void FromHost(u32 host_val)
  {
#if defined(_MSC_VER)
    val = _byteswap_ulong(host_val);
#else
    val = __builtin_bswap32(host_val);
#endif
  }

  // construct from host value
  static be_u32 FromHostValue(u32 host_val) { return be_u32(host_val); }
};
struct be_s32
{
  s32 val;  // stored big endian

  be_s32() : val(0) {}

  explicit be_s32(s32 host_val) { FromHost(host_val); }

  s32 ToHost() const
  {
#if defined(_MSC_VER)
    return static_cast<s32>(_byteswap_ulong(static_cast<u32>(val)));
#else
    return static_cast<s32>(__builtin_bswap32(static_cast<u32>(val)));
#endif
  }

  void FromHost(s32 host_val)
  {
#if defined(_MSC_VER)
    val = static_cast<s32>(_byteswap_ulong(static_cast<u32>(host_val)));
#else
    val = static_cast<s32>(__builtin_bswap32(static_cast<u32>(host_val)));
#endif
  }

  static be_s32 FromHostValue(s32 host_val) { return be_s32(host_val); }
};
struct be_u16
{
  u16 val;  // stored big-endian

  u16 ToHost() const
  {
#if defined(_MSC_VER)
    return _byteswap_ushort(val);
#else
    return __builtin_bswap16(val);
#endif
  }

  void FromHost(u16 v)
  {
#if defined(_MSC_VER)
    val = _byteswap_ushort(v);
#else
    val = __builtin_bswap16(v);
#endif
  }
};
struct be_float
{
  u32 be;  // stored big-endian

  float ToHost() const
  {
#if defined(_MSC_VER)
    u32 le = _byteswap_ulong(be);
#else
    u32 le = __builtin_bswap32(be);
#endif
    float f;
    std::memcpy(&f, &le, sizeof(float));
    return f;
  }

  void FromHost(float f)
  {
    u32 le;
    std::memcpy(&le, &f, sizeof(float));
#if defined(_MSC_VER)
    be = _byteswap_ulong(le);
#else
    be = __builtin_bswap32(le);
#endif
  }
};
struct be_vec2
{
  be_float x;
  be_float y;
};
struct be_vec3
{
  be_float x;
  be_float y;
  be_float z;
};

struct DolDataSection
{
  u32 address;
  u32 size;
};

struct CpuState
{
  u32 gpr[32];
  PowerPC::PairedSingle fpr[32];
  u32 pc;
  u32 npc;
  PowerPC::ConditionRegister cr;
  UReg_MSR msr;
  UReg_FPSCR fpscr;
  u32 xer_ca;
  u32 xer_so_ov;
  u32 xer_stringctrl;
};

struct SavestateChunk
{
  u32 address;
  u32 size;
  u8* data_ptr;     // exists in savestate
};

struct SavestateHeader
{
  u32 frame_idx;
  CpuState cpu;
  size_t chunk_num;
};

// commands to identify operations
typedef enum
{
  // identify EXI device, returns STARPOLE_DEVICE_ID
  STARPOLE_CMD_ID,

  // test
  STARPOLE_CMD_TEST,

  // recording
  STARPOLE_CMD_MODSAVE,
  STARPOLE_CMD_MATCH,
  STARPOLE_CMD_FRAME,
  STARPOLE_CMD_END,

  // playback
  STARPOLE_CMD_REQMODSAVE,
  STARPOLE_CMD_REQMATCH,
  STARPOLE_CMD_REQFRAME,

  // playback
  STARPOLE_CMD_CHECKPLAYBACK,

  // dolphin
  STARPOLE_CMD_DOLPHIN,

  // netsync
  STARPOLE_CMD_NETSTART,
  STARPOLE_CMD_NETSAVE,
  STARPOLE_CMD_NETPADSEND,
  STARPOLE_CMD_NETPADRECV,
  STARPOLE_CMD_NETGETCONFIRM,
  STARPOLE_CMD_NETGAMESTATE,
  STARPOLE_CMD_NETEND,

  // end
  STARPOLE_CMD_NUM,
} StarpoleCmd;

typedef enum
{
  STARPOLE_REPLAYSTATE_NONE,
  STARPOLE_REPLAYSTATE_RECORD,
  STARPOLE_REPLAYSTATE_PLAYBACK,
} StarpoleReplayState;

typedef enum
{
  STARPOLE_NETPAD_NOTRECEIVED,
  STARPOLE_NETPAD_PREDICTED,
  STARPOLE_NETPAD_CORRECTED,
  STARPOLE_NETPAD_VERIFIED,
} StarpoleNetPadState;

typedef enum
{
  STARPOLE_KIND_DELAY,
  STARPOLE_KIND_ROLLBACK,
} StarpoleNetPadKind;


// payload structures
typedef struct
{
  char str[128];
} StarpoleDataTest;

//typedef struct
//{
//  char magic[4];      // will be SPRP (starpole replay)
//  u16 version_major;
//  u16 version_minor;
//  char ply_names[4][30];
//  u8 replay_num;      // number of replays contained in this file
//  struct
//  {
//    u8 is_rollback;
//    u32 mod_data;  // to-do
//    struct
//    {
//      u32 match;      // StarpoleDataMatch
//      u32 frames;     // StarpoleDataFrame array
//      u32 result;     // to-do
//    } offset;
//  } replay[3];        // maximum of 3 per replay (city trial + up to 2 stadium rounds)
//} StarpoleReplayHeader;

typedef struct
{
  char magic[4];
  struct
  {
    u16 major;
    u16 minor;
  } version;
  struct
  {
    u32 mod_save;
    u32 match;
    u32 results;
    u32 netplay;
    u32 frame;
  } offset;
} StarpoleReplayHeader;

typedef struct
{
  be_u16 num;
  be_u16 size;
} StarpoleDataModSave;

typedef struct
{
  be_u32 frame;
  be_u32 hash;
} StarpoleDataGameState;

typedef struct
{
  GCPadStatus status[4];
} StarpoleDataInputs;

typedef struct
{
  be_u32 rng_seed;
  be_u16 frame_size;
  be_u16 stage_kind;
  char stadium_kind;
  //char city_kind;
  //be_u16 time_seconds;
  //u8 tempo;
  char misc[0xac4 - 0xa94];
  struct
  {
    s8 ply_stats[5][9];
    u8 is_bike[5];
    u8 machine_kind[5];
  } stadium;
  struct
  {
    u8 p_kind;        // 0x00
    u8 rider_kind;    // 0x01
    u8 is_bike;       // 0x02
    u8 machine_kind;  // 0x03
    u8 color;         // 0x04
    u8 x5;            // 0x05
    u8 ply;           // 0x06
    s8 x7;            // 0x07
    int x8;           // 0x08
    int xc;           // 0x0c
    int x10;          // 0x10
    int x14;          // 0x14
    int x18;          // 0x18
    int x1c;          // 0x1c
    int x20;          // 0x20
    int x24;          // 0x24
    int x28;          // 0x28
    int x2c;          // 0x2c
  }ply_desc[4];
} StarpoleDataMatch;

typedef struct
{
  be_float aspect_mult;
  be_u32 is_netplay;
  be_s32 ply;
  be_u32 rng_seed;
  char usernames[4][31];
} StarpoleDataNetplay;

#pragma pack(push, 1)
typedef struct
{
  be_u32 frame_idx;
  be_u32 rng_seed;
  be_u32 hash;
  u8 ply_num;
  struct
  {
    u8 idx;
    struct
    {
      u16 down;
      s8 stickX;
      s8 stickY;
      s8 substickX;
      s8 substickY;
      u8 trigger;
    } input;
    //be_u32 rd_state;
    //be_s32 machine_kind;
    //be_vec3 pos;
  } ply[4];
} StarpoleDataFrame;
#pragma pack(pop)

struct NetPad
{
  GCPadStatus status;
  GCPadStatus status_predict;
  u32 instance_idx;
  StarpoleNetPadState state;
  u32 frame;
  u32 hash_real;
  u32 hash_predict;
};

// file write/read. dolphin probably already has similar classes i can leverage...
class StreamWriter
{
public:
  StreamWriter(const std::string& filename) : file(filename, std::ios::binary)
  {
    if (!file)
      throw std::runtime_error("Failed to open file for streaming write");
  }

  ~StreamWriter() { file.close(); }

  // Write a chunk of data
  void WriteChunk(const uint8_t* data, size_t size)
  {
    file.write(reinterpret_cast<const char*>(data), size);
    if (!file)
      throw std::runtime_error("Failed to write chunk to file");
  }

  // Optional: flush to disk
  void Flush() { file.flush(); }

private:
  std::ofstream file;
};
class StreamReader
{
public:
  StreamReader(const std::string& filename) : file(filename, std::ios::binary)
  {
    if (!file)
      throw std::runtime_error("Failed to open file for reading");
  }

  // Read the entire file into a vector
  std::vector<uint8_t> ReadAll()
  {
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0)
      throw std::runtime_error("Failed to determine file size");

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
      throw std::runtime_error("Failed to read file");

    return buffer;
  }

  void ReadChunk(uint8_t* buffer, size_t size)
  {
    if (!file.read(reinterpret_cast<char*>(buffer), size))
      throw std::runtime_error("Failed to read chunk");
  }

  void ReadChunkOffset(uint8_t* buffer, std::streampos offset, size_t size)
  {
    file.seekg(offset);
    if (!file)
      throw std::runtime_error("Failed to seek to offset");

    if (!file.read(reinterpret_cast<char*>(buffer), size))
      throw std::runtime_error("Failed to read bytes");
  }

  std::streamsize GetFileSize()
  {
    file.seekg(0, std::ios::end);
    return file.tellg();
  }

private:
  std::ifstream file;
};

namespace ExpansionInterface
{
class CEXIStarpole final : public IEXIDevice
{
public:
  CEXIStarpole(Core::System& system, const std::string& name);

  void DoState(PointerWrap& p) override;

  void ImmWrite(u32 data, u32 size) override;
  u32  ImmRead(u32 size) override;

  void DMAWrite(u32 address, u32 size) override;
  void DMARead(u32 address, u32 size) override;

  bool IsPresent() const override;

  void CreateFile(const std::string& path) { writer = std::make_unique<StreamWriter>(path); }
  void WriteFile(const uint8_t *data, u32 size) { writer->WriteChunk(data, size); }
  void CloseFile() { writer->Flush(); }

  void OpenFile(const std::string& path) { reader = std::make_unique<StreamReader>(path); }
  void ReadFile(uint8_t* buffer, u32 size) { reader->ReadChunk(buffer, size); }
  void ReadFileOffset(uint8_t* buffer, u32 offset, u32 size) { reader->ReadChunkOffset(buffer, offset, size); }
  std::streamsize ReadFileSize() { return reader->GetFileSize(); }

  std::string GenerateReplayFilename();
  int GetLocalNetplayIndex();
  void SetReplay(std::string);

  bool NetPlay_SendGameInput(GCPadStatus* status);
  void NetPlay_InitData();
  void NetPlay_DrainPadQueue();
  u32 NetPlay_GetGameRNG();

  bool CheckActive();

private:
  // rollback
  static constexpr bool ROLLBACK_ENABLE = true;
  static constexpr size_t MAX_ROLLBACK_NUM = 5;
  static constexpr size_t MAX_SAVESTATES = MAX_ROLLBACK_NUM;
  std::unique_ptr<u8[]> m_savestate_alloc;
  u32 m_savestate_num = 0;
  u32 m_req_load = 0;
  size_t m_savestate_size;
  bool m_is_rollback_active = false;
  static constexpr bool ALWAYS_DELAY = false;
  static constexpr bool FORCE_ROLLBACK = false;
  static constexpr size_t MAX_DELAY = 99;
  static constexpr size_t PAD_BUFFER_SIZE = MAX_ROLLBACK_NUM + 1 + MAX_DELAY;  // rollback frames + 1 forward sim frame + 2 delay frames

  int m_local_pid;
  int m_input_delay;
  std::array<Common::SPSCQueue<NetPlay::GameInput>, 4> m_game_queue;
  NetPad m_rollback_buffer[PAD_BUFFER_SIZE][4] = {0};         //
  NetPad m_delay_buffer[PAD_BUFFER_SIZE][4] = {0};            //
  u32 m_gamestate_hash_buffer[PAD_BUFFER_SIZE] = {0};           // all local game state hashes

  u32 m_player_gamestate_hash[4] = {0};           // last hash of the game state we've received from each player
  u32 m_player_gamestate_frame[4] = {0};          // index of the last game state frame we've received from each player
  u32 m_player_drain_num[4] = {0};                // how many frames of inputs we've drained per player this instance. is relative to m_forward_frame
  u32 m_player_confirm_num[4] = {0};              // how many frames of inputs we've confirmed per player this instance. is relative to m_forward_frame
  u8 m_player_pad_map[4] = {0};                   // which ports are present
  int m_sim_frames = 0;                           // how many frames the game should simulate this tick
  bool m_is_sim_forward;                          // whether or not we are simulating forward this update
  u32 m_rollback_num;                             // number of frames we rollback this update
  u32 m_confirm_frame;                            // used to know when we are in a prediction
  u32 m_forward_frame;                            // used for keeping track of the next forward simulation frame
  u32 m_inputs_sent;                              // incremented every time SendGameInput is called. used to prevent sending duplicate inputs for a frame index. also sent over as the input's framestamp (just for debugging)
  u16 m_instance_idx;                             // number of times we switched between delay and rollback. inputs are stamped with this to know whether or not we should discard old inputs after an instance changes
  u32 m_instance_read_start;

  void TransferByte(u8& byte) override;

  // Netplay
  int DolphinData_Prepare();
  void DolphinData_Create(StarpoleDataNetplay* netplay);
  void DolphinData_Send(u8* write_ptr);
  void Netsync_ReceiveInputs(u8* read_ptr, u32 size);
  void Netsync_ReceiveGameState(u8* read_ptr, u32 size);
  void Netsync_SendInputs(u8* write_ptr);
  void Netsync_Init(bool is_rollback_active, u32 input_delay);
  int Netsync_GetConfirmedInputNum();
  u32 Netsync_ValidatePrediction(int ply);
  bool Netsync_CheckSimForward();
  u32 Netsync_GetRollbackNum();
  void Netsync_PredictInputs(int ply);

  // Input
  u32 NetPlay_HashPadStatus(GCPadStatus* status);
  u8 NetPlay_ClampStick(u8 val);
  u8 NetPlay_ClampTrigger(u8 val);
  u32 NetPlay_GetDelay();

  // Recording
  void Replay_Create(u32 modsave_size);
  void ModSave_Receive(u8* read_ptr, u32 size);
  void Match_Receive(u8* read_ptr, u32 size);
  void Frame_Receive(u8* read_ptr, u32 size);
  void End_Receive();

  // Playback
  int Match_Prepare();
  void ModSave_Send(u8* write_ptr);
  void Match_Send(u8* write_ptr);
  int Frame_Prepare(int index);
  void Frame_Send(u8* write_ptr, u32 index);
  bool Frame_Read(StarpoleDataFrame* frame, u32 file_frame_index);
  bool Frame_Get(StarpoleDataFrame* frame, u32 index);
  bool Playback_CheckSimForward();
  u32 Playback_GetRollbackNum();

  // Rollback
  void SaveState_GetChunkSizes(std::vector<DolDataSection> sections, u32 section_num,
                               std::vector<std::pair<u32, u32>>& chunks);
  void SaveState_Init(DolDataSection* read_ptr, u32 section_num);
  void SaveState_End();
  SavestateHeader* SaveState_Get(u32 frame_idx);
  void SaveState(u32 frame_idx);
  void LoadState(u32 frames_back);

  std::string m_name;

  StarpoleCmd cur_cmd = STARPOLE_CMD_NUM;  // current operation being carried out
  u32         cur_args = 0;

  StarpoleReplayHeader    m_replay_header;
  StarpoleDataMatch       m_match_data;   
  u32                     m_file_frame_idx;       // the frame in the file we are reading (can diverge from the game frame if rollbacks are included in the replay)
  u32                     m_game_frame_idx;       // the game frame number the current frame corresponds to
  StarpoleReplayState     replay_state;
  bool                    is_active = false;

  // file
  std::unique_ptr<StreamWriter> writer;
  std::unique_ptr<StreamReader> reader;
  bool is_playback_queued = 0;
  std::string replay_file_path = "";
};

ExpansionInterface::CEXIStarpole* Starpole_Get();
}  // namespace ExpansionInterface

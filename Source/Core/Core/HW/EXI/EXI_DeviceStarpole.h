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
  u32 ToHost() const { return Common::swap32(val); }

  // store host value in big endian format
  void FromHost(u32 host_val) { val = Common::swap32(host_val); }

  // construct from host value
  static be_u32 FromHostValue(u32 host_val) { return be_u32(host_val); }
};
struct be_s32
{
  s32 val;  // stored big endian

  be_s32() : val(0) {}

  explicit be_s32(s32 host_val) { FromHost(host_val); }

  s32 ToHost() const { return static_cast<s32>(Common::swap32(static_cast<u32>(val))); }

  void FromHost(s32 host_val) { val = static_cast<s32>(Common::swap32(static_cast<u32>(host_val))); }

  static be_s32 FromHostValue(s32 host_val) { return be_s32(host_val); }
};
struct be_u16
{
  u16 val;  // stored big-endian

  u16 ToHost() const { return Common::swap16(val); }

  void FromHost(u16 v) { val = Common::swap16(v); }
};
struct be_float
{
  u32 be;  // stored big-endian

  float ToHost() const
  {
    u32 le = Common::swap32(be);
    float f;
    std::memcpy(&f, &le, sizeof(float));
    return f;
  }

  void FromHost(float f)
  {
    u32 le;
    std::memcpy(&le, &f, sizeof(float));
    be = Common::swap32(le);
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

// commands to identify operations
enum class StarpoleCmd
{
  // identify EXI device, returns STARPOLE_DEVICE_ID
  ID,

  // test
  TEST,

  // recording
  MODSAVE,
  MATCH,
  FRAME,
  MATCHEND,
  SEQEND,     // sequence is a collection of matches, like in city trial where there can be 2 stadiums after the city

  // playback
  REQMODSAVE,
  REQMATCH,
  REQFRAME,

  // playback
  CHECKPLAYBACK,

  // dolphin
  DOLPHIN,

  // netsync
  NETSTART,
  NETSAVE,
  NETPADSEND,
  NETPADRECV,
  NETGETCONFIRM,
  NETGAMESTATE,
  NETEND,

  // end
  NUM,
} ;

enum class StarpoleReplayState
{
  NONE,
  RECORD,
  PLAYBACK,
};

enum class StarpoleNetPadState
{
  NOTRECEIVED,
  PREDICTED,
  CORRECTED,
  VERIFIED,
};

enum class GroundKind
{
  CITY1 = 9,
  DRAG1,
  DRAG2,
  DRAG3,
  DRAG4,
  AIRGLIDER,
  TARGETFLIGHT,
  HIGHJUMP,
  KIRBYMELEE1,
  KIRBYMELEE2,
  DESTRUCTIONDERBY1,
  DESTRUCTIONDERBY2,
  DESTRUCTIONDERBY3,
  DESTRUCTIONDERBY4,
  DESTRUCTIONDERBY5,
  SINGLERACE1,
  SINGLERACE2,
  SINGLERACE3,
  SINGLERACE4,
  SINGLERACE5,
  SINGLERACE6,
  SINGLERACE7,
  SINGLERACE8,
  SINGLERACE9,
  VSKINGDEDEDE,
};

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
  char stadium_round;
                          // two more bytes here
  u32 stadium_score[4];
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
  StreamWriter(const std::string& filename);
  ~StreamWriter();
  void WriteChunk(const uint8_t* data, size_t size);

  std::streampos Tell();
  void Seek(std::streampos pos);
  void Flush();

private:
  std::ofstream file;
};
class StreamReader
{
public:
  StreamReader(const std::string& filename);
  std::vector<uint8_t> ReadAll();
  void ReadChunk(uint8_t* buffer, size_t size);
  void ReadChunkOffset(uint8_t* buffer, std::streampos offset, size_t size);
  std::streamsize GetFileSize();
  void Seek(std::streampos pos);
  std::streampos Tell();

private:
  std::ifstream file;
};

namespace ExpansionInterface
{
namespace Starpole
{
enum class SaveKind
{
  Partial,  // used for rollback, does not backup audio
  Full,     // used for playback seeking, includes audio state
};

struct DolDataSection
{
  u32 address;
  u32 size;
  int is_audio;
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
  size_t data_offset;  // exists in savestate
};

struct SavestateHeader
{
  u32 frame_idx;
  u32 file_frame;
  u32 game_frame;
  CpuState cpu;
  size_t chunk_num;
  // followed by array of SavestateChunk
};

// SaveState
class Savestate
{
public:
  Savestate(std::vector<Starpole::DolDataSection> sections, u32 section_num, u32 savestate_num,
            Starpole::SaveKind state_kind, Core::System& system);
  ~Savestate();
  u32 GetSavestateFrameNearest(u32 frame);
  bool Save(u32 frame_idx, u32 file_frame = 0, u32 game_frame = 0);
  bool Load(u32 frame_idx, u32 *file_frame = 0, u32 *game_frame = 0);

private:
  std::vector<std::pair<u32, u32>> GetChunkSizes(std::vector<DolDataSection> sections,
                                                 u32 section_num, SaveKind save_kind);
  Starpole::SavestateHeader* GetFrame(u32 frame_idx);

  size_t m_state_size;  // size of each savestate in the array
  u32 m_state_num;      // number of savestates in the array
  std::unique_ptr<u8[]> m_alloc;

protected:
  Core::System& m_system;
};
}  // namespace Starpole

class ReplayBridge
{
public:
  ReplayBridge();
  ~ReplayBridge();

  bool IsVisible();

  u32 GetCurrentFrame();
  u32 GetTotalFrames();
  u32 GetState();
  bool GetShow();
  bool GetHide();

  void SetCurrentFrame(u32 frame);
  void SetTotalFrames(u32 frames);
  std::optional<u32> ConsumeSeek();
  void SetShow();
  void SetHide();
  void SetSeek(u32 frame);

private:
  struct
  {
    bool is_visible = false;
    int total_frames;
    int current_frame;
    int state;              // loading or playing i guess

    std::atomic<bool> gui_req_show = false;
    std::atomic<bool> gui_req_hide = false;
    std::atomic<int> seek_frame = -1;
  } data;

protected:
};

// Replay Player
static std::mutex crit_replay_bridge;
static ReplayBridge* replay_bridge = nullptr;
void ReplayBridge_Enable(ReplayBridge* const bridge);
void ReplayBridge_Disable();
ReplayBridge* ReplayBridge_Get();

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

  std::string GenerateReplayFilename();
  int GetLocalNetplayIndex();
  void SetReplay(std::string);

  bool CheckActive();

private:
  // rollback
  static constexpr bool ROLLBACK_ENABLE = true;
  static constexpr size_t MAX_ROLLBACK_NUM = 5;
  static constexpr u32 MAX_SAVESTATES = MAX_ROLLBACK_NUM;
  std::unique_ptr<Starpole::Savestate> m_rollback_savestates;
  u32 m_req_rollback = 0;
  bool m_is_rollback_active = false;
  static constexpr bool ALWAYS_DELAY = false;
  static constexpr bool FORCE_ROLLBACK = false;
  static constexpr size_t MAX_DELAY = 99;
  static constexpr size_t PAD_BUFFER_SIZE = MAX_ROLLBACK_NUM + 1 + MAX_DELAY;  // rollback frames + 1 forward sim frame + 2 delay frames

  bool m_is_spectator = false;
  int m_local_pid;
  int m_input_delay;
  std::array<Common::SPSCQueue<NetPlay::GameInput>, 4> m_game_queue;

  std::array<Common::SPSCQueue<NetPad>, 4> m_spectate_queue;
  u32 m_spectate_confirm_num[4] = {0};

  NetPad m_rollback_buffer[PAD_BUFFER_SIZE][4] = {0};           //
  NetPad m_delay_buffer[PAD_BUFFER_SIZE][4] = {0};              //
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

  static constexpr u32 PLAYBACK_SAVESTATE_NUM = (7 * 60) / 5;     // take a savestate every 5 seconds
  std::unique_ptr<Starpole::Savestate> m_playback_savestates;

  void TransferByte(u8& byte) override;

  // file stuff
  void CreateFile(const std::string& path);
  void WriteFile(const uint8_t* data, u32 size);
  void CloseWriter();
  void OpenFile(const std::string& path);
  void ReadFile(uint8_t* buffer, u32 size);
  void ReadFileOffset(uint8_t* buffer, u32 offset, u32 size);
  std::streamsize ReadFileSize();
  void CloseReader();

  // Netplay
  int DolphinData_Prepare();
  void DolphinData_Create(StarpoleDataNetplay* netplay);
  void DolphinData_Send(u8* write_ptr);
  void Netsync_ReceiveInputs(u8* read_ptr, u32 size);
  void Netsync_ReceiveGameState(u8* read_ptr, u32 size);
  void Netsync_SendInputs(u8* write_ptr);
  void Netsync_UpdateTimeSync();
  void Netsync_Init(bool is_rollback_active, u32 input_delay);
  int Netsync_GetConfirmedInputNum();
  u32 Netsync_ValidatePrediction(int ply);
  bool Netsync_CheckSimForward();
  u32 Netsync_GetRollbackNum();
  void Netsync_PredictInputs(int ply);
  bool NetPlay_SendGameInput(GCPadStatus* status);
  void NetPlay_InitData();
  void NetPlay_DrainPadQueue();
  u32 NetPlay_GetGameRNG();
  s32 NetPlay_GetTimeOffset();
  void NetPlay_ClearTimeOffsets();

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
  void MatchEnd_Receive();
  void SeqEnd_Receive();
  std::string Replay_GetStageName(GroundKind kind, int stadium_round);

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

  // State
  void SaveState_Init(Starpole::DolDataSection* read_ptr, u32 section_num);
  void SaveState_End();

  static constexpr bool PLAYBACK_UNLOCKSPEED = true;      // fastforward replay playback by uncapping emulation speed instead of processing the game loop 10x
  static constexpr size_t MAX_SIM_FRAMES = 10;

  std::string m_name;

  StarpoleCmd cur_cmd = StarpoleCmd::NUM;  // current operation being carried out
  u32         cur_args = 0;

  std::string             m_replay_folder_name;
  bool                    m_replay_seq_end = true;
  StarpoleReplayHeader    m_replay_header;
  std::unique_ptr<u8[]>   m_mod_save_alloc;
  u32                     m_mod_save_size;
  StarpoleDataMatch       m_match_data;   
  StarpoleDataNetplay     m_dolphin_data;
  u32                     m_file_frame_idx;       // the frame in the file we are reading (can diverge from the game frame if rollbacks are included in the replay)
  u32                     m_game_frame_idx;       // the game frame number the current frame corresponds to
  StarpoleReplayState     replay_state;
  bool                    is_active = false;

  // file
  std::unique_ptr<StreamWriter> writer;
  std::unique_ptr<StreamReader> reader;
  bool is_playback_queued = 0;
  std::string replay_file_path = "";

  std::optional<u32> m_playback_desired_frame = std::nullopt;
  std::optional<u32> m_playback_req_load = std::nullopt;     // savestate index to load

};

ExpansionInterface::CEXIStarpole* Starpole_Get();
}  // namespace ExpansionInterface

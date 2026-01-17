// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "Core/HW/EXI/EXI_Device.h"

#include <cstdint>
#include <fstream>

#include "Common/FileUtil.h"      // needed to access user directory
#include <filesystem>
#include <iostream>

#define STARPOLE_DEVICE_ID 0x0A000000

// big endian conversion
struct be_u32
{
  u32 val;  // stored big-endian

  // Default constructor
  be_u32() : val(0) {}

  // Construct from host uint32
  explicit be_u32(u32 host_val) { FromHost(host_val); }

  // Convert stored big-endian to host
  u32 ToHost() const
  {
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
  }

  // Store host value in big-endian format
  void FromHost(u32 host_val)
  {
#if defined(_MSC_VER)
    val = _byteswap_ulong(host_val);
#else
    val = __builtin_bswap32(host_val);
#endif
  }

  // Static helper: construct from host value
  static be_u32 FromHostValue(u32 host_val) { return be_u32(host_val); }
};
struct be_s32
{
  s32 val;  // stored big-endian

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

// commands to identify operations
typedef enum
{
  // identify EXI device, returns STARPOLE_DEVICE_ID
  STARPOLE_CMD_ID,

  // test
  STARPOLE_CMD_TEST,

  // recording
  STARPOLE_CMD_MATCH,
  STARPOLE_CMD_FRAME,
  STARPOLE_CMD_END,

  // playback
  STARPOLE_CMD_REQMATCH,
  STARPOLE_CMD_REQFRAME,

  // playback
  STARPOLE_CMD_CHECKPLAYBACK,

  // netplay
  STARPOLE_CMD_NETPLAY,

  // end
  STARPOLE_CMD_NUM,
} StarpoleCmd;

typedef enum
{
  STARPOLE_REPLAYSTATE_NONE,
  STARPOLE_REPLAYSTATE_RECORD,
  STARPOLE_REPLAYSTATE_PLAYBACK,
} StarpoleReplayState;

// payload structures
typedef struct
{
  char str[128];
} StarpoleDataTest;
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
  be_s32 ply;
  char usernames[4][31];
} StarpoleDataNetplay;

#pragma pack(push, 1)
typedef struct
{
  be_u32 frame_idx;
  be_u32 rng_seed;
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

private:
  void TransferByte(u8& byte) override;

  // Netplay
  void Netplay_SendInfo(u8* write_ptr);

  // Recroding
  void Match_Receive(u8* read_ptr, u32 size);
  void Frame_Receive(u8* read_ptr, u32 size);
  void End_Receive();

  // Playback
  int Match_Prepare();
  void Match_Send(u8* write_ptr);
  int Frame_Prepare(int index);
  void Frame_Send(u8* write_ptr, u32 index);

  std::string m_name;

  StarpoleCmd cur_cmd = STARPOLE_CMD_NUM;  // current operation being carried out
  u32         cur_args = 0;

  StarpoleDataMatch     match_data;   
  u32                   frame_idx;       // used to sequentially send game frames
  StarpoleReplayState   replay_state;

  // file
  std::unique_ptr<StreamWriter> writer;
  std::unique_ptr<StreamReader> reader;
  bool is_playback_queued = 0;
  std::string replay_file_path = "";
};

void DroppedReplay(std::string path);
}  // namespace ExpansionInterface

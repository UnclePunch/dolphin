// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "Core/HW/EXI/EXI_Device.h"

#include <cstdint>
#include <fstream>

#define STARPOLE_DEVICE_ID 0x0A000000

// big endian conversion
struct be_u32
{
  u32 val;  // stored big-endian

  u32 ToHost() const
  {
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
  }

  void FromHost(u32 v)
  {
#if defined(_MSC_VER)
    val = _byteswap_ulong(v);
#else
    val = __builtin_bswap32(v);
#endif
  }
};
struct be_s32
{
  s32 val;  // stored big-endian

  s32 ToHost() const
  {
#if defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return __builtin_bswap32(val);
#endif
  }

  void FromHost(u32 v)
  {
#if defined(_MSC_VER)
    val = _byteswap_ulong(v);
#else
    val = __builtin_bswap32(v);
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

  // end
  STARPOLE_CMD_NUM,
} StarpoleCmd;

// payload structures
typedef struct
{
  char str[128];
} StarpoleDataTest;
typedef struct
{
  be_u32 rng_seed;
  be_u32 gr_kind;
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
  be_u32 frame_idx;
  be_u32 rng_seed;
  be_u32 ply_num;
  struct
  {
    be_u32 idx;
    struct
    {
      be_vec2 lstick;
      be_vec2 rstick;
      be_u32 buttons;
    } input;
    be_u32 rd_state;
    be_s32 machine_kind;
    be_vec3 pos;
  } ply[4];
} StarpoleDataFrame;

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

private:
  void TransferByte(u8& byte) override;

  void Receive_Match(u8* read_ptr, u32 size);
  void Receive_Frame(u8* read_ptr, u32 size);
  void Receive_End();

  int Match_Prepare();
  void Send_Match(u8* write_ptr);
  void Send_Frame(u8* write_ptr, u32 index);

  std::string m_name;

  StarpoleCmd cur_cmd = STARPOLE_CMD_NUM;  // current operation being carried out
  u32         cur_args = 0;

  u32 frame_idx;                           // used to sequentially send game frames
  StarpoleDataMatch in_match_data;

  // file
  std::unique_ptr<StreamWriter> writer = 0;
  std::unique_ptr<StreamReader> reader = 0;
};
}  // namespace ExpansionInterface

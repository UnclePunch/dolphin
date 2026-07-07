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

#include "Core/Config/MainSettings.h" // needed to access values from dolphins config files
#include "Core/Core.h"                // needed to exec code from UI thread on main thread
#include "Core/HW/Memmap.h"           // needed to write directly to game memory using DMA
#include "Core/System.h"              // needed to write directly to game memory using DMA
#include "Core/PowerPC/PowerPC.h"     // 

static ExpansionInterface::Starpole::DolDataSection m_preserve_sections[] = {
    // {0, 0},
    {0x80003100, 0x2500},    // dol text section 1
    {0x80005800, 0x483C40},  // dol text section 2
    {0, 0},                  // stay
    {0, 0},                  // AllM
    {0x00000000, 0x96000},   // XFB buffer 1 80589a48
    {0x00000000, 0x96000},   // XFB buffer 2 80589a4c
    {0x00000000, 0x80000},   // gx init alloc in arena lo, performed at 8040fc3c
    {0, 0},                  // audio heap
    {0, 0},                  // hoshi + mods
    {0x80550f68, 0x1008},    // file preload table

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
    // 8056d958 - thread data? unsure of size, referenced @ 803d9e8c. also some r13 variables, E48 -
    // E50 inclusive
    // interrupt data. 0xD80 -> 0xE0C

    {0x80508bc8, 0x4 * 3},  // BGM PID's. needed to stop a song from playing
    // {0x80535994, 16 * 4},                     // AR region, actual size is 16 * 4
    // {0x80538088, 0x17a28},                    // AudioSourceTable
    // {0x805383c4, 0xB8 * 512},                 // just audio emitters?
    {0x805dd0e0 + 0xF20, 0xF68 - 0xF20},  // ARQ and hsd audio sbss

    {0x805dd0e0 + 0xAC, 0x8},  // 64 bitfield that is raised when the corresponding sg has its
                               // volume changed, 0x8044c450

    {0x80599c60,
     0x8059a818 -
         0x80599c60},  // more audio stuff. sg indexed audio data in here @ 8059a178 and 8059a160?
    {0x805dd0e0 + 0x1358, 0x1470 - 0x1358},  // hsd audio sbss

    {0x8056ccb4, 0x24},  // DVD Waiting Queue
    {0x8056cb40, 0xE0},  // DVD Interrupt stuff @ 803c40b4. includes alarm
    {0x8056cc20, 0x94},  // DVD state stuff @ 803c67f0. another alarm at 0x70 of this?

    //{0x8056CCB4, 0x1D34},                    // lots of stuff, VI, SI, etc. just testing
    //{0x8056e3a0, 0x144},                      // VI Frame Buffer stuff @ 803df32c
    //{0x805dd0e0 + 0xED8, 0xEDC - 0xEB0},      // VI Frame Buffer variables

    {0x805dd0e0 + 0xC60, 0xCF4 - 0xC60},  // disc read variables

    {0x805dd0e0 + 0xDC8, 0xDD0 - 0xDC8},  // OSAlarm variables

    {0x805dd0e0 + 0x4C8, 0x4},  // file async load flag

    {0x8056e9e8,
     0x80587A60 - 0x8056e9e8},  // all the AX data i know of, AXStack head -> end of __AXVPB
    {0x805dd0e0 + 0xF30, 0x1054 - 0xF30},  // AX region sbss

    {0x8058e298, 64 * 0x4},    // array of VPB pointers? indexed by FGMInstance index
    {0x8058E398, 0x8F8},       // unknown in between chunks, part of this is the fgm_kind struct,
                               // referenced @ 80442a24
    {0x8058ec90, 0x90},        // hps stream unk struct @ 804464bc
    {0x8058ed20, 2 * 0x4000},  // hps double buffer?
    {0x80596d20, 0x40},        // hps streaming @ 80446a74
    {0x80596d60, 0x50},        // hps streaming stuff
    {0x80596da0, 160 + (512 * 3)},  // FGM region, multiple offsets of this loaded around 80447ee4.
                                    // also includes some HPS streaming stuff
    {0x80597440, 0x220},            // unknown in between chunks

    {0x80597660, 64 * 0x98},  // AXLive voice array. (8044ccf0)
    {0x80597F20, 64 * 152},   // static audio lookup 0X8c0 (8044ccf0)
    // above ends at 0x8059A520 for reference

    {0x8059a880,
     0x618},  // memcard thread data? referenced by the function 8045b848 in the thread func
    {0x805b4698, 0x35C},  // memcard thread data

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

namespace ExpansionInterface
{
namespace Starpole
{
Savestate::Savestate(std::vector<DolDataSection> sections, u32 section_num,
                     u32 savestate_num, SaveKind state_kind, Core::System& system)
    : m_system(system)
{
  // determine chunk info
  auto chunks = GetChunkSizes(sections, section_num, state_kind);
  size_t chunk_num = chunks.size();

  // determine size of the raw data to backup
  size_t data_size = 0;
  for (const auto& [addr, size] : chunks)
  {
    data_size += size;
  }

  // determine size of each savestate
  size_t savestate_size =
      sizeof(SavestateHeader) + (sizeof(SavestateChunk) * chunk_num) + data_size;

  // alloc
  m_alloc =
      std::make_unique<u8[]>((savestate_size * savestate_num));

  // get header
  m_state_num = savestate_num;
  m_state_size = savestate_size;

  // init savestates
  for (u32 save_idx = 0; save_idx < savestate_num; save_idx++)
  {
    auto* state_header =
        reinterpret_cast<SavestateHeader*>(m_alloc.get() + (savestate_size * save_idx));

    state_header->chunk_num = chunk_num;

    size_t this_chunk_data_offset = sizeof(SavestateHeader) + (sizeof(SavestateChunk) * chunk_num);

    // init chunks
    for (int chunk_idx = 0; chunk_idx < chunk_num; chunk_idx++)
    {
      auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)state_header + sizeof(SavestateHeader) +
                                                      (sizeof(SavestateChunk) * chunk_idx));
      chunk->address = chunks[chunk_idx].first;
      chunk->size = chunks[chunk_idx].second;
      chunk->data_offset = this_chunk_data_offset;

      this_chunk_data_offset += chunk->size;
    }
  }
}

Savestate::~Savestate() = default;

std::vector<std::pair<u32, u32>> Savestate::GetChunkSizes(std::vector<DolDataSection> sections, u32 section_num, SaveKind save_kind)
{
  std::vector<std::pair<u32, u32>> chunks;

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

    // temp: skip audio sections
    if (save_kind == SaveKind::Full && ex.is_audio == 1)
      continue;

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

  return chunks;
}

SavestateHeader* Savestate::GetFrame(u32 frame_idx)
{
  u32 save_idx = frame_idx % m_state_num;

  auto* savestate =
      reinterpret_cast<SavestateHeader*>(m_alloc.get() + (m_state_size * save_idx));

  return savestate;
}

bool Savestate::Save(u32 frame_idx, u32 file_frame, u32 game_frame)
{
  auto start = std::chrono::high_resolution_clock::now();

  // u32 heap_start = m_system.GetMemory().Read_U32(0x80537f58);
  // u32 heap_size = m_system.GetMemory().Read_U32(0x80537f5c);

  auto* savestate = GetFrame(frame_idx);

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

  savestate->file_frame = file_frame;
  savestate->game_frame = game_frame;

  // save chunks
  for (int chunk_idx = 0; chunk_idx < savestate->chunk_num; chunk_idx++)
  {
    auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)savestate + sizeof(SavestateHeader) +
                                                    (sizeof(SavestateChunk) * chunk_idx));

    u8* ptr = m_system.GetMemory().GetPointerForRange(chunk->address, chunk->size);
    if (ptr)
    {
      auto copy_start = std::chrono::high_resolution_clock::now();
      memcpy((u8*)savestate + chunk->data_offset, ptr, chunk->size);
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
               savestate->frame_idx, m_state_size / (1024.0f * 1024.0f),
               duration.count() / 1000.0);

  // for (u32 i = 0; i < m_savestate_num; i++)
  //{
  //   int target_idx = ((int)i + MAX_SAVESTATES) % MAX_SAVESTATES;
  //   savestate = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() +
  //                                                  (m_savestate_size * target_idx));

  //  INFO_LOG_FMT(EXPANSIONINTERFACE, "savestate index {}: frame {}", i, savestate->frame_idx);
  //}

  return true;
}

bool Savestate::Load(u32 frame_idx, u32 *file_frame, u32 *game_frame)
{
  auto start = std::chrono::high_resolution_clock::now();

  auto* savestate = GetFrame(frame_idx);

  // ensure its for the frame we want
  if (savestate->frame_idx != frame_idx)
  {
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Error loading savestate for frame {}. Does not exist.",
                  frame_idx);
    return false;
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

  if (file_frame)
    *file_frame = savestate->file_frame;

  if (game_frame)
    *game_frame = savestate->game_frame;

  // restore chunks
  for (int chunk_idx = 0; chunk_idx < savestate->chunk_num; chunk_idx++)
  {
    auto* chunk = reinterpret_cast<SavestateChunk*>((u8*)savestate + sizeof(SavestateHeader) +
                                                    (sizeof(SavestateChunk) * chunk_idx));

    u8* ptr = m_system.GetMemory().GetPointerForRange(chunk->address, chunk->size);
    if (ptr)
    {
      // auto copy_start = std::chrono::high_resolution_clock::now();
      memcpy(ptr, (u8*)savestate + chunk->data_offset, chunk->size);
      // auto copy_end = std::chrono::high_resolution_clock::now();

      // auto copy_duration =
      //     std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start);
      // INFO_LOG_FMT(EXPANSIONINTERFACE, " memcpy'd {:08X} section in {:.4f} ms", chunk->size,
      //              copy_duration.count() / 1000.0);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  INFO_LOG_FMT(EXPANSIONINTERFACE, "frame {} savestate loaded: {:.2f} MB in {:.2f} ms",
               savestate->frame_idx, (m_state_size / (1024.0 * 1024.0)),
               duration.count() / 1000.0);

  // for (u32 i = 0; i < m_savestate_num; i++)
  //{
  //   int target_idx = ((int)i + MAX_SAVESTATES) % MAX_SAVESTATES;
  //   savestate = reinterpret_cast<SavestateHeader*>(m_savestate_alloc.get() +
  //                                                  (m_savestate_size * target_idx));

  //  INFO_LOG_FMT(EXPANSIONINTERFACE, "savestate index {}: frame {}", i, savestate->frame_idx);
  //}

  return true;
}

}  // namespace Starpole

void CEXIStarpole::SaveState_Init(Starpole::DolDataSection* read_ptr, u32 section_num)
{
  if (ALWAYS_DELAY)
    return;

  if (m_is_spectator)
    return;

  if (m_rollback_savestates != nullptr)
    SaveState_End();

  // swap byte order cause its coming from PPC
  std::vector<Starpole::DolDataSection> sections(section_num);
  for (size_t i = 0; i < section_num; i++)
  {
    sections[i].address = Common::swap32(read_ptr[i].address);
    sections[i].size = Common::swap32(read_ptr[i].size);
    sections[i].is_audio = Common::swap32(read_ptr[i].is_audio);
  }

  m_rollback_savestates = std::make_unique<Starpole::Savestate>(sections, section_num, MAX_SAVESTATES, Starpole::SaveKind::Partial, m_system);
  m_playback_savestates = std::make_unique<Starpole::Savestate>(sections, section_num, PLAYBACK_SAVESTATE_NUM, Starpole::SaveKind::Full, m_system);

  //m_savestate_alloc = SaveState_Alloc(sections, section_num, (7 * 60 * 60) / 5, StarpoleSavestate::Full);

  Netsync_Init(true, Config::Get(Config::MAIN_STARPOLE_NET_DELAY));
}

void CEXIStarpole::SaveState_End()
{
  if (m_is_spectator)
    return;

  // free savestate buffers
  m_rollback_savestates.reset();
  m_playback_savestates.reset();

  // init delay based netcode
  Netsync_Init(false, 0);
}
}  // namespace ExpansionInterface



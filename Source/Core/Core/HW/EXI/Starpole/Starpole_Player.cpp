// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Channel.h"
#include "Core/HW/EXI/EXI_Device.h"

#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "Core/Config/MainSettings.h"  // starpole UI pane config settings

namespace ExpansionInterface
{
ReplayBridge::ReplayBridge()
{
  ReplayBridge_Enable(this);
}
ReplayBridge::~ReplayBridge()
{
  ReplayBridge_Disable();
}
bool ReplayBridge::IsVisible()
{
  return data.is_visible;
}
u32 ReplayBridge::GetCurrentFrame()
{
  return data.current_frame;
}
u32 ReplayBridge::GetTotalFrames()
{
  return data.total_frames;
}
u32 ReplayBridge::GetState()
{
  return data.state;
}
bool ReplayBridge::GetShow()
{
  return data.gui_req_show.exchange(false);
}
bool ReplayBridge::GetHide()
{
  return data.gui_req_hide.exchange(false);
}
std::optional<u32> ReplayBridge::ConsumeSeek()
{
  int frame = data.seek_frame.exchange(-1);
  if (frame < 0)
    return std::nullopt;

   return frame;
}

void ReplayBridge::SetTotalFrames(u32 frames)
{
  data.total_frames = frames;
}
void ReplayBridge::SetCurrentFrame(u32 frame)
{
  data.current_frame = frame;
}
void ReplayBridge::SetShow()
{
  data.gui_req_show = true;
}
void ReplayBridge::SetHide()
{
  data.gui_req_hide = true;
}
void ReplayBridge::SetSeek(u32 frame)
{
  data.seek_frame = frame;
}

void ReplayBridge_Enable(ReplayBridge* const bridge)
{
  std::lock_guard lk(crit_replay_bridge);
  replay_bridge = bridge;
}
void ReplayBridge_Disable()
{
  std::lock_guard lk(crit_replay_bridge);
  replay_bridge = nullptr;
}
ReplayBridge* ReplayBridge_Get()
{
  return replay_bridge;
}
} // namespace ExpansionInterface

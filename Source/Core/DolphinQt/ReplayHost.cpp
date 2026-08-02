// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/ReplayHost.h"

#include <functional>

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QLocale>
#include <QThread>

#include "Common/Common.h"

#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/Debugger/PPCDebugInterface.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/State.h"
#include "Core/System.h"

#include "DolphinQt/QtUtils/QueueOnObject.h"
#include "DolphinQt/Settings.h"
#include "DolphinQt/ReplayHost.h"

ReplayHost::ReplayHost()
{
}

ReplayHost::~ReplayHost()
{
}

ReplayHost* ReplayHost::GetInstance()
{
  static ReplayHost* s_instance = new ReplayHost();
  return s_instance;
}

void ReplayHost::ReqSeek(u32 frame)
{
  m_seek_frame = frame;
}

std::optional<u32> ReplayHost::ConsumeSeek()
{
  int frame = m_seek_frame.exchange(-1);
  if (frame < 0)
    return std::nullopt;

  return frame;
}

void ReplayHost::SetCurrentFrame(u32 frame)
{
  m_current_frame = frame;
}
u32 ReplayHost::GetCurrentFrame()
{
  return m_current_frame;
}

void ReplayHost::SetTotalFrames(u32 frame)
{
  m_total_frames = frame;
}
u32 ReplayHost::GetTotalFrames()
{
  return m_total_frames;
}

void ReplayHost::SetHide()
{
  m_is_hide = true;
}
bool ReplayHost::GetHide()
{
  return m_is_hide.exchange(false);
}

void ReplayHost::SetShow()
{
  m_is_show = true;
}
bool ReplayHost::GetShow()
{
  return m_is_show.exchange(false);
}

// Access Functions
std::optional<u32> ReplayHost_ConsumeSeek()
{
  return ReplayHost::GetInstance()->ConsumeSeek();
}

void ReplayHost_SetCurrentFrame(u32 frame)
{
  ReplayHost::GetInstance()->SetCurrentFrame(frame);
}
u32 ReplayHost_GetCurrentFrame()
{
  return ReplayHost::GetInstance()->GetCurrentFrame();
}

void ReplayHost_SetTotalFrames(u32 frames)
{
  ReplayHost::GetInstance()->SetTotalFrames(frames);
}
u32 ReplayHost_GetTotalFrames()
{
  return ReplayHost::GetInstance()->GetTotalFrames();
}

void ReplayHost_SetHide()
{
  ReplayHost::GetInstance()->SetHide();
}
bool ReplayHost_GetHide()
{
  return ReplayHost::GetInstance()->GetHide();
}

void ReplayHost_SetShow()
{
  ReplayHost::GetInstance()->SetShow();
}
bool ReplayHost_GetShow()
{
  return ReplayHost::GetInstance()->GetShow();
}

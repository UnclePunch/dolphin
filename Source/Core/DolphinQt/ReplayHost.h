#pragma once
// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include <QObject>

// Singleton that talks to the Core via the interface defined in Core/ReplayHost.h.
// Because ReplayHost_* calls might come from different threads than the MainWindow,
// the ReplayHost class communicates with it via signals/slots only.

// Many of the ReplayHost_* functions are ignored, and some shouldn't exist.
class ReplayHost final : public QObject
{
  Q_OBJECT

public:
  ~ReplayHost() override;

  static ReplayHost* GetInstance();

  void ReqSeek(u32 frame);
  std::optional<u32> ConsumeSeek();
  void SetCurrentFrame(u32 frame);
  u32 GetCurrentFrame();

  void SetTotalFrames(u32 frames);
  u32 GetTotalFrames();

  void SetHide();
  bool GetHide();

  void SetShow();
  bool GetShow();

signals:

private:
  ReplayHost();

  std::atomic<u32> m_seek_frame = -1;
  std::atomic<u32> m_current_frame = 0;
  std::atomic<u32> m_total_frames = 0;
  std::atomic<bool> m_is_show = 0;
  std::atomic<bool> m_is_hide = 0;
  
};

// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/Mapping/HotkeyStarpole.h"

#include <QGroupBox>
#include <QHBoxLayout>

#include "Core/HotkeyManager.h"

HotkeyStarpole::HotkeyStarpole(MappingWindow* window) : MappingWidget(window)
{
  CreateMainLayout();
}

void HotkeyStarpole::CreateMainLayout()
{
  m_main_layout = new QHBoxLayout();

  m_main_layout->addWidget(
      CreateGroupBox(tr("Replays"), HotkeyManagerEmu::GetHotkeyGroup(HKGP_STARPOLE)));

  setLayout(m_main_layout);
}

InputConfig* HotkeyStarpole::GetConfig()
{
  return HotkeyManagerEmu::GetConfig();
}

void HotkeyStarpole::LoadSettings()
{
  HotkeyManagerEmu::LoadConfig();
}

void HotkeyStarpole::SaveSettings()
{
  HotkeyManagerEmu::GetConfig()->SaveConfig();
}

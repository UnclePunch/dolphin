// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

class QLineEdit;
class QPushButton;

class ReplayScrubber : public QWidget
{
  Q_OBJECT
public:
  explicit ReplayScrubber(QWidget* parent = nullptr);
  void showNormal();

signals:
  void SeekFrame(u32 frame);
  void SetCurrentFrame(u32 frame);

private:
  void CreateWidgets();
  void ConnectWidgets();

  bool eventFilter(QObject* object, QEvent* event) final;
  void Update();
  void OnSliderReleased();

  QLabel* m_time_label;
  QSlider* m_slider;
};

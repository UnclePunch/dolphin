// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QEvent>
#include <QWidget>

class QMouseEvent;
class QTimer;
class RenderWidget;
class ReplayScrubber;

class RenderWindow final : public QWidget
{
  Q_OBJECT

public:
  explicit RenderWindow(QWidget* parent = nullptr);
};

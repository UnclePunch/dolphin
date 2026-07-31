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
  void showFullScreen();
  bool event(QEvent* event) override;
  void showNormal();
  void hide();
  RenderWidget *m_render_widget = nullptr;

signals:
  void EscapePressed();
  void Closed();
  void HandleChanged(void* handle);
  void StateChanged(bool fullscreen);
  void SizeChanged(int new_width, int new_height);
  void FocusChanged(bool focus);

private:
  ReplayScrubber* m_scrubber_widget;
  void OnHandleChanged(void* handle);
};

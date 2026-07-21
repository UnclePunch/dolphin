// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/RenderWindow.h"

#include <array>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

#include "DolphinQt/Host.h"
#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"
#include "DolphinQt/RenderWidget.h"
#include "DolphinQt/ReplayScrubber.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/OnScreenUI.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoConfig.h"

#ifdef _WIN32
#include <Windows.h>
#include <dwmapi.h>
#endif

RenderWindow::RenderWindow(QWidget* parent) : QWidget(parent)
{
  m_render_widget = new RenderWidget;
  m_scrubber_widget = new ReplayScrubber;

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_render_widget, 1);
  layout->addWidget(m_scrubber_widget, 0);

}

bool RenderWindow::event(QEvent* event)
{
  return m_render_widget->event(event);
}

void RenderWindow::Show()
{
  m_render_widget->showNormal();
  m_scrubber_widget->Show();
  show();
}
void RenderWindow::Hide()
{
  m_render_widget->hide();
  m_scrubber_widget->Hide();
  hide();
}

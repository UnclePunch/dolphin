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
  setWindowTitle(QStringLiteral("Dolphin"));
  setWindowIcon(Resources::GetAppIcon());
  setWindowRole(QStringLiteral("renderer"));

  QPalette p;
  p.setColor(QPalette::Window, Qt::black);
  setPalette(p);

  m_render_widget = new RenderWidget;
  installEventFilter(m_render_widget);

  m_scrubber_widget = new ReplayScrubber;
  m_scrubber_widget->hide();

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_render_widget, 1);
  layout->addWidget(m_scrubber_widget, 0);

  connect(Host::GetInstance(), &Host::RequestTitle, this, &RenderWindow::setWindowTitle);
  connect(this, &RenderWindow::StateChanged, Host::GetInstance(), &Host::SetRenderFullscreen,
          Qt::DirectConnection);
  connect(this, &RenderWindow::HandleChanged, this, &RenderWindow::OnHandleChanged,
           Qt::DirectConnection);
  connect(this, &RenderWindow::FocusChanged, Host::GetInstance(), &Host::SetRenderFocus,
           Qt::DirectConnection);
  connect(this, &RenderWindow::SizeChanged, Host::GetInstance(), &Host::ResizeSurface,
           Qt::DirectConnection);
}

bool RenderWindow::event(QEvent* event)
{
  // PassEventToPresenter(event);

  switch (event->type())
  {
  case QEvent::WindowStateChange:
    emit StateChanged(isFullScreen());
    break;
  // Note that this event in Windows is not always aligned to the window that is highlighted,
  // it's the window that has keyboard and mouse focus
  case QEvent::WindowActivate:
    emit FocusChanged(true);
    break;
  case QEvent::WindowDeactivate:
    emit FocusChanged(false);
    break;
  case QEvent::WinIdChange:
    emit HandleChanged(reinterpret_cast<void*>(winId()));
    break;
  case QEvent::Close:
    emit Closed();
    break;
  default:
    break;
  }
  return QWidget::event(event);
}

void RenderWindow::showNormal()
{
  m_render_widget->showNormal();
  QWidget::showNormal();
}
void RenderWindow::hide()
{
  m_render_widget->hide();
  m_scrubber_widget->hide();
  QWidget::hide();
}

void RenderWindow::OnHandleChanged(void* handle)
{
  if (handle)
  {
#ifdef _WIN32
    // Remove rounded corners from the render window on Windows 11
    const DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(static_cast<HWND>(handle), DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner_preference, sizeof(corner_preference));
#endif
  }
  Host::GetInstance()->SetRenderHandle(handle);
}

void RenderWindow::showFullScreen()
{
  QWidget::showFullScreen();

  QScreen* screen = window()->windowHandle()->screen();

  const auto dpr = screen->devicePixelRatio();

  emit SizeChanged(width() * dpr, height() * dpr);
}

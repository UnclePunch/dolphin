// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/ReplayScrubber.h"
#include "DolphinQt/ClickSlider.h"

#include "Core/HW/EXI/EXI_DeviceStarpole.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

ReplayScrubber::ReplayScrubber(QWidget* parent) : QWidget(parent)
{
  CreateWidgets();
  ConnectWidgets();

  // setFixedHeight(32);
  // setHidden(true);

  installEventFilter(this);

  // create replay player bridge
  m_bridge = new ExpansionInterface::ReplayBridge();
}

void ReplayScrubber::CreateWidgets()
{
  m_time_label = new QLabel;
  m_time_label->setMinimumWidth(80);
  m_time_label->setText(QStringLiteral("0:00 / 7:00"));
  m_slider = new ClickSlider(Qt::Horizontal);

  auto* layout = new QHBoxLayout();
  layout->addWidget(m_time_label, 0);
  layout->addWidget(m_slider, 1);

  setLayout(layout);
}

void ReplayScrubber::Show()
{
  m_slider->setRange(0, m_bridge->GetTotalFrames() / 60);
  show();
}

void ReplayScrubber::Hide()
{
  hide();
}

void ReplayScrubber::ConnectWidgets()
{
  // create timer to update widget every 16ms
  auto timer = new QTimer(this);
  timer->start(16);   // ~60Hz

  // set update function
  connect(timer, &QTimer::timeout, this, &ReplayScrubber::Update);
  connect(m_slider, &ClickSlider::sliderReleased, this, &ReplayScrubber::OnSliderReleased);

  //connect(m_search_edit, &QLineEdit::textChanged, this, &ReplayScrubber::Search);
  //connect(m_close_button, &QPushButton::clicked, this, &ReplayScrubber::Hide);
}

bool ReplayScrubber::eventFilter(QObject* object, QEvent* event)
{
  //if (event->type() == QEvent::KeyPress)
  //{
  //  if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
  //    Hide();
  //}

  return false;
}

void ReplayScrubber::OnSliderReleased()
{
  const u32 frame = static_cast<u32>(m_slider->value() * 60);
  m_bridge->SetSeek(frame);
  m_bridge->SetCurrentFrame(frame);   // might fix the slider jumping around
}

void ReplayScrubber::Update()
{
  if (m_bridge->GetShow())
    Show();
  else if (m_bridge->GetHide())
    Hide();

  u32 cur_frame_idx = m_bridge->GetCurrentFrame();
  const u32 cur_total_seconds = cur_frame_idx / 60;
  const u32 cur_minutes = cur_total_seconds / 60;
  const u32 cur_seconds = cur_total_seconds % 60;

  u32 end_frame_idx = m_bridge->GetTotalFrames();
  const u32 end_total_seconds = end_frame_idx / 60;
  const u32 end_minutes = end_total_seconds / 60;
  const u32 end_seconds = end_total_seconds % 60;

  std::ostringstream oss;
  oss << cur_minutes << ':' << std::setfill('0') << std::setw(2) << cur_seconds;
  oss << " / ";
  oss << end_minutes << ':' << std::setfill('0') << std::setw(2) << end_seconds;

  m_time_label->setText(QString::fromStdString(oss.str()));

  if (!m_slider->isSliderDown())
    m_slider->setValue(cur_frame_idx / 60);
  
}

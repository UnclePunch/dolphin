// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/ReplayScrubber.h"
#include "DolphinQt/ClickSlider.h"
#include "DolphinQt/ReplayHost.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QSlider>
#include <QLabel>

ReplayScrubber::ReplayScrubber(QWidget* parent) : QWidget(parent)
{
  CreateWidgets();
  ConnectWidgets();

  installEventFilter(this);
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

void ReplayScrubber::showNormal()
{
  m_slider->setRange(0, ReplayHost_GetTotalFrames());
  QWidget::showNormal();
}

void ReplayScrubber::ConnectWidgets()
{
  // create timer to update widget every 16ms
  auto timer = new QTimer(this);
  timer->start(16);   // ~60Hz

  // set update function
  connect(timer, &QTimer::timeout, this, &ReplayScrubber::Update);
  connect(m_slider, &ClickSlider::sliderReleased, this, &ReplayScrubber::OnSliderReleased);

  connect(this, &ReplayScrubber::SeekFrame, ReplayHost::GetInstance(), &ReplayHost::ReqSeek,
          Qt::DirectConnection);
  connect(this, &ReplayScrubber::SetCurrentFrame, ReplayHost::GetInstance(), &ReplayHost::SetCurrentFrame,
          Qt::DirectConnection);
}

bool ReplayScrubber::eventFilter(QObject* object, QEvent* event)
{
  return false;
}

void ReplayScrubber::OnSliderReleased()
{
  const u32 frame = static_cast<u32>(m_slider->value());
  emit SeekFrame(frame);
  emit SetCurrentFrame(frame);

  //m_bridge->SetSeek(frame);
  //m_bridge->SetCurrentFrame(frame);   // might fix the slider jumping around
}

void ReplayScrubber::Update()
{
  if (ReplayHost_GetShow())
    showNormal();
  else if (ReplayHost_GetHide())
    hide();

  u32 cur_frame_idx = ReplayHost_GetCurrentFrame();
  const u32 cur_total_seconds = cur_frame_idx / 60;
  const u32 cur_minutes = cur_total_seconds / 60;
  const u32 cur_seconds = cur_total_seconds % 60;

  u32 end_frame_idx = ReplayHost_GetTotalFrames();
  const u32 end_total_seconds = end_frame_idx / 60;
  const u32 end_minutes = end_total_seconds / 60;
  const u32 end_seconds = end_total_seconds % 60;

  std::ostringstream oss;
  oss << cur_minutes << ':' << std::setfill('0') << std::setw(2) << cur_seconds;
  oss << " / ";
  oss << end_minutes << ':' << std::setfill('0') << std::setw(2) << end_seconds;

  m_time_label->setText(QString::fromStdString(oss.str()));

  if (!m_slider->isSliderDown())
    m_slider->setValue(cur_frame_idx);
  
}

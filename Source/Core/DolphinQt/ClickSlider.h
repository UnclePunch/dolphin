#pragma once

#include <QSlider>

class ClickSlider : public QSlider
{
public:
  using QSlider::QSlider;

protected:
  void mousePressEvent(QMouseEvent* event) override;
};

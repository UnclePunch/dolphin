#include "DolphinQt/ClickSlider.h"

#include <QMouseEvent>
#include <QStyle>

void ClickSlider::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    const int val = QStyle::sliderValueFromPosition(
        minimum(), maximum(), orientation() == Qt::Horizontal ? event->pos().x() : event->pos().y(),
        orientation() == Qt::Horizontal ? width() : height());
    setValue(val);
    event->accept();
  }
  QSlider::mousePressEvent(event);
}

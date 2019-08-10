/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2017 Calle Laakkonen

   Drawpile is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Drawpile is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Drawpile.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RESIZERWIDGET_H
#define RESIZERWIDGET_H

#include <QWidget>

#ifdef DESIGNER_PLUGIN
#include <QtUiPlugin/QDesignerExportWidget>
#else
#define QDESIGNER_WIDGET_EXPORT
#endif

namespace widgets {

class QDESIGNER_WIDGET_EXPORT ResizerWidget : public QWidget
{
	Q_PROPERTY(QSize targetSize READ targetSize WRITE setTargetSize)
	Q_PROPERTY(QSize originalSize READ originalSize WRITE setOriginalSize)
	Q_PROPERTY(QPoint offset READ offset WRITE setOffset NOTIFY offsetChanged)
	Q_OBJECT
public:
	explicit ResizerWidget(QWidget *parent=nullptr);

	void setImage(const QImage &image);
	void setTargetSize(const QSize &size);
	void setOriginalSize(const QSize &size);

	QSize targetSize() const { return m_targetSize; }
	QSize originalSize() const { return m_originalSize; }

	QPoint offset() const { return m_offset; }
	void setOffset(const QPoint &offset);

public slots:
	void center();

signals:
	void offsetChanged(const QPoint &);

protected:
	void paintEvent(QPaintEvent*);
	void resizeEvent(QResizeEvent*);
	void mousePressEvent(QMouseEvent*);
	void mouseMoveEvent(QMouseEvent*);

private:
	void updateScales();

	QSize m_originalSize;
	QSize m_targetSize;
	QPoint m_offset;

	QRectF m_targetScaled;
	QSize m_originalScaled;
	qreal m_scale;

	QPoint m_grabPoint;
	QPoint m_grabOffset;

	QColor m_bgColor;
	QPixmap m_originalPixmap;
};

}

#endif

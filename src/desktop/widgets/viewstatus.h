/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2008-2019 Calle Laakkonen

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
#ifndef VIEWSTATUS_H
#define VIEWSTATUS_H

#include <QWidget>

class QComboBox;
class QSlider;
class QToolButton;

namespace widgets {

class ViewStatus : public QWidget
{
	Q_OBJECT
public:
	ViewStatus(QWidget *parent=nullptr);

	void setActions(QAction *flip, QAction *mirror, QAction *rotationReset, QAction *zoomReset);

public slots:
	void setTransformation(qreal zoom, qreal angle);
	void setMinimumZoom(int zoom);

signals:
	void zoomChanged(qreal newZoom);
	void angleChanged(qreal newAngle);

protected:
	void changeEvent(QEvent *event) override;

private slots:
	void zoomBoxChanged(const QString &text);
	void zoomSliderChanged(int value);
	void angleBoxChanged(const QString &text);

private:
	void updatePalette();

	QSlider *m_zoomSlider;
	QComboBox *m_zoomBox;
	QComboBox *m_angleBox;
	bool m_updating;
	QToolButton *m_viewFlip, *m_viewMirror, *m_rotationReset, *m_zoomReset;
};

}

#endif

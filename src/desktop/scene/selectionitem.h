/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2013-2015 Calle Laakkonen

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
#ifndef SELECTIONITEM_H
#define SELECTIONITEM_H

#include "canvas/selection.h"

#include <QGraphicsObject>

namespace drawingboard {

class SelectionItem : public QGraphicsObject
{
public:
	enum { Type= UserType + 11 };

	SelectionItem(canvas::Selection *selection, QGraphicsItem *parent=0);

	QRectF boundingRect() const;
	int type() const { return Type; }

	void marchingAnts();

private slots:
	void onShapeChanged();
	void onAdjustmentModeChanged();

protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *options, QWidget *);

private:
	QPolygonF m_shape;
	canvas::Selection *m_selection;
	qreal m_marchingants;
};

}

#endif // SELECTIONITEM_H

/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2008-2016 Calle Laakkonen

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

#include <QApplication>
#include <QPalette>
#include <QPainter>

#include "core/layerstack.h"
#include "scene/annotationitem.h"
#include "../libshared/net/annotation.h"

namespace drawingboard {

AnnotationItem::AnnotationItem(int id, QGraphicsItem *parent)
	: QGraphicsItem(parent), m_id(id), m_valign(0), m_highlight(false), m_showborder(false)
{
}

void AnnotationItem::setGeometry(const QRect &rect)
{
	prepareGeometryChange();
	m_rect = rect;
	m_doc.setTextWidth(rect.width());
}

void AnnotationItem::setColor(const QColor &color)
{
	m_color = color;
	update();
}

void AnnotationItem::setText(const QString &text)
{
	m_doc.setHtml(text);
	update();
}

void AnnotationItem::setValign(int valign)
{
	m_valign = valign;
	update();
}

/**
 * Highlight is used to indicate the selected annotation.
 * @param hl
 */
void AnnotationItem::setHighlight(bool hl)
{
	if(m_highlight != hl) {
		m_highlight = hl;
		update();
	}
}

/**
 * Border is normally drawn when the annotation is highlighted or has no text.
 * The border is forced on when the annotation edit tool is selected.
 */
void AnnotationItem::setShowBorder(bool show)
{
	if(m_showborder != show) {
		m_showborder = show;
		update();
	}
}

QRectF AnnotationItem::boundingRect() const
{
	return m_rect.adjusted(-HANDLE/2, -HANDLE/2, HANDLE/2, HANDLE/2);
}

void AnnotationItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *options, QWidget *widget)
{
	Q_UNUSED(options);
	Q_UNUSED(widget);

	painter->save();
	painter->setClipRect(boundingRect().adjusted(-1, -1, 1, 1));

	painter->fillRect(m_rect, m_color);

	paintHiddenBorder(painter);

	if(m_highlight) {
		// Draw resizing handles
		QColor border = QApplication::palette().color(QPalette::Highlight);
		border.setAlpha(255);

		QPen pen(border);
		pen.setCosmetic(true);
		pen.setWidth(HANDLE);
		painter->setPen(pen);
		painter->drawPoint(m_rect.topLeft());
		painter->drawPoint(m_rect.topLeft() + QPointF(m_rect.width()/2, 0));
		painter->drawPoint(m_rect.topRight());

		painter->drawPoint(m_rect.topLeft() + QPointF(0, m_rect.height()/2));
		painter->drawPoint(m_rect.topRight() + QPointF(0, m_rect.height()/2));

		painter->drawPoint(m_rect.bottomLeft());
		painter->drawPoint(m_rect.bottomLeft() + QPointF(m_rect.width()/2, 0));
		painter->drawPoint(m_rect.bottomRight());
	}

	m_doc.setTextWidth(m_rect.width());

	QPointF offset;
	if(m_valign == protocol::AnnotationEdit::FLAG_VALIGN_CENTER) {
		offset.setY((m_rect.height() - m_doc.size().height()) / 2);

	} else if(m_valign == protocol::AnnotationEdit::FLAG_VALIGN_BOTTOM) {
		offset.setY(m_rect.height() - m_doc.size().height());
	}

	painter->translate(m_rect.topLeft() + offset);
	m_doc.drawContents(painter, QRectF(-offset, m_rect.size()));

	painter->restore();
}

void AnnotationItem::paintHiddenBorder(QPainter *painter)
{
	// Hidden border is usually hidden
	if(!m_showborder && !m_doc.isEmpty())
		return;

	const qreal devicePixelRatio = qApp->devicePixelRatio();
	QColor highlightColor = QApplication::palette().color(QPalette::Highlight);
	highlightColor.setAlpha(255);

	QPen bpen(m_highlight && m_showborder ? Qt::DashLine : Qt::DotLine);
	bpen.setWidth(devicePixelRatio);
	bpen.setCosmetic(true);
	bpen.setColor(highlightColor);
	painter->setPen(bpen);
	painter->drawRect(m_rect);
}

}


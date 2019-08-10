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

#include "viewstatus.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QToolButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QHBoxLayout>
#include <QEvent>

namespace widgets {

ViewStatus::ViewStatus(QWidget *parent)
	: QWidget(parent), m_updating(false)
{
	setMinimumHeight(22);
	QHBoxLayout *layout = new QHBoxLayout(this);

	layout->setMargin(1);
	layout->setSpacing(0);

	// View flipping
	m_viewFlip = new QToolButton(this);
	m_viewFlip->setAutoRaise(true);

	m_viewMirror = new QToolButton(this);
	m_viewMirror->setAutoRaise(true);

	layout->addWidget(m_viewFlip);
	layout->addWidget(m_viewMirror);

	// Canvas rotation reset button
	m_rotationReset = new QToolButton(this);
	m_rotationReset->setAutoRaise(true);

	layout->addSpacing(10);
	layout->addWidget(m_rotationReset);

	// Canvas rotation box
	m_angleBox = new QComboBox(this);
	m_angleBox->setFixedWidth(m_angleBox->fontMetrics().width("9999-O--"));
	m_angleBox->setFrame(false);
	m_angleBox->setEditable(true);
	m_angleBox->setToolTip(tr("Canvas Rotation"));

	layout->addWidget(m_angleBox);

	m_angleBox->addItem(QStringLiteral("-90°"));
	m_angleBox->addItem(QStringLiteral("-45°"));
	m_angleBox->addItem(QStringLiteral("0°"));
	m_angleBox->addItem(QStringLiteral("45°"));
	m_angleBox->addItem(QStringLiteral("90°"));
	m_angleBox->setEditText(QStringLiteral("0°"));

	m_angleBox->lineEdit()->setValidator(
		new QRegularExpressionValidator(
			QRegularExpression("-?[0-9]{0,3}°?"),
			this
		)
	);
	connect(m_angleBox, &QComboBox::editTextChanged, this, &ViewStatus::angleBoxChanged);

	// Zoom reset button
	m_zoomReset = new QToolButton(this);
	m_zoomReset->setAutoRaise(true);

	layout->addSpacing(10);
	layout->addWidget(m_zoomReset);

	// Zoom slider
	m_zoomSlider = new QSlider(Qt::Horizontal, this);
	m_zoomSlider->setMinimumWidth(200);
	m_zoomSlider->setMaximumWidth(200);
	m_zoomSlider->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum));
	m_zoomSlider->setMinimum(50);
	m_zoomSlider->setMaximum(1000);
	m_zoomSlider->setPageStep(50);
	m_zoomSlider->setValue(100);
	connect(m_zoomSlider, &QSlider::valueChanged, this, &ViewStatus::zoomSliderChanged);

	// Zoom box
	m_zoomBox = new QComboBox(this);
	m_zoomBox->setFixedWidth(m_zoomBox->fontMetrics().width("9999.9%--"));
	m_zoomBox->setFrame(false);
	m_zoomBox->setEditable(true);

	layout->addWidget(m_zoomSlider);
	layout->addWidget(m_zoomBox);

	m_zoomBox->addItem(QStringLiteral("1600%"));
	m_zoomBox->addItem(QStringLiteral("800%"));
	m_zoomBox->addItem(QStringLiteral("400%"));
	m_zoomBox->addItem(QStringLiteral("200%"));
	m_zoomBox->addItem(QStringLiteral("100%"));
	m_zoomBox->addItem(QStringLiteral("50%"));
	m_zoomBox->setEditText(QStringLiteral("100%"));

	m_zoomBox->lineEdit()->setValidator(
		new QRegularExpressionValidator(
			QRegularExpression("[0-9]{0,4}%?"),
			this
		)
	);
	connect(m_zoomBox, &QComboBox::editTextChanged, this, &ViewStatus::zoomBoxChanged);

	updatePalette();
}

void ViewStatus::updatePalette()
{
#ifndef Q_OS_MAC
	auto boxPalette = palette();
	boxPalette.setColor(QPalette::Base, boxPalette.color(QPalette::Window));
	m_angleBox->setPalette(boxPalette);
	m_zoomBox->setPalette(boxPalette);
#endif
}

void ViewStatus::setActions(QAction *flip, QAction *mirror, QAction *rotationReset, QAction *zoomReset)
{
	m_viewFlip->setDefaultAction(flip);
	m_viewMirror->setDefaultAction(mirror);
	m_rotationReset->setDefaultAction(rotationReset);
	m_zoomReset->setDefaultAction(zoomReset);
}

void ViewStatus::setTransformation(qreal zoom, qreal angle)
{
	m_updating = true;
	const int intZoom = qRound(zoom);
	const int zoomCursorPos = m_zoomBox->lineEdit()->cursorPosition();
	m_zoomBox->setEditText(QString::number(intZoom) + QChar('%'));
	m_zoomBox->lineEdit()->setCursorPosition(zoomCursorPos);

	const int intAngle = qRound(angle);
	const int angleCursorPos = m_angleBox->lineEdit()->cursorPosition();
	m_angleBox->setEditText(QString::number(intAngle) + QChar(0x00b0));
	m_angleBox->lineEdit()->setCursorPosition(angleCursorPos);

	if(intZoom != m_zoomSlider->value()) {
		m_zoomSlider->setValue(intZoom);
	}
	m_updating = false;
}

void ViewStatus::setMinimumZoom(int zoom)
{
	m_zoomSlider->setMinimum(qMax(1, zoom));
}

void ViewStatus::zoomBoxChanged(const QString &text)
{
	if(m_updating)
		return;

	const int suffix = text.indexOf('%');
	const QStringRef num = suffix>0 ? text.leftRef(suffix) : &text;

	bool ok;
	const int number = num.toInt(&ok);
	if(ok && number>= 1 && number < 10000)
		emit zoomChanged(number);
}

void ViewStatus::zoomSliderChanged(int value)
{
	if(m_updating)
		return;
	emit zoomChanged(value);
}

void ViewStatus::angleBoxChanged(const QString &text)
{
	if(m_updating)
		return;

	const int suffix = text.indexOf(0x00b0);
	const QStringRef num = suffix>0 ? text.leftRef(suffix) : &text;

	bool ok;
	const int number = num.toInt(&ok);
	if(ok)
		emit angleChanged(number);
}

void ViewStatus::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if(event->type() == QEvent::PaletteChange)
		updatePalette();
}

}

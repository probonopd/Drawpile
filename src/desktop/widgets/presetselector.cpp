/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2019 Calle Laakkonen

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

#include "presetselector.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QStringListModel>
#include <QRegularExpressionValidator>
#include <QFile>
#include <QDir>

namespace widgets {

PresetSelector::PresetSelector(QWidget *parent, Qt::WindowFlags f)
	: QWidget(parent,f), m_writeOnly(false)
{

	auto *layout = new QHBoxLayout;
	setLayout(layout);

	m_presets = new QStringListModel(this);

	m_presetBox = new QComboBox(this);
	m_presetBox->setEditable(true);
	m_presetBox->lineEdit()->setPlaceholderText(tr("Presets"));
	m_presetBox->setModel(m_presets);
	m_presetBox->setValidator(new QRegularExpressionValidator(
		QRegularExpression("^[^/\\\\]+$"),
		this
		));
	layout->addWidget(m_presetBox, 1);

	m_loadButton = new QPushButton(tr("Load"), this);
	layout->addWidget(m_loadButton);

	m_saveButton = new QPushButton(tr("Save"), this);
	layout->addWidget(m_saveButton);

	m_deleteButton = new QPushButton(tr("Delete"), this);
	layout->addWidget(m_deleteButton);

	setPresetFolder("presets");
	onTextChanged(QString());

	connect(m_loadButton, &QPushButton::clicked, this, &PresetSelector::onLoadClicked);
	connect(m_saveButton, &QPushButton::clicked, this, &PresetSelector::onSaveClicked);
	connect(m_deleteButton, &QPushButton::clicked, this, &PresetSelector::deleteSelected);
	connect(m_presetBox, &QComboBox::editTextChanged, this, &PresetSelector::onTextChanged);
}

QDir PresetSelector::dir() const
{
#ifdef DESIGNER_PLUGIN
		const auto location = QStandardPaths::TempLocation;
#else
		const auto location = QStandardPaths::AppLocalDataLocation;
#endif

		return QDir(QStandardPaths::writableLocation(location) + QChar('/') + m_folder);
}

void PresetSelector::setPresetFolder(const QString &folder)
{
	if(m_folder != folder) {
		m_folder = folder;

		auto presets = dir().entryList(
				QStringList() << "*.preset",
				QDir::Files | QDir::Readable,
				QDir::Name
			);

		for(QString &str : presets) {
			const int ext = str.lastIndexOf('.');
			str.truncate(ext);
		}

		m_presets->setStringList(presets);
	}
}

void PresetSelector::setWriteOnly(bool writeOnly)
{
	m_writeOnly = writeOnly;
}

void PresetSelector::onTextChanged(const QString &text)
{
	const bool enabled = !text.trimmed().isEmpty();
	m_saveButton->setEnabled(enabled);
	m_deleteButton->setEnabled(enabled);
	m_loadButton->setEnabled(enabled && dir().exists(text.trimmed() + ".preset"));
}

void PresetSelector::onLoadClicked()
{
	const auto filename = m_presetBox->currentText().trimmed() + QStringLiteral(".preset");
	const auto d = dir();

	if(d.exists(filename))
		emit loadRequested(d.filePath(filename));
	else
		qWarning("%s: does not exist", qPrintable(filename));
}

void PresetSelector::onSaveClicked()
{
	const QString filename = m_presetBox->currentText().trimmed();
	if(filename.isEmpty())
		return;

	const QDir d = dir();
	d.mkpath(".");

	const QString path = d.filePath(filename + ".preset");

#ifdef DESIGNER_PLUGIN
	QFile testFile(path);
	testFile.open(QFile::WriteOnly);
	testFile.write("Testing: " + filename.toUtf8());
#endif

	emit saveRequested(path);

	auto presets = m_presets->stringList();
	if(!presets.contains(filename)) {
		presets << filename;
		m_presets->setStringList(presets);
		m_presetBox->setCurrentText(filename);
	}
}

void PresetSelector::deleteSelected()
{
	const QString filename = m_presetBox->currentText().trimmed();
	if(filename.isEmpty())
		return;

	QFile f(dir().filePath(filename + ".preset"));
	if(f.remove()) {
		auto presets = m_presets->stringList();
		presets.removeAll(filename);
		m_presets->setStringList(presets);
	}
}

}


/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2014 Calle Laakkonen

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
#ifndef IMAGESERIESEXPORTER_H
#define IMAGESERIESEXPORTER_H

#include "videoexporter.h"

class ImageSeriesExporter : public VideoExporter
{
	Q_OBJECT
public:
	ImageSeriesExporter(QObject *parent=0);

	void setOutputPath(const QString &path) { _path = path; }
	void setFilePattern(const QString &pattern) { _filepattern = pattern; }
	void setFormat(const QString &format) { _format = format.toLatin1(); }

protected:
	void initExporter();
	void writeFrame(const QImage &image, int repeat);
	void shutdownExporter();
	bool variableSizeSupported() { return true; }

private:
	QString _path;
	QString _filepattern;
	QByteArray _format;
};

#endif // IMAGESERIESEXPORTER_H

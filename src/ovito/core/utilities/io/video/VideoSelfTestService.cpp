////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//

#include <ovito/core/Core.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/utilities/Exception.h>
#include "VideoEncoder.h"
#include "VideoSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(VideoSelfTestService);

namespace {

/// A gradient whose colors shift with the frame number, so that consecutive GIF frames differ.
QImage makeFrame(int frame)
{
    QImage image(64, 48, QImage::Format_RGB32);
    for(int y = 0; y < image.height(); y++)
        for(int x = 0; x < image.width(); x++)
            image.setPixel(x, y, qRgb((x * 4 + frame * 40) & 255, y * 5, 255 - frame * 60));
    return image;
}

bool checkFile(const QString& path, const QByteArray& signature)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << "Video self-test: FAILED, file was not written:" << path;
        return false;
    }
    const QByteArray data = file.readAll();
    if(data.size() < 100 || !data.startsWith(signature)) {
        qCritical().noquote() << "Video self-test: FAILED, bad file content in" << path << "(" << data.size() << "bytes)";
        return false;
    }
    qInfo().noquote() << "Video self-test: ok" << QFileInfo(path).fileName() << "(" << data.size() << "bytes)";
    return true;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void VideoSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("video-selftest")},
        tr("Write a test PNG image and animated GIF and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void VideoSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("video-selftest")))
        return;

    bool ok = true;
    try {
        QTemporaryDir dir;
        if(!dir.isValid())
            throw Exception(tr("Cannot create a temporary directory."));

        const QString png = dir.filePath(QStringLiteral("frame.png"));
        if(!makeFrame(0).save(png, "PNG"))
            throw Exception(tr("Cannot write a PNG image."));
        ok &= checkFile(png, QByteArray("\x89PNG", 4));

        QList<VideoEncoder::Format> formats = VideoEncoder::supportedFormats();
        qInfo().noquote() << "Video self-test: formats offered:" << [&] { QStringList n; for(const auto& f : formats) n << QString::fromLatin1(f.name); return n.join(QLatin1Char(',')); }();
        for(const QByteArray name : {QByteArray("gif"), QByteArray("avi")}) {
            auto it = std::find_if(formats.begin(), formats.end(), [&](const VideoEncoder::Format& f) { return f.name == name; });
            if(it == formats.end()) {
                qCritical().noquote() << "Video self-test: FAILED, the" << name << "format is not available.";
                ok = false;
                continue;
            }
            const QString path = dir.filePath(QStringLiteral("anim.") + QString::fromLatin1(name));
            VideoEncoder encoder;
            encoder.openFile(path, 64, 48, 10.0f, &*it);
            for(int frame = 0; frame < 4; frame++)
                encoder.writeFrame(makeFrame(frame));
            encoder.closeFile();
            ok &= checkFile(path, name == "gif" ? QByteArray("GIF89a") : QByteArray("RIFF"));
        }
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Video self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Video self-test failed."));
    QCoreApplication::exit(0);
}

}   // End of namespace

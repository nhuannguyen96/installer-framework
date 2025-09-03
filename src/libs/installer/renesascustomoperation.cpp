/**************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
**************************************************************************/

#include "renesascustomoperation.h"
#include "packagemanagercore.h"
#include "utils.h"
#include "globals.h"
#include "progresscoordinator.h"

#include <QFile>
#include <QDir>
#include <QProcess>
#include <QDebug>

using namespace QInstaller;

/*!
    \inmodule QtInstallerFramework
    \class QInstaller::RenesasCustomOperation
    \internal
*/

RenesasCustomOperation::RenesasCustomOperation(PackageManagerCore *core)
    : UpdateOperation(core)
{
    setName(QLatin1String("RenesasCustom"));
}

void RenesasCustomOperation::backup()
{
}

bool RenesasCustomOperation::performOperation()
{
    // Arguments:
    // 1. key where the output will be saved
    // 2. executable path
    // 3. argument for the executable
    // 4. more arguments possible ...

    if (!checkArgumentCount(2, INT_MAX, tr("<to be saved installer key name> "
                                           "<executable> [argument1] [argument2] [...]")))
        return false;

    PackageManagerCore *const core = packageManager();
    if (!core) {
        setError(UserDefinedError);
        setErrorString(tr("Needed installer object in %1 operation is empty.").arg(name()));
        return false;
    }

    const QString keyName = arguments().at(0);
    const QString program = arguments().at(1);
    const QStringList args = arguments().mid(2);

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    
    const QString message = tr("Download with command: %1 %2")
                                  .arg(program, args.join(QLatin1Char(' ')));
    ProgressCoordinator::instance()->emitDetailTextChanged(message);
    ProgressCoordinator::instance()->emitDetailTextChanged(tr("some text will not appear")); // W/A

    QString finalOutput;

    // When data arrives, push that chunk into an installer key.
    // The JS controller will listen to installer.valueChanged and append to the UI.
    QObject::connect(&process, &QProcess::readyRead, [core, keyName, &process, &finalOutput]() {
        QByteArray chunk = process.readAll();
        if (!chunk.isEmpty()) {
            // qDebug() << "Chunk is:" << chunk;
            finalOutput += QString::fromLocal8Bit(chunk); // accumulate output
            // send *only this chunk* so controller can append it
            // ProgressCoordinator::instance()->emitDetailTextChanged(tr("__CLEAR__"));
            QString text = QString::fromLocal8Bit(chunk);
            // split chunk into lines, keep only non-empty
            QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            lines = text.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                // only update the last line from this chunk
                QString lastLine = lines.last();
                ProgressCoordinator::instance()->replaceDetailText(lastLine);
                core->setValue(keyName, lastLine);
            }
        }
    });
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [core, keyName, &finalOutput]() {
            // // Split finalOutput into lines
            // QStringList lines = finalOutput.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
            // QString lastLine;
            // if (!lines.isEmpty()) {
            //     lastLine = lines.last().trimmed(); // remove trailing whitespace
            // } else {
            //     lastLine = tr("No output received.");
            // }
            // qDebug() << "lastLine detected: " << lastLine;
            // Filter out empty or whitespace-only lines
            QStringList rawLines = finalOutput.split(QLatin1Char('\r'), Qt::KeepEmptyParts);
            QStringList lines;
            for (const QString &line : rawLines) {
                QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) {
                    lines.append(trimmed);
                }
            }
            QString lastLine;
            if (!lines.isEmpty()) {
                lastLine = lines.last(); // now guaranteed to be non-empty
            } else {
                lastLine = tr("No output received.");
            }
            qDebug() << "lastLine detected: " << lastLine;
            // Emit the last line only
            ProgressCoordinator::instance()->emitDetailTextChanged(tr("Final status:"));
            ProgressCoordinator::instance()->emitDetailTextChanged(lastLine);
            ProgressCoordinator::instance()->emitDetailTextChanged(tr("Download completed!!!!"));
            // Now the process is done — safe to emit final output
    });
    // ProgressCoordinator::instance()->emitDetailTextChanged(tr("Downnoad compnete"));
    // Announce start (also sent to UI via same key)
    core->setValue(keyName, tr("Starting: %1 %2")
                                 .arg(program, args.join(QLatin1Char(' '))));

    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        setError(UserDefinedError);
        setErrorString(QString::fromLatin1("Failed to start %1").arg(program));
        return false;
    }

    // pump events while process runs so readyRead signals fire and JS/UI updates happen
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        QCoreApplication::processEvents();
    }

    // final flush
    QByteArray remaining = process.readAll();
    if (!remaining.isEmpty()) {
        core->setValue(keyName, QString::fromLocal8Bit(remaining));
    }

    // signal finish by setting a separate key (optional, makes it easy for JS to detect end)
    core->setValue(keyName + QLatin1String(":exit"), QString::number(process.exitCode()));

    return (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);

    // QObject::connect(&process, &QProcess::readyReadStandardOutput, [&process]() {
    //     QByteArray data = process.readAllStandardOutput();
    //     const QList<QByteArray> lines = data.split('\n');
    //     for (const QByteArray &line : lines) {
    //         if (!line.trimmed().isEmpty())
    //             qCDebug(QInstaller::lcInstallerInstallLog).noquote() << QString::fromLocal8Bit(line);
    //     }
    // });

    // qCDebug(QInstaller::lcInstallerInstallLog) << "Starting:" << program << args;
    // process.start(program, args);

    // if (!process.waitForStarted(5000)) {
    //     setError(UserDefinedError);
    //     setErrorString(QString::fromLatin1("Failed to start %1").arg(program));
    //     return false;
    // }

    // // Keep pumping until finished
    // while (!process.waitForFinished(100)) {
    //     QCoreApplication::processEvents();
    // }


    // // Final flush
    // QByteArray remaining = process.readAllStandardOutput();
    // if (!remaining.isEmpty())
    //     qCDebug(QInstaller::lcInstallerInstallLog).noquote() << QString::fromLocal8Bit(remaining);

    // if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    //     setError(UserDefinedError);
    //     setErrorString(QString::fromLatin1("Failed to start %1").arg(program));
    //     return false;
    // }

    // // // Optional: save output key
    // packageManager()->setValue(keyName, QString::fromLocal8Bit(remaining));
    // return true;
}


bool RenesasCustomOperation::undoOperation()
{
    return true;
}

bool RenesasCustomOperation::testOperation()
{
    return true;
}

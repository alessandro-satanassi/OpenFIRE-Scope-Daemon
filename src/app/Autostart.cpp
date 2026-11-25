/*  OpenFIRE Scope Daemon: video from the PC to the OpenFIRE scopes.
    Copyright (C) 2026  Alessandro Satanassi, Team OpenFIRE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "app/Autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

#include "app/Log.h"

namespace {
#if defined(Q_OS_WIN)
const char *kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char *kRunName = "OpenFIRE Scope Daemon";
#elif defined(Q_OS_MACOS)
const char *kAgentLabel = "org.TeamOpenFIRE.ScopeDaemon";

QString agentFile()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + QLatin1String(kAgentLabel) + QStringLiteral(".plist");
}

QString xmlEscaped(QString text)
{
    return text.toHtmlEscaped();
}
#else
// EN: Linux and the other XDG systems. / IT: Linux e gli altri sistemi XDG.
const char *kStoredKey = "X-OpenFIRE-Program=";

QString desktopFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart/openfire-scope-daemon.desktop");
}

// EN: Exec argument of the Desktop Entry specification: in double quotes with " \` $ \\ escaped, then the
//     backslashes doubled again as in every string of the file; % becomes %%.
// IT: argomento di Exec della specifica Desktop Entry: tra doppi apici con " \` $ \\ protetti, poi le barre
//     rovesciate raddoppiate di nuovo come in ogni stringa del file; % diventa %%.
QString execQuoted(const QString &path)
{
    QString arg;
    for (const QChar c : path) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
            arg += QLatin1Char('\\');
        arg += c;
    }
    arg.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    arg.replace(QLatin1Char('%'), QStringLiteral("%%"));
    return QLatin1Char('"') + arg + QLatin1Char('"');
}
#endif

#if !defined(Q_OS_WIN)
bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
#endif
} // namespace

QString Autostart::program()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // EN: inside an AppImage the program is in a temporary mount: the AppImage file is started instead.
    // IT: dentro un'AppImage il programma sta in un montaggio temporaneo: si avvia invece il file AppImage.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) return appImage;
#endif
    return QCoreApplication::applicationFilePath();
}

QString Autostart::storedProgram()
{
#if defined(Q_OS_WIN)
    QSettings run(QLatin1String(kRunKey), QSettings::NativeFormat);
    QString value = run.value(QLatin1String(kRunName)).toString().trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2);
    return value;
#elif defined(Q_OS_MACOS)
    QFile file(agentFile());
    if (!file.open(QIODevice::ReadOnly)) return QString();
    // EN: the file is written only by this program: the first <string> after ProgramArguments.
    // IT: il file lo scrive solo questo programma: la prima <string> dopo ProgramArguments.
    const QString text = QString::fromUtf8(file.readAll());
    const int args = text.indexOf(QStringLiteral("<key>ProgramArguments</key>"));
    const int begin = args < 0 ? -1 : text.indexOf(QStringLiteral("<string>"), args);
    const int end = begin < 0 ? -1 : text.indexOf(QStringLiteral("</string>"), begin);
    if (end < 0) return QString();
    QString value = text.mid(begin + 8, end - begin - 8);
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<")).replace(QStringLiteral("&gt;"), QStringLiteral(">"))
         .replace(QStringLiteral("&quot;"), QStringLiteral("\"")).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return value;
#else
    QFile file(desktopFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1String(kStoredKey))) return line.mid(int(qstrlen(kStoredKey)));
    }
    // EN: entry without the key (written by hand): it counts as present. / IT: voce senza la chiave (scritta a mano): conta come presente.
    return file.fileName();
#endif
}

bool Autostart::isEnabled()
{
    return !storedProgram().isEmpty();
}

bool Autostart::setEnabled(bool on, QString *error)
{
    const QString path = program();
#if defined(Q_OS_WIN)
    QSettings run(QLatin1String(kRunKey), QSettings::NativeFormat);
    if (on) run.setValue(QLatin1String(kRunName), QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"'));
    else run.remove(QLatin1String(kRunName));
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error) *error = tr("the registry key cannot be written");
        return false;
    }
#elif defined(Q_OS_MACOS)
    if (on) {
        // EN: read at the next login; ProcessType Interactive: the same priority as a program started by hand.
        // IT: letto al prossimo accesso; ProcessType Interactive: la stessa priorita' di un programma avviato a mano.
        const QString plist = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "\t<key>Label</key>\n"
            "\t<string>%1</string>\n"
            "\t<key>ProgramArguments</key>\n"
            "\t<array>\n"
            "\t\t<string>%2</string>\n"
            "\t</array>\n"
            "\t<key>RunAtLoad</key>\n"
            "\t<true/>\n"
            "\t<key>ProcessType</key>\n"
            "\t<string>Interactive</string>\n"
            "</dict>\n"
            "</plist>\n").arg(QLatin1String(kAgentLabel), xmlEscaped(path));
        if (!writeFile(agentFile(), plist.toUtf8(), error)) return false;
    } else if (QFile::exists(agentFile()) && !QFile::remove(agentFile())) {
        if (error) *error = tr("%1 cannot be removed").arg(agentFile());
        return false;
    }
#else
    if (on) {
        const QString entry = QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=OpenFIRE Scope Daemon\n"
            "Comment=Video from the PC to the OpenFIRE scopes\n"
            "Exec=%1\n"
            "Icon=org.TeamOpenFIRE.ScopeDaemon\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n"
            "%2%3\n").arg(execQuoted(path), QLatin1String(kStoredKey), path);
        if (!writeFile(desktopFile(), entry.toUtf8(), error)) return false;
    } else if (QFile::exists(desktopFile()) && !QFile::remove(desktopFile())) {
        if (error) *error = tr("%1 cannot be removed").arg(desktopFile());
        return false;
    }
#endif
    Log::info(on ? tr("Start at login: on (%1)").arg(QDir::toNativeSeparators(path)) : tr("Start at login: off"));
    return true;
}

void Autostart::refresh()
{
    const QString stored = storedProgram();
    if (stored.isEmpty() || QFileInfo::exists(stored)) return;
    QString error;
    if (!setEnabled(true, &error)) Log::warn(tr("Start at login: the entry cannot be updated: %1").arg(error));
}

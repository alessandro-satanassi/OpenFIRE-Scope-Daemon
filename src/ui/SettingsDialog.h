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

#ifndef OFD_SETTINGSDIALOG_H
#define OFD_SETTINGSDIALOG_H

#include <QDialog>
#include <QVector>
#include <functional>

#include "app/Settings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

// EN: Settings dialog. Values are written only with OK, then Settings::changed applies them at once.
// IT: Finestra delle impostazioni. I valori si scrivono solo con OK, poi Settings::changed li applica subito.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // EN: applyPorts: tries the ports before they are saved (false + message: busy, nothing is saved).
    // IT: applyPorts: prova le porte prima di salvarle (false + messaggio: occupata, non si salva nulla).
    using PortApplier = std::function<bool(const QVector<quint16> &video, quint16 discovery, QString *message)>;
    explicit SettingsDialog(Settings *settings, PortApplier applyPorts = PortApplier(), QWidget *parent = nullptr);

    void accept() override;

private:
    QWidget *buildNetworkTab();
    QWidget *buildCaptureTab();
    QWidget *buildAdvancedTab();
    void addLightgunRow(const LightgunMapEntry &entry);
    bool readLightgunTable(QVector<LightgunMapEntry> *map, QString *error) const;
    static bool parseVidPid(const QString &text, quint16 *vid, quint16 *pid);

    Settings *m_settings = nullptr;
    PortApplier m_applyPorts;
    QSpinBox *m_videoPort[Settings::Players] = {};
    QCheckBox *m_captureEnabled = nullptr;
    QComboBox *m_monitor = nullptr;
    QComboBox *m_zoom = nullptr;
    QTableWidget *m_lightguns = nullptr;
    QSpinBox *m_discoveryPort = nullptr;
    QLineEdit *m_dongleVidPid = nullptr;
    QCheckBox *m_logToFile = nullptr;
    QCheckBox *m_autostart = nullptr;
    // EN: shown values; a value is saved only if changed, so a command line value never reaches the INI.
    // IT: valori mostrati; un valore si salva solo se cambiato, cosi' un valore della riga di comando non finisce nell'INI.
    int m_shownDiscoveryPort = 0;
    bool m_shownLogToFile = false;
    bool m_shownCaptureEnabled = false;
    bool m_shownAutostart = false;
};

#endif // OFD_SETTINGSDIALOG_H

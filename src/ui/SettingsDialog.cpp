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

#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "OpenFIRE_Scope_Protocol.h"

namespace {
QSpinBox *newPortBox(quint16 value)
{
    auto *box = new QSpinBox();
    box->setRange(1024, 65535);
    box->setValue(value);
    return box;
}

QString hex4(quint16 v)
{
    return QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0')).toUpper();
}
} // namespace

SettingsDialog::SettingsDialog(Settings *settings, PortApplier applyPorts, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_applyPorts(std::move(applyPorts))
{
    setWindowTitle(tr("Settings"));

    auto *tabs = new QTabWidget();
    tabs->addTab(buildNetworkTab(), tr("Network"));
    tabs->addTab(buildCaptureTab(), tr("Screen capture"));
    tabs->addTab(buildAdvancedTab(), tr("Advanced"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    if (m_settings->hasOverrides()) {
        auto *note = new QLabel(tr("Some values come from the command line and are used for this run only."));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addWidget(buttons);
}

QWidget *SettingsDialog::buildNetworkTab()
{
    auto *page = new QWidget();
    auto *form = new QFormLayout(page);
    auto *intro = new QLabel(tr("Preferred UDP port of each player. If it is taken, the daemon uses another one and shows it in the window."));
    intro->setWordWrap(true);
    form->addRow(intro);
    for (int i = 0; i < Settings::Players; ++i) {
        m_videoPort[i] = newPortBox(m_settings->preferredVideoPort(i + 1));
        form->addRow(tr("Player %1").arg(i + 1), m_videoPort[i]);
    }
    return page;
}

QWidget *SettingsDialog::buildCaptureTab()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    if (!Settings::captureSupported()) {
        auto *label = new QLabel(tr("Screen capture is not available on this system. Only the video from the games (UDP) is used."));
        label->setWordWrap(true);
        layout->addWidget(label);
        layout->addStretch();
        return page;
    }

    auto *form = new QFormLayout();
    m_captureEnabled = new QCheckBox(tr("Capture the screen when a player has no video from the game"));
    m_shownCaptureEnabled = m_settings->captureEnabled();
    m_captureEnabled->setChecked(m_shownCaptureEnabled);
    form->addRow(m_captureEnabled);

    m_monitor = new QComboBox();
    m_monitor->addItem(tr("Primary monitor"), QString());
    for (QScreen *screen : QGuiApplication::screens())
        m_monitor->addItem(screen->name(), screen->name());
    const QString monitor = m_settings->captureMonitor();
    if (!monitor.isEmpty()) {
        int index = m_monitor->findData(monitor);
        if (index < 0) {
            // EN: a monitor not connected now stays selected. / IT: un monitor ora non collegato resta selezionato.
            m_monitor->addItem(tr("%1 (not connected)").arg(monitor), monitor);
            index = m_monitor->count() - 1;
        }
        m_monitor->setCurrentIndex(index);
    }
    form->addRow(tr("Game monitor"), m_monitor);

    m_zoom = new QComboBox();
    for (int z = 1; z <= 4; ++z) m_zoom->addItem(QStringLiteral("%1×").arg(z), z);
    m_zoom->setCurrentIndex(m_settings->captureZoom() - 1);
    form->addRow(tr("Zoom"), m_zoom);
    layout->addLayout(form);

    auto *group = new QGroupBox(tr("Lightguns (VID:PID → player)"));
    auto *groupLayout = new QVBoxLayout(group);
    m_lightguns = new QTableWidget(0, 3);
    m_lightguns->setHorizontalHeaderLabels({ tr("VID"), tr("PID"), tr("Player") });
    m_lightguns->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_lightguns->verticalHeader()->hide();
    m_lightguns->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (const LightgunMapEntry &e : m_settings->lightgunMap()) addLightgunRow(e);
    groupLayout->addWidget(m_lightguns);

    auto *rowButtons = new QHBoxLayout();
    auto *add = new QPushButton(tr("Add"));
    auto *remove = new QPushButton(tr("Remove"));
    connect(add, &QPushButton::clicked, this, [this]() {
        addLightgunRow({ quint16(OF_LIGHTGUN_VID), 0, 1 });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = m_lightguns->currentRow();
        if (row >= 0) m_lightguns->removeRow(row);
    });
    rowButtons->addWidget(add);
    rowButtons->addWidget(remove);
    rowButtons->addStretch();
    groupLayout->addLayout(rowButtons);
    layout->addWidget(group);

    return page;
}

QWidget *SettingsDialog::buildAdvancedTab()
{
    auto *page = new QWidget();
    auto *form = new QFormLayout(page);

    auto *discoveryRow = new QHBoxLayout();
    m_shownDiscoveryPort = m_settings->discoveryPort();
    m_discoveryPort = newPortBox(quint16(m_shownDiscoveryPort));
    auto *restore = new QPushButton(tr("Restore default"));
    connect(restore, &QPushButton::clicked, this, [this]() { m_discoveryPort->setValue(OF_UDP_DISCOVERY_PORT); });
    discoveryRow->addWidget(m_discoveryPort);
    discoveryRow->addWidget(restore);
    discoveryRow->addStretch();
    form->addRow(tr("Discovery port"), discoveryRow);

    m_dongleVidPid = new QLineEdit(hex4(m_settings->dongleVid()) + QLatin1Char(':') + hex4(m_settings->donglePid()));
    m_dongleVidPid->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9A-Fa-f]{1,4}:[0-9A-Fa-f]{1,4}")), m_dongleVidPid));
    m_dongleVidPid->setPlaceholderText(QStringLiteral("303A:1001"));
    form->addRow(tr("Dongle VID:PID"), m_dongleVidPid);

    m_logToFile = new QCheckBox(tr("Also write the log to a file"));
    m_shownLogToFile = m_settings->logToFile();
    m_logToFile->setChecked(m_shownLogToFile);
    form->addRow(m_logToFile);
    auto *path = new QLabel(QDir::toNativeSeparators(m_settings->logFilePath()));
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setWordWrap(true);
    form->addRow(tr("Log file"), path);

    auto *ini = new QLabel(QDir::toNativeSeparators(m_settings->filePath()));
    ini->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ini->setWordWrap(true);
    form->addRow(tr("Settings file"), ini);

    return page;
}

void SettingsDialog::addLightgunRow(const LightgunMapEntry &entry)
{
    const int row = m_lightguns->rowCount();
    m_lightguns->insertRow(row);
    m_lightguns->setItem(row, 0, new QTableWidgetItem(hex4(entry.vid)));
    m_lightguns->setItem(row, 1, new QTableWidgetItem(hex4(entry.pid)));
    auto *player = new QSpinBox();
    player->setRange(1, Settings::Players);
    player->setValue(entry.player);
    m_lightguns->setCellWidget(row, 2, player);
}

bool SettingsDialog::readLightgunTable(QVector<LightgunMapEntry> *map, QString *error) const
{
    QVector<LightgunMapEntry> out;
    QSet<quint32> seen;
    for (int row = 0; row < m_lightguns->rowCount(); ++row) {
        const QTableWidgetItem *v = m_lightguns->item(row, 0);
        const QTableWidgetItem *p = m_lightguns->item(row, 1);
        quint16 vid = 0, pid = 0;
        if (!v || !p || !parseVidPid(v->text().trimmed() + QLatin1Char(':') + p->text().trimmed(), &vid, &pid)) {
            *error = tr("Lightgun row %1: VID and PID must be hexadecimal numbers (for example F143 and 0001).").arg(row + 1);
            return false;
        }
        const quint32 key = (quint32(vid) << 16) | pid;
        if (seen.contains(key)) {
            *error = tr("Lightgun %1:%2 is in the table twice.").arg(hex4(vid), hex4(pid));
            return false;
        }
        seen.insert(key);
        const auto *player = qobject_cast<const QSpinBox *>(m_lightguns->cellWidget(row, 2));
        out.append({ vid, pid, player ? player->value() : 1 });
    }
    *map = out;
    return true;
}

bool SettingsDialog::parseVidPid(const QString &text, quint16 *vid, quint16 *pid)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 2) return false;
    bool okV = false, okP = false;
    const uint v = parts[0].toUInt(&okV, 16);
    const uint p = parts[1].toUInt(&okP, 16);
    if (!okV || !okP || v == 0 || v > 0xFFFF || p > 0xFFFF) return false;
    *vid = quint16(v);
    *pid = quint16(p);
    return true;
}

void SettingsDialog::accept()
{
    // EN: all the UDP ports must be different. / IT: tutte le porte UDP devono essere diverse.
    QSet<int> ports;
    ports.insert(m_discoveryPort->value());
    for (QSpinBox *box : m_videoPort) {
        if (ports.contains(box->value())) {
            QMessageBox::warning(this, windowTitle(), tr("The discovery port and the ports of the players must all be different."));
            return;
        }
        ports.insert(box->value());
    }

    quint16 vid = 0, pid = 0;
    if (!parseVidPid(m_dongleVidPid->text(), &vid, &pid) || pid == 0) {
        QMessageBox::warning(this, windowTitle(), tr("The dongle VID:PID must be two hexadecimal numbers, for example 303A:1001."));
        return;
    }

    QVector<LightgunMapEntry> map;
    if (m_lightguns) {
        QString error;
        if (!readLightgunTable(&map, &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
    }

    // EN: the ports are tried before anything is saved: if one is busy, nothing changes and the dialog stays open.
    // IT: le porte si provano prima di salvare qualsiasi cosa: se una e' occupata, non cambia nulla e la finestra resta aperta.
    if (m_applyPorts) {
        QVector<quint16> video;
        for (QSpinBox *box : m_videoPort) video << quint16(box->value());
        QString message;
        if (!m_applyPorts(video, quint16(m_discoveryPort->value()), &message)) {
            QMessageBox::warning(this, windowTitle(), message);
            return;
        }
    }

    for (int i = 0; i < Settings::Players; ++i) m_settings->setPreferredVideoPort(i + 1, quint16(m_videoPort[i]->value()));
    if (m_discoveryPort->value() != m_shownDiscoveryPort) m_settings->setDiscoveryPort(quint16(m_discoveryPort->value()));
    m_settings->setDongleVidPid(vid, pid);
    if (m_logToFile->isChecked() != m_shownLogToFile) m_settings->setLogToFile(m_logToFile->isChecked());
    if (m_captureEnabled) {
        if (m_captureEnabled->isChecked() != m_shownCaptureEnabled) m_settings->setCaptureEnabled(m_captureEnabled->isChecked());
        m_settings->setCaptureMonitor(m_monitor->currentData().toString());
        m_settings->setCaptureZoom(m_zoom->currentData().toInt());
        m_settings->setLightgunMap(map);
    }
    m_settings->notifyChanged();
    QDialog::accept();
}

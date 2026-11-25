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

#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/PreviewWidget.h"
#include "ui/SettingsDialog.h"

namespace {

enum PlayerColumn {
    ColScope, ColPort, ColChannel, ColFirmware, ColUdpPort, ColSource,
    ColFpsIn, ColFpsOut, ColKbFrame, ColQ, ColCapacity, ColDrop, ColQfull, PlayerColumns
};

enum DeviceColumn { DevPort, DevSerial, DevState, DevChannel, DevFirmware, DevDetail, DevRetry, DeviceColumns };

const QString kDash = QStringLiteral("—");

QString number(double value, int decimals)
{
    return QString::number(value, 'f', decimals);
}

bool canRetry(PortState state)
{
    return state == PortState::NoAnswer || state == PortState::Discarded || state == PortState::Busy
        || state == PortState::AccessDenied || state == PortState::Lost;
}

bool sameDevices(const QVector<DeviceStatus> &a, const QVector<DeviceStatus> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        const DeviceStatus &x = a[i], &y = b[i];
        if (x.port != y.port || x.serialNumber != y.serialNumber || x.state != y.state || x.channel != y.channel
            || x.firmware != y.firmware || x.detail != y.detail)
            return false;
    }
    return true;
}

void setCell(QTableWidget *table, int row, int col, const QString &text)
{
    QTableWidgetItem *item = table->item(row, col);
    if (!item) {
        item = new QTableWidgetItem();
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (col != 0) item->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, col, item);
    }
    if (item->text() != text) item->setText(text);
}

} // namespace

MainWindow::MainWindow(Daemon *daemon, bool trayAvailable, QWidget *parent)
    : QMainWindow(parent)
    , m_daemon(daemon)
    , m_trayAvailable(trayAvailable)
{
    qRegisterMetaType<Log::Entry>();

    setWindowTitle(QStringLiteral("OpenFIRE Scope Daemon"));

    m_tabs = new QTabWidget();
    m_tabs->addTab(buildStatusTab(), tr("Status"));
    m_tabs->addTab(buildPreviewTab(), tr("Preview"));
    m_tabs->addTab(buildLogTab(), tr("Log"));
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updatePreviewTimer);

    buildMenus();
    m_statusText = new QLabel();
    statusBar()->addWidget(m_statusText, 1);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(500);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::refreshStatus);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(1000 / 15);
    connect(m_previewTimer, &QTimer::timeout, this, &MainWindow::refreshPreview);

    connect(m_daemon, &Daemon::statusChanged, this, [this]() { if (isVisible()) refreshStatus(); });
    connect(m_daemon, &Daemon::suspendedChanged, this, &MainWindow::refreshStatus);

    for (const Log::Entry &e : Log::instance().entries()) appendLog(e);
    connect(&Log::instance(), &Log::entryAdded, this, &MainWindow::appendLog);

    const QByteArray geometry = m_daemon->settings()->windowGeometry();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) resize(980, 560);

    refreshStatus();
}

MainWindow::~MainWindow()
{
    if (isVisible()) storeGeometry();
}

// ----------------------------------------------------------------- building / costruzione
QWidget *MainWindow::buildStatusTab()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    m_warnings = new QLabel();
    m_warnings->setWordWrap(true);
    m_warnings->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_warnings->setStyleSheet(QStringLiteral("QLabel { background: #FFF3C4; color: #5D4000; border: 1px solid #E0C060; padding: 6px; }"));
    m_warnings->hide();
    layout->addWidget(m_warnings);

    m_players = new QTableWidget(Settings::Players, PlayerColumns);
    m_players->setHorizontalHeaderLabels({
        tr("Scope"), tr("COM port"), tr("CH"), tr("FW"), tr("UDP port"), tr("Source"),
        tr("fps in"), tr("fps sent"), tr("KB/frame"), tr("q avg"), tr("C KB/s"), tr("DROP/s"), tr("QFULL/s") });
    QStringList rows;
    for (int i = 1; i <= Settings::Players; ++i) rows << QStringLiteral("P%1").arg(i);
    m_players->setVerticalHeaderLabels(rows);
    m_players->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_players->setSelectionMode(QAbstractItemView::NoSelection);
    m_players->setFocusPolicy(Qt::NoFocus);
    m_players->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_players->horizontalHeader()->setMinimumSectionSize(40);
    m_players->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_players->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int height = m_players->horizontalHeader()->sizeHint().height() + 2 * m_players->frameWidth();
    for (int i = 0; i < Settings::Players; ++i) height += m_players->verticalHeader()->sectionSize(i);
    m_players->setFixedHeight(height);
    layout->addWidget(m_players);

    auto *group = new QGroupBox(tr("Other devices"));
    auto *groupLayout = new QVBoxLayout(group);
    m_devices = new QTableWidget(0, DeviceColumns);
    m_devices->setHorizontalHeaderLabels({ tr("Port"), tr("Serial number"), tr("State"), tr("CH"), tr("FW"), tr("Reason"), QString() });
    m_devices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_devices->setSelectionMode(QAbstractItemView::NoSelection);
    m_devices->setFocusPolicy(Qt::NoFocus);
    m_devices->verticalHeader()->hide();
    m_devices->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_devices->horizontalHeader()->setSectionResizeMode(DevDetail, QHeaderView::Stretch);
    groupLayout->addWidget(m_devices);
    layout->addWidget(group, 1);

    return page;
}

QWidget *MainWindow::buildPreviewTab()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *options = new QHBoxLayout();
    m_showPreview = new QCheckBox(tr("Show preview"));
    m_circle = new QCheckBox(tr("Circle"));
    m_circle->setToolTip(tr("Darken the pixels the scope does not show"));
    options->addWidget(m_showPreview);
    options->addWidget(m_circle);
    options->addStretch();
    layout->addLayout(options);

    auto *grid = new QGridLayout();
    grid->setSpacing(12);
    for (int i = 0; i < Settings::Players; ++i) {
        m_preview[i] = new PreviewWidget(i + 1);
        grid->addWidget(m_preview[i], i / 2, i % 2);
    }
    auto *gridRow = new QHBoxLayout();
    gridRow->addStretch();
    gridRow->addLayout(grid);
    gridRow->addStretch();
    layout->addLayout(gridRow);
    layout->addStretch();

    connect(m_showPreview, &QCheckBox::toggled, this, [this](bool on) {
        m_circle->setEnabled(on);
        if (!on)
            for (PreviewWidget *w : m_preview) w->setFrame(QImage());
        updatePreviewTimer();
        refreshPreview();
    });
    connect(m_circle, &QCheckBox::toggled, this, [this](bool on) {
        for (PreviewWidget *w : m_preview) w->setCircle(on);
    });
    m_circle->setEnabled(false);

    return page;
}

QWidget *MainWindow::buildLogTab()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(Log::Capacity);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_log->setFont(mono);
    layout->addWidget(m_log);

    auto *buttons = new QHBoxLayout();
    auto *copy = new QPushButton(tr("Copy"));
    connect(copy, &QPushButton::clicked, this, &MainWindow::copyLog);
    buttons->addStretch();
    buttons->addWidget(copy);
    layout->addLayout(buttons);

    return page;
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *settings = file->addAction(tr("&Settings…"));
    settings->setMenuRole(QAction::PreferencesRole);
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    m_suspendAction = file->addAction(tr("Suspend (release the ports)"));
    connect(m_suspendAction, &QAction::triggered, this, [this]() { m_daemon->setSuspended(!m_daemon->isSuspended()); });
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"));
    quit->setMenuRole(QAction::QuitRole);
    connect(quit, &QAction::triggered, this, &MainWindow::quitRequested);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *about = help->addAction(tr("&About"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

// ----------------------------------------------------------------- refresh / aggiornamento
void MainWindow::refreshStatus()
{
    const QVector<SlotStatus> slotList = m_daemon->slotStatus();
    for (int row = 0; row < slotList.size() && row < Settings::Players; ++row) {
        const SlotStatus &s = slotList[row];
        const bool dongle = s.scope != ScopeState::NoDongle;

        QString scope;
        switch (s.scope) {
        case ScopeState::NoDongle:  scope = tr("no dongle"); break;
        case ScopeState::Paired:    scope = tr("paired"); break;
        case ScopeState::Duplicate: scope = tr("duplicate"); break;
        }
        QString source;
        switch (s.source) {
        case SourceKind::None:      source = tr("none"); break;
        case SourceKind::Udp:       source = QStringLiteral("UDP"); break;
        case SourceKind::UdpPaused: source = tr("pause"); break;
        case SourceKind::Dxgi:      source = QStringLiteral("DXGI"); break;
        case SourceKind::Clip:      source = tr("clip"); break;
        }
        if (s.aimMissing) source += tr(" (no aim)");

        setCell(m_players, row, ColScope, scope);
        setCell(m_players, row, ColPort, dongle ? s.port : kDash);
        setCell(m_players, row, ColChannel, dongle && s.channel ? QString::number(s.channel) : kDash);
        setCell(m_players, row, ColFirmware, dongle && !s.firmware.isEmpty() ? s.firmware : kDash);
        setCell(m_players, row, ColUdpPort, s.udpPort ? QString::number(s.udpPort) : kDash);
        setCell(m_players, row, ColSource, source);
        setCell(m_players, row, ColFpsIn, number(s.fpsIn, 0));
        setCell(m_players, row, ColFpsOut, dongle ? number(s.fpsOut, 0) : kDash);
        setCell(m_players, row, ColKbFrame, dongle ? number(s.kbPerFrame, 1) : kDash);
        setCell(m_players, row, ColQ, dongle ? number(s.q, 0) : kDash);
        setCell(m_players, row, ColCapacity, dongle && s.capacityKBs > 0 ? number(s.capacityKBs, 0) : kDash);
        setCell(m_players, row, ColDrop, dongle ? number(s.dropPerSec, 1) : kDash);
        setCell(m_players, row, ColQfull, dongle ? number(s.qfullPerSec, 1) : kDash);
    }

    const QStringList warnings = m_daemon->warnings();
    m_warnings->setVisible(!warnings.isEmpty());
    if (!warnings.isEmpty()) m_warnings->setText(warnings.join(QLatin1Char('\n')));

    refreshDevices();

    m_suspendAction->setText(m_daemon->isSuspended() ? tr("Resume") : tr("Suspend (release the ports)"));
    const QStringList summary = m_daemon->summary().split(QLatin1Char('\n'));
    m_statusText->setText(summary.size() > 1 ? summary.at(1) : QString());
}

void MainWindow::refreshDevices()
{
    const QVector<DeviceStatus> devices = m_daemon->devices();
    if (sameDevices(devices, m_shownDevices)) return;
    m_shownDevices = devices;

    m_devices->setRowCount(devices.size());
    for (int row = 0; row < devices.size(); ++row) {
        const DeviceStatus &d = devices[row];
        QString state;
        switch (d.state) {
        case PortState::Waiting:      state = tr("waiting"); break;
        case PortState::Probing:      state = tr("probing"); break;
        case PortState::Boot:         state = tr("starting"); break;
        case PortState::Free:         state = tr("free"); break;
        case PortState::Duplicate:    state = tr("duplicate"); break;
        case PortState::NoAnswer:     state = tr("no answer"); break;
        case PortState::Discarded:    state = tr("discarded"); break;
        case PortState::Busy:         state = tr("busy"); break;
        case PortState::AccessDenied: state = tr("access denied"); break;
        case PortState::Lost:         state = tr("lost"); break;
        }
        setCell(m_devices, row, DevPort, d.port);
        setCell(m_devices, row, DevSerial, d.serialNumber.isEmpty() ? kDash : d.serialNumber);
        setCell(m_devices, row, DevState, state);
        setCell(m_devices, row, DevChannel, d.channel ? QString::number(d.channel) : kDash);
        setCell(m_devices, row, DevFirmware, d.firmware.isEmpty() ? kDash : d.firmware);
        setCell(m_devices, row, DevDetail, d.detail);
        m_devices->item(row, DevDetail)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        if (canRetry(d.state)) {
            auto *retry = new QPushButton(tr("Retry"));
            const QString port = d.port;
            connect(retry, &QPushButton::clicked, m_daemon, [this, port]() { m_daemon->retryPort(port); });
            m_devices->setCellWidget(row, DevRetry, retry);
        } else {
            m_devices->removeCellWidget(row, DevRetry);
        }
    }
}

void MainWindow::refreshPreview()
{
    // EN: phase 1: only the labels; the frames arrive with the UDP (phase 3). / IT: fase 1: solo le etichette; i frame arrivano con l'UDP (fase 3).
    const QVector<SlotStatus> slotList = m_daemon->slotStatus();
    for (int i = 0; i < slotList.size() && i < Settings::Players; ++i) {
        const SlotStatus &s = slotList[i];
        QString source;
        switch (s.source) {
        case SourceKind::None:      source = tr("no source"); break;
        case SourceKind::Udp:       source = QStringLiteral("UDP"); break;
        case SourceKind::UdpPaused: source = tr("pause"); break;
        case SourceKind::Dxgi:      source = QStringLiteral("DXGI"); break;
        case SourceKind::Clip:      source = tr("clip"); break;
        }
        m_preview[i]->setLabel(tr("P%1 · %2 · %3 fps").arg(s.player).arg(source).arg(qRound(s.fpsIn)));
        m_preview[i]->setPaused(s.source == SourceKind::UdpPaused);
    }
}

void MainWindow::updatePreviewTimer()
{
    // EN: 15 times a second, only with the window visible, the Preview tab open and the box ticked.
    // IT: 15 volte al secondo, solo con la finestra visibile, la scheda Anteprima aperta e la spunta attiva.
    const bool on = isVisible() && !isMinimized() && m_showPreview->isChecked() && m_tabs->currentIndex() == 1;
    if (on && !m_previewTimer->isActive()) m_previewTimer->start();
    else if (!on && m_previewTimer->isActive()) m_previewTimer->stop();
}

// ----------------------------------------------------------------- log / registro
void MainWindow::appendLog(const Log::Entry &entry)
{
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->value() == bar->maximum();

    const QString line = Log::format(entry).toHtmlEscaped();
    QString html;
    switch (entry.level) {
    case Log::Level::Info:    html = line; break;
    case Log::Level::Warning: html = QStringLiteral("<span style=\"color:#B26A00\">%1</span>").arg(line); break;
    case Log::Level::Error:   html = QStringLiteral("<span style=\"color:#C62828\">%1</span>").arg(line); break;
    }
    m_log->appendHtml(html);

    if (atBottom) bar->setValue(bar->maximum());
}

void MainWindow::copyLog()
{
    QStringList lines;
    for (const Log::Entry &e : Log::instance().entries()) lines << Log::format(e);
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    statusBar()->showMessage(tr("Log copied (%n event(s))", nullptr, int(lines.size())), 3000);
}

// ----------------------------------------------------------------- actions / azioni
void MainWindow::openSettings()
{
    SettingsDialog dialog(m_daemon->settings(), this);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QString version = QStringLiteral(OFD_VERSION);
#ifdef OFD_GITHASH
    version += QStringLiteral(" (") + QStringLiteral(OFD_GITHASH) + QLatin1Char(')');
#endif
    QMessageBox::about(this, tr("About"),
        tr("<h3>OpenFIRE Scope Daemon %1</h3>"
           "<p>Video from the PC to the OpenFIRE scopes.</p>"
           "<p>Team OpenFIRE. License GPL v3.</p>"
           "<p>Qt %2</p>").arg(version, QString::fromLatin1(qVersion())));
}

void MainWindow::showAndRaise()
{
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void MainWindow::storeGeometry()
{
    m_daemon->settings()->setWindowGeometry(QMainWindow::saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    storeGeometry();
    if (m_trayAvailable) {
        // EN: the daemon keeps running in the notification area. / IT: il demone resta attivo nell'area di notifica.
        hide();
        event->ignore();
    } else {
        // EN: no notification area: minimize, so the daemon can still be reached. / IT: nessuna area di notifica: riduce a icona, cosi' il demone resta raggiungibile.
        showMinimized();
        event->ignore();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    refreshStatus();
    m_statusTimer->start();
    updatePreviewTimer();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    m_statusTimer->stop();
    updatePreviewTimer();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) updatePreviewTimer();
}

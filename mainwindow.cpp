#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define GLOBAL_POS(ev) (ev)->globalPosition().toPoint()
#else
#  define GLOBAL_POS(ev) (ev)->globalPos()
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QCursor>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QToolTip>
#include <QAction>
#include <QMessageBox>
#include <QFont>
#include <QIcon>
#include <QSize>
#include <QProcess>
#include <QTemporaryFile>
#include "translations/tsparser.h"
#include "audiocapture.h"
#include "levelmeter.h"
#include "whisperengine.h"
#include "modelmanager.h"
#include "modeldownloader.h"
#include "globalhotkey.h"
#include "transcriptpreview.h"
#include <QProgressDialog>
#include <QDir>
#include <QKeySequence>
#ifdef Q_OS_MAC
#  include "macos_utils.h"
#endif

// ─────────────────────────────────────────────────────────────
//  Palette constants
// ─────────────────────────────────────────────────────────────
static const QColor COL_BG       ("#2D2D2D");
static const QColor COL_ACCENT   ("#FFA600");
static const QColor COL_BG_BTN  ("#3A3A3A");
static const QColor COL_TEXT     ("#FFFFFF");
static const QColor COL_SUBTEXT  ("#AAAAAA");
static const QColor COL_OVERLAY  (0, 0, 0, 128); // rgba(0,0,0,0.5)
static const QColor COL_DANGER   ("#CC3333");

static const char* BASE_STYLE = R"(
QWidget {
    background: #2D2D2D;
    color: #FFFFFF;
    font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif;
    font-size: 11px;
}
QProgressBar {
    border: 1px solid #FFA600;
    border-radius: 3px;
    background: rgba(0,0,0,0.5);
    text-align: center;
    color: #FFA600;
    font-size: 9px;
}
QProgressBar::chunk {
    background: #FFA600;
    border-radius: 2px;
}
QToolTip {
    background: #1A1A1A;
    color: #FFA600;
    border: 1px solid #FFA600;
    padding: 3px 6px;
}
)";

// ─────────────────────────────────────────────────────────────
//  MainWindow
// ─────────────────────────────────────────────────────────────
MainWindow::MainWindow(QTranslator* startupTranslator, QWidget* parent)
    : QWidget(parent)
    , m_persist("TrackType", "TrackType")
{
    // Load persisted settings
    m_settings.windowOpacity   = m_persist.value("window/opacity",  1.0).toDouble();
    m_settings.alwaysOnTop     = m_persist.value("window/alwaysOnTop", true).toBool();
    m_settings.startMinimized  = m_persist.value("window/startMin",         false).toBool();
    m_settings.xMinimizesApp   = m_persist.value("window/xMinimizesApp",    false).toBool();
    m_settings.launchOnStartup = m_persist.value("window/launchOnStartup",  false).toBool();
    m_settings.audioFeedback   = m_persist.value("audio/enabled",    false).toBool();
    m_settings.inputDevice     = m_persist.value("audio/inputDevice",  "").toString();
    m_settings.sttModel        = m_persist.value("stt/model", "ggml-base.en.bin").toString();
    m_settings.sttLanguage     = m_persist.value("stt/language",     "en").toString();
    m_settings.injectionMode   = static_cast<InjectionMode>(
                                     m_persist.value("inject/mode", 0).toInt());
    m_settings.injectPartials  = m_persist.value("inject/partials",  false).toBool();
    m_settings.autoFormat      = m_persist.value("inject/autoFormat", true).toBool();
    m_settings.hotkey          = m_persist.value("hotkey/sequence",   "").toString();
    m_settings.hotkeyPushToTalk= m_persist.value("hotkey/pushToTalk", false).toBool();
    m_settings.language        = m_persist.value("language",          "en").toString();
    m_settings.edgeLock        = static_cast<EdgeLock>(m_persist.value("window/edgeLock", 0).toInt());
    m_settings.edgeHide        = m_persist.value("window/edgeHide", false).toBool();

    // Adopt any translator already installed at startup so installLanguage()
    // can remove it when the user later switches languages (e.g. back to English).
    if (startupTranslator) {
        m_translator = startupTranslator;
        m_translator->setParent(this);   // transfer ownership from QApplication
    }

    // Window flags: frameless, stays on top
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint | Qt::Tool;
    if (m_settings.alwaysOnTop) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground, false);
    // On macOS, Qt::Tool creates an NSPanel that hides when another app becomes
    // active (hidesOnDeactivation = true by default).  This attribute disables
    // that behaviour so the toolbar stays visible regardless of focus.
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#ifdef Q_OS_MAC
    // Keep the window visible during Mission Control / Exposé and on all Spaces.
    // Must be called after setWindowFlags (which can recreate the native handle).
    applyMacOSWindowBehavior(winId());
#endif
    setStyleSheet(BASE_STYLE);
    setWindowTitle(tr("TrackType"));
    setWindowOpacity(m_settings.windowOpacity);

#ifdef HAVE_MULTIMEDIA
    m_clickSound = new QSoundEffect(this);
    m_clickSound->setSource(QUrl("qrc:/sounds/click-noise.wav"));
#endif

    buildUi();
    buildTray();
    loadWindowSettings();
    initAudio();
    initStt();
    setupHotkey();

    // If launch-on-startup is enabled, make sure the OS registration still
    // exists and points at the current executable (self-heals after updates).
    syncLaunchOnStartup();
}

void MainWindow::promptForInputAccessIfNeeded()
{
#ifdef Q_OS_LINUX
    if (TextInjector::hasInputDeviceAccess())
        return;

    // Respect a previous "Don't ask again" choice.
    if (m_persist.value("linux/skipInputAccessPrompt", false).toBool())
        return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Enable cursor tracking"));
    box.setText(tr("TrackType needs permission to read mouse movement."));
    box.setInformativeText(tr(
        "Without it, dwell-clicking only works while the cursor is over the "
        "TrackType window. Granting permission installs a small system rule "
        "and shows a password prompt — no terminal required."));
    QPushButton* grant = box.addButton(tr("Grant Permission…"), QMessageBox::AcceptRole);
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    QPushButton* never = box.addButton(tr("Don't Ask Again"), QMessageBox::ActionRole);
    box.setDefaultButton(grant);
    box.exec();

    if (box.clickedButton() == never)
        m_persist.setValue("linux/skipInputAccessPrompt", true);
    // Only proceed on an explicit Grant; "Not Now", "Don't Ask Again" and
    // dismissing the dialog all leave permissions unchanged.
    if (box.clickedButton() != grant)
        return;

    // Grant: extract the bundled udev rule, then install it as root via pkexec
    // (graphical polkit auth), reload the rules and re-tag input devices so the
    // uaccess ACL applies to the current session.
    QString ruleText;
    {
        QFile res(":/linux/71-tracktype-input.rules");
        if (res.open(QIODevice::ReadOnly | QIODevice::Text))
            ruleText = QString::fromUtf8(res.readAll());
    }
    if (ruleText.isEmpty()) {
        QMessageBox::warning(this, tr("TrackType"),
            tr("Internal error: the permission rule could not be loaded."));
        return;
    }

    QString tmpPath;
    {
        QTemporaryFile tmp(QDir::tempPath() + "/tracktype-XXXXXX.rules");
        tmp.setAutoRemove(false);
        if (!tmp.open()) {
            QMessageBox::warning(this, tr("TrackType"),
                tr("Could not create a temporary file for the permission rule."));
            return;
        }
        tmp.write(ruleText.toUtf8());
        tmp.flush();
        tmpPath = tmp.fileName();
    }

    const QString dest = QStringLiteral("/etc/udev/rules.d/71-tracktype-input.rules");
    const QString script = QStringLiteral(
        "cp '%1' '%2' && chmod 0644 '%2' && "
        "udevadm control --reload-rules && "
        "udevadm trigger --subsystem-match=input --action=change")
        .arg(tmpPath, dest);

    QProcess proc;
    proc.start(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"),
                                          QStringLiteral("-c"), script});
    const bool started = proc.waitForStarted(5000);
    bool finished = false;
    if (started)
        finished = proc.waitForFinished(120000);  // allow time at the password prompt

    QFile::remove(tmpPath);

    if (!started) {
        // pkexec unavailable — fall back to copyable manual instructions.
        QMessageBox::information(this, tr("TrackType"),
            tr("Could not launch the graphical authentication helper (pkexec).\n\n"
               "To enable full cursor tracking, install this file as root:\n  %1\n\n"
               "with the following contents:\n\n%2").arg(dest, ruleText));
        return;
    }
    if (finished && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        QMessageBox::information(this, tr("TrackType"),
            tr("Permission granted. Please restart TrackType to enable cursor "
               "tracking across all windows."));
    } else {
        QMessageBox::warning(this, tr("TrackType"),
            tr("Permission was not granted. TrackType will ask again next time "
               "it starts. (On an X11/Xorg session this permission is not needed.)"));
    }
#endif
}

void MainWindow::buildUi()
{
    setMouseTracking(true);   // needed for cursor updates without a button held

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // ── Title bar ─────────────────────────────────────────────
    m_titleBar = new QWidget;
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setFixedHeight(30);
    m_titleBar->setStyleSheet(
        "#titleBar { background: #1A1A1A; border-bottom: 2px solid #FFA600; }"
        "QLabel    { color: #FFA600; font-weight: bold; font-size: 12px;"
        "            background: transparent; border: none; }"
    );
    auto* tbLayout = new QHBoxLayout(m_titleBar);
    tbLayout->setContentsMargins(8, 0, 4, 0);
    tbLayout->setSpacing(4);

    m_titleIcon = new QLabel;
    m_titleIcon->setPixmap(QIcon(":/icons/app.svg").pixmap(16, 16));
    m_titleIcon->setFixedSize(20, 20);
    m_titleIcon->setAlignment(Qt::AlignCenter);
    tbLayout->addWidget(m_titleIcon);

    m_titleLabel = new QLabel(tr("TrackType"));
    m_titleLabel->setMinimumWidth(0);  // let it clip rather than force window wider
    tbLayout->addWidget(m_titleLabel);
    tbLayout->addStretch(1);  // always pushes the action buttons to the right

    // Primary "Dictate" toggle — starts/stops the capture → STT → inject
    // pipeline.  The checkable button swaps the mic icon via QIcon On/Off states.
    QIcon micIcon;
    micIcon.addFile(":/icons/mic_off.svg", QSize(18, 18), QIcon::Normal, QIcon::Off);
    micIcon.addFile(":/icons/mic_on.svg",  QSize(18, 18), QIcon::Normal, QIcon::On);
    m_dictateBtn = new QPushButton;
    m_dictateBtn->setCheckable(true);
    m_dictateBtn->setIcon(micIcon);
    m_dictateBtn->setIconSize(QSize(18, 18));
    m_dictateBtn->setFixedSize(28, 24);
    m_dictateBtn->setEnabled(false);   // enabled once the speech model is ready
    m_dictateBtn->setToolTip(tr("Loading speech model…"));
    m_dictateBtn->setStyleSheet(
        "QPushButton { background:#3A3A3A; border:1px solid #FFA600; border-radius:3px; }"
        "QPushButton:hover { background:#4A4A4A; }"
        "QPushButton:checked { background:#4D3300; }"
        "QPushButton:disabled { border:1px solid #555; }"
    );
    connect(m_dictateBtn, &QPushButton::toggled, this, &MainWindow::onDictateToggled);
    tbLayout->addWidget(m_dictateBtn);

    // STT status indicator — a small coloured dot with a tooltip carrying the
    // current state (model/microphone status surfaced in the toolbar).
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(10, 10);
    tbLayout->addWidget(m_statusDot);

    // Microphone input-level meter — reuses the app-wide orange QProgressBar
    // style (BASE_STYLE) so it matches the look of the old dwell countdown.
    m_levelMeter = new LevelMeter;
    tbLayout->addWidget(m_levelMeter);

    // Settings button
    m_settingsBtn = new QPushButton;
    m_settingsBtn->setIcon(QIcon(":/icons/settings.svg"));
    m_settingsBtn->setIconSize(QSize(16, 16));
    m_settingsBtn->setFixedSize(22, 22);
    m_settingsBtn->setToolTip(tr("Settings"));
    m_settingsBtn->setStyleSheet(
        "QPushButton { background:#3A3A3A; color:#CCC; border:1px solid #555; border-radius:3px; font-size:14px; }"
        "QPushButton:hover { background:#4A4A4A; color:#FFA600; border:1px solid #FFA600; }"
        "QPushButton:pressed { background:#2A2A2A; }"
    );
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    tbLayout->addWidget(m_settingsBtn);

    // Close/hide button
    m_exitBtn = new QPushButton;
    m_exitBtn->setIcon(QIcon(":/icons/close.svg"));
    m_exitBtn->setIconSize(QSize(14, 14));
    m_exitBtn->setFixedSize(22, 22);
    m_exitBtn->setToolTip(tr("Hide to tray (right-click tray icon to quit)"));
    m_exitBtn->setStyleSheet(
        "QPushButton { background:#3A3A3A; color:#CCC; border:1px solid #555; border-radius:3px; font-size:12px; }"
        "QPushButton:hover { background:#CC3333; color:#FFF; border:1px solid #CC3333; }"
        "QPushButton:pressed { background:#991111; }"
    );
    connect(m_exitBtn, &QPushButton::clicked, this, &MainWindow::onExitClicked);
    tbLayout->addWidget(m_exitBtn);

    root->addWidget(m_titleBar);

    adjustSize();
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    m_tray = new QSystemTrayIcon(QIcon(":/icons/app.svg"), this);
    m_tray->setToolTip(tr("TrackType Virtual Mouse"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->setStyleSheet(
        "QMenu { background:#2D2D2D; color:#FFF; border:1px solid #FFA600; }"
        "QMenu::item:selected { background:#FFA600; color:#1A1A1A; }"
    );
    m_showAct = m_trayMenu->addAction(tr("Show / Hide"));
    m_trayMenu->addSeparator();
    m_dictateAct = m_trayMenu->addAction(tr("Start Dictation"));
    m_dictateAct->setEnabled(false);   // until the model is ready
    m_pauseAct = m_trayMenu->addAction(tr("Pause Injecting"));
    m_pauseAct->setEnabled(false);     // only meaningful while dictating
    m_undoAct = m_trayMenu->addAction(tr("Undo Last Injection"));
    m_undoAct->setEnabled(false);
    m_trayMenu->addSeparator();
    m_settingsAct = m_trayMenu->addAction(tr("Settings…"));
    m_quitAct = m_trayMenu->addAction(tr("Quit TrackType"));

    connect(m_showAct, &QAction::triggered, this, [this](){
        setVisible(!isVisible());
        if (isVisible()) raise();
    });
    // Mirror the toolbar toggle: triggering the tray action flips the button,
    // which drives the whole pipeline through onDictateToggled().
    connect(m_dictateAct, &QAction::triggered, this, [this](){
        if (m_dictateBtn && m_dictateBtn->isEnabled())
            m_dictateBtn->toggle();
    });
    connect(m_pauseAct,    &QAction::triggered, this, &MainWindow::togglePause);
    connect(m_undoAct,     &QAction::triggered, this, &MainWindow::undoLastInjection);
    connect(m_settingsAct, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(m_quitAct, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    m_tray->setContextMenu(m_trayMenu);
    m_tray->show();
}

void MainWindow::initAudio()
{
    // The microphone is opened only while dictating (toggled on), not at launch.
    m_audio = new AudioCapture(this);
    m_audio->setInputDevice(m_settings.inputDevice);
    if (m_levelMeter)
        connect(m_audio, &AudioCapture::levelChanged,
                m_levelMeter, &LevelMeter::setLevel);
    // Surface microphone problems in the toolbar/tray and drop out of dictation.
    connect(m_audio, &AudioCapture::captureError, this, [this](const QString& msg){
        qWarning("TrackType audio error: %s", qUtf8Printable(msg));
        if (m_dictateBtn && m_dictateBtn->isChecked()) {
            QSignalBlocker block(m_dictateBtn);   // avoid re-entering onDictateToggled
            m_dictateBtn->setChecked(false);
        }
        if (m_stt) m_stt->stop();
        setSttStatus(SttState::Error, msg);
    });
}

void MainWindow::initStt()
{
    m_preview = new TranscriptPreview(this);

    m_stt = new WhisperSttEngine(this);
    m_stt->setLanguage(m_settings.sttLanguage);

    // Feed the contiguous capture stream into the recognizer; its VAD segments
    // utterances and decides when to finalize (the chunker's overlapping windows
    // are unsuitable for accumulation).
    if (m_audio)
        connect(m_audio, &AudioCapture::bufferReady,
                m_stt, &WhisperSttEngine::feedAudio);

    // Model ready → enable the Dictate toggle (recognition starts on demand).
    connect(m_stt, &SttEngine::modelLoaded, this, [this]{
        qInfo("TrackType STT: model loaded — ready to dictate.");
        if (m_dictateBtn) {
            m_dictateBtn->setEnabled(true);
            m_dictateBtn->setToolTip(tr("Start dictation"));
        }
        if (m_dictateAct) m_dictateAct->setEnabled(true);
        if (!m_dictateBtn || !m_dictateBtn->isChecked())
            setSttStatus(SttState::Idle, tr("Ready to dictate"));
    });

    // Recognized text → typed into the focused window (and echoed to the log).
    connect(m_stt, &SttEngine::partialTranscript, this, &MainWindow::onSttPartial);
    connect(m_stt, &SttEngine::finalTranscript,   this, &MainWindow::onSttFinal);
    connect(m_stt, &SttEngine::errorOccurred, this, [this](const QString& msg){
        qWarning("TrackType STT error: %s", qUtf8Printable(msg));
        setSttStatus(SttState::Error, msg);
    });

    applyInjectionSettings();
    setSttStatus(SttState::Busy, tr("Starting speech recognition…"));

    // Defer the model check/download until the event loop is running so the main
    // window is visible before any modal download dialog appears.
    QTimer::singleShot(0, this, [this]{ ensureModelThenStart(); });
}

void MainWindow::ensureModelThenStart()
{
    const ModelInfo mi   = ModelManager::modelInfo(m_settings.sttModel);
    const QString    path = ModelManager::modelFilePath(mi.name);

    if (ModelManager::isModelAvailable(mi.name)) {
        setSttStatus(SttState::Busy, tr("Loading speech model…"));
        m_stt->loadModel(path);
        return;
    }

    // First run (or model switch): download with a cancelable progress dialog.
    setSttStatus(SttState::Busy, tr("Downloading speech model…"));
    QDir().mkpath(ModelManager::modelsDir());
    auto* dl   = new ModelDownloader(this);
    auto* prog = new QProgressDialog(
        tr("Downloading speech recognition model…"), tr("Cancel"), 0, 100, this);
    prog->setWindowTitle(tr("TrackType"));
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->setAutoClose(false);
    prog->setAutoReset(false);

    connect(dl, &ModelDownloader::progress, this, [prog](qint64 received, qint64 total){
        if (total > 0) {
            prog->setMaximum(100);
            prog->setValue(int((received * 100) / total));
        } else {
            prog->setMaximum(0);   // unknown length → busy indicator
        }
    });
    connect(prog, &QProgressDialog::canceled, dl, &ModelDownloader::cancel);
    connect(dl, &ModelDownloader::finished, this,
            [this, dl, prog, path](bool ok, const QString& error){
        prog->close();
        prog->deleteLater();
        dl->deleteLater();
        if (ok) {
            setSttStatus(SttState::Busy, tr("Loading speech model…"));
            m_stt->loadModel(path);
        } else {
            setSttStatus(SttState::Error, tr("No speech model"));
            QMessageBox::warning(this, tr("TrackType"),
                tr("Could not download the speech recognition model:\n%1\n\n"
                   "Dictation will be unavailable until it is downloaded.")
                    .arg(error));
        }
    });

    dl->start(mi.url, path, mi.sha256);
}

void MainWindow::setSttStatus(SttState state, const QString& text)
{
    if (m_statusDot) {
        QString color;
        switch (state) {
            case SttState::Idle:        color = "#888888"; break;  // grey
            case SttState::Busy:        color = "#FFA600"; break;  // accent orange
            case SttState::Listening:   color = "#3FB950"; break;  // green
            case SttState::Transcribing:color = "#3B82F6"; break;  // blue
            case SttState::Injecting:   color = "#FFA600"; break;  // accent orange
            case SttState::Paused:      color = "#E3B341"; break;  // amber
            case SttState::Error:       color = "#CC3333"; break;  // red
        }
        m_statusDot->setStyleSheet(
            QString("background:%1; border-radius:5px;").arg(color));
        m_statusDot->setToolTip(text);
    }
    if (m_tray) {
        m_tray->setToolTip(tr("TrackType — %1").arg(text));
        if (state == SttState::Error)
            m_tray->showMessage(tr("TrackType"), text,
                                QSystemTrayIcon::Warning, 4000);
    }
}

void MainWindow::applyInjectionSettings()
{
    TextInjector::setMode(
        m_settings.injectionMode == InjectionMode::ClipboardPaste
            ? TextInjector::Mode::ClipboardPaste
            : TextInjector::Mode::Type);
    m_normalizer.setEnabled(m_settings.autoFormat);
}

void MainWindow::onSttPartial(const QString& raw)
{
    qInfo("TrackType STT (partial) > %s", qUtf8Printable(raw));

    // Always preview the would-be-typed text near the toolbar.
    const QString shown = m_normalizer.previewPartial(raw);
    if (m_preview)
        m_preview->showText(shown, frameGeometry());
    if (!m_paused)
        setSttStatus(SttState::Transcribing, tr("Transcribing…"));

    // Live keystroke injection of partials is opt-in, Type-mode only, not paused.
    if (m_paused
        || !m_settings.injectPartials
        || m_settings.injectionMode != InjectionMode::Type)
        return;

    if (m_pendingPartialLen > 0)
        TextInjector::sendBackspaces(m_pendingPartialLen);
    TextInjector::typeText(shown);
    m_pendingPartialLen = int(shown.size());
}

void MainWindow::onSttFinal(const QString& raw)
{
    qInfo("TrackType STT > %s", qUtf8Printable(raw));

    if (m_preview)
        m_preview->hide();   // committed → drop the preview

    // Retract any live partial before committing the finalized text.
    if (m_pendingPartialLen > 0) {
        TextInjector::sendBackspaces(m_pendingPartialLen);
        m_pendingPartialLen = 0;
    }

    const QString out = m_normalizer.normalize(raw);

    if (m_paused) {
        // Engine stays warm but nothing is typed while paused.
        return;
    }
    if (!out.isEmpty()) {
        TextInjector::typeText(out);
        m_lastInjectedLen = int(out.size());
        if (m_undoAct) m_undoAct->setEnabled(true);
        setSttStatus(SttState::Injecting, tr("Injecting…"));
        // Fall back to the listening state shortly after.
        QTimer::singleShot(150, this, [this]{
            if (m_dictateBtn && m_dictateBtn->isChecked() && !m_paused)
                setSttStatus(SttState::Listening, tr("Listening"));
        });
    }
}

void MainWindow::onDictateToggled(bool on)
{
    if (on) {
        if (!m_stt || !m_stt->isModelLoaded()) {
            QSignalBlocker block(m_dictateBtn);   // button is normally disabled here
            m_dictateBtn->setChecked(false);
            return;
        }
        // Fresh session: reset formatting + partial/undo bookkeeping + pause.
        m_normalizer.reset();
        m_pendingPartialLen = 0;
        m_paused = false;

        m_audio->start();                 // open the mic
        if (!m_audio->isCapturing())      // captureError already reverted the toggle
            return;
        m_stt->start();                   // begin recognizing → inject finals/partials

        playCue();
        m_dictateBtn->setToolTip(tr("Stop dictation"));
        if (m_dictateAct) m_dictateAct->setText(tr("Stop Dictation"));
        if (m_pauseAct) { m_pauseAct->setEnabled(true); m_pauseAct->setText(tr("Pause Injecting")); }
        setSttStatus(SttState::Listening, tr("Listening"));
    } else {
        if (m_stt)   m_stt->stop();
        if (m_audio) m_audio->stop();
        m_pendingPartialLen = 0;
        m_paused = false;
        if (m_preview) m_preview->hide();

        playCue();
        m_dictateBtn->setToolTip(tr("Start dictation"));
        if (m_dictateAct) m_dictateAct->setText(tr("Start Dictation"));
        if (m_pauseAct)   m_pauseAct->setEnabled(false);
        setSttStatus(SttState::Idle, tr("Ready to dictate"));
    }
}

void MainWindow::setupHotkey()
{
    if (!m_hotkey) {
        m_hotkey = new GlobalHotkey(this);
        connect(m_hotkey, &GlobalHotkey::pressed,  this, &MainWindow::onHotkeyPressed);
        connect(m_hotkey, &GlobalHotkey::released, this, &MainWindow::onHotkeyReleased);
    }
    m_hotkey->setShortcut(QKeySequence(m_settings.hotkey));
}

void MainWindow::onHotkeyPressed()
{
    if (!m_dictateBtn || !m_dictateBtn->isEnabled())
        return;
    if (m_settings.hotkeyPushToTalk) {
        if (!m_dictateBtn->isChecked())
            m_dictateBtn->setChecked(true);   // start while held
    } else {
        m_dictateBtn->toggle();               // press toggles
    }
}

void MainWindow::onHotkeyReleased()
{
    // Only push-to-talk reacts to release (toggle ignores it; on Windows the
    // release fires immediately, so PTT there behaves like toggle).
    if (m_settings.hotkeyPushToTalk && m_dictateBtn && m_dictateBtn->isChecked())
        m_dictateBtn->setChecked(false);
}

void MainWindow::togglePause()
{
    if (!m_dictateBtn || !m_dictateBtn->isChecked())
        return;   // pause only matters during an active session
    m_paused = !m_paused;
    if (m_preview && m_paused) m_preview->hide();
    if (m_pauseAct)
        m_pauseAct->setText(m_paused ? tr("Resume Injecting") : tr("Pause Injecting"));
    setSttStatus(m_paused ? SttState::Paused : SttState::Listening,
                 m_paused ? tr("Paused (engine warm)") : tr("Listening"));
}

void MainWindow::undoLastInjection()
{
    if (m_lastInjectedLen <= 0)
        return;
    TextInjector::sendBackspaces(m_lastInjectedLen);
    m_lastInjectedLen = 0;
    if (m_undoAct) m_undoAct->setEnabled(false);
}

void MainWindow::playCue()
{
#ifdef HAVE_MULTIMEDIA
    if (m_settings.audioFeedback && m_clickSound)
        m_clickSound->play();
#endif
}

// ─── Resize / drag the frameless window ──────────────────────────────────

MainWindow::ResizeEdge MainWindow::edgeAt(QPoint p) const
{
    const QRect  r = rect();
    const int    m = RESIZE_MARGIN;
    const bool onL = p.x() <= m;
    const bool onR = p.x() >= r.width()  - m;
    const bool onT = p.y() <= m;
    const bool onB = p.y() >= r.height() - m;
    if (onT && onL) return ResizeEdge::TopLeft;
    if (onT && onR) return ResizeEdge::TopRight;
    if (onB && onL) return ResizeEdge::BottomLeft;
    if (onB && onR) return ResizeEdge::BottomRight;
    if (onL)        return ResizeEdge::Left;
    if (onR)        return ResizeEdge::Right;
    if (onT)        return ResizeEdge::Top;
    if (onB)        return ResizeEdge::Bottom;
    return ResizeEdge::None;
}

Qt::CursorShape MainWindow::cursorForEdge(ResizeEdge e)
{
    switch (e) {
        case ResizeEdge::Left:  case ResizeEdge::Right:        return Qt::SizeHorCursor;
        case ResizeEdge::Top:   case ResizeEdge::Bottom:       return Qt::SizeVerCursor;
        case ResizeEdge::TopLeft: case ResizeEdge::BottomRight:return Qt::SizeFDiagCursor;
        case ResizeEdge::TopRight:case ResizeEdge::BottomLeft: return Qt::SizeBDiagCursor;
        default:                                               return Qt::ArrowCursor;
    }
}

void MainWindow::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;

    const ResizeEdge edge = edgeAt(ev->pos());
    if (edge != ResizeEdge::None) {
        m_resizeEdge  = edge;
        m_resizeStart = GLOBAL_POS(ev);
        m_resizeGeo   = frameGeometry();
        return;
    }

    if (m_titleBar && m_titleBar->geometry().contains(ev->pos())) {
        m_dragging   = true;
        m_dragOffset = GLOBAL_POS(ev) - frameGeometry().topLeft();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_resizeEdge != ResizeEdge::None) {
        const QPoint delta = GLOBAL_POS(ev) - m_resizeStart;
        QRect geo = m_resizeGeo;
        switch (m_resizeEdge) {
            case ResizeEdge::Left:        geo.setLeft(geo.left()   + delta.x()); break;
            case ResizeEdge::Right:       geo.setRight(geo.right() + delta.x()); break;
            case ResizeEdge::Top:         geo.setTop(geo.top()     + delta.y()); break;
            case ResizeEdge::Bottom:      geo.setBottom(geo.bottom()+ delta.y());break;
            case ResizeEdge::TopLeft:     geo.setTopLeft(geo.topLeft()         + delta); break;
            case ResizeEdge::TopRight:    geo.setTopRight(geo.topRight()       + delta); break;
            case ResizeEdge::BottomLeft:  geo.setBottomLeft(geo.bottomLeft()   + delta); break;
            case ResizeEdge::BottomRight: geo.setBottomRight(geo.bottomRight() + delta); break;
            default: break;
        }
        setGeometry(geo.normalized());
        return;
    }

    if (m_dragging) {
        QPoint newPos = GLOBAL_POS(ev) - m_dragOffset;
        if (m_settings.edgeLock != EdgeLock::None) {
            QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            newPos.setX(m_settings.edgeLock == EdgeLock::Left
                ? avail.left()
                : avail.right() - width() + 1);
        }
        move(newPos);
        return;
    }

    // Hover — update cursor to hint at available resize edges
    setCursor(cursorForEdge(edgeAt(ev->pos())));
}

void MainWindow::mouseReleaseEvent(QMouseEvent*)
{
    m_resizeEdge = ResizeEdge::None;
    m_dragging   = false;
    saveWindowSettings();
}

void MainWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Dark background
    p.fillRect(rect(), COL_BG);
    // Orange border
    p.setPen(QPen(COL_ACCENT, 2));
    p.drawRect(rect().adjusted(1,1,-1,-1));
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    if (m_settings.xMinimizesApp) {
        ev->ignore();
        hide();
    } else {
        ev->accept();
    }
}

void MainWindow::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::WindowStateChange && isMinimized()) {
        hide();
    }
    QWidget::changeEvent(ev);
}

void MainWindow::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    // When restored from tray (or any show), always snap to the visible edge position
    // so the window never appears in its partially-hidden state.
    if (m_settings.edgeLock != EdgeLock::None) {
        if (m_edgeAnim) m_edgeAnim->stop();
        QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        int shownX = (m_settings.edgeLock == EdgeLock::Left)
            ? avail.left()
            : avail.right() - width() + 1;
        if (x() != shownX)
            move(shownX, y());
        m_edgeShown       = true;
        m_edgeHideCountMs = 0;
    }
}

// ── Edge lock / slide-hide ────────────────────────────────────────────────────

static constexpr int k_edgePeekPx      = 8;   // pixels visible when hidden
static constexpr int k_edgeHideDelayMs = 800;  // idle time before sliding away
static constexpr int k_edgePollMs      = 80;   // poll interval
static constexpr int k_edgeAnimMs      = 180;  // slide animation duration

void MainWindow::applyEdgeLock()
{
    if (m_edgePollTimer) m_edgePollTimer->stop();
    if (m_edgeAnim)      m_edgeAnim->stop();

    m_edgeShown       = true;
    m_edgeHideCountMs = 0;

    const EdgeLock lock = m_settings.edgeLock;
    if (lock == EdgeLock::None) return;

    QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    int shownX  = (lock == EdgeLock::Left)
        ? avail.left()
        : avail.right() - width() + 1;
    move(shownX, qBound(avail.top(), y(), avail.bottom() - height()));

    if (m_settings.edgeHide) {
        if (!m_edgePollTimer) {
            m_edgePollTimer = new QTimer(this);
            connect(m_edgePollTimer, &QTimer::timeout, this, &MainWindow::onEdgePoll);
        }
        m_edgePollTimer->start(k_edgePollMs);
    }
}

void MainWindow::onEdgePoll()
{
    if (!isVisible()) {
        m_edgeShown = true;
        m_edgeHideCountMs = 0;
        return;
    }

    const EdgeLock lock = m_settings.edgeLock;
    if (lock == EdgeLock::None) return;

    QRect  avail  = QGuiApplication::primaryScreen()->availableGeometry();
    // Global cursor position for the slide-away/peek detection.
    QPoint cursor = QCursor::pos();

    const int shownX  = (lock == EdgeLock::Left)
        ? avail.left()
        : avail.right() - width() + 1;
    const int hiddenX = (lock == EdgeLock::Left)
        ? avail.left() - width() + k_edgePeekPx
        : avail.right() - k_edgePeekPx + 1;

    if (m_edgeShown) {
        bool over = geometry().contains(cursor);
        if (over) {
            m_edgeHideCountMs = 0;
        } else {
            m_edgeHideCountMs += k_edgePollMs;
            if (m_edgeHideCountMs >= k_edgeHideDelayMs) {
                m_edgeShown       = false;
                m_edgeHideCountMs = 0;
                animateEdgeTo(QPoint(hiddenX, y()));
            }
        }
    } else {
        // Show when cursor enters the visible sliver at the screen edge
        bool inPeek = (lock == EdgeLock::Left)
            ? (cursor.x() <= avail.left() + k_edgePeekPx - 1
               && cursor.y() >= y() && cursor.y() < y() + height())
            : (cursor.x() >= avail.right() - k_edgePeekPx + 1
               && cursor.y() >= y() && cursor.y() < y() + height());

        if (inPeek) {
            m_edgeShown       = true;
            m_edgeHideCountMs = 0;
            animateEdgeTo(QPoint(shownX, y()));
        }
    }
}

void MainWindow::animateEdgeTo(QPoint target)
{
    if (!m_edgeAnim) {
        m_edgeAnim = new QPropertyAnimation(this, "pos", this);
        m_edgeAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_edgeAnim->setDuration(k_edgeAnimMs);
    }
    m_edgeAnim->stop();
    m_edgeAnim->setStartValue(pos());
    m_edgeAnim->setEndValue(target);
    m_edgeAnim->start();
}

// ─── Slots ────────────────────────────────────────────────────────────────
void MainWindow::onSettingsClicked()
{
    SettingsDialog dlg(m_settings, m_translator, this);
    if (dlg.exec() == QDialog::Accepted) {
        applySettings(dlg.settings());
    }
}

// ── Launch-on-startup helpers (platform-specific) ─────────────────────────

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void setLaunchOnStartup(bool enable)
{
    const wchar_t* runKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* approvedKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    const wchar_t* name = L"TrackType";

    HKEY hKey;

    if (enable) {
        // Write quoted exe path as REG_SZ under the Run key
        const std::wstring exe = QString("\"%1\"")
            .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()))
            .toStdWString();

        if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                            nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(exe.c_str()),
                           static_cast<DWORD>((exe.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        // Remove any stale "disabled" entry from StartupApproved so Windows
        // treats the Run key entry as approved (absent = enabled by default).
        if (RegOpenKeyExW(HKEY_CURRENT_USER, approvedKey, 0,
                          KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, name);
            RegCloseKey(hKey);
        }
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0,
                          KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, name);
            RegCloseKey(hKey);
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, approvedKey, 0,
                          KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, name);
            RegCloseKey(hKey);
        }
    }
}

#elif defined(Q_OS_MACOS)
static void setLaunchOnStartup(bool enable)
{
    const QString plist =
        QDir::homePath() + "/Library/LaunchAgents/com.optitrack.tracktype.plist";
    if (enable) {
        QDir().mkpath(QFileInfo(plist).absolutePath());
        QFile f(plist);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
                   " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                   "<plist version=\"1.0\">\n"
                   "<dict>\n"
                   "    <key>Label</key>\n"
                   "    <string>com.optitrack.tracktype</string>\n"
                   "    <key>ProgramArguments</key>\n"
                   "    <array>\n"
                   "        <string>" << QCoreApplication::applicationFilePath() << "</string>\n"
                   "    </array>\n"
                   "    <key>RunAtLoad</key>\n"
                   "    <true/>\n"
                   "</dict>\n"
                   "</plist>\n";
        }
    } else {
        QFile::remove(plist);
    }
}

#elif defined(Q_OS_LINUX)
static void setLaunchOnStartup(bool enable)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    const QString path = dir + "/tracktype.desktop";
    if (enable) {
        QDir().mkpath(dir);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "[Desktop Entry]\n"
                   "Type=Application\n"
                   "Name=TrackType\n"
                   "Exec=" << QCoreApplication::applicationFilePath() << "\n"
                   "Hidden=false\n"
                   "X-GNOME-Autostart-enabled=true\n";
        }
    } else {
        QFile::remove(path);
    }
}

#else
static void setLaunchOnStartup(bool) {}
#endif

void MainWindow::syncLaunchOnStartup()
{
    // Only re-assert when enabled; disabling is handled in applySettings so we
    // never touch the OS registration on launch for users who opted out.
    if (m_settings.launchOnStartup)
        setLaunchOnStartup(true);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::applySettings(const AppSettings& s)
{
    const QString oldLanguage    = m_settings.language;
    const QString oldSttModel    = m_settings.sttModel;
    const QString oldSttLanguage = m_settings.sttLanguage;
    m_settings = s;

    setWindowOpacity(s.windowOpacity);

    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint | Qt::Tool;
    if (s.alwaysOnTop) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#ifdef Q_OS_MAC
    applyMacOSWindowBehavior(winId());
#endif
    show();

    if (s.language != oldLanguage)
        installLanguage(s.language);  // also calls retranslateUi

    if (m_exitBtn) m_exitBtn->setToolTip(s.xMinimizesApp
        ? tr("Hide to tray (right-click tray icon to quit)")
        : tr("Close application"));

    // Persist
    m_persist.setValue("window/opacity",     s.windowOpacity);
    m_persist.setValue("window/alwaysOnTop", s.alwaysOnTop);
    m_persist.setValue("window/startMin",      s.startMinimized);
    m_persist.setValue("window/xMinimizesApp", s.xMinimizesApp);
    m_persist.setValue("window/launchOnStartup", s.launchOnStartup);
    // Reconcile unconditionally: the registry/autostart entry must match the
    // setting even when it is unchanged here but was lost externally, never
    // written by an older build, or points at a stale executable path.
    setLaunchOnStartup(s.launchOnStartup);
    m_persist.setValue("audio/enabled",      s.audioFeedback);
    m_persist.setValue("audio/inputDevice",  s.inputDevice);
    m_persist.setValue("stt/model",          s.sttModel);
    m_persist.setValue("stt/language",       s.sttLanguage);
    m_persist.setValue("inject/mode",        int(s.injectionMode));
    m_persist.setValue("inject/partials",    s.injectPartials);
    m_persist.setValue("inject/autoFormat",  s.autoFormat);
    m_persist.setValue("hotkey/sequence",    s.hotkey);
    m_persist.setValue("hotkey/pushToTalk",  s.hotkeyPushToTalk);
    m_persist.setValue("language",           s.language);

    // Switch the live capture to the chosen microphone (restarts capture if the
    // device changed); ensure it is running in case it had no device at launch.
    if (m_audio) {
        m_audio->setInputDevice(s.inputDevice);
        if (!m_audio->isCapturing())
            m_audio->start();
    }

    // Injection mode / formatting and the global hotkey take effect immediately.
    applyInjectionSettings();
    setupHotkey();

    // Apply STT changes: language is cheap; a model change re-runs the
    // ensure/download/reload flow.
    if (m_stt) {
        if (s.sttLanguage != oldSttLanguage)
            m_stt->setLanguage(s.sttLanguage);
        if (s.sttModel != oldSttModel)
            ensureModelThenStart();   // m_settings already updated above
    }
    m_persist.setValue("window/edgeLock",    static_cast<int>(s.edgeLock));
    m_persist.setValue("window/edgeHide",    s.edgeHide);
    applyEdgeLock();
}

void MainWindow::onExitClicked()
{
    if (m_settings.xMinimizesApp) {
        hide();
        if (m_tray) {
            m_tray->showMessage(tr("TrackType"),
                tr("Running in the system tray. Right-click the tray icon to quit."),
                QSystemTrayIcon::Information, 2000);
        }
    } else {
        qApp->quit();
    }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        setVisible(!isVisible());
        if (isVisible()) { raise(); activateWindow(); }
    }
}

void MainWindow::saveWindowSettings()
{
    m_persist.setValue("window/pos", pos());
}

void MainWindow::loadWindowSettings()
{
    QPoint savedPos = m_persist.value("window/pos", QPoint(-1,-1)).toPoint();
    if (savedPos.x() >= 0) {
        move(savedPos);
    } else {
        // Default: top-right of primary screen
        QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        move(screen.right() - width() - 20, screen.top() + 40);
    }
    applyEdgeLock();
}

// ─── Translation ──────────────────────────────────────────────────────────────
void MainWindow::installLanguage(const QString& lang)
{
    if (m_translator) {
        qApp->removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    if (lang != "en") {
        m_translator = loadBestTranslator(lang, this);
        if (m_translator)
            qApp->installTranslator(m_translator);
    }

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("TrackType"));
    if (m_titleLabel)
        m_titleLabel->setText(tr("TrackType"));
    if (m_settingsBtn) m_settingsBtn->setToolTip(tr("Settings"));
    if (m_exitBtn)     m_exitBtn->setToolTip(m_settings.xMinimizesApp
                           ? tr("Hide to tray (right-click tray icon to quit)")
                           : tr("Close application"));
    if (m_tray)        m_tray->setToolTip(tr("TrackType Virtual Mouse"));
    if (m_showAct)     m_showAct->setText(tr("Show / Hide"));
    const bool dictating = m_dictateBtn && m_dictateBtn->isChecked();
    if (m_dictateAct)  m_dictateAct->setText(dictating ? tr("Stop Dictation")
                                                       : tr("Start Dictation"));
    if (m_pauseAct)    m_pauseAct->setText(m_paused ? tr("Resume Injecting")
                                                    : tr("Pause Injecting"));
    if (m_undoAct)     m_undoAct->setText(tr("Undo Last Injection"));
    if (m_settingsAct) m_settingsAct->setText(tr("Settings…"));
    if (m_quitAct)     m_quitAct->setText(tr("Quit TrackType"));
}

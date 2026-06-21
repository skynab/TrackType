#include "settingsdialog.h"
#include "audiocapture.h"
#include "translations/tsparser.h"
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QAbstractButton>
#include <QPushButton>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QSysInfo>
#ifdef Q_OS_MAC
#  include <QDesktopServices>
#  include <QUrl>
#endif

// ── TrackIR palette ──────────────────────────────────────────
static const char* STYLE = R"(
QDialog {
    background: #2D2D2D;
    color: #979797;
    font-family: "Segoe UI", Arial, sans-serif;
    font-size: 12px;
}
QGroupBox {
    color: #FFA600;
    border: 1px solid #979797;
    border-radius: 4px;
    margin-top: 10px;
    padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    padding: 0 4px;
}
QLabel  { color: #979797; }
QCheckBox { color: #979797; spacing: 6px; }
QCheckBox::indicator {
    width: 25px; height: 11px;
    border: none;
    border-radius: 0;
    background: transparent;
    image: url(:/icons/toggle_off.svg);
}
QCheckBox::indicator:checked {
    image: url(:/icons/toggle_on.svg);
}
QSpinBox, QDoubleSpinBox, QComboBox {
    background: #1A1A1A;
    color: #979797;
    border: 1px solid #555;
    border-radius: 3px;
    padding: 2px 4px;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: #2D2D2D;
    color: #979797;
    border: 1px solid #555;
    selection-background-color: #FFA600;
    selection-color: #1A1A1A;
}
QSlider::groove:horizontal {
    height: 4px;
    background: #555;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px; height: 14px;
    background: #FFA600;
    border-radius: 7px;
    margin: -5px 0;
}
QSlider::sub-page:horizontal { background: #FFA600; border-radius: 2px; }
QPushButton {
    background: #FFA600;
    color: #1A1A1A;
    border: none;
    border-radius: 4px;
    padding: 6px 18px;
    font-weight: bold;
}
QPushButton:hover  { background: #FFB833; }
QPushButton:pressed{ background: #CC8400; }
QPushButton[flat=true] {
    background: #3D3D3D;
    color: #FFFFFF;
}
QPushButton[flat=true]:hover { background: #4D4D4D; }
)";

// ─────────────────────────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog(const AppSettings& current,
                               QTranslator* appTranslator,
                               QWidget* parent)
    : QDialog(parent), m_settings(current), m_appTranslator(appTranslator)
{
    setWindowTitle(tr("TrackType — Settings"));
    setModal(true);
    setStyleSheet(STYLE);
    buildUi();
    loadFrom(current);

    // Seize sole control of the qApp translator for the dialog's lifetime.
    // Removing MainWindow's translator here ensures that when the user picks
    // English the preview system can install nothing and tr() correctly falls
    // through to source strings — even if the app was previously in another
    // language.  Screen repaints are deferred until the event loop runs, so
    // the two rapid LanguageChange events below produce no visible flicker.
    if (m_appTranslator)
        qApp->removeTranslator(m_appTranslator);

    // Warm up the preview for the starting language so the dialog reflects
    // the current language immediately.  Connected AFTER this call so the
    // combo's initial value doesn't fire a duplicate preview.
    applyLanguagePreview(current.language);

    connect(m_cmbLanguage, &QComboBox::currentIndexChanged, this, [this](){
        applyLanguagePreview(m_cmbLanguage->currentData().toString());
    });

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this](){
        m_settings = readUi();
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_resetBtn, &QPushButton::clicked, this, [this](){
        loadFrom(AppSettings{});
    });
}

// ── Language preview ──────────────────────────────────────────────────────────

void SettingsDialog::applyLanguagePreview(const QString& lang)
{
    // Swap only the preview translator — do NOT restore m_appTranslator here.
    // m_appTranslator stays out of qApp for the entire dialog lifetime so that
    // choosing English (no preview translator) correctly shows English rather
    // than falling back to the previously-active language.
    if (m_previewTranslator) {
        qApp->removeTranslator(m_previewTranslator);
        delete m_previewTranslator;
        m_previewTranslator = nullptr;
    }

    if (lang != "en") {
        m_previewTranslator = loadBestTranslator(lang, this);
        if (m_previewTranslator)
            qApp->installTranslator(m_previewTranslator);
    }
    // Qt automatically broadcasts QEvent::LanguageChange to this dialog when
    // the translator list changes, which triggers changeEvent → retranslateUi().
}

void SettingsDialog::cleanupPreviewTranslator()
{
    if (m_previewTranslator) {
        qApp->removeTranslator(m_previewTranslator);
        delete m_previewTranslator;
        m_previewTranslator = nullptr;
    }
    // Hand the app translator back to qApp so that after the dialog closes
    // the main window reverts to its previous language (on Cancel) or
    // MainWindow::installLanguage can remove and replace it (on Accept).
    if (m_appTranslator) {
        qApp->installTranslator(m_appTranslator);
        m_appTranslator = nullptr; // ownership stays with MainWindow
    }
}

void SettingsDialog::done(int result)
{
    // Always clean up before closing so that MainWindow::installLanguage()
    // (called on Accept) starts with only its own translator installed, and
    // on Cancel the app reverts to whatever translator was active before the
    // dialog opened.
    cleanupPreviewTranslator();
    QDialog::done(result);
}

// ── Retranslation ─────────────────────────────────────────────────────────────

void SettingsDialog::changeEvent(QEvent* e)
{
    QDialog::changeEvent(e);
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
}

void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("TrackType — Settings"));

#ifdef Q_OS_MAC
    m_lblPermissions->setText(tr("Permissions:"));
    m_btnAccessibility->setText(tr("Open Accessibility Settings…"));
#endif

    m_grpWin->setTitle(tr("Window"));
    m_lblEdgeLock->setText(tr("Lock to screen edge:"));
    m_cmbEdgeLock->setItemText(0, tr("None"));
    m_cmbEdgeLock->setItemText(1, tr("Left edge"));
    m_cmbEdgeLock->setItemText(2, tr("Right edge"));
    m_chkEdgeHide->setText(tr("Slide off screen when idle"));
    m_chkAlwaysOnTop->setText(tr("Always on top"));
    m_chkStartMinimized->setText(tr("Start minimized to tray"));
    m_chkXMinimizesApp->setText(tr("Top X minimizes app"));
    m_chkLaunchOnStartup->setText(tr("Launch on system startup (Windows)"));
    m_chkAudio->setText(tr("Audio feedback"));
    m_lblInputDevice->setText(tr("Microphone:"));
    m_cmbInputDevice->setItemText(0, tr("System default"));
    m_lblOpacity->setText(tr("Opacity:"));
    m_lblLanguage->setText(tr("Language:"));
    m_resetBtn->setText(tr("Reset to Defaults"));
    m_btnOnScreenKbd->setText(tr("Open On-Screen Keyboard"));
}

// ── UI construction ───────────────────────────────────────────────────────────

void SettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(14, 14, 14, 14);

    // ── Brand header ──────────────────────────────────────────
    auto* header = new QWidget;
    header->setObjectName("appHeader");
    header->setStyleSheet(
        "QWidget#appHeader { background: #1A1A1A; border-radius: 6px; }");

    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(14, 10, 14, 10);
    hLay->setSpacing(14);

    // OptiTrack logo rendered from embedded SVG
    QSvgRenderer svgRend(QString(":/icons/optitrack_logo.svg"));
    const int logoH = 30;
    const int logoW = qRound(svgRend.defaultSize().width()
                             * logoH / double(svgRend.defaultSize().height()));
    QPixmap logoPx(logoW, logoH);
    logoPx.fill(Qt::transparent);
    QPainter logoPainter(&logoPx);
    svgRend.render(&logoPainter);

    auto* logoLbl = new QLabel;
    logoLbl->setPixmap(logoPx);
    logoLbl->setStyleSheet("background: transparent;");
    hLay->addWidget(logoLbl);

    // Vertical divider
    auto* vSep = new QFrame;
    vSep->setFrameShape(QFrame::VLine);
    vSep->setStyleSheet("color: #444;");
    hLay->addWidget(vSep);

    // App name + version
    auto* nameLay = new QVBoxLayout;
    nameLay->setSpacing(1);
    auto* appNameLbl = new QLabel("TrackType");
    appNameLbl->setStyleSheet(
        "color: #FFFFFF; font-size: 16px; font-weight: bold; background: transparent;");
#ifdef BUILD_NUMBER
#  define TC_STR_(x) #x
#  define TC_STR(x) TC_STR_(x)
    auto* versionLbl = new QLabel("Version 0.9.2 (build " TC_STR(BUILD_NUMBER) ")");
#else
    auto* versionLbl = new QLabel("Version 0.9.2");
#endif
    versionLbl->setStyleSheet(
        "color: #666666; font-size: 11px; background: transparent;");
    nameLay->addWidget(appNameLbl);
    nameLay->addWidget(versionLbl);
    hLay->addLayout(nameLay);
    hLay->addStretch(1);

    root->addWidget(header);

    // ── Window ────────────────────────────────────────────────
    m_grpWin     = new QGroupBox(tr("Window"));
    auto* wfl    = new QFormLayout(m_grpWin);
    wfl->setSpacing(6);
    wfl->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    wfl->setLabelAlignment(Qt::AlignHCenter);

    auto* opRow = new QHBoxLayout;
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(20, 100);
    m_opacityLabel  = new QLabel("100%");
    m_opacityLabel->setFixedWidth(36);
    opRow->addWidget(m_opacitySlider);
    opRow->addWidget(m_opacityLabel);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){
        m_opacityLabel->setText(QString::number(v) + "%");
    });

    m_lblEdgeLock = new QLabel(tr("Lock to screen edge:"));
    m_cmbEdgeLock = new QComboBox;
    m_cmbEdgeLock->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbEdgeLock->setMinimumWidth(110);
    m_cmbEdgeLock->addItem(tr("None"),       static_cast<int>(EdgeLock::None));
    m_cmbEdgeLock->addItem(tr("Left edge"),  static_cast<int>(EdgeLock::Left));
    m_cmbEdgeLock->addItem(tr("Right edge"), static_cast<int>(EdgeLock::Right));
    m_chkEdgeHide = new QCheckBox(tr("Slide off screen when idle"));
    connect(m_cmbEdgeLock, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){ m_chkEdgeHide->setEnabled(idx != 0); });

    m_chkAlwaysOnTop    = new QCheckBox(tr("Always on top"));
    m_chkStartMinimized = new QCheckBox(tr("Start minimized to tray"));
    m_chkXMinimizesApp  = new QCheckBox(tr("Top X minimizes app"));
    m_chkLaunchOnStartup= new QCheckBox(tr("Launch on system startup (Windows)"));
    m_chkAudio          = new QCheckBox(tr("Audio feedback"));

    // Microphone selection — enumerated from AudioCapture; the first entry is the
    // system default (empty id) so the app follows OS default-device changes.
    m_lblInputDevice = new QLabel(tr("Microphone:"));
    m_cmbInputDevice = new QComboBox;
    m_cmbInputDevice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbInputDevice->setMinimumWidth(130);
    m_cmbInputDevice->addItem(tr("System default"), QString());
    for (const AudioInputDevice& dev : AudioCapture::availableDevices()) {
        const QString label = dev.isDefault
            ? tr("%1 (default)").arg(dev.name) : dev.name;
        m_cmbInputDevice->addItem(label, dev.id);
    }

    // Language names are shown in their native script — intentionally not tr()
    m_cmbLanguage = new QComboBox;
    m_cmbLanguage->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbLanguage->setMinimumWidth(130);
    m_cmbLanguage->addItem("English",   "en");
    m_cmbLanguage->addItem("Čeština",   "cs");
    m_cmbLanguage->addItem("Français",  "fr");
    m_cmbLanguage->addItem("Español",   "es");
    m_cmbLanguage->addItem("中文简体",   "zh_CN");
    m_cmbLanguage->addItem("日本語",     "ja");
    m_cmbLanguage->addItem("한국어",     "ko");
    m_cmbLanguage->addItem("हिन्दी",    "hi");
    m_cmbLanguage->addItem("العربية",   "ar");
    m_cmbLanguage->addItem("বাংলা",     "bn");
    m_cmbLanguage->addItem("Português", "pt");
    m_cmbLanguage->addItem("Русский",   "ru");
    m_cmbLanguage->addItem("اردو",      "ur");

    m_lblOpacity  = new QLabel(tr("Opacity:"));
    m_lblLanguage = new QLabel(tr("Language:"));

    wfl->addRow(m_lblOpacity, opRow);
    wfl->addRow(m_lblEdgeLock, m_cmbEdgeLock);
    wfl->addRow(m_chkEdgeHide);
    wfl->addRow(m_chkAlwaysOnTop);
    // "Start minimized to tray" hidden from the UI — not added to the layout.
    // The checkbox is still constructed above so load/save/retranslate refs stay valid.
    // wfl->addRow(m_chkStartMinimized);
    wfl->addRow(m_chkXMinimizesApp);
    wfl->addRow(m_chkLaunchOnStartup);
    wfl->addRow(m_chkAudio);
    wfl->addRow(m_lblInputDevice, m_cmbInputDevice);
    wfl->addRow(m_lblLanguage,  m_cmbLanguage);

#ifdef Q_OS_MAC
    // macOS needs Accessibility permission for TrackType to type into other apps.
    m_lblPermissions   = new QLabel(tr("Permissions:"));
    m_btnAccessibility = new QPushButton(tr("Open Accessibility Settings…"));
    m_btnAccessibility->setProperty("flat", true);
    connect(m_btnAccessibility, &QPushButton::clicked, this, [](){
        QDesktopServices::openUrl(QUrl(
            "x-apple.systempreferences:"
            "com.apple.preference.security?Privacy_Accessibility"));
    });
    wfl->addRow(m_lblPermissions, m_btnAccessibility);
#endif

    m_btnOnScreenKbd = new QPushButton(tr("Open On-Screen Keyboard"));
    m_btnOnScreenKbd->setFlat(true);
    wfl->addRow(m_btnOnScreenKbd);
    connect(m_btnOnScreenKbd, &QPushButton::clicked, this, [this]() {
#if defined(Q_OS_WIN)
        // Build full path from %SystemRoot% so PATH resolution isn't needed.
        const QString sysRoot = QString::fromLocal8Bit(qgetenv("SystemRoot"));
        const QString osk = (sysRoot.isEmpty() ? QString("C:\\Windows") : sysRoot)
                            + "\\System32\\osk.exe";
        QProcess::startDetached(osk, {});
#elif defined(Q_OS_MAC)
        // The macOS Accessibility Keyboard is a system service, not a standalone app.
        // Navigate to Accessibility > Keyboard in System Settings so the user can
        // enable it — once enabled it appears as a persistent floating keyboard.
        const int macMajor = QSysInfo::productVersion().split('.').value(0).toInt();
        if (macMajor >= 13) {
            // macOS 13 Ventura+ uses the new System Settings URL scheme
            QDesktopServices::openUrl(QUrl(
                "x-apple.systempreferences:"
                "com.apple.Accessibility-Settings.extension?Keyboard"));
        } else {
            QDesktopServices::openUrl(QUrl(
                "x-apple.systempreferences:"
                "com.apple.preference.universalaccess?Keyboard"));
        }
#else
        // Prefer Wayland-native keyboards on Wayland to avoid X11 keyboards
        // starting (returning true) and then immediately crashing on pure Wayland.
        const bool onWayland = !qgetenv("WAYLAND_DISPLAY").isEmpty();
        const QStringList kbs = onWayland
            ? QStringList{"onboard", "squeekboard", "wvkbd",
                          "maliit-keyboard", "florence", "kvkbd", "matchbox-keyboard"}
            : QStringList{"onboard", "florence", "xvkbd", "kvkbd", "matchbox-keyboard"};
        for (const QString& kb : kbs) {
            if (QProcess::startDetached(kb, {}))
                return;
        }
        promptInstallOnScreenKeyboard();
#endif
    });

    root->addWidget(m_grpWin);

    // ── Buttons ───────────────────────────────────────────────
    m_buttons  = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
#ifdef Q_OS_LINUX
    // On Linux/GTK the system theme injects icons into standard buttons; remove them.
    for (QAbstractButton* btn : m_buttons->buttons())
        btn->setIcon(QIcon());
#endif
    m_resetBtn = m_buttons->addButton(tr("Reset to Defaults"), QDialogButtonBox::ResetRole);
    root->addWidget(m_buttons);
}

#ifdef Q_OS_LINUX
void SettingsDialog::promptInstallOnScreenKeyboard()
{
    // Build the install command for "onboard" (the keyboard the launcher tries
    // first, packaged on every major distro) using whichever package manager is
    // present.  Elevation is done with pkexec so the user gets a graphical
    // password prompt rather than needing a terminal.
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    QStringList   installArgs;
    if (!QStandardPaths::findExecutable("apt-get").isEmpty())
        installArgs = {"env", "DEBIAN_FRONTEND=noninteractive",
                       "apt-get", "install", "-y", "onboard"};
    else if (!QStandardPaths::findExecutable("dnf").isEmpty())
        installArgs = {"dnf", "install", "-y", "onboard"};
    else if (!QStandardPaths::findExecutable("zypper").isEmpty())
        installArgs = {"zypper", "--non-interactive", "install", "onboard"};
    else if (!QStandardPaths::findExecutable("pacman").isEmpty())
        installArgs = {"pacman", "-S", "--noconfirm", "onboard"};

    // Without pkexec or a recognised package manager, fall back to advising.
    if (pkexec.isEmpty() || installArgs.isEmpty()) {
        QMessageBox::information(this, tr("On-Screen Keyboard"),
            tr("No on-screen keyboard was found.\n"
               "Please install 'onboard' or 'florence'."));
        return;
    }

    if (QMessageBox::question(this, tr("On-Screen Keyboard"),
            tr("No on-screen keyboard is installed. Install 'onboard' now?\n"
               "You'll be asked for your password."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;

    // Run the install asynchronously behind a busy dialog so the settings
    // window stays responsive during the download.
    auto* proc     = new QProcess(this);
    auto* progress = new QProgressDialog(tr("Installing on-screen keyboard…"),
                                         QString(), 0, 0, this);
    progress->setWindowTitle(tr("On-Screen Keyboard"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);   // apt cannot be safely interrupted mid-run
    progress->show();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, progress](int code, QProcess::ExitStatus status) {
        progress->close();
        progress->deleteLater();
        proc->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            if (!QProcess::startDetached("onboard", {}))
                QMessageBox::information(this, tr("On-Screen Keyboard"),
                    tr("'onboard' was installed. Click the button again to open it."));
        } else {
            QMessageBox::warning(this, tr("On-Screen Keyboard"),
                tr("The on-screen keyboard could not be installed automatically.\n"
                   "You can install it from a terminal with:  sudo apt install onboard"));
        }
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, progress](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;   // other errors are followed by finished(), handled above
        progress->close();
        progress->deleteLater();
        proc->deleteLater();
        QMessageBox::warning(this, tr("On-Screen Keyboard"),
            tr("Could not launch the installer (pkexec).\n"
               "You can install it from a terminal with:  sudo apt install onboard"));
    });

    proc->start(pkexec, installArgs);
}
#endif

void SettingsDialog::loadFrom(const AppSettings& s)
{
    m_cmbEdgeLock->setCurrentIndex(static_cast<int>(s.edgeLock));
    m_chkEdgeHide->setChecked(s.edgeHide);
    m_chkEdgeHide->setEnabled(s.edgeLock != EdgeLock::None);

    m_opacitySlider->setValue(static_cast<int>(s.windowOpacity * 100));
    m_chkAlwaysOnTop->setChecked(s.alwaysOnTop);
    m_chkStartMinimized->setChecked(s.startMinimized);
    m_chkXMinimizesApp->setChecked(s.xMinimizesApp);
    m_chkLaunchOnStartup->setChecked(s.launchOnStartup);
    m_chkAudio->setChecked(s.audioFeedback);

    // Fall back to "System default" (index 0) when the saved device is gone.
    const int devIdx = m_cmbInputDevice->findData(s.inputDevice);
    m_cmbInputDevice->setCurrentIndex(devIdx >= 0 ? devIdx : 0);

    for (int i = 0; i < m_cmbLanguage->count(); ++i) {
        if (m_cmbLanguage->itemData(i).toString() == s.language) {
            m_cmbLanguage->setCurrentIndex(i);
            break;
        }
    }
}

AppSettings SettingsDialog::readUi() const
{
    AppSettings s;
    s.edgeLock = static_cast<EdgeLock>(m_cmbEdgeLock->currentIndex());
    s.edgeHide = m_chkEdgeHide->isChecked() && (s.edgeLock != EdgeLock::None);

    s.windowOpacity    = m_opacitySlider->value() / 100.0;
    s.alwaysOnTop      = m_chkAlwaysOnTop->isChecked();
    s.startMinimized   = m_chkStartMinimized->isChecked();
    s.xMinimizesApp    = m_chkXMinimizesApp->isChecked();
    s.launchOnStartup  = m_chkLaunchOnStartup->isChecked();
    s.audioFeedback    = m_chkAudio->isChecked();
    s.inputDevice      = m_cmbInputDevice->currentData().toString();
    s.language         = m_cmbLanguage->currentData().toString();
    return s;
}

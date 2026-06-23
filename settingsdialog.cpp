#include "settingsdialog.h"
#include "audiocapture.h"
#include "modelmanager.h"
#include "translations/tsparser.h"
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QAbstractButton>
#include <QPushButton>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QHeaderView>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QSysInfo>
#ifdef Q_OS_MAC
#  include <QDesktopServices>
#  include <QUrl>
#endif

namespace {
// Make control characters visible/editable in the commands table.
QString escapeForEditor(const QString& s)
{
    QString e = s;
    e.replace('\\', "\\\\");
    e.replace('\n', "\\n");
    e.replace('\t', "\\t");
    return e;
}
QString unescapeFromEditor(const QString& s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) == '\\' && i + 1 < s.size()) {
            const QChar n = s.at(++i);
            if (n == 'n') out += '\n';
            else if (n == 't') out += '\t';
            else out += n;             // includes "\\" → "\"
        } else {
            out += s.at(i);
        }
    }
    return out;
}
} // namespace

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
QTabWidget::pane {
    border: 1px solid #979797;
    border-radius: 4px;
    top: -1px;
}
QTabBar::tab {
    background: #1A1A1A;
    color: #979797;
    border: 1px solid #555;
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 6px 14px;
    margin-right: 2px;
}
QTabBar::tab:selected { background: #2D2D2D; color: #FFA600; }
QTabBar::tab:hover    { color: #FFB833; }
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

    if (m_tabs) {
        m_tabs->setTabText(0, tr("Window"));
        m_tabs->setTabText(1, tr("Dictation"));
        m_tabs->setTabText(2, tr("Voice commands"));
        m_tabs->setTabText(3, tr("Substitutions"));
        m_tabs->setTabText(4, tr("Profiles"));
    }
    m_profileDetailGroup->setTitle(tr("Profile details"));
    m_lblProfileName->setText(tr("Name:"));
    m_profileNameEdit->setPlaceholderText(tr("e.g. Coding"));
    m_lblProfilePattern->setText(tr("Window pattern (regex):"));
    m_profilePatternEdit->setPlaceholderText(tr("e.g. .*Code.*|.*Visual Studio.*"));
    m_lblProfileVocab->setText(tr("Vocabulary hint:"));
    m_profileVocabEdit->setPlaceholderText(tr("e.g. camelCase, nullptr, std::vector"));
    m_profileSubTable->setHorizontalHeaderLabels({tr("Misheard"), tr("Intended")});
    m_btnAddProfile->setText(tr("Add profile"));
    m_btnRemoveProfile->setText(tr("Remove"));
    m_cmdTable->setHorizontalHeaderLabels({tr("Spoken phrase"), tr("Output")});
    m_subTable->setHorizontalHeaderLabels({tr("Misheard"), tr("Intended")});
    m_btnAddCmd->setText(tr("Add"));
    m_btnRemoveCmd->setText(tr("Remove"));
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
    m_lblSttModel->setText(tr("Speech model:"));
    m_lblSttLanguage->setText(tr("Speech language:"));
    m_cmbSttLanguage->setItemText(0, tr("Auto-detect"));
    m_lblInitialPrompt->setText(tr("Personal vocabulary:"));
    m_initialPromptEdit->setPlaceholderText(tr("e.g. Stuart, OptiTrack, TrackType"));
    m_chkAiCleanup->setText(tr("AI cleanup (Claude API)"));
    m_lblClaudeApiKey->setText(tr("Claude API key:"));
    m_claudeApiKeyEdit->setPlaceholderText(tr("sk-ant-…"));
    m_lblInjectMode->setText(tr("Type text by:"));
    m_cmbInjectMode->setItemText(0, tr("Simulated keystrokes"));
    m_cmbInjectMode->setItemText(1, tr("Clipboard paste"));
    m_chkInjectPartials->setText(tr("Type partial results live"));
    m_chkAutoFormat->setText(tr("Auto spacing && capitalization"));
    m_chkReviewMode->setText(tr("Review before injecting"));
    m_lblHotkey->setText(tr("Dictation hotkey:"));
    m_chkPushToTalk->setText(tr("Hold to talk (push-to-talk)"));
    m_lblUndoHotkey->setText(tr("Undo dictation hotkey:"));
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
    auto* versionLbl = new QLabel("Version 0.9.0 (build " TC_STR(BUILD_NUMBER) ")");
#else
    auto* versionLbl = new QLabel("Version 0.9.0");
#endif
    versionLbl->setStyleSheet(
        "color: #666666; font-size: 11px; background: transparent;");
    nameLay->addWidget(appNameLbl);
    nameLay->addWidget(versionLbl);
    hLay->addLayout(nameLay);
    hLay->addStretch(1);

    root->addWidget(header);

    // ── Tab pages ─────────────────────────────────────────────
    // "wfl" is the Window page form, "dfl" the Dictation page form; each lives in
    // its own QWidget added to the tab bar (top) further below.  The Voice-commands
    // page is built later as a third tab.
    auto styleForm = [](QFormLayout* fl){
        fl->setSpacing(6);
        fl->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
        fl->setLabelAlignment(Qt::AlignHCenter);
    };
    auto* winPage  = new QWidget;
    auto* wfl      = new QFormLayout(winPage);
    styleForm(wfl);
    auto* dictPage = new QWidget;
    auto* dfl      = new QFormLayout(dictPage);
    styleForm(dfl);

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

    // Speech-to-text model selector (from the download catalog).
    m_lblSttModel = new QLabel(tr("Speech model:"));
    m_cmbSttModel = new QComboBox;
    m_cmbSttModel->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbSttModel->setMinimumWidth(130);
    for (const ModelInfo& mi : ModelManager::catalog())
        m_cmbSttModel->addItem(mi.label, mi.name);

    // Speech recognition language.  Only meaningful for multilingual models, so
    // it is disabled (forced to English) when an English-only model is selected.
    m_lblSttLanguage = new QLabel(tr("Speech language:"));
    m_cmbSttLanguage = new QComboBox;
    m_cmbSttLanguage->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbSttLanguage->setMinimumWidth(130);
    m_cmbSttLanguage->addItem(tr("Auto-detect"), "auto");
    m_cmbSttLanguage->addItem("English",    "en");
    m_cmbSttLanguage->addItem("Français",   "fr");
    m_cmbSttLanguage->addItem("Español",    "es");
    m_cmbSttLanguage->addItem("Deutsch",    "de");
    m_cmbSttLanguage->addItem("Italiano",   "it");
    m_cmbSttLanguage->addItem("Português",  "pt");
    m_cmbSttLanguage->addItem("Русский",    "ru");
    m_cmbSttLanguage->addItem("中文",        "zh");
    m_cmbSttLanguage->addItem("日本語",      "ja");
    m_cmbSttLanguage->addItem("한국어",      "ko");

    // Disable the language picker for English-only (.en) models.
    m_lblInitialPrompt = new QLabel(tr("Personal vocabulary:"));
    m_initialPromptEdit = new QLineEdit;
    m_initialPromptEdit->setPlaceholderText(tr("e.g. Stuart, OptiTrack, TrackType"));

    // AI cleanup
    m_chkAiCleanup    = new QCheckBox(tr("AI cleanup (Claude API)"));
    m_lblClaudeApiKey = new QLabel(tr("Claude API key:"));
    m_claudeApiKeyEdit = new QLineEdit;
    m_claudeApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_claudeApiKeyEdit->setPlaceholderText(tr("sk-ant-…"));
    connect(m_chkAiCleanup, &QCheckBox::toggled, m_lblClaudeApiKey,   &QWidget::setEnabled);
    connect(m_chkAiCleanup, &QCheckBox::toggled, m_claudeApiKeyEdit,  &QWidget::setEnabled);

    connect(m_cmbSttModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
        const ModelInfo mi = ModelManager::modelInfo(
            m_cmbSttModel->currentData().toString());
        m_cmbSttLanguage->setEnabled(mi.multilingual);
        if (!mi.multilingual) {
            const int en = m_cmbSttLanguage->findData("en");
            if (en >= 0) m_cmbSttLanguage->setCurrentIndex(en);
        }
    });

    // Text-injection mode + behaviour.
    m_lblInjectMode = new QLabel(tr("Type text by:"));
    m_cmbInjectMode = new QComboBox;
    m_cmbInjectMode->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_cmbInjectMode->setMinimumWidth(130);
    m_cmbInjectMode->addItem(tr("Simulated keystrokes"),
                             int(InjectionMode::Type));
    m_cmbInjectMode->addItem(tr("Clipboard paste"),
                             int(InjectionMode::ClipboardPaste));
    m_chkInjectPartials = new QCheckBox(tr("Type partial results live"));
    m_chkAutoFormat     = new QCheckBox(tr("Auto spacing && capitalization"));
    m_chkReviewMode     = new QCheckBox(tr("Review before injecting"));

    // Live partials only make sense with simulated keystrokes (paste cannot
    // retract); disable the option in clipboard-paste mode.
    connect(m_cmbInjectMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
        const bool typeMode = m_cmbInjectMode->currentData().toInt()
                              == int(InjectionMode::Type);
        m_chkInjectPartials->setEnabled(typeMode);
    });

    // Global dictation hotkey.
    m_lblHotkey = new QLabel(tr("Dictation hotkey:"));
    m_hotkeyEdit = new QKeySequenceEdit;   // only the first chord is used
    m_chkPushToTalk = new QCheckBox(tr("Hold to talk (push-to-talk)"));

    // Global undo hotkey.
    m_lblUndoHotkey  = new QLabel(tr("Undo dictation hotkey:"));
    m_undoHotkeyEdit = new QKeySequenceEdit;

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

    // Window page
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

    // Dictation page
    dfl->addRow(m_lblInputDevice, m_cmbInputDevice);
    dfl->addRow(m_lblSttModel,    m_cmbSttModel);
    dfl->addRow(m_lblSttLanguage,   m_cmbSttLanguage);
    dfl->addRow(m_lblInitialPrompt, m_initialPromptEdit);
    dfl->addRow(m_chkAiCleanup);
    dfl->addRow(m_lblClaudeApiKey, m_claudeApiKeyEdit);
    dfl->addRow(m_lblInjectMode,    m_cmbInjectMode);
    dfl->addRow(m_chkInjectPartials);
    dfl->addRow(m_chkAutoFormat);
    dfl->addRow(m_chkReviewMode);
    dfl->addRow(m_lblHotkey,      m_hotkeyEdit);
    dfl->addRow(m_chkPushToTalk);
    dfl->addRow(m_lblUndoHotkey,  m_undoHotkeyEdit);

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
    dfl->addRow(m_lblPermissions, m_btnAccessibility);
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

    // Language is a global control: it sits above the tabs, not inside a page.
    auto* langRow = new QHBoxLayout;
    langRow->addWidget(m_lblLanguage);
    langRow->addWidget(m_cmbLanguage);
    langRow->addStretch(1);
    root->addLayout(langRow);

    // ── Tabs (bar on top) ─────────────────────────────────────
    m_tabs = new QTabWidget;
    m_tabs->addTab(winPage,  tr("Window"));
    m_tabs->addTab(dictPage, tr("Dictation"));
    root->addWidget(m_tabs);

    // ── Voice commands tab ────────────────────────────────────
    auto* cmdPage = new QWidget;
    auto* cmdLay  = new QVBoxLayout(cmdPage);
    cmdLay->setSpacing(6);

    m_cmdTable = new QTableWidget(0, 2);
    m_cmdTable->setHorizontalHeaderLabels({tr("Spoken phrase"), tr("Output")});
    m_cmdTable->horizontalHeader()->setStretchLastSection(true);
    m_cmdTable->verticalHeader()->setVisible(false);
    m_cmdTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cmdTable->setMaximumHeight(140);
    cmdLay->addWidget(m_cmdTable);

    auto* cmdBtns = new QHBoxLayout;
    m_btnAddCmd    = new QPushButton(tr("Add"));
    m_btnRemoveCmd = new QPushButton(tr("Remove"));
    m_btnAddCmd->setProperty("flat", true);
    m_btnRemoveCmd->setProperty("flat", true);
    cmdBtns->addStretch(1);
    cmdBtns->addWidget(m_btnAddCmd);
    cmdBtns->addWidget(m_btnRemoveCmd);
    cmdLay->addLayout(cmdBtns);

    connect(m_btnAddCmd, &QPushButton::clicked, this, [this]{
        const int row = m_cmdTable->rowCount();
        m_cmdTable->insertRow(row);
        m_cmdTable->setItem(row, 0, new QTableWidgetItem);
        m_cmdTable->setItem(row, 1, new QTableWidgetItem);
        m_cmdTable->editItem(m_cmdTable->item(row, 0));
    });
    connect(m_btnRemoveCmd, &QPushButton::clicked, this, [this]{
        const int row = m_cmdTable->currentRow();
        if (row >= 0)
            m_cmdTable->removeRow(row);
    });

    m_tabs->addTab(cmdPage, tr("Voice commands"));

    // ── Substitutions tab ─────────────────────────────────────
    auto* subPage = new QWidget;
    auto* subLay  = new QVBoxLayout(subPage);
    subLay->setSpacing(6);

    m_subTable = new QTableWidget(0, 2);
    m_subTable->setHorizontalHeaderLabels({tr("Misheard"), tr("Intended")});
    m_subTable->horizontalHeader()->setStretchLastSection(true);
    m_subTable->verticalHeader()->setVisible(false);
    m_subTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_subTable->setMaximumHeight(140);
    subLay->addWidget(m_subTable);

    auto* subBtns = new QHBoxLayout;
    m_btnAddSub    = new QPushButton(tr("Add"));
    m_btnRemoveSub = new QPushButton(tr("Remove"));
    m_btnAddSub->setProperty("flat", true);
    m_btnRemoveSub->setProperty("flat", true);
    subBtns->addStretch(1);
    subBtns->addWidget(m_btnAddSub);
    subBtns->addWidget(m_btnRemoveSub);
    subLay->addLayout(subBtns);

    connect(m_btnAddSub, &QPushButton::clicked, this, [this]{
        const int row = m_subTable->rowCount();
        m_subTable->insertRow(row);
        m_subTable->setItem(row, 0, new QTableWidgetItem);
        m_subTable->setItem(row, 1, new QTableWidgetItem);
        m_subTable->editItem(m_subTable->item(row, 0));
    });
    connect(m_btnRemoveSub, &QPushButton::clicked, this, [this]{
        const int row = m_subTable->currentRow();
        if (row >= 0)
            m_subTable->removeRow(row);
    });

    m_tabs->addTab(subPage, tr("Substitutions"));

    // ── Profiles tab ──────────────────────────────────────────
    auto* profPage = new QWidget;
    auto* profLay  = new QVBoxLayout(profPage);
    profLay->setSpacing(6);

    // Master list — names only; tooltip shows the window pattern
    m_profileListWidget = new QListWidget;
    m_profileListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_profileListWidget->setMaximumHeight(110);
    profLay->addWidget(m_profileListWidget);

    auto* profListBtns = new QHBoxLayout;
    m_btnAddProfile    = new QPushButton(tr("Add profile"));
    m_btnRemoveProfile = new QPushButton(tr("Remove"));
    m_btnAddProfile->setProperty("flat", true);
    m_btnRemoveProfile->setProperty("flat", true);
    profListBtns->addStretch(1);
    profListBtns->addWidget(m_btnAddProfile);
    profListBtns->addWidget(m_btnRemoveProfile);
    profLay->addLayout(profListBtns);

    // Detail panel — enabled only when a profile is selected
    m_profileDetailGroup = new QGroupBox(tr("Profile details"));
    m_profileDetailGroup->setEnabled(false);
    auto* detailLay = new QVBoxLayout(m_profileDetailGroup);
    detailLay->setSpacing(4);

    auto* detailForm = new QFormLayout;
    detailForm->setSpacing(5);

    m_lblProfileName   = new QLabel(tr("Name:"));
    m_profileNameEdit  = new QLineEdit;
    m_profileNameEdit->setPlaceholderText(tr("e.g. Coding"));
    detailForm->addRow(m_lblProfileName, m_profileNameEdit);

    m_lblProfilePattern  = new QLabel(tr("Window pattern (regex):"));
    m_profilePatternEdit = new QLineEdit;
    m_profilePatternEdit->setPlaceholderText(tr("e.g. .*Code.*|.*Visual Studio.*"));
    detailForm->addRow(m_lblProfilePattern, m_profilePatternEdit);

    m_lblProfileVocab  = new QLabel(tr("Vocabulary hint:"));
    m_profileVocabEdit = new QLineEdit;
    m_profileVocabEdit->setPlaceholderText(tr("e.g. camelCase, nullptr, std::vector"));
    detailForm->addRow(m_lblProfileVocab, m_profileVocabEdit);

    detailLay->addLayout(detailForm);

    // Per-profile substitution mini-table
    m_profileSubTable = new QTableWidget(0, 2);
    m_profileSubTable->setHorizontalHeaderLabels({tr("Misheard"), tr("Intended")});
    m_profileSubTable->horizontalHeader()->setStretchLastSection(true);
    m_profileSubTable->verticalHeader()->setVisible(false);
    m_profileSubTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_profileSubTable->setMaximumHeight(110);
    detailLay->addWidget(m_profileSubTable);

    auto* profSubBtns    = new QHBoxLayout;
    m_btnAddProfileSub    = new QPushButton(tr("Add"));
    m_btnRemoveProfileSub = new QPushButton(tr("Remove"));
    m_btnAddProfileSub->setProperty("flat", true);
    m_btnRemoveProfileSub->setProperty("flat", true);
    profSubBtns->addStretch(1);
    profSubBtns->addWidget(m_btnAddProfileSub);
    profSubBtns->addWidget(m_btnRemoveProfileSub);
    detailLay->addLayout(profSubBtns);

    profLay->addWidget(m_profileDetailGroup);
    profLay->addStretch(1);

    m_tabs->addTab(profPage, tr("Profiles"));

    // ── Profile list interactions ─────────────────────────────
    connect(m_btnAddProfile, &QPushButton::clicked, this, [this]{
        DictationProfile p;
        p.name = tr("New profile");
        m_profiles.append(p);
        auto* item = new QListWidgetItem(p.name);
        item->setToolTip(p.windowPattern);
        m_profileListWidget->addItem(item);
        m_profileListWidget->setCurrentItem(item);
    });
    connect(m_btnRemoveProfile, &QPushButton::clicked, this, [this]{
        const int row = m_profileListWidget->currentRow();
        if (row < 0 || row >= m_profiles.size()) return;
        m_profiles.removeAt(row);
        delete m_profileListWidget->takeItem(row);
        // After removal, selection moves to whatever Qt picked — reload detail.
        loadProfileDetail(m_profileListWidget->currentRow());
    });

    connect(m_profileListWidget, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current, QListWidgetItem* previous) {
            if (previous) {
                // Save the departing profile's edits before loading the new one.
                const int prevRow = m_profileListWidget->row(previous);
                if (prevRow >= 0 && prevRow < m_profiles.size()) {
                    m_profiles[prevRow] = readProfileDetail();
                    QSignalBlocker bl(m_profileListWidget);
                    const QString name = m_profiles[prevRow].name;
                    previous->setText(name.isEmpty() ? tr("(unnamed)") : name);
                    previous->setToolTip(m_profiles[prevRow].windowPattern);
                }
            }
            loadProfileDetail(current ? m_profileListWidget->row(current) : -1);
        });

    connect(m_btnAddProfileSub, &QPushButton::clicked, this, [this]{
        const int r = m_profileSubTable->rowCount();
        m_profileSubTable->insertRow(r);
        m_profileSubTable->setItem(r, 0, new QTableWidgetItem);
        m_profileSubTable->setItem(r, 1, new QTableWidgetItem);
        m_profileSubTable->editItem(m_profileSubTable->item(r, 0));
    });
    connect(m_btnRemoveProfileSub, &QPushButton::clicked, this, [this]{
        const int r = m_profileSubTable->currentRow();
        if (r >= 0) m_profileSubTable->removeRow(r);
    });

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

    const int modIdx = m_cmbSttModel->findData(s.sttModel);
    m_cmbSttModel->setCurrentIndex(modIdx >= 0 ? modIdx : 0);

    const ModelInfo mi = ModelManager::modelInfo(s.sttModel);
    m_cmbSttLanguage->setEnabled(mi.multilingual);
    const int langIdx = m_cmbSttLanguage->findData(
        mi.multilingual ? s.sttLanguage : QStringLiteral("en"));
    m_cmbSttLanguage->setCurrentIndex(langIdx >= 0 ? langIdx : 0);

    m_initialPromptEdit->setText(s.initialPrompt);

    m_chkAiCleanup->setChecked(s.aiCleanupEnabled);
    m_claudeApiKeyEdit->setText(s.claudeApiKey);
    m_claudeApiKeyEdit->setEnabled(s.aiCleanupEnabled);
    m_lblClaudeApiKey->setEnabled(s.aiCleanupEnabled);

    const int modeIdx = m_cmbInjectMode->findData(int(s.injectionMode));
    m_cmbInjectMode->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);
    m_chkInjectPartials->setChecked(s.injectPartials);
    m_chkInjectPartials->setEnabled(s.injectionMode == InjectionMode::Type);
    m_chkAutoFormat->setChecked(s.autoFormat);
    m_chkReviewMode->setChecked(s.reviewBeforeInjecting);

    m_hotkeyEdit->setKeySequence(QKeySequence(s.hotkey));
    m_chkPushToTalk->setChecked(s.hotkeyPushToTalk);
    m_undoHotkeyEdit->setKeySequence(QKeySequence(s.undoHotkey));

    m_cmdTable->setRowCount(0);
    for (auto it = s.commands.constBegin(); it != s.commands.constEnd(); ++it) {
        const int row = m_cmdTable->rowCount();
        m_cmdTable->insertRow(row);
        m_cmdTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_cmdTable->setItem(row, 1, new QTableWidgetItem(escapeForEditor(it.value())));
    }

    m_subTable->setRowCount(0);
    for (auto it = s.substitutions.constBegin(); it != s.substitutions.constEnd(); ++it) {
        const int row = m_subTable->rowCount();
        m_subTable->insertRow(row);
        m_subTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_subTable->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    for (int i = 0; i < m_cmbLanguage->count(); ++i) {
        if (m_cmbLanguage->itemData(i).toString() == s.language) {
            m_cmbLanguage->setCurrentIndex(i);
            break;
        }
    }

    // Profiles
    m_profiles = s.profiles;
    {
        QSignalBlocker bl(m_profileListWidget);
        m_profileListWidget->clear();
        for (const DictationProfile& p : m_profiles) {
            auto* item = new QListWidgetItem(p.name.isEmpty() ? tr("(unnamed)") : p.name);
            item->setToolTip(p.windowPattern);
            m_profileListWidget->addItem(item);
        }
        m_profileListWidget->setCurrentRow(-1);
    }
    loadProfileDetail(-1);
}

// ── Profile master-detail helpers ─────────────────────────────────────────────

DictationProfile SettingsDialog::readProfileDetail() const
{
    DictationProfile p;
    p.name         = m_profileNameEdit->text().trimmed();
    p.windowPattern= m_profilePatternEdit->text().trimmed();
    p.vocabularyHint = m_profileVocabEdit->text().trimmed();
    for (int r = 0; r < m_profileSubTable->rowCount(); ++r) {
        const QTableWidgetItem* k = m_profileSubTable->item(r, 0);
        const QTableWidgetItem* v = m_profileSubTable->item(r, 1);
        const QString key = k ? k->text().trimmed() : QString();
        if (!key.isEmpty())
            p.substitutions.insert(key, v ? v->text().trimmed() : QString());
    }
    return p;
}

void SettingsDialog::loadProfileDetail(int row)
{
    const bool valid = row >= 0 && row < m_profiles.size();
    m_profileDetailGroup->setEnabled(valid);
    QSignalBlocker blkName(m_profileNameEdit);
    QSignalBlocker blkPat(m_profilePatternEdit);
    QSignalBlocker blkVoc(m_profileVocabEdit);
    QSignalBlocker blkTbl(m_profileSubTable);
    if (!valid) {
        m_profileNameEdit->clear();
        m_profilePatternEdit->clear();
        m_profileVocabEdit->clear();
        m_profileSubTable->setRowCount(0);
        return;
    }
    const DictationProfile& p = m_profiles.at(row);
    m_profileNameEdit->setText(p.name);
    m_profilePatternEdit->setText(p.windowPattern);
    m_profileVocabEdit->setText(p.vocabularyHint);
    m_profileSubTable->setRowCount(0);
    for (auto it = p.substitutions.constBegin(); it != p.substitutions.constEnd(); ++it) {
        const int r = m_profileSubTable->rowCount();
        m_profileSubTable->insertRow(r);
        m_profileSubTable->setItem(r, 0, new QTableWidgetItem(it.key()));
        m_profileSubTable->setItem(r, 1, new QTableWidgetItem(it.value()));
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
    s.sttModel         = m_cmbSttModel->currentData().toString();
    // English-only models pin the language to "en" (the picker is disabled).
    s.sttLanguage      = m_cmbSttLanguage->isEnabled()
        ? m_cmbSttLanguage->currentData().toString()
        : QStringLiteral("en");
    s.injectionMode    = InjectionMode(m_cmbInjectMode->currentData().toInt());
    s.injectPartials   = m_chkInjectPartials->isChecked()
                         && (s.injectionMode == InjectionMode::Type);
    s.autoFormat              = m_chkAutoFormat->isChecked();
    s.reviewBeforeInjecting   = m_chkReviewMode->isChecked();
    s.initialPrompt    = m_initialPromptEdit->text().trimmed();
    s.aiCleanupEnabled = m_chkAiCleanup->isChecked();
    s.claudeApiKey     = m_claudeApiKeyEdit->text().trimmed();
    s.hotkey           = m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
    s.hotkeyPushToTalk = m_chkPushToTalk->isChecked();
    s.undoHotkey       = m_undoHotkeyEdit->keySequence().toString(QKeySequence::PortableText);

    QMap<QString, QString> cmds;
    for (int row = 0; row < m_cmdTable->rowCount(); ++row) {
        const QTableWidgetItem* p = m_cmdTable->item(row, 0);
        const QTableWidgetItem* o = m_cmdTable->item(row, 1);
        const QString phrase = p ? p->text().trimmed().toLower() : QString();
        if (phrase.isEmpty())
            continue;
        cmds.insert(phrase, o ? unescapeFromEditor(o->text()) : QString());
    }
    s.commands         = cmds;

    QMap<QString, QString> subs;
    for (int row = 0; row < m_subTable->rowCount(); ++row) {
        const QTableWidgetItem* k = m_subTable->item(row, 0);
        const QTableWidgetItem* v = m_subTable->item(row, 1);
        const QString misheard = k ? k->text().trimmed() : QString();
        if (misheard.isEmpty())
            continue;
        subs.insert(misheard, v ? v->text().trimmed() : QString());
    }
    s.substitutions    = subs;

    // Profiles: start from the working copy; override the currently-selected
    // row with whatever is live in the detail panel (the user may not have
    // switched away, so currentItemChanged hasn't saved it yet).
    s.profiles = m_profiles;
    const int profileRow = m_profileListWidget->currentRow();
    if (profileRow >= 0 && profileRow < s.profiles.size()) {
        s.profiles[profileRow] = readProfileDetail();
        const QString nm = s.profiles[profileRow].name;
        if (auto* item = m_profileListWidget->item(profileRow))
            const_cast<QListWidgetItem*>(item)->setText(nm.isEmpty() ? tr("(unnamed)") : nm);
    }

    s.language         = m_cmbLanguage->currentData().toString();
    return s;
}

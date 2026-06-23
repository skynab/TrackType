#pragma once
#include <QComboBox>
#include <QDialog>
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMap>

QT_BEGIN_NAMESPACE
class QTableWidget;
class QTabWidget;
QT_END_NAMESPACE
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTranslator>

enum class EdgeLock { None, Left, Right };

// How recognized text is injected into the focused window.
enum class InjectionMode { Type, ClipboardPaste };

struct AppSettings {
    // Window
    double windowOpacity   = 1.0;
    bool   alwaysOnTop     = true;
    bool   startMinimized  = false;
    bool   xMinimizesApp   = false;
    bool   launchOnStartup = false;

    // Audio feedback
    bool audioFeedback = false;

    // Capture microphone: persisted AudioCapture device id ("" = system default)
    QString inputDevice;

    // Speech-to-text: model file (catalog key) and recognition language
    // (whisper code such as "en"/"fr", or "auto").
    QString sttModel    = "ggml-base.en.bin";
    QString sttLanguage = "en";

    // Text injection of recognized speech.
    InjectionMode injectionMode = InjectionMode::Type;
    bool injectPartials         = false; // type live partials (Type mode) vs. only finals
    bool autoFormat             = true;  // auto spacing + sentence capitalization
    bool reviewBeforeInjecting  = false; // hold finals for review before typing them

    // Global dictation hotkey.
    QString hotkey;                // e.g. "Ctrl+Alt+D" ("" = none)
    bool    hotkeyPushToTalk = false;  // hold-to-talk vs. press-to-toggle

    // Global undo hotkey — erases the last injected segment.
    QString undoHotkey;            // e.g. "Ctrl+Alt+Z" ("" = none)

    // Voice commands: spoken phrase → output text (the "{|}" marker positions the
    // caret).  Empty means "use the built-in defaults".
    QMap<QString, QString> commands;

    // Substitutions: misheard word/phrase → intended replacement.
    // Applied after the command check but before auto-formatting.
    QMap<QString, QString> substitutions;

    // Personal vocabulary hint passed to Whisper as initial_prompt to bias
    // recognition toward uncommon proper nouns, names, and domain terms.
    QString initialPrompt;

    // Language (ISO code: "en", "fr", "es", "zh_CN", "ja", "ko", …)
    QString language = "en";

    // Edge lock / hide
    EdgeLock edgeLock = EdgeLock::None;
    bool     edgeHide = false;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // appTranslator — the translator currently installed by MainWindow (may be
    // nullptr for English).  The dialog removes it from qApp for its lifetime
    // so that preview translators have sole control, then restores it on close.
    explicit SettingsDialog(const AppSettings& current,
                            QTranslator* appTranslator = nullptr,
                            QWidget* parent = nullptr);
    AppSettings settings() const { return m_settings; }

protected:
    void changeEvent(QEvent* e) override;
    void done(int result) override;

private:
    void buildUi();
    void loadFrom(const AppSettings& s);
    AppSettings readUi() const;
    void retranslateUi();
    void applyLanguagePreview(const QString& lang);
    void cleanupPreviewTranslator();
#ifdef Q_OS_LINUX
    // When no on-screen keyboard is installed, offer to install one through the
    // system package manager (with a graphical pkexec password prompt) rather
    // than just reporting that none was found.
    void promptInstallOnScreenKeyboard();
#endif

    AppSettings  m_settings;
    QTranslator* m_previewTranslator = nullptr;
    QTranslator* m_appTranslator     = nullptr; // borrowed from MainWindow

    // ── Settings tabs (tab labels need retranslation) ─────────
    QTabWidget* m_tabs = nullptr;

    // Voice-commands editor.
    QTableWidget* m_cmdTable     = nullptr;
    QPushButton*  m_btnAddCmd    = nullptr;
    QPushButton*  m_btnRemoveCmd = nullptr;

    // Substitutions editor.
    QTableWidget* m_subTable     = nullptr;
    QPushButton*  m_btnAddSub    = nullptr;
    QPushButton*  m_btnRemoveSub = nullptr;

    // ── Form-row labels (need retranslation) ──────────────────
    QLabel* m_lblOpacity  = nullptr;
    QLabel* m_lblLanguage = nullptr;
#ifdef Q_OS_MAC
    QLabel*      m_lblPermissions   = nullptr;
    QPushButton* m_btnAccessibility = nullptr;
#endif

    // Window
    QSlider*     m_opacitySlider;
    QLabel*      m_opacityLabel;
    QCheckBox*   m_chkAlwaysOnTop;
    QCheckBox*   m_chkStartMinimized;
    QCheckBox*   m_chkXMinimizesApp;
    QCheckBox*   m_chkLaunchOnStartup;
    QCheckBox*   m_chkAudio;
    QLabel*      m_lblInputDevice = nullptr;
    QComboBox*   m_cmbInputDevice = nullptr;
    QLabel*      m_lblSttModel        = nullptr;
    QComboBox*   m_cmbSttModel        = nullptr;
    QLabel*      m_lblSttLanguage     = nullptr;
    QComboBox*   m_cmbSttLanguage     = nullptr;
    QLabel*      m_lblInitialPrompt   = nullptr;
    QLineEdit*   m_initialPromptEdit  = nullptr;
    QLabel*      m_lblInjectMode  = nullptr;
    QComboBox*   m_cmbInjectMode  = nullptr;
    QCheckBox*   m_chkInjectPartials    = nullptr;
    QCheckBox*   m_chkAutoFormat        = nullptr;
    QCheckBox*   m_chkReviewMode        = nullptr;
    QLabel*           m_lblHotkey      = nullptr;
    QKeySequenceEdit* m_hotkeyEdit     = nullptr;
    QCheckBox*        m_chkPushToTalk  = nullptr;
    QLabel*           m_lblUndoHotkey  = nullptr;
    QKeySequenceEdit* m_undoHotkeyEdit = nullptr;
    QComboBox*   m_cmbLanguage;

    // Edge lock
    QLabel*    m_lblEdgeLock = nullptr;
    QComboBox* m_cmbEdgeLock = nullptr;
    QCheckBox* m_chkEdgeHide = nullptr;

    QDialogButtonBox* m_buttons;
    QPushButton*      m_resetBtn       = nullptr;
    QPushButton*      m_btnOnScreenKbd = nullptr;
};

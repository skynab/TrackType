#pragma once
#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QPropertyAnimation>
#include <QAction>
#include <QSettings>
#ifdef HAVE_MULTIMEDIA
#  include <QSoundEffect>
#endif
#include <QTranslator>
#include "textinjector.h"
#include "textnormalizer.h"
#include "settingsdialog.h"

class AudioCapture;
class LevelMeter;
class WhisperSttEngine;
class GlobalHotkey;
class TranscriptPreview;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QTranslator* startupTranslator = nullptr, QWidget* parent = nullptr);
    ~MainWindow() override = default;

    // On Linux/Wayland, if TrackType cannot read pointer-motion devices, offer
    // a one-time GUI prompt to install the udev rule that grants access (via a
    // graphical polkit password dialog — no terminal required).  No-op on other
    // platforms and when access is already available.
    void promptForInputAccessIfNeeded();

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void paintEvent(QPaintEvent*)      override;
    void closeEvent(QCloseEvent*)      override;
    void changeEvent(QEvent*)          override;
    void showEvent(QShowEvent*)        override;

private slots:
    void onDictateToggled(bool on);   // primary toggle: run/stop the dictation pipeline
    void onSettingsClicked();
    void onExitClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason);
    void applySettings(const AppSettings& s);
    void onEdgePoll();

private:
    void buildUi();
    void buildTray();
    void initAudio();
    void initStt();
    void ensureModelThenStart();

    // Inject recognized text into the focused window (applies normalization and,
    // in live-partial mode, retracts the previous partial first).
    void onSttPartial(const QString& raw);
    void onSttFinal(const QString& raw);
    void applyInjectionSettings();

    // STT status surfaced in the toolbar (a coloured dot + tooltip) and tray.
    enum class SttState { Idle, Busy, Listening, Transcribing, Injecting, Paused, Error };
    void setSttStatus(SttState state, const QString& text);

    void setupHotkey();          // (re)register the global dictation hotkey
    void onHotkeyPressed();
    void onHotkeyReleased();
    void togglePause();          // stop injecting but keep the engine warm
    void undoLastInjection();    // backspace the last committed text
    void playCue();              // optional start/stop audio cue
    void saveWindowSettings();
    void loadWindowSettings();
    void retranslateUi();
    void installLanguage(const QString& lang);
    // Re-assert the launch-on-startup registration at launch so it self-heals if
    // the entry was removed externally or the executable path changed.
    void syncLaunchOnStartup();
    void applyEdgeLock();
    void animateEdgeTo(QPoint target);

    // ── Resize helpers ────────────────────────────────────────────
    enum class ResizeEdge {
        None,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };
    ResizeEdge edgeAt(QPoint pos) const;
    static Qt::CursorShape cursorForEdge(ResizeEdge e);
    static constexpr int RESIZE_MARGIN = 10;

    // ── UI elements ───────────────────────────────────────────
    QWidget*      m_titleBar   = nullptr;
    QLabel*       m_titleIcon  = nullptr;
    QLabel*       m_titleLabel = nullptr;
    QPushButton*  m_dictateBtn = nullptr;   // primary "Dictate" toggle
    QPushButton*  m_settingsBtn= nullptr;
    QPushButton*  m_exitBtn    = nullptr;
    LevelMeter*   m_levelMeter = nullptr;
    QLabel*       m_statusDot  = nullptr;

    // ── State ─────────────────────────────────────────────────
    bool        m_dragging     = false;  // window drag in progress
    QPoint      m_dragOffset;            // for window dragging
    ResizeEdge  m_resizeEdge  = ResizeEdge::None;
    QPoint      m_resizeStart;           // global pos when resize began
    QRect       m_resizeGeo;             // window geometry when resize began

    // ── Edge lock / slide-hide ─────────────────────────────────
    QTimer*             m_edgePollTimer   = nullptr;
    QPropertyAnimation* m_edgeAnim        = nullptr;
    bool                m_edgeShown       = true;
    int                 m_edgeHideCountMs = 0;

    // ── Sub-objects ───────────────────────────────────────────
    AudioCapture*     m_audio      = nullptr;
    WhisperSttEngine* m_stt        = nullptr;
    GlobalHotkey*     m_hotkey     = nullptr;
    TranscriptPreview* m_preview   = nullptr;
    TextNormalizer    m_normalizer;          // spacing/capitalization of segments
    int               m_pendingPartialLen = 0; // chars of the live partial on screen
    int               m_lastInjectedLen   = 0; // chars of the last committed final
    bool              m_paused            = false;
    QSystemTrayIcon*  m_tray       = nullptr;
    QMenu*            m_trayMenu   = nullptr;
    QAction*          m_showAct    = nullptr;
    QAction*          m_dictateAct = nullptr;
    QAction*          m_pauseAct   = nullptr;
    QAction*          m_undoAct    = nullptr;
    QAction*          m_settingsAct= nullptr;
    QAction*          m_quitAct    = nullptr;
    QTranslator*      m_translator = nullptr;
#ifdef HAVE_MULTIMEDIA
    QSoundEffect*     m_clickSound = nullptr;
#endif

    AppSettings   m_settings;
    QSettings     m_persist;
};

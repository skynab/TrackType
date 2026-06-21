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
#include "clickinjector.h"
#include "settingsdialog.h"

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
    void onSettingsClicked();
    void onExitClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason);
    void applySettings(const AppSettings& s);
    void onEdgePoll();

private:
    void buildUi();
    void buildTray();
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
    QPushButton*  m_settingsBtn= nullptr;
    QPushButton*  m_exitBtn    = nullptr;

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
    QSystemTrayIcon*  m_tray       = nullptr;
    QMenu*            m_trayMenu   = nullptr;
    QAction*          m_showAct    = nullptr;
    QAction*          m_quitAct    = nullptr;
    QTranslator*      m_translator = nullptr;
#ifdef HAVE_MULTIMEDIA
    QSoundEffect*     m_clickSound = nullptr;
#endif

    AppSettings   m_settings;
    QSettings     m_persist;
};

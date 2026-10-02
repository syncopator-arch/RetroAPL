/*
 * main.cpp — APL Terminal Qt frontend, integrated with libapl backend.
 *
 * Threading model:
 *   Main thread  : Qt event loop, all widget operations.
 *   Engine thread: APL backend (apl_engine_thread). Blocks in alt_getin()
 *                  waiting on g_inputCond. Output callbacks post back to the
 *                  main thread via QMetaObject::invokeMethod (AutoConnection →
 *                  queued when called from a non-GUI thread).
 *
 * Platform registration:
 *   APLMainWindow::startEngine() builds a PlatformFuncs struct whose function
 *   pointers are plain C functions (below) that capture the widget pointer via
 *   a module-level global (g_terminal). This avoids C++ lambda/closure issues
 *   with the C struct.
 *
 * Input bridge:
 *   terminal emits lineEntered(QString) → APLMainWindow::handleLineEntered()
 *   converts to a raw byte buffer (font indices, null-terminated) and pushes it
 *   onto g_inputQueue, then wakes alt_getin() via g_inputCond.
 *   alt_getin() is implemented here (it is declared extern in platform.h and
 *   called by the backend's NO_INPUT_PROC path in lineproc.c).
 *
 * Settings:
 *   Persistent settings (keyboard layout, font size, background brightness,
 *   dark mode, window geometry) are stored in $APLHOME/settings.ini via
 *   QSettings (IniFormat).  Loaded on startup, saved from the Settings dialog
 *   (OK or Apply), and window geometry updated on close.
 *
 * File menu:
 *   In terminal mode: New Function, Open Function, ── , Print..., ── ,
 *                     Quit APL.
 *   In edit mode:     Save Function, Save As, ── , Compile & Return,
 *                     Discard & Return, ── , Print..., ── , Quit APL.
 *   The two sets are mutually exclusive; separators toggle with their group.
 *   Print... is always visible in both modes and is unaffected by that
 *   toggle -- it prints whatever the shared APLTerminalWidget currently
 *   displays (terminal transcript or function source, whichever mode is
 *   active).
 *
 * Printing:
 *   Always renders black text on white paper using the loaded APL font
 *   (kapl.ttf), regardless of the on-screen background-brightness/dark-mode
 *   setting. Font size defaults to the terminal's current on-screen size.
 *   A small options dialog (orientation, shrink-to-fit, printer-vs-PDF
 *   destination, optional print preview) is shown first, pre-filled with
 *   whatever was chosen last time (persisted in settings.ini alongside
 *   the other settings); APLTerminalWidget::createPrintDocument() then
 *   clones the widget's QTextDocument with colour/font overridden, and
 *   manual pagination (see APLMainWindow::renderPrintJob()) lays it out
 *   page by page.
 */

#include <algorithm>

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QAbstractButton>
#include <QStatusBar>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QMetaObject>
#include <QByteArray>
#include <QQueue>
#include <QVector>
#include <QCloseEvent>
/* Settings dialog */
#include <QDialog>
#include <QSettings>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextCursor>
#include <QRegularExpression>
#include <QSplitter>
#include <QListWidget>
#include <QScrollBar>
/* Printing */
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPageLayout>
#include <QMarginsF>
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QStringList>

#include "APLTerminalWidget.h"
#include "graphmanager.h"

/* ── Backend C interface ────────────────────────────────────────────── */
/*
 * Include order mirrors main.c exactly — depend.h must come first as it
 * defines UCHAR, PTF, ws_off_t and other types that externs.h depends on.
 * parse.h and mplmem.h define structs referenced in externs.h.
 * All wrapped in extern "C" so the C++ compiler treats them as C linkage.
 */
extern "C" {
#include "depend.h"     /* UCHAR, PTF, ws_off_t, GLOBARGC, etc. */
#include "parse.h"      /* struct FuncEnt, MemEnt, parse types  */
#include "mplmem.h"     /* workspace memory types               */
#include "globals.h"    /* ABORTSIG, g_abortsig */
#include "externs.h"    /* set_width, all backend externs */
#include "platform.h"   /* PlatformFuncs, platform_register(), platform_defaults() */
}

extern "C" const char *get_version(void);
QString g_appTitle;   // e.g. "APL 4.1.2.1" -- see BACKEND_VERSION/FRONTEND_VERSION in depend.h

/* ── Module-level globals shared between C callbacks and Qt slots ──── */
static APLTerminalWidget *g_terminal = nullptr;
static bool g_suppressNextPrompt = false;

/* ── CTRL bridge (⎕CTRL) ─────────────────────────────────────────────
 *
 * Minimize/restore/maximize the main terminal window, called from
 * sysfuncs.c's CTRL case (engine thread) via the extern "C" declaration
 * in externs.h. Folded in here rather than a separate term_bridge.h/
 * .cpp pair (compare graph_bridge.h/.cpp): this is one small function
 * operating on g_terminal, which is already static/internal-linkage to
 * this file -- a separate pair would either need g_terminal promoted to
 * extern (widening its visibility for no real benefit) or would just be
 * a thin forwarder to logic that's still conceptually main.cpp's own
 * business. Revisit only if this ever grows well past its current
 * single-function scope -- see apl_graphics_other_ideas_notes_v1.md's
 * CTRL section for the fuller reasoning, and for the other ideas
 * (programmatic exit, error-trap config) deliberately kept out of this
 * scope rather than folded in alongside it.
 *
 * Fire-and-forget (Qt::QueuedConnection) -- no result the engine thread
 * needs back, same category as the graphics module's own window-state-
 * only calls (setPen/clear/etc.), not gw_size()'s blocking query
 * category. mode is validated in sysfuncs.c before this is ever called
 * (0/1/2 only, ERR_DOMAIN otherwise); an out-of-range value reaching
 * here regardless is just a silent no-op, not a second validation pass.
 *
 * Single Qt6 implementation, no platform-specific code needed:
 * showMinimized()/showNormal()/showMaximized() are the same call on
 * every platform Qt6 targets here (Linux and Windows) -- unlike the old
 * sys_ctrl() this replaces (Windows-specific, removed), there's no
 * platform split to speak of anymore. */
extern "C" void sys_ctrl(long mode)
{
    if (!g_terminal) return;
    QWidget *win = g_terminal->window();
    if (!win) return;
    QMetaObject::invokeMethod(g_terminal, [win, mode]() {
        switch (mode) {
            case 0: win->showMinimized(); break;
            case 1: win->showNormal();    break;
            case 2: win->showMaximized(); break;
            default: break;   /* sysfuncs.c already validates; defensive only */
        }
    }, Qt::QueuedConnection);
}

/* ── Font list bridge ()FONT) ──────────────────────────────────────────
 *
 * Supplies commands.c's )FONT with the font families graphics windows
 * can use (WFONT's face selection). Same link-time hook arrangement as
 * sys_ctrl() above: declared in externs.h, defined here for the Qt
 * build, with a placeholder returning -1 in apl_cli and stub_test.
 *
 * Writes the names into buf as NUL-terminated strings back to back,
 * whole names only -- stops at the first name that won't fit rather
 * than truncating one -- and returns how many were written. Pattern
 * filtering, sorting and layout are commands.c's job; this only fetches.
 * Returns -1 before the terminal exists (not reachable in practice,
 * since no command can run before then).
 *
 * Every family is listed except private ones (QFontDatabase::
 * isPrivateFamily() -- e.g. macOS's internal "."-prefixed system fonts).
 * No language/script filtering, deliberately: a filter based on Qt's
 * per-family writing-system coverage was tried and removed. Most script-
 * specific fonts (Thai, Malayalam, Khmer, ...) also contain Latin
 * letters, so a "covers Latin" test let them through, while KAPL itself
 * failed it (Qt/fontconfig's Latin test wants accented letters KAPL
 * probably lacks). A tighter "Latin plus only one or two other scripts"
 * guess still missed fonts whose scripts Qt doesn't report. The fonts
 * this APL cares about most -- KAPL, APL Unicode fonts -- were exactly
 * the ones coverage filtering got wrong, so )FONT's pattern argument
 * does the narrowing instead.
 *
 * Blocking (Qt::BlockingQueuedConnection) onto the GUI thread, the same
 * category as the graphics module's queries: it has a result the engine
 * thread needs, and running the font database on the GUI thread
 * matches how every other Qt query here is made, rather than relying on
 * QFontDatabase being safe to call from the engine thread.
 *
 * Converted with toLatin1(), the same conversion every other string
 * crossing this boundary uses; a name with characters beyond Latin-1
 * would come out with '?' in it, and couldn't be selected by WFONT
 * either. */
extern "C" long sys_font_families(char *buf, long bufsize)
{
    if (!buf || bufsize <= 0 || !g_terminal) return -1;

    QStringList families;
    QMetaObject::invokeMethod(g_terminal, [&families]() {
        const QStringList all = QFontDatabase::families();
        for (const QString &f : all)
            if (!QFontDatabase::isPrivateFamily(f)) families.append(f);
    }, Qt::BlockingQueuedConnection);

    long count = 0;
    long used = 0;
    for (const QString &f : families) {
        QByteArray bytes = f.toLatin1();
        long n = bytes.size();
        if (n == 0) continue;
        if (used + n + 1 > bufsize) break;
        const char *src = bytes.constData();
        for (long i = 0; i < n; ++i) buf[used++] = src[i];
        buf[used++] = '\0';
        ++count;
    }
    return count;
}

/*
 * Startup workspace name and ⎕ARGS pass-through values, populated from the
 * command line in main() before the engine thread starts. These must live
 * for the whole process (not just be locals in main()) because GLOBARGV's
 * strings are read by ⎕ARGS at arbitrary points during the session, not
 * just at startup. g_startupArgPtrs points into g_startupArgStorage, which
 * owns the actual bytes -- keep the two in sync if this is ever touched
 * again after main() populates them once.
 */
static QByteArray         g_startupWsName;
static QVector<QByteArray> g_startupArgStorage;
static QVector<char*>      g_startupArgPtrs;

/*
 * Input queue.
 * The engine thread blocks in g_inputCond.wait() between lines.
 * The main thread pushes a raw byte buffer and wakes it.
 */
static QMutex             g_inputMutex;
static QWaitCondition     g_inputCond;
static QQueue<QByteArray> g_inputQueue;
static bool               g_engineRunning = false;
static QMutex             g_editMutex;
static QWaitCondition     g_editCond;
static bool               g_editActive = false;

/* ── Dark-mode stylesheet ────────────────────────────────────────────
 *
 * Targets specific widget types — intentionally excludes QTextEdit so the
 * terminal widget's own QPalette (set by setBackgroundBrightness) governs its
 * background and text colour independently of the chrome theme.
 */
static const QString kDarkStyleSheet = QStringLiteral(
    "QMainWindow, QDialog, QMessageBox {"
    "    background-color: #2b2b2b; color: #e0e0e0; }"
    "QMenuBar {"
    "    background-color: #383838; color: #e0e0e0; }"
    "QMenuBar::item:selected { background-color: #505050; }"
    "QMenu {"
    "    background-color: #383838; color: #e0e0e0;"
    "    border: 1px solid #555555; }"
    "QMenu::item:selected { background-color: #505050; }"
    "QMenu::separator { height: 1px; background: #555555; margin: 2px 0; }"
    "QStatusBar { background-color: #383838; color: #e0e0e0; }"
    "QGroupBox {"
    "    color: #e0e0e0; border: 1px solid #555555;"
    "    border-radius: 4px; margin-top: 1.2ex; padding-top: 6px; }"
    "QGroupBox::title {"
    "    color: #e0e0e0; subcontrol-origin: margin;"
    "    subcontrol-position: top left; padding: 0 4px; }"
    "QLabel { color: #e0e0e0; background-color: transparent; }"
    "QRadioButton { color: #e0e0e0; background-color: transparent; }"
    "QCheckBox  { color: #e0e0e0; background-color: transparent; }"
    "QComboBox {"
    "    background-color: #484848; color: #e0e0e0;"
    "    border: 1px solid #606060; border-radius: 3px; padding: 2px 6px; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView {"
    "    background-color: #484848; color: #e0e0e0;"
    "    selection-background-color: #606060; }"
    "QSlider::groove:horizontal {"
    "    background: #555555; height: 4px; border-radius: 2px; }"
    "QSlider::handle:horizontal {"
    "    background: #aaaaaa; width: 14px; height: 14px;"
    "    margin: -5px 0; border-radius: 7px; }"
    "QSlider::sub-page:horizontal {"
    "    background: #6088b8; border-radius: 2px; }"
    "QPushButton {"
    "    background-color: #484848; color: #e0e0e0;"
    "    border: 1px solid #606060; border-radius: 4px; padding: 4px 14px; }"
    "QPushButton:hover { background-color: #606060; }"
    "QPushButton:pressed { background-color: #707070; }"
    "QLineEdit {"
    "    background-color: #484848; color: #e0e0e0;"
    "    border: 1px solid #606060; border-radius: 3px; padding: 2px; }"
    "QInputDialog QLabel { color: #e0e0e0; }"
    "QTreeView, QListView {"
    "    background-color: #2b2b2b; color: #e0e0e0;"
    "    alternate-background-color: #323232;"
    "    selection-background-color: #606060; selection-color: #ffffff;"
    "    border: 1px solid #555555; }"
    "QHeaderView::section {"
    "    background-color: #383838; color: #e0e0e0;"
    "    border: 1px solid #555555; padding: 2px 4px; }"
    "QScrollBar:vertical {"
    "    background: #383838; width: 14px; border-radius: 3px; }"
    "QScrollBar::handle:vertical {"
    "    background: #606060; border-radius: 3px; min-height: 20px; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
	"QScrollBar:horizontal {"
	"    background: #383838; height: 14px; border-radius: 3px; }"
	"QScrollBar::handle:horizontal {"
	"    background: #606060; border-radius: 3px; min-width: 20px; }"
	"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
);

/* ── Platform output callbacks (called from engine thread) ──────────── */

static void qt_puts(const char *s)
{
    if (!g_terminal || !s) return;
    QString text = QString::fromLatin1(s);
    text.replace(QChar(0x23), QChar(0xcc));   // CHAR_QD → quad glyph
    QMetaObject::invokeMethod(g_terminal, [text]() {
        g_terminal->printOutput(text);
    }, Qt::QueuedConnection);
}

static int qt_pchar(char c)
{
    if (!g_terminal) return (unsigned char)c;
    QChar qc(static_cast<unsigned char>(c));
    QMetaObject::invokeMethod(g_terminal, [qc]() {
        g_terminal->printOutput(QString(qc));
    }, Qt::QueuedConnection);
    return (unsigned char)c;
}

static void qt_nl(void)
{
    if (!g_terminal) return;
    /* BlockingQueuedConnection: engine thread waits until the main thread
     * has processed this newline before continuing.  This prevents the Qt
     * event queue from flooding during large output (e.g. iota 10000),
     * keeps the display updating line-by-line, and gives the event loop
     * enough breathing room to process Ctrl+C (ABORTSIG) between lines. */
    QMetaObject::invokeMethod(g_terminal, []() {
        g_terminal->printOutput(QString("\n"));
    }, Qt::BlockingQueuedConnection);
}

static void qt_clear_screen(void)
{
    if (!g_terminal) return;
    QMetaObject::invokeMethod(g_terminal, []() {
        g_terminal->clearScreen();
    }, Qt::BlockingQueuedConnection);
}

static void qt_home(void)
{
    if (!g_terminal) return;
    QMetaObject::invokeMethod(g_terminal, []() {
        g_terminal->home();
    }, Qt::BlockingQueuedConnection);
}

static short qt_getin(UCHAR *p)
{
    flush_output();   /* push the ⎕: prompt to the display before blocking */

    QByteArray line;
    {
        QMutexLocker lock(&g_inputMutex);
        while (g_inputQueue.isEmpty() && g_engineRunning)
            g_inputCond.wait(&g_inputMutex);
        if (!g_engineRunning) {
            p[0] = '\0';
            return 0;
        }
        line = g_inputQueue.dequeue();
    }

    int n = qMin(line.size(), LINE_SIZE - 1);
    memcpy(p, line.constData(), n);
    p[n] = '\0';
    return 1;
}

/* Cursor / screen control — no-ops for now; Qt manages its own display */
static void qt_cursor_move(short, short) {}
static void qt_cursor_left(short)        {}
static void qt_cursor_right(short)       {}
static void qt_cursor_up(short)          {}
static void qt_cursor_down(short)        {}
static void qt_clear_eol(void)           {}
static void qt_standout(int)             {}

/* Lifecycle */
static void qt_open(void)    {}
static void qt_close(void)   {}
static void qt_setraw(void)  {}
static void qt_setcook(void) {}
static void qt_setabort(void){}

static void qt_edit(const char *fname)
{
    if (!g_terminal || !fname || !*fname) return;
    QString funcName = QString::fromLatin1(fname).trimmed();
    {
        QMutexLocker lock(&g_editMutex);
        g_editActive = true;
    }
    QMetaObject::invokeMethod(g_terminal, [funcName]() {
        emit g_terminal->editRequested(funcName);
    }, Qt::QueuedConnection);
    QMutexLocker lock(&g_editMutex);
    while (g_editActive)
        g_editCond.wait(&g_editMutex);
}

/* ── Engine thread ──────────────────────────────────────────────────── */
extern "C" {
    int  dll_startup(int debug, int xflag, char *wsname, int argc, char **argv);
    int  exec_apl_line(unsigned char *linebuf);
    void dll_cleanup(void);
    /* quad-function syntax mode (parse.c; QSYN_BOTH 0, QSYN_WARN 1,
       QSYN_NEW 2 in parse.h), set from -q in main() before the engine
       thread starts */
    extern int quad_syntax;
}

class EngineThread : public QThread
{
public:
    explicit EngineThread(QObject *parent = nullptr) : QThread(parent) {}

protected:
    void run() override
    {
        platform_defaults();

        int rc = dll_startup(0 /*debug*/, 1 /*xflag: run boot fn if present*/,
                              g_startupWsName.data(),
                              g_startupArgPtrs.size(), g_startupArgPtrs.data());
        if (rc < 0) {
            QMetaObject::invokeMethod(g_terminal, []() {
                g_terminal->printOutput("APL: workspace initialisation failed\n");
            }, Qt::QueuedConnection);
            return;
        }

        if (rc == 1) {
            unsigned char empty[2] = {0, 0};
            exec_apl_line(empty);
        }

        QMetaObject::invokeMethod(g_terminal, []() {
            g_terminal->printOutput("     ");
        }, Qt::QueuedConnection);

        while (g_engineRunning) {
            QByteArray line;
            {
                QMutexLocker lock(&g_inputMutex);
                while (g_inputQueue.isEmpty() && g_engineRunning)
                    g_inputCond.wait(&g_inputMutex);
                if (!g_engineRunning) break;
                line = g_inputQueue.dequeue();
            }

            line.append('\0');
            unsigned char exec_line[1028] = {0};  /* LINE_SIZE */
            alt_input_proc(
                reinterpret_cast<unsigned char *>(line.data()),
                exec_line);
            int ret = exec_apl_line(exec_line);

            if (ret == -9) break;   /* )OFF or ERR_KILL */
            if (g_suppressNextPrompt) {
                g_suppressNextPrompt = false;
            } else {
                QMetaObject::invokeMethod(g_terminal, []() {
                    g_terminal->printOutput("     ");
                }, Qt::QueuedConnection);
            }
        }

        dll_cleanup();

        QMetaObject::invokeMethod(qApp, []() {
            qApp->quit();
        }, Qt::QueuedConnection);
    }
};

/* ── Settings Dialog ────────────────────────────────────────────────── */
/*
 * All saveable settings in one place:
 *   - Keyboard layout (ASCII / Classic)
 *   - Font size (fixed list)
 *   - Terminal background brightness (slider, live preview)
 *   - Dark mode (app-level stylesheet)
 *
 * OK      → apply + save + close
 * Apply   → apply + save, stay open (useful for brightness tuning)
 * Cancel  → restore original brightness, discard everything else
 */
class APLSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    struct Settings {
        APLTerminalWidget::KeyboardLayout keyboard = APLTerminalWidget::ASCII;
        int  fontSize   = 14;
        int  brightness = 190;   /* 0 = black … 255 = white */
        bool darkMode   = false;
        bool unicodeClipboard = true;   /* translate KAPL<->Unicode APL glyphs on copy/paste */
        bool nativeFileDialog = false;  /* FDIALOG: false = Qt's own drawn
            dialog (freely movable, picks up dark mode via the QDialog
            selector in kDarkStyleSheet); true = the OS/GTK native picker
            (better desktop integration, but confirmed on this GNOME
            session to render oversized and awkward to move out of the
            way -- see GRAPHICS_NOTES.md). Defaults to false to match
            what's already shipped and confirmed working; native is
            opt-in, not the default, per "additive over breaking." */
        bool crashTranscript = true;   /* keep the last 32 KB of the session
            (output and entered lines) in memory, for crash.log if a fatal
            internal error ends the session -- see crashlog.c. Nothing is
            written to disk unless that happens. On by default: the first
            crash is the one that can't be reproduced without it. */
    };

    APLSettingsDialog(APLTerminalWidget *terminal,
                      const Settings    &current,
                      QWidget           *parent = nullptr)
        : QDialog(parent)
        , m_terminal(terminal)
        , m_original(current)
    {
        setWindowTitle("APL Settings");
        setMinimumWidth(380);

        /* Keyboard Layout */
        QGroupBox *keyGroup = new QGroupBox("Keyboard Layout");
        m_asciiRadio   = new QRadioButton("Altered 2741 Keymap");
        m_classicRadio = new QRadioButton("Original 2741 Keymap");
        (current.keyboard == APLTerminalWidget::ASCII
            ? m_asciiRadio : m_classicRadio)->setChecked(true);
        QVBoxLayout *keyLay = new QVBoxLayout(keyGroup);
        keyLay->addWidget(m_asciiRadio);
        keyLay->addWidget(m_classicRadio);

        /* Font Size */
        QGroupBox *fontGroup = new QGroupBox("Font Size");
        m_fontCombo = new QComboBox();
        static const int kSizes[] = {8, 10, 11, 12, 13, 14, 16, 18, 20, 24};
        for (int sz : kSizes)
            m_fontCombo->addItem(QString("%1 pt").arg(sz), sz);
        for (int i = 0; i < m_fontCombo->count(); ++i) {
            if (m_fontCombo->itemData(i).toInt() == current.fontSize) {
                m_fontCombo->setCurrentIndex(i);
                break;
            }
        }
        QHBoxLayout *fontLay = new QHBoxLayout(fontGroup);
        fontLay->addWidget(new QLabel("Size:"));
        fontLay->addWidget(m_fontCombo);
        fontLay->addStretch();

        /* Background Brightness — live preview as slider moves */
        QGroupBox *brightGroup = new QGroupBox("Terminal Background Brightness");
        m_brightnessSlider = new QSlider(Qt::Horizontal);
        m_brightnessSlider->setRange(0, 255);
        m_brightnessSlider->setValue(current.brightness);
        m_brightnessSlider->setTickPosition(QSlider::TicksBelow);
        m_brightnessSlider->setTickInterval(32);
        QHBoxLayout *brightLay = new QHBoxLayout(brightGroup);
        brightLay->addWidget(new QLabel("Dark"));
        brightLay->addWidget(m_brightnessSlider);
        brightLay->addWidget(new QLabel("Bright"));

        /* Dark Mode */
        m_darkModeCheck = new QCheckBox("Dark Mode  (menus, dialogs, and chrome)");
        m_darkModeCheck->setChecked(current.darkMode);

        /* Clipboard Unicode Translation */
        m_unicodeClipboardCheck = new QCheckBox(
            "Translate APL glyphs to Unicode on copy/paste");
        m_unicodeClipboardCheck->setChecked(current.unicodeClipboard);
        m_unicodeClipboardCheck->setToolTip(
            "When on, copying APL text produces real Unicode APL symbols "
            "(so it reads correctly when pasted elsewhere), and pasted "
            "Unicode APL symbols are converted back to screen codes.\n"
            "When off, copy/paste uses the raw screen codes as-is.");

        /* Native File Dialog (FDIALOG) */
        m_nativeFileDialogCheck = new QCheckBox(
            "Use native file picker  (⎕FDIALOG)");
        m_nativeFileDialogCheck->setChecked(current.nativeFileDialog);
        m_nativeFileDialogCheck->setToolTip(
            "When off (default), FDIALOG uses Qt's own drawn file dialog: "
            "freely movable/resizable, and follows Dark Mode above.\n"
            "When on, FDIALOG uses the desktop's native file picker "
            "instead -- better system integration (bookmarks, recent "
            "files), but on some desktops (confirmed on GNOME) it renders "
            "oversized and is awkward to move out of the way.");

        /* Crash-log session transcript */
        m_crashTranscriptCheck = new QCheckBox(
            "Keep recent session for the crash log");
        m_crashTranscriptCheck->setChecked(current.crashTranscript);
        m_crashTranscriptCheck->setToolTip(
            "When on (default), the last 32 KB of the session -- output and "
            "the lines you entered -- is kept in memory. If APL ever stops "
            "with a fatal internal error, it goes into crash.log in $APLHOME "
            "along with the error, to help work out what led to it.\n"
            "Nothing is written to disk unless that happens.");

        /* Buttons */
        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);

        /* Main layout */
        QVBoxLayout *mainLay = new QVBoxLayout(this);
        mainLay->addWidget(keyGroup);
        mainLay->addWidget(fontGroup);
        mainLay->addWidget(brightGroup);
        mainLay->addWidget(m_darkModeCheck);
        mainLay->addWidget(m_unicodeClipboardCheck);
        mainLay->addWidget(m_nativeFileDialogCheck);
        mainLay->addWidget(m_crashTranscriptCheck);
        mainLay->addSpacing(8);
        mainLay->addWidget(buttons);

        /* Live brightness preview */
        connect(m_brightnessSlider, &QSlider::valueChanged,
                this, [this](int v) { m_terminal->setBackgroundBrightness(v); });

        connect(buttons, &QDialogButtonBox::accepted, this, &APLSettingsDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &APLSettingsDialog::reject);
        connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
                this, &APLSettingsDialog::onApply);
    }

    /* Current state of all controls */
    Settings currentSettings() const
    {
        Settings s;
        s.keyboard   = m_asciiRadio->isChecked()
                       ? APLTerminalWidget::ASCII : APLTerminalWidget::Classic;
        s.fontSize   = m_fontCombo->currentData().toInt();
        s.brightness = m_brightnessSlider->value();
        s.darkMode   = m_darkModeCheck->isChecked();
        s.unicodeClipboard = m_unicodeClipboardCheck->isChecked();
        s.nativeFileDialog = m_nativeFileDialogCheck->isChecked();
        s.crashTranscript  = m_crashTranscriptCheck->isChecked();
        return s;
    }

signals:
    void applyRequested(const APLSettingsDialog::Settings &s);

protected:
    /* Cancel: restore original brightness that may have been live-previewed */
    void reject() override
    {
        m_terminal->setBackgroundBrightness(m_original.brightness);
        QDialog::reject();
    }

private slots:
    void onApply() { emit applyRequested(currentSettings()); }

private:
    APLTerminalWidget *m_terminal;
    Settings           m_original;
    QRadioButton      *m_asciiRadio;
    QRadioButton      *m_classicRadio;
    QComboBox         *m_fontCombo;
    QSlider           *m_brightnessSlider;
    QCheckBox         *m_darkModeCheck;
    QCheckBox         *m_unicodeClipboardCheck;
    QCheckBox         *m_nativeFileDialogCheck;
    QCheckBox         *m_crashTranscriptCheck;
};

/* ── Print options dialog ───────────────────────────────────────────────
 *
 * Gathers the print-time choices that aren't already covered by
 * QPrintDialog/QPrintPreviewDialog themselves (orientation, shrink-to-fit,
 * printer-vs-PDF destination, and whether to preview first). Printer
 * selection, copies, and page range are handled afterward by whichever of
 * those two dialogs applies -- this dialog only decides how the document
 * is laid out and where it ultimately goes.
 */
class APLPrintOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Destination { ToPrinter, ToPdf };

    explicit APLPrintOptionsDialog(bool landscape, bool shrinkToFit, bool toPdf,
                                    bool showPreview, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Print Options");
        setMinimumWidth(320);

        QGroupBox *orientGroup = new QGroupBox("Orientation");
        m_portraitRadio  = new QRadioButton("Portrait");
        m_landscapeRadio = new QRadioButton("Landscape");
        (landscape ? m_landscapeRadio : m_portraitRadio)->setChecked(true);
        QVBoxLayout *orientLay = new QVBoxLayout(orientGroup);
        orientLay->addWidget(m_portraitRadio);
        orientLay->addWidget(m_landscapeRadio);

        m_shrinkCheck = new QCheckBox("Shrink to fit page width");
        m_shrinkCheck->setChecked(shrinkToFit);
        m_shrinkCheck->setToolTip(
            "Reduces the print font size (never below 5pt) so the widest "
            "line fits the page width. If unchecked, uses the current "
            "on-screen font size as-is -- wide lines may then clip at the "
            "page edge.");

        QGroupBox *destGroup = new QGroupBox("Destination");
        m_printerRadio = new QRadioButton("Printer...");
        m_pdfRadio     = new QRadioButton("Save as PDF...");
        (toPdf ? m_pdfRadio : m_printerRadio)->setChecked(true);
        QVBoxLayout *destLay = new QVBoxLayout(destGroup);
        destLay->addWidget(m_printerRadio);
        destLay->addWidget(m_pdfRadio);

        m_previewCheck = new QCheckBox("Show print preview first");
        m_previewCheck->setChecked(showPreview);

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        QVBoxLayout *mainLay = new QVBoxLayout(this);
        mainLay->addWidget(orientGroup);
        mainLay->addWidget(m_shrinkCheck);
        mainLay->addWidget(destGroup);
        mainLay->addWidget(m_previewCheck);
        mainLay->addSpacing(8);
        mainLay->addWidget(buttons);
    }

    QPageLayout::Orientation orientation() const
    {
        return m_landscapeRadio->isChecked() ? QPageLayout::Landscape
                                              : QPageLayout::Portrait;
    }
    bool shrinkToFit() const { return m_shrinkCheck->isChecked(); }
    bool showPreview() const { return m_previewCheck->isChecked(); }
    Destination destination() const
    {
        return m_pdfRadio->isChecked() ? ToPdf : ToPrinter;
    }

private:
    QRadioButton *m_portraitRadio;
    QRadioButton *m_landscapeRadio;
    QCheckBox    *m_shrinkCheck;
    QRadioButton *m_printerRadio;
    QRadioButton *m_pdfRadio;
    QCheckBox    *m_previewCheck;
};

/* ── Help reference window ──────────────────────────────────────────────
 *
 * Modeless -- unlike the Settings/Print dialogs above, this one is meant
 * to be left open and referred back to while working in the terminal, so
 * APLMainWindow shows it with show()/raise()/activateWindow() rather than
 * exec(), and it's never deleted on close (no WA_DeleteOnClose): closing
 * it just hides it, so a single instance persists for the app's lifetime
 * and a second Help-menu click re-shows the same window rather than
 * rebuilding it.
 *
 * Content is plain Markdown, hand-maintained outside the app in a file
 * this class only reads, never writes (see help_reference_window_notes.md
 * for the authoring format this depends on).
 *
 * One class serves every reference document: the window title and the
 * content file name (resolved under resourcesDirectory()) are
 * constructor arguments, and APLMainWindow keeps one lazily-created
 * instance per document -- currently the quad function reference
 * (quad_function_help.md), the system command reference
 * (system_command_help.md) and the primitive function reference
 * (primitive_help.md). All three share the same authoring format:
 * entry syntax lines are paragraphs made only of code spans,
 * categories are `##` headings. The primitive reference also has a
 * Markdown table, which styleAndIndexContent() leaves alone.
 */
class APLHelpReferenceWindow : public QDialog
{
    Q_OBJECT
public:
    APLHelpReferenceWindow(const QString &title, const QString &fileName,
                           QWidget *parent = nullptr)
        : QDialog(parent), m_fileName(fileName)
    {
        setWindowTitle(title);
        setModal(false);
        resize(700, 800);

        m_index = new QListWidget(this);
        m_index->setMaximumWidth(220);
        m_index->setVisible(false);   /* starts collapsed */
        connect(m_index, &QListWidget::itemClicked,
                this, &APLHelpReferenceWindow::jumpToIndexEntry);

        m_browser = new QTextBrowser(this);
        m_browser->setOpenExternalLinks(false);
        loadContent();

        QPushButton *toggleIndexButton = new QPushButton("Index", this);
        toggleIndexButton->setCheckable(true);
        toggleIndexButton->setChecked(false);
        connect(toggleIndexButton, &QPushButton::toggled, m_index, &QWidget::setVisible);

        QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(m_index);
        splitter->addWidget(m_browser);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({180, 520});

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

        QHBoxLayout *topBar = new QHBoxLayout();
        topBar->addWidget(toggleIndexButton);
        topBar->addStretch();

        QVBoxLayout *mainLay = new QVBoxLayout(this);
        mainLay->addLayout(topBar);
        mainLay->addWidget(splitter);
        mainLay->addWidget(buttons);
    }

    /* Mirrors APLTerminalWidget::setBackgroundBrightness()'s exact
     * algorithm (clamp, grayscale background, contrast-based foreground)
     * and its reasoning: an explicit widget-level stylesheet is used
     * rather than just QPalette because it wins over the app-level
     * dark-mode QApplication::setStyleSheet() cascade (see
     * kDarkStyleSheet's comment) -- which, like the terminal's own
     * QTextEdit, deliberately excludes QTextBrowser and QListWidget from
     * its selector list, so their background/foreground is meant to be
     * governed by brightness directly rather than by the chrome theme.
     * Dark mode's own effect on this window -- the QDialog background,
     * the Close/Index buttons -- already comes for free from that same
     * app-level stylesheet, since QDialog and QPushButton are both in
     * its selector list; nothing extra needed here for that part.
     *
     * Called from APLMainWindow::openHelpReference() on every show, not
     * just at construction, using whatever m_settings.brightness is
     * current at that moment -- so a brightness change made via the
     * Settings dialog while this window was hidden still shows up next
     * time it's opened, without needing this window to live-track the
     * setting while both are open simultaneously.
     */
    void applyBrightness(int brightness)
    {
        int v = qBound(0, brightness, 255);
        QColor bg(v, v, v);
        QColor fg = (v < 128) ? Qt::white : Qt::black;

        const QString style = QStringLiteral(
            "QTextBrowser { background-color: %1; color: %2; border: none; }"
            "QListWidget { background-color: %1; color: %2; border: none; }")
            .arg(bg.name(), fg.name());

        m_browser->setStyleSheet(style);
        m_index->setStyleSheet(style);

        QPalette p = m_browser->palette();
        p.setColor(QPalette::Base, bg);
        p.setColor(QPalette::Text, fg);
        m_browser->setPalette(p);
        m_index->setPalette(p);
    }

private:
    /* Jumps the browser to whichever paragraph position this index entry
     * (a category heading or a function's syntax line) was recorded
     * against in styleAndIndexContent() -- see that method for why this
     * is a stored position rather than a text search.
     *
     * Deliberately doesn't use QTextEdit::ensureCursorVisible(): that
     * only scrolls the minimum distance needed to bring the target back
     * into view, so where it lands depends on which direction you're
     * scrolling from -- a forward jump parks the target at the very
     * bottom edge of the viewport, a backward jump parks it at the very
     * top edge, which reads as inconsistent from click to click even
     * though it's actually just direction-dependent. Setting the
     * scrollbar directly to the target block's own pixel position
     * instead puts it at the top of the viewport every time, regardless
     * of where the browser happened to be scrolled beforehand.
     *
     * End-of-document padding: an entry within one viewport-height of
     * the document's end can't be scrolled to the top, because the
     * scrollbar's maximum stops short of it (seen in both reference
     * files -- the whole Diagnostics section of the command reference,
     * and the last few File entries of the quad reference). Fixed by
     * giving the document's root frame a bottom margin equal to the
     * viewport's current height. It's set here, at jump time, rather
     * than once at load, so it always matches the window's size at the
     * moment it matters -- no resize tracking needed -- and it's only
     * touched when the height actually changed, to avoid a needless
     * relayout on every click. Verified in the PySide6 harness at
     * several window sizes, jumping in both directions, with no event
     * loop pass between setting the margin and setting the scrollbar.
     *
     * Two things tried and rejected there: trailing blank paragraphs,
     * or a bottom margin on an empty last block. Qt's layout ignores
     * the last block's bottom margin, so neither extended the scroll
     * range at all. */
    void jumpToIndexEntry(QListWidgetItem *item)
    {
        QTextDocument *doc = m_browser->document();

        QTextFrame *root = doc->rootFrame();
        QTextFrameFormat rootFmt = root->frameFormat();
        const qreal pad = m_browser->viewport()->height();
        if (rootFmt.bottomMargin() != pad) {
            rootFmt.setBottomMargin(pad);
            root->setFrameFormat(rootFmt);
        }

        int pos = item->data(Qt::UserRole).toInt();
        QTextCursor cursor(doc);
        cursor.setPosition(pos);
        m_browser->setTextCursor(cursor);

        QRectF blockRect = doc->documentLayout()->blockBoundingRect(cursor.block());
        m_browser->verticalScrollBar()->setValue(int(blockRect.top()));
    }

    /* Mirrors APLTerminalWidget::getFnsDirectory()'s fallback chain:
     * 1. $APLHOME/resources/
     * 2. $HOME/apl/resources/
     * 3. ./resources/
     */
    static QString resourcesDirectory()
    {
        QByteArray aplHome = qgetenv("APLHOME");
        if (!aplHome.isEmpty())
            return QString::fromLocal8Bit(aplHome) + "/resources/";

        QByteArray home = qgetenv("HOME");
        if (!home.isEmpty())
            return QString::fromLocal8Bit(home) + "/apl/resources/";

        return QString("./resources/");
    }

    void loadContent()
    {
        QString path = resourcesDirectory() + m_fileName;
        QFile file(path);

        if (!file.open(QIODevice::ReadOnly)) {
            m_browser->setPlainText(
                "Reference file not found.\n\nExpected at:\n" + path +
                "\n\nCheck that $APLHOME is set and the file exists there.");
            return;
        }

        QString content = QString::fromUtf8(file.readAll());
        applyFontDirective(content);   /* strips the directive line too */
        m_browser->setMarkdown(content);
        styleAndIndexContent();
    }

    /* Markdown's own bold+code nesting (`**`text`**`) silently drops the
     * bold and keeps only the monospacing -- confirmed directly against
     * this Qt build, not assumed. Embedding raw HTML in the source to
     * work around that crashes the importer outright, also confirmed --
     * so this content file only ever uses bold and code as separate,
     * non-nested spans, and never contains raw HTML.
     *
     * Each calling-syntax line is meant to stay monospace throughout,
     * but with only its `⎕NAME` token(s) bold -- params, quotes, and
     * (now that two-form lines are written as two separate paragraphs
     * rather than joined with "or") nothing else shares the line at
     * all. Solved here instead of in Markdown source: first force the
     * whole paragraph to fixed-pitch/normal weight, then re-bold just
     * the substrings matching a `⎕`-name pattern. QTextCharFormat has
     * no trouble holding fixedPitch and bold independently per
     * character range; it's specifically Markdown's nested-emphasis
     * parsing that can't express "bold this part, not that part, all
     * monospace."
     *
     * This same pass also adds extra space above each function entry's
     * *first* syntax line, so consecutive entries read as visually
     * separated rather than run together -- but not above a syntax
     * line that's the second half of a two-form pair (e.g. the `n`
     * form right after the bare form of `⎕WI`), so that pair still
     * reads as belonging together rather than as two entries.
     *
     * A paragraph is treated as a syntax line if every one of its
     * fragments is already fixed-pitch (an ordinary code span) --
     * this matches every calling-syntax paragraph in the reference
     * file and nothing else in it (prose paragraphs always mix in
     * non-code text, and headings are never fixed-pitch to begin
     * with), and it's an entry-start if the immediately preceding
     * block isn't itself one of these paragraphs.
     *
     * Since this pass already visits every category heading (Markdown
     * `##`, which Qt's importer tags with blockFormat().headingLevel()
     * == 2) and every entry-start syntax line exactly once each, it
     * also builds the sidebar index here rather than as a second,
     * separate walk over the document -- recording each entry's exact
     * block position rather than just its name. That's what the
     * sidebar's click handler jumps to directly: a stored position,
     * not a text search. A plain QTextDocument::find() by name would
     * land on the first occurrence of that name anywhere in the
     * document, which isn't always the entry itself -- e.g. ⎕WNUMS's
     * own entry mentions "⎕WSIZE's wnum argument below" before
     * ⎕WSIZE's actual entry appears, so a top-down search for ⎕WSIZE
     * would stop at that cross-reference instead.
     *
     * The name pattern covers both reference documents: `⎕NAME` for
     * quad functions, `)NAME` for system commands. One combined pattern
     * rather than a per-document one is safe because it's only ever
     * applied to syntax lines: the quad reference does mention `)`
     * commands (`)INTDIV`, `)ORIGIN`, `)SI`, ...), but always inside
     * prose paragraphs, which are skipped before the pattern is used.
     * Confirmed in the PySide6 harness: the quad reference's index is
     * identical (same 62 rows, same positions) under the old `⎕`-only
     * pattern and this one. Argument words after a command, like the ON
     * in `)INTDIV ON`, stay plain weight -- they aren't preceded by `)`.
     *
     * The primitive reference (primitive_help.md) needed two additions.
     * Its syntax lines (`L⍴R`, `⍳R`) have no `⎕`/`)` name, so when the
     * pattern finds none, the index row is labelled with the whole
     * line instead; the quad and command references always have a
     * name, so this never applies to them. And it opens with a
     * Markdown table: Qt's importer makes each table cell a block of
     * its own, and a cell holding only a code span (`+`) passes the
     * syntax-line test, so cells got the entry margin (spreading the
     * rows) and would have got index rows. Blocks inside a table are
     * now skipped. Both checked in the PySide6 harness: the quad and
     * command references' indexes are identical (same rows, same
     * positions) and their margins unchanged; the primitive table gets
     * no margins or index rows.
     */
    void styleAndIndexContent()
    {
        static const QRegularExpression nameRe(QStringLiteral("[⎕)][A-Z]+"));
        QTextDocument *doc = m_browser->document();
        bool prevWasSyntaxLine = false;

        m_index->clear();

        for (QTextBlock block = doc->firstBlock(); block.isValid(); block = block.next()) {
            if (block.blockFormat().headingLevel() == 2) {
                QListWidgetItem *catItem = new QListWidgetItem(block.text());
                QFont catFont = catItem->font();
                catFont.setBold(true);
                catItem->setFont(catFont);
                catItem->setData(Qt::UserRole, block.position());
                m_index->addItem(catItem);

                prevWasSyntaxLine = false;
                continue;
            }

            /* Table cells are blocks too; leave the table as Markdown
             * made it (see the comment above). */
            if (QTextCursor(block).currentTable()) {
                prevWasSyntaxLine = false;
                continue;
            }

            bool isSyntaxLine = false;
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                QTextFragment frag = it.fragment();
                if (!frag.isValid() || frag.text().trimmed().isEmpty())
                    continue;

                isSyntaxLine = true;   /* at least one real fragment seen */
                if (!frag.charFormat().fontFixedPitch()) {
                    isSyntaxLine = false;
                    break;
                }
            }
            if (!isSyntaxLine) {
                prevWasSyntaxLine = false;
                continue;
            }

            /* Whole line: monospace, normal weight -- baseline before
             * re-bolding just the name token(s) below. */
            QTextCursor cursor(block);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat baseFmt;
            baseFmt.setFontFixedPitch(true);
            baseFmt.setFontWeight(QFont::Normal);
            cursor.setCharFormat(baseFmt);

            /* Re-bold just the ⎕NAME token(s), possibly more than one
             * on a line like the compiler-caveat sentence isn't (that's
             * prose, skipped above) -- in practice just one per line
             * now that two-form lines are two separate paragraphs. */
            const QString text = block.text();
            QString firstName;
            QRegularExpressionMatchIterator mi = nameRe.globalMatch(text);
            while (mi.hasNext()) {
                QRegularExpressionMatch m = mi.next();
                if (firstName.isEmpty())
                    firstName = m.captured(0);
                QTextCursor nameCursor(doc);
                nameCursor.setPosition(block.position() + m.capturedStart());
                nameCursor.setPosition(block.position() + m.capturedEnd(),
                                        QTextCursor::KeepAnchor);
                QTextCharFormat boldFmt;
                boldFmt.setFontWeight(QFont::Bold);
                boldFmt.setFontFixedPitch(true);
                nameCursor.mergeCharFormat(boldFmt);
            }
            /* No ⎕/) name (primitive reference): index by the line */
            if (firstName.isEmpty())
                firstName = text.trimmed();

            /* Extra space above this entry's first line only; sidebar
             * entry only for the first line too -- the second half of
             * a two-form pair isn't a separate function to jump to. */
            if (!prevWasSyntaxLine) {
                QTextCursor marginCursor(block);
                QTextBlockFormat blockFmt = block.blockFormat();
                blockFmt.setTopMargin(blockFmt.topMargin() + 16);
                marginCursor.setBlockFormat(blockFmt);

                if (!firstName.isEmpty()) {
                    QListWidgetItem *fnItem =
                        new QListWidgetItem(QStringLiteral("  ") + firstName);
                    fnItem->setData(Qt::UserRole, block.position());
                    m_index->addItem(fnItem);
                }
            }
            prevWasSyntaxLine = true;
        }
    }

    /* Looks for a single directive line anywhere in the file:
     *
     *     <!-- font: Georgia, DejaVu Serif, serif -->
     *
     * and, if found, sets it (comma-separated names as an ordered
     * fallback list, via QFont::setFamilies -- lets one line cover both
     * a Windows box and a Linux box that don't have the same fonts
     * installed) as the browser's body font. The directive line itself
     * is removed from `content` before it reaches setMarkdown(), so it
     * never appears in the rendered output -- this is parsed by hand
     * rather than relied on Qt to treat the HTML comment as invisible.
     * No directive present, or none of the listed families resolve to
     * anything Qt has installed: falls back to the dialog's default
     * font silently, no error.
     */
    void applyFontDirective(QString &content)
    {
        static const QRegularExpression re(
            QStringLiteral("^[ \\t]*<!--\\s*font:\\s*(.+?)\\s*-->[ \\t]*\\n?"),
            QRegularExpression::MultilineOption);

        QRegularExpressionMatch m = re.match(content);
        if (!m.hasMatch())
            return;

        QStringList families = m.captured(1).split(',', Qt::SkipEmptyParts);
        for (QString &f : families)
            f = f.trimmed();

        content.remove(m.capturedStart(0), m.capturedLength(0));

        if (!families.isEmpty()) {
            QFont f = m_browser->font();
            f.setFamilies(families);
            m_browser->setFont(f);
        }
    }

    QString       m_fileName;   /* content file, relative to resourcesDirectory() */
    QTextBrowser *m_browser;
    QListWidget  *m_index;
};

/* ── Main window ────────────────────────────────────────────────────── */
class APLMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    APLMainWindow(bool enableLogging, QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_loggingEnabled(enableLogging)
        , m_logFile(nullptr)
        , m_newFuncAction(nullptr)
        , m_openFuncAction(nullptr)
        , m_termSep(nullptr)
        , m_saveAction(nullptr)
        , m_saveAsAction(nullptr)
        , m_editSep1(nullptr)
        , m_exitEditorAction(nullptr)
        , m_exitNoCompileAction(nullptr)
        , m_editSep2(nullptr)
        , m_insertLineAction(nullptr)
        , m_deleteLineAction(nullptr)
        , m_engineThread(nullptr)
        , m_graphManager(nullptr)
    {
        if (m_loggingEnabled) {
            m_logFile = new QFile("apl_input.bin");
            if (m_logFile->open(QIODevice::WriteOnly)) {
                statusBar()->showMessage("Logging to: apl_input.bin", 3000);
            } else {
                delete m_logFile;
                m_logFile = nullptr;
                statusBar()->showMessage("Warning: Could not create log file", 3000);
            }
        }

        terminal = new APLTerminalWidget(this);
        setCentralWidget(terminal);

        /* Owned by this window like everything else here; g_graphManager
         * (the pointer graph_bridge.cpp's engine-thread-facing gw_*
         * functions actually use) isn't set until startEngine(), same
         * timing as g_terminal below -- nothing can reach either global
         * before the engine thread exists to call them. */
        m_graphManager = new GraphManager(this);
        m_graphManager->setFocusTarget(terminal);

        if (!terminal->initialize()) {
            QMessageBox::warning(this, "Font Error",
                "Failed to load KAPL.ttf font. "
                "Please ensure the font file is in $APLHOME/fonts/.");
        }

        createMenus();
        statusBar()->showMessage("Ready");

        connect(terminal, &APLTerminalWidget::lineEntered,
                this, &APLMainWindow::handleLineEntered);
        connect(terminal, &APLTerminalWidget::interruptRequested,
                this, &APLMainWindow::handleInterrupt);
        connect(terminal, &APLTerminalWidget::editModeEntered,
                this, &APLMainWindow::onEditModeEntered);
        connect(terminal, &APLTerminalWidget::editModeExited,
                this, &APLMainWindow::onEditModeExited);
        connect(terminal, &APLTerminalWidget::saveAsRequested,
                this, &APLMainWindow::saveFunctionAs);
        connect(terminal, &APLTerminalWidget::overwriteRequested,
                this, &APLMainWindow::handleOverwriteRequest);
        connect(terminal, &APLTerminalWidget::editRequested,
                this, &APLMainWindow::handleEditRequest);
        connect(terminal, &APLTerminalWidget::backupFailed,
                this, [this](const QString &reason) {
                    statusBar()->showMessage(reason, 3000);
                });

        resize(960, 640);   /* default — may be overridden by loadSettings() */
        loadSettings();
    }

    ~APLMainWindow()
    {
        stopEngine();
        if (m_logFile) {
            m_logFile->close();
            delete m_logFile;
        }
    }

    /*
     * Call after show() so the window geometry is known.
     * Registers platform callbacks, sets backend screen-size globals,
     * then starts the engine thread.
     */
    void startEngine()
    {
        g_terminal = terminal;
        g_graphManager = m_graphManager;

        PlatformFuncs fns;
        fns.puts_fn         = qt_puts;
        fns.pchar_fn        = qt_pchar;
        fns.nl_fn           = qt_nl;
        fns.cursor_move_fn  = qt_cursor_move;
        fns.cursor_left_fn  = qt_cursor_left;
        fns.cursor_right_fn = qt_cursor_right;
        fns.cursor_up_fn    = qt_cursor_up;
        fns.cursor_down_fn  = qt_cursor_down;
        fns.clear_eol_fn    = qt_clear_eol;
        fns.clear_screen_fn = qt_clear_screen;
		fns.home_fn			= qt_home;
        fns.standout_fn     = qt_standout;
        fns.open_fn         = qt_open;
        fns.close_fn        = qt_close;
        fns.setraw_fn       = qt_setraw;
        fns.setcook_fn      = qt_setcook;
        fns.setabort_fn     = qt_setabort;
        fns.edit_fn         = qt_edit;
        fns.getin_fn        = qt_getin;
        platform_register(&fns);

        int w = terminal->width();
        int h = terminal->height();
        set_width(w > 0 ? w / 8 : 80);
        SCRW = w;
        SCRH = h;

        g_engineRunning = true;
        m_engineThread = new EngineThread(this);
        m_engineThread->start();
    }

    /*
     * If an earlier session ended with a fatal internal error that hasn't
     * been acknowledged (crashlog.c's crash_log_pending()), say so -- or,
     * if only warnings are pending (an inconsistency the session survived,
     * e.g. GarbCollect()'s bad reference), say that instead. Called
     * from main() after show() and before startEngine(), so it runs on the
     * GUI thread with no engine running -- the crash itself did nothing on
     * this side; this is where the user hears about it.
     *
     * Acknowledge appends the "*** acknowledged ***" line to crash.log (the
     * log itself is the only record; nothing goes in settings.ini) and
     * stops the notice. "Remind Me Next Time" (also Escape, or closing the
     * box) leaves it pending. The log is never removed or rewritten here.
     * The text is selectable so the path can be copied.
     */
    void reportPendingCrash()
    {
        char path[PATHBUF_SIZE];
        char summary[512];

        int kind = crash_log_pending(path, sizeof path, summary, sizeof summary);
        if (!kind)
            return;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        if (kind == 1) {
            box.setWindowTitle("APL - previous session crashed");
            box.setText("An earlier APL session ended with a fatal internal error.");
        } else {
            box.setWindowTitle("APL - internal warning");
            box.setText("An earlier APL session reported a problem in its "
                        "workspace memory and kept running.");
        }
        box.setInformativeText(
            QString("%1\n\nThe details (the message, the recent session and the "
                    "SI stack) are in:\n%2")
                .arg(QString::fromUtf8(summary), QString::fromLocal8Bit(path)));
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton *ackBtn = box.addButton("Acknowledge", QMessageBox::AcceptRole);
        box.addButton("Remind Me Next Time", QMessageBox::RejectRole);
        box.setDefaultButton(ackBtn);
        box.exec();

        if (box.clickedButton() == ackBtn && crash_log_acknowledge() != 0)
            statusBar()->showMessage(
                "Could not write to crash.log -- the notice will appear again", 5000);
    }

private slots:
    /*
     * Called on the main thread when the user presses Enter in terminal mode.
     */
    void handleLineEntered(const QString &line)
    {
        if (m_loggingEnabled && m_logFile) {
            QByteArray internalCodes = terminal->getInternalRepresentation(line);
            m_logFile->write(internalCodes);
            m_logFile->write("\n", 1);
            m_logFile->flush();
        }

        QByteArray raw;
        raw.reserve(line.size());
        for (QChar qc : line)
            raw.append(static_cast<char>(qc.unicode() & 0xFF));

        {
            QMutexLocker lock(&g_inputMutex);
            g_inputQueue.enqueue(raw);
        }
        g_inputCond.wakeOne();
    }

    void handleEditRequest(const QString &functionName)
    {
        terminal->enterEditMode(functionName.toUpper());
        statusBar()->showMessage("Editing: " + functionName.toUpper(), 2000);
    }

    void handleInterrupt()
    {
        ABORTSIG = 1;
        if (m_loggingEnabled && m_logFile) {
            char marker = (char)0xFF;
            m_logFile->write(&marker, 1);
            m_logFile->flush();
        }
        statusBar()->showMessage("Interrupt requested", 2000);
    }

    void clearScreen()
    {
        terminal->clearScreen();
        terminal->printOutput("     ");
        statusBar()->showMessage("Screen cleared");
    }

    /* Shared by every Help-menu reference document. Each document has its
     * own window pointer member, passed in by reference: the window is
     * created on first use and persists for the app's lifetime after
     * that (see APLHelpReferenceWindow's class comment for why), so
     * title/fileName only matter on that first call. Separate windows
     * rather than one switchable window, so two references can be open
     * side by side. */
    void showHelpWindow(APLHelpReferenceWindow *&window,
                        const QString &title, const QString &fileName)
    {
        if (!window)
            window = new APLHelpReferenceWindow(title, fileName, this);

        window->applyBrightness(m_settings.brightness);
        window->show();
        window->raise();
        window->activateWindow();
    }

    void openSystemCommandReference()
    {
        showHelpWindow(m_cmdHelpWindow, "APL System Command Reference",
                       "system_command_help.md");
    }

    void openHelpReference()
    {
        showHelpWindow(m_helpWindow, "APL Quad Function Reference",
                       "quad_function_help.md");
    }

    void openPrimitiveReference()
    {
        showHelpWindow(m_primHelpWindow, "APL Primitive Function Reference",
                       "primitive_help.md");
    }

    void aboutDialog()
    {
        QString logStatus = m_loggingEnabled ? "Enabled (apl_input.bin)" : "Disabled";
        QMessageBox::about(this, "About RetroAPL",
            QString("<h3>%1</h3>"
                    "<p>Custom APL language interpreter, vintage APL\\360 edition.</p>"
                    "<p>With Qt6-based emulator using KAPL.ttf font</p>"
                    "<p>Features IBM 2741 backspace-overstrike composition</p>"
                    "<p>Written initially for Windows by Keith David Doyle</p>"
                    "<p>Linux port with assistance from Claude</p>"
                    "<p><b>Logging:</b> %2</p>")
                    .arg(g_appTitle).arg(logStatus));
    }

    void newFunction()
    {
        bool ok;
        QString name = QInputDialog::getText(this, "New Function",
            "Enter function name:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            terminal->enterEditMode(name.toUpper());
            statusBar()->showMessage("Editing: " + name.toUpper(), 2000);
        }
    }

    void openFunction()
    {
        QString fnsDir = APLTerminalWidget::getFnsDirectory();
        QString filename = QFileDialog::getOpenFileName(this, "Open Function",
            fnsDir, "APL Function Files (*.fn *.FN)");
        if (!filename.isEmpty()) {
            APLTerminalWidget::KeyboardLayout layout = terminal->getKeyboardLayout();
            QStringList lines = APLTerminalWidget::loadFnFile(filename, layout);
            if (!lines.isEmpty()) {
                QFileInfo info(filename);
                QString funcName = info.baseName().toLower();
                terminal->enterEditMode(funcName, lines);
                statusBar()->showMessage("Loaded: " + funcName, 2000);
            } else {
                statusBar()->showMessage("Failed to load: " + filename, 2000);
            }
        }
    }

    void saveFunction()
    {
        if (terminal->isEditMode()) {
            if (terminal->saveCurrentFunction()) {
                statusBar()->showMessage(
                    "Function saved: " + terminal->getCurrentFunctionName(), 2000);
            } else {
                statusBar()->showMessage("Save failed!", 2000);
            }
        }
    }

    void saveFunctionAs()
    {
        if (terminal->isEditMode()) {
            QString fnsDir = APLTerminalWidget::getFnsDirectory();
            QString filename = QFileDialog::getSaveFileName(this, "Save Function As",
                fnsDir + "/" + terminal->getCurrentFunctionName().toLower() + ".fn",
                "APL Function Files (*.fn)");
            if (!filename.isEmpty()) {
                if (terminal->saveCurrentFunctionAs(filename)) {
                    statusBar()->showMessage("Function saved as: " + filename, 2000);
                } else {
                    statusBar()->showMessage("Save failed!", 2000);
                }
            }
        }
    }

    void exitEditorCompile()   { exitEditMode(true);  }
    void exitEditorNoCompile() { exitEditMode(false); }

    // Saves the currently-edited function to its existing path, or prompts
    // for one via Save As if it's never been saved. Returns false if the
    // user cancels that dialog or the write itself fails, either of which
    // the caller needs to treat as "did not save."
    bool forceSaveCurrentFunction()
    {
        QString savePath = terminal->getCurrentFilePath();
        if (savePath.isEmpty()) {
            QString fnsDir = APLTerminalWidget::getFnsDirectory();
            savePath = QFileDialog::getSaveFileName(this, "Save Function",
                fnsDir + "/" + terminal->getCurrentFunctionName().toLower() + ".fn",
                "APL Function Files (*.fn)");
            if (savePath.isEmpty()) return false;
            if (!savePath.endsWith(".fn")) savePath += ".fn";
        }
        return terminal->forceSaveToFile(savePath);
    }

    void exitEditMode(bool compile)
    {
        if (!terminal->isEditMode()) return;

        if (compile) {
            // )ENTER goes to the engine thread's own input queue -- the
            // same channel as anything typed at the terminal -- so it has
            // no direct view into the editor's live buffer and reloads the
            // function from its .fn file on disk. There's no coherent
            // "discard changes, but still compile" case, so save
            // unconditionally here instead of offering a choice.
            if (terminal->hasUnsavedChanges()) {
                if (!forceSaveCurrentFunction()) {
                    statusBar()->showMessage("Save failed - compile cancelled", 3000);
                    return;
                }
                statusBar()->showMessage("Saved - compiling...", 2000);
            }
        } else if (terminal->hasUnsavedChanges()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("Unsaved Changes");
            msgBox.setText("You have unsaved changes. Save before exiting?");
            QAbstractButton *saveBtn    = msgBox.addButton(QMessageBox::Save);
            QAbstractButton *discardBtn = msgBox.addButton(QMessageBox::Discard);
            QAbstractButton *cancelBtn  = msgBox.addButton(QMessageBox::Cancel);
            Q_UNUSED(cancelBtn);
            // "Close without saving" is the common case here; give it a
            // plain, immediate accelerator, and actually show it on the
            // button -- a shortcut with no visible hint isn't discoverable.
            discardBtn->setText("Discard (X)");
            discardBtn->setShortcut(QKeySequence(Qt::Key_X));
            msgBox.setDefaultButton(static_cast<QPushButton *>(saveBtn));
            msgBox.exec();

            QAbstractButton *clicked = msgBox.clickedButton();
            if (clicked == nullptr || clicked == cancelBtn) return;
            if (clicked == saveBtn) {
                if (!forceSaveCurrentFunction())
                    statusBar()->showMessage("Save failed - exiting anyway", 2000);
            }
        }

        QString fnName = terminal->getCurrentFunctionName();
        terminal->exitEditMode(compile);

        if (compile && g_engineRunning && !fnName.isEmpty()) {
            QString cmd = QString(")ENTER ") + fnName;
            QByteArray raw;
            for (QChar qc : cmd)
                raw.append(static_cast<char>(qc.unicode() & 0xFF));
            g_suppressNextPrompt = true;
            {
                QMutexLocker lock(&g_inputMutex);
                g_inputQueue.enqueue(raw);
            }
            g_inputCond.wakeOne();
        }

        {
            QMutexLocker lock(&g_editMutex);
            g_editActive = false;
        }
        g_editCond.wakeAll();
    }

    /* Slot connected to terminal's editModeEntered signal */
    void onEditModeEntered(const QString &functionName)
    {
        /* Hide terminal-mode File items */
        if (m_newFuncAction)  m_newFuncAction->setVisible(false);
        if (m_openFuncAction) m_openFuncAction->setVisible(false);
        if (m_termSep)        m_termSep->setVisible(false);

        /* Show edit-mode File items */
        if (m_saveAction)          m_saveAction->setVisible(true);
        if (m_saveAsAction)        m_saveAsAction->setVisible(true);
        if (m_editSep1)            m_editSep1->setVisible(true);
        if (m_exitEditorAction)    m_exitEditorAction->setVisible(true);
        if (m_exitNoCompileAction) m_exitNoCompileAction->setVisible(true);
        if (m_editSep2)            m_editSep2->setVisible(true);

        /* Show edit-mode Edit items */
        if (m_insertLineAction) m_insertLineAction->setVisible(true);
        if (m_deleteLineAction) m_deleteLineAction->setVisible(true);

        terminal->clearUndoStack();
        setWindowTitle(g_appTitle + " - Editing: " + functionName);
    }

    /* Slot connected to terminal's editModeExited signal */
    void onEditModeExited()
    {
        /* Restore terminal-mode File items */
        if (m_newFuncAction)  m_newFuncAction->setVisible(true);
        if (m_openFuncAction) m_openFuncAction->setVisible(true);
        if (m_termSep)        m_termSep->setVisible(true);

        /* Hide edit-mode File items */
        if (m_saveAction)          m_saveAction->setVisible(false);
        if (m_saveAsAction)        m_saveAsAction->setVisible(false);
        if (m_editSep1)            m_editSep1->setVisible(false);
        if (m_exitEditorAction)    m_exitEditorAction->setVisible(false);
        if (m_exitNoCompileAction) m_exitNoCompileAction->setVisible(false);
        if (m_editSep2)            m_editSep2->setVisible(false);

        /* Hide edit-mode Edit items */
        if (m_insertLineAction) m_insertLineAction->setVisible(false);
        if (m_deleteLineAction) m_deleteLineAction->setVisible(false);

        g_terminal->window()->setWindowTitle(g_appTitle);
    }

    void handleOverwriteRequest(const QString &filename)
    {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "File Exists",
            QString("File '%1' already exists.\nOverwrite?").arg(filename),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes)
            terminal->forceSaveToFile(filename);
    }

    void insertLine()
    {
        if (terminal->isEditMode()) {
            QTextCursor cursor = terminal->textCursor();
            terminal->insertLine(cursor.blockNumber() + 1);
        }
    }

    void deleteLine()
    {
        if (terminal->isEditMode()) {
            QTextCursor cursor = terminal->textCursor();
            terminal->deleteLine(cursor.blockNumber());
        }
    }

    /* ── Settings ────────────────────────────────────────────────────── */

    /* Path to INI file: $APLHOME/settings.ini or ~/apl/settings.ini */
    QString settingsPath() const
    {
        const char *aplhome = getenv("APLHOME");
        QString base = (aplhome && *aplhome)
                       ? QString::fromLocal8Bit(aplhome)
                       : QDir::homePath() + "/apl";
        return base + "/settings.ini";
    }

    void loadSettings()
    {
        QSettings cfg(settingsPath(), QSettings::IniFormat);

        QString kb     = cfg.value("keyboard",   "ascii").toString();
        m_settings.keyboard   = (kb == "classic")
                                ? APLTerminalWidget::Classic : APLTerminalWidget::ASCII;

        /* Font size: must be one of the presets APLSettingsDialog's combo
         * box actually offers (see kSizes there, 8..24) -- anything else
         * (corrupted ini, hand edit, a stale value from a future version
         * with a different preset list) falls back to the default rather
         * than being handed to setFontSize() as-is. */
        static const int kValidFontSizes[] = {8, 10, 11, 12, 13, 14, 16, 18, 20, 24};
        int fs = cfg.value("fontsize", 14).toInt();
        bool fsValid = std::find(std::begin(kValidFontSizes), std::end(kValidFontSizes), fs)
                       != std::end(kValidFontSizes);
        m_settings.fontSize   = fsValid ? fs : 14;

        /* Brightness is already clamped downstream in
         * setBackgroundBrightness() via qBound(0, v, 255), but bounding it
         * here too keeps m_settings itself consistent with what's actually
         * applied, rather than temporarily holding an out-of-range value. */
        m_settings.brightness = qBound(0, cfg.value("brightness", 190).toInt(), 255);
        m_settings.darkMode   = cfg.value("darkmode",   false).toBool();
        m_settings.unicodeClipboard = cfg.value("unicodeclipboard", true).toBool();
        m_settings.nativeFileDialog = cfg.value("nativefiledialog", false).toBool();
        m_settings.crashTranscript  = cfg.value("crashtranscript", true).toBool();

        m_printLandscape   = cfg.value("print_landscape",   true).toBool();
        m_printShrinkToFit = cfg.value("print_shrinktofit", true).toBool();
        m_printToPdf       = cfg.value("print_topdf",       false).toBool();
        m_printShowPreview = cfg.value("print_preview",     false).toBool();
        m_lastPdfDir       = cfg.value("print_pdfdir", QDir::homePath()).toString();

        /* Window geometry — only restore if previously saved. Clamped so a
         * corrupted or absurd width/height (e.g. 0, negative, or a huge
         * garbage value) can't produce a degenerate window -- this is
         * almost certainly what caused the menu bar to render only
         * partially: an oversized window gets auto-centered by the window
         * manager with a negative x origin so its center still lands
         * on-screen, which pushes the window's actual left edge -- where
         * File and Edit live -- off the visible display. Only whatever
         * portion of the menu bar happens to fall within the screen (e.g.
         * Settings/Help) is then visible, appearing shifted to the far
         * left. Bounds are generous on purpose -- 320x240 is below any
         * reasonable use of this app, and 7680x4320 covers 8K displays --
         * this is a sanity check, not a real limit on window size. */
        if (cfg.contains("windowwidth")) {
            int w = qBound(320, cfg.value("windowwidth",  960).toInt(), 7680);
            int h = qBound(240, cfg.value("windowheight", 640).toInt(), 4320);
            resize(w, h);
        }

        /* Apply loaded settings to terminal */
        terminal->setKeyboardLayout(m_settings.keyboard);
        terminal->setFontSize(m_settings.fontSize);
        terminal->setBackgroundBrightness(m_settings.brightness);
        terminal->setClipboardUnicodeTranslationEnabled(m_settings.unicodeClipboard);
        applyDarkMode(m_settings.darkMode);
        m_graphManager->setUseNativeFileDialog(m_settings.nativeFileDialog);
        crash_set_transcript(m_settings.crashTranscript ? 1 : 0);   /* crashlog.c */
    }

    /* Write all settings + current geometry to INI file */
    void saveSettings()
    {
        QSettings cfg(settingsPath(), QSettings::IniFormat);
        cfg.setValue("keyboard",
            m_settings.keyboard == APLTerminalWidget::Classic ? "classic" : "ascii");
        cfg.setValue("fontsize",   m_settings.fontSize);
        cfg.setValue("brightness", m_settings.brightness);
        cfg.setValue("darkmode",   m_settings.darkMode);
        cfg.setValue("unicodeclipboard", m_settings.unicodeClipboard);
        cfg.setValue("nativefiledialog", m_settings.nativeFileDialog);
        cfg.setValue("crashtranscript",  m_settings.crashTranscript);
        cfg.setValue("print_landscape",   m_printLandscape);
        cfg.setValue("print_shrinktofit", m_printShrinkToFit);
        cfg.setValue("print_topdf",       m_printToPdf);
        cfg.setValue("print_preview",     m_printShowPreview);
        cfg.setValue("print_pdfdir",      m_lastPdfDir);
        cfg.setValue("windowwidth",  width());
        cfg.setValue("windowheight", height());
    }

    /* Save only window geometry on close, so final size is always captured
     * even if the user never opened the Settings dialog this session. */
    void saveWindowSize()
    {
        QSettings cfg(settingsPath(), QSettings::IniFormat);
        cfg.setValue("windowwidth",  width());
        cfg.setValue("windowheight", height());
    }

    /* Apply or clear the dark-mode QApplication stylesheet, then re-assert
     * the terminal's own palette in case the stylesheet interfered. */
    void applyDarkMode(bool dark)
    {
        qApp->setStyleSheet(dark ? kDarkStyleSheet : QString());
        terminal->setBackgroundBrightness(m_settings.brightness);
    }

    void openSettingsDialog()
    {
        APLSettingsDialog dlg(terminal, m_settings, this);
        connect(&dlg, &APLSettingsDialog::applyRequested,
                this, &APLMainWindow::applyAndSaveSettings);

        if (dlg.exec() == QDialog::Accepted) {
            applyAndSaveSettings(dlg.currentSettings());
        } else {
            /* Cancelled: restore brightness that may have been live-previewed.
             * The dialog's own reject() also does this, but an extra call is
             * harmless and makes the intent explicit here. */
            terminal->setBackgroundBrightness(m_settings.brightness);
        }
    }

    void applyAndSaveSettings(const APLSettingsDialog::Settings &s)
    {
        m_settings = s;                                /* update stored copy first */
        terminal->setKeyboardLayout(s.keyboard);
        terminal->setFontSize(s.fontSize);
        terminal->setBackgroundBrightness(s.brightness);
        terminal->setClipboardUnicodeTranslationEnabled(s.unicodeClipboard);
        applyDarkMode(s.darkMode);                     /* uses m_settings.brightness */
        m_graphManager->setUseNativeFileDialog(s.nativeFileDialog);
        /* Plain flag, read by the engine thread -- the same arrangement as
         * ABORTSIG. Turning it off and on again starts the transcript
         * afresh (crashlog.c). */
        crash_set_transcript(s.crashTranscript ? 1 : 0);
        saveSettings();
        statusBar()->showMessage("Settings saved", 2000);
    }

    /* ── Print ───────────────────────────────────────────────────────── */
    void printDocument()
    {
        APLPrintOptionsDialog optDlg(m_printLandscape, m_printShrinkToFit,
                                     m_printToPdf, m_printShowPreview, this);
        if (optDlg.exec() != QDialog::Accepted) return;

        /* Remember these choices for next time regardless of what happens
         * next (cancelled printer dialog, failed save, etc.) -- the user
         * picked them, that's enough to remember. */
        m_printLandscape   = (optDlg.orientation() == QPageLayout::Landscape);
        m_printShrinkToFit = optDlg.shrinkToFit();
        m_printToPdf       = (optDlg.destination() == APLPrintOptionsDialog::ToPdf);
        m_printShowPreview = optDlg.showPreview();
        saveSettings();

        QPrinter printer(QPrinter::HighResolution);
        printer.setPageOrientation(optDlg.orientation());

        bool toPdf = (optDlg.destination() == APLPrintOptionsDialog::ToPdf);
        if (toPdf) {
            /* DontUseNativeDialog: Linux's native (GTK/KDE) save dialog is
             * known to substitute its own remembered last-used folder for
             * whatever starting directory an app passes in, once it has
             * its own history for that app -- which is exactly why
             * m_lastPdfDir appeared to have no effect. Qt's own dialog
             * always honors the directory we give it. */
            QString path = QFileDialog::getSaveFileName(this, "Save as PDF",
                m_lastPdfDir + "/untitled.pdf", "PDF Files (*.pdf)",
                nullptr, QFileDialog::DontUseNativeDialog);
            if (path.isEmpty()) return;
            if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(path);

            /* Remember the folder, not the filename -- next PDF export
             * starts back in the same place without silently reusing the
             * same filename (which would just overwrite it each time). */
            m_lastPdfDir = QFileInfo(path).absolutePath();
            saveSettings();
        }

        bool shrink = optDlg.shrinkToFit();

        if (optDlg.showPreview()) {
            /* paintRequested fires against whatever printer state is
             * current at that moment -- including any orientation/paper
             * change made via the preview dialog's own toolbar, and again
             * for the final commit when the user actually prints from
             * inside the preview. Sizing and margins are (re)computed
             * fresh inside renderPrintJob() each time, rather than baked
             * in once up front, so the preview and the final printed page
             * always match. */
            QPrintPreviewDialog previewDlg(&printer, this);
            connect(&previewDlg, &QPrintPreviewDialog::paintRequested,
                    this, [this, shrink](QPrinter *p) { renderPrintJob(p, shrink); });
            previewDlg.exec();
        } else {
            if (!toPdf) {
                /* Run the native print dialog *before* sizing is finalized:
                 * choosing a different printer or paper size here can
                 * change the true printable area, which renderPrintJob()
                 * queries fresh (via setFullPage(true) + its own margin
                 * math) once the dialog is accepted. */
                QPrintDialog printDlg(&printer, this);
                if (printDlg.exec() != QDialog::Accepted) return;

                /* The user may have picked "Print to File (PDF)" from
                 * inside the native dialog's own Properties, rather than
                 * our "Save as PDF" destination -- that's a separate,
                 * OS/CUPS-managed file browser we have no control over
                 * (we can't set its starting folder). Best we can do is
                 * notice the choice afterward and keep our own
                 * "remembered folder" in sync for next time. */
                if (!printer.outputFileName().isEmpty()) {
                    m_lastPdfDir = QFileInfo(printer.outputFileName()).absolutePath();
                    saveSettings();
                }
            }
            renderPrintJob(&printer, shrink);
        }

        bool wroteFile = toPdf || !printer.outputFileName().isEmpty();
        statusBar()->showMessage(wroteFile ? "PDF saved" : "Print complete", 2000);
    }

private:
    /* Fixed minimum margin, in millimetres -- not user-adjustable, just
     * enough that output never sits at the physical edge of the page. */
    static constexpr qreal kMarginMM = 12.0;

    /* Shared by the direct-print and print-preview paths so both always
     * build against the printer's current, final geometry at the moment
     * of rendering -- rather than a snapshot taken before the print
     * dialog (or the preview toolbar's own orientation/paper controls)
     * had a chance to change it.
     *
     * This does its own manual pagination rather than calling the
     * convenience QTextDocument::print(QPrinter*) -- that method turned
     * out not to reliably apply QPrinter's page margins in practice (V42
     * tried reasserting them right before printing; no visible change,
     * including on the pure-PDF path that never touches a native print
     * dialog at all, which rules out driver/dialog interference as the
     * cause). setFullPage(true) hands us the entire physical sheet and we
     * inset it by kMarginMM ourselves, so the margin actually used is
     * never in question. This also fixes shrink-to-fit sizing, which
     * previously measured against printer.pageLayout().paintRectPixels()
     * -- the same unreliable automatic margin this bypasses -- and so was
     * silently too generous, leaving lines wider than the page.
     */
    void renderPrintJob(QPrinter *printer, bool shrinkToFit)
    {
        printer->setFullPage(true);

        const qreal dpi = printer->resolution();
        const int marginPx = qRound(kMarginMM / 25.4 * dpi);
        const QRect fullRect = printer->pageLayout().fullRectPixels(dpi);
        const QRect contentRect = fullRect.adjusted(
            marginPx, marginPx, -marginPx, -marginPx);

        int pointSize = shrinkToFit
            ? shrinkToFitPointSize(printer, contentRect.width()) : terminal->getFontSize();

        QTextDocument *printDoc = terminal->createPrintDocument(pointSize);
        /* Without this, the document's layout sizes fonts using the
         * application's screen DPI (~96) regardless of what device it's
         * actually being painted on -- since contentRect above is in real
         * device pixels at the printer's (often 600-1200+) DPI, that
         * mismatch is exactly what produced postage-stamp-sized text in
         * the corner of the page. QTextDocument::print() calls this
         * internally; it's the one piece manual pagination has to redo
         * itself. */
        printDoc->documentLayout()->setPaintDevice(printer);
        printDoc->setDocumentMargin(0);   /* our margin above is the only one */
        printDoc->setPageSize(QSizeF(contentRect.size()));

        QPainter painter(printer);
        painter.translate(contentRect.topLeft());

        const qreal pageHeight = contentRect.height();
        const int pageCount = printDoc->pageCount();
        for (int page = 0; page < pageCount; ++page) {
            if (page > 0) printer->newPage();
            painter.save();
            painter.translate(0, -page * pageHeight);
            QRectF clip(0, page * pageHeight, contentRect.width(), pageHeight);
            printDoc->drawContents(&painter, clip);
            painter.restore();
        }

        delete printDoc;
    }

    /* kapl.ttf's non-ASCII glyphs (overstrikes, APL symbols) aren't
     * guaranteed to share the ASCII characters' advance width, so this
     * measures the widest line's *actual* text via QFontMetrics rather
     * than assuming a fixed per-character width -- avoids under-
     * estimating and still clipping after shrink-to-fit was meant to
     * prevent it. printableWidthPx comes from renderPrintJob()'s own
     * manually-computed content rect, so it's the exact width that will
     * actually be used, not an estimate pulled from the printer's
     * (unreliable) automatic layout. The 0.98 factor is a small safety
     * margin against rounding in the final int() truncation.
     *
     * QFontMetrics is constructed against the printer device itself --
     * without that, it measures at the application's screen DPI (~96)
     * while printableWidthPx is in the printer's real DPI (often
     * 600-1200+), so idealWidth is numerically far smaller than
     * printableWidthPx regardless of actual fit and the shrink branch
     * below never triggers. Same class of bug as the postage-stamp text
     * issue, just affecting this measurement instead of the document's
     * own layout. */
    int shrinkToFitPointSize(QPrinter *printer, int printableWidthPx) const
    {
        int pointSize = terminal->getFontSize();
        QString widest = terminal->widestLine();
        if (widest.isEmpty() || printableWidthPx <= 0) return pointSize;

        QFontMetrics fm(QFont(terminal->fontFamily(), pointSize), printer);
        int idealWidth = fm.horizontalAdvance(widest);

        if (idealWidth > printableWidthPx) {
            double scale = double(printableWidthPx) / double(idealWidth);
            pointSize = qMax(5, int(pointSize * scale * 0.98));
        }
        return pointSize;
    }

    void stopEngine()
    {
        if (!m_engineThread) return;
        {
            QMutexLocker lock(&g_inputMutex);
            g_engineRunning = false;
        }
        g_inputCond.wakeAll();
        m_engineThread->wait(3000);
    }

    void createMenus()
    {
        /* ── File ──────────────────────────────────────────────────────
         *
         * Terminal mode shows: New Function, Open Function, ── , Quit APL
         * Edit mode shows:     Save Function, Save As, ── ,
         *                      Compile & Return, Discard & Return, ── , Quit APL
         *
         * Visibility is toggled in onEditModeEntered / onEditModeExited.
         * Separators are QAction* (returned by addSeparator) and toggle too.
         */
        QMenu *fileMenu = menuBar()->addMenu("&File");

        /* Terminal-mode items */
        m_newFuncAction = new QAction("&New Function...", this);
        m_newFuncAction->setShortcut(QKeySequence::New);
        connect(m_newFuncAction, &QAction::triggered, this, &APLMainWindow::newFunction);
        fileMenu->addAction(m_newFuncAction);

        m_openFuncAction = new QAction("&Open Function...", this);
        m_openFuncAction->setShortcut(QKeySequence::Open);
        connect(m_openFuncAction, &QAction::triggered, this, &APLMainWindow::openFunction);
        fileMenu->addAction(m_openFuncAction);

        m_termSep = fileMenu->addSeparator();   /* hidden during edit mode */

        /* Edit-mode items (hidden on startup) */
        m_saveAction = new QAction("&Save Function", this);
        m_saveAction->setShortcut(QKeySequence::Save);
        connect(m_saveAction, &QAction::triggered, this, &APLMainWindow::saveFunction);
        m_saveAction->setVisible(false);
        fileMenu->addAction(m_saveAction);

        m_saveAsAction = new QAction("Save &As...", this);
        m_saveAsAction->setShortcut(QKeySequence::SaveAs);
        connect(m_saveAsAction, &QAction::triggered, this, &APLMainWindow::saveFunctionAs);
        m_saveAsAction->setVisible(false);
        fileMenu->addAction(m_saveAsAction);

        m_editSep1 = fileMenu->addSeparator();
        m_editSep1->setVisible(false);

        // Confirmed empirically (not just inferred): QTextEdit only claims
        // ShortcutOverride for its own small set of built-in editing
        // shortcuts (Copy/Paste/Cut/Undo/etc.) -- Return and Escape aren't
        // among them, so a plain QAction shortcut here wins outright and
        // pre-empts APLTerminalWidget::keyPressEvent's own handling of
        // those keys entirely, the same as every other shortcut below.
        m_exitEditorAction = new QAction("&Compile && Return", this);
        m_exitEditorAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
        connect(m_exitEditorAction, &QAction::triggered,
                this, &APLMainWindow::exitEditorCompile);
        m_exitEditorAction->setVisible(false);
        fileMenu->addAction(m_exitEditorAction);

        m_exitNoCompileAction = new QAction("&Discard && Return", this);
        m_exitNoCompileAction->setShortcut(QKeySequence(Qt::Key_Escape));
        connect(m_exitNoCompileAction, &QAction::triggered,
                this, &APLMainWindow::exitEditorNoCompile);
        m_exitNoCompileAction->setVisible(false);
        fileMenu->addAction(m_exitNoCompileAction);

        m_editSep2 = fileMenu->addSeparator();
        m_editSep2->setVisible(false);

        /* Always visible */
        QAction *printAction = new QAction("&Print...", this);
        printAction->setShortcut(QKeySequence::Print);
        connect(printAction, &QAction::triggered, this, &APLMainWindow::printDocument);
        fileMenu->addAction(printAction);

        fileMenu->addSeparator();

        QAction *quitAction = new QAction("&Quit APL", this);
        quitAction->setShortcut(QKeySequence::Quit);
        connect(quitAction, &QAction::triggered, this, &QWidget::close);
        fileMenu->addAction(quitAction);

        /* ── Edit ────────────────────────────────────────────────────── */
        QMenu *editMenu = menuBar()->addMenu("&Edit");

        QAction *clearAction = new QAction("&Clear Screen", this);
        clearAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
        connect(clearAction, &QAction::triggered, this, &APLMainWindow::clearScreen);
        editMenu->addAction(clearAction);

        editMenu->addSeparator();

        QAction *homeAction = new QAction("&Home", this);
        homeAction->setShortcut(QKeySequence::MoveToStartOfDocument);
        connect(homeAction, &QAction::triggered, terminal, &APLTerminalWidget::home);
        editMenu->addAction(homeAction);

        QAction *endAction = new QAction("&End", this);
        endAction->setShortcut(QKeySequence::MoveToEndOfDocument);
        connect(endAction, &QAction::triggered, terminal, &APLTerminalWidget::end);
        editMenu->addAction(endAction);

        editMenu->addSeparator();

        m_insertLineAction = new QAction("&Insert Line", this);
        m_insertLineAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
        connect(m_insertLineAction, &QAction::triggered, this, &APLMainWindow::insertLine);
        m_insertLineAction->setVisible(false);
        editMenu->addAction(m_insertLineAction);

        m_deleteLineAction = new QAction("&Delete Line", this);
        m_deleteLineAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        connect(m_deleteLineAction, &QAction::triggered, this, &APLMainWindow::deleteLine);
        m_deleteLineAction->setVisible(false);
        editMenu->addAction(m_deleteLineAction);

        editMenu->addSeparator();

        QAction *undoAction = new QAction("&Undo", this);
        undoAction->setShortcut(QKeySequence::Undo);
        connect(undoAction, &QAction::triggered, terminal, &APLTerminalWidget::undo);
        editMenu->addAction(undoAction);

        QAction *redoAction = new QAction("&Redo", this);
        redoAction->setShortcut(QKeySequence::Redo);
        connect(redoAction, &QAction::triggered, terminal, &APLTerminalWidget::redo);
        editMenu->addAction(redoAction);

        /* ── Settings ─────────────────────────────────────────────────
         * Single menu, single item.  All adjustable settings live in the
         * dialog; nothing is buried in submenus.
         */
        QMenu *settingsMenu = menuBar()->addMenu("&Settings");
        QAction *settingsAction = new QAction("&Settings...", this);
        connect(settingsAction, &QAction::triggered,
                this, &APLMainWindow::openSettingsDialog);
        settingsMenu->addAction(settingsAction);

        /* ── Help ──────────────────────────────────────────────────────
         * Quad Function Reference first: expected to be used far more
         * often than the system command reference (the person's call).
         */
        QMenu *helpMenu = menuBar()->addMenu("&Help");

        QAction *quadRefAction = new QAction("&Quad Function Reference", this);
        connect(quadRefAction, &QAction::triggered, this, &APLMainWindow::openHelpReference);
        helpMenu->addAction(quadRefAction);

        QAction *cmdRefAction = new QAction("&System Command Reference", this);
        connect(cmdRefAction, &QAction::triggered, this, &APLMainWindow::openSystemCommandReference);
        helpMenu->addAction(cmdRefAction);

        QAction *primRefAction = new QAction("&Primitive Function Reference", this);
        connect(primRefAction, &QAction::triggered, this, &APLMainWindow::openPrimitiveReference);
        helpMenu->addAction(primRefAction);

        helpMenu->addSeparator();

        QAction *aboutAction = new QAction("&About", this);
        connect(aboutAction, &QAction::triggered, this, &APLMainWindow::aboutDialog);
        helpMenu->addAction(aboutAction);
    }

    /* ── Member variables ────────────────────────────────────────────── */
    APLTerminalWidget *terminal;
    bool               m_loggingEnabled;
    QFile             *m_logFile;

    /* File menu — terminal-mode items */
    QAction *m_newFuncAction;
    QAction *m_openFuncAction;
    QAction *m_termSep;           /* separator between Open and Quit (terminal mode) */

    /* File menu — edit-mode items */
    QAction *m_saveAction;
    QAction *m_saveAsAction;
    QAction *m_editSep1;          /* separator: Save As → Compile/Discard */
    QAction *m_exitEditorAction;
    QAction *m_exitNoCompileAction;
    QAction *m_editSep2;          /* separator: Discard → Quit */

    /* Edit menu — edit-mode items */
    QAction *m_insertLineAction;
    QAction *m_deleteLineAction;

    EngineThread *m_engineThread;
    GraphManager *m_graphManager;

    /* Help reference windows, one per document -- each created on first
     * use, kept alive (hidden, not destroyed) after that; see
     * APLHelpReferenceWindow's comment and showHelpWindow(). */
    APLHelpReferenceWindow *m_helpWindow    = nullptr;   /* quad functions  */
    APLHelpReferenceWindow *m_cmdHelpWindow = nullptr;   /* system commands */
    APLHelpReferenceWindow *m_primHelpWindow = nullptr;  /* primitives      */

    /* Current persistent settings */
    APLSettingsDialog::Settings m_settings;

    /* Print options, remembered across sessions (same settings.ini) */
    bool m_printLandscape   = true;
    bool m_printShrinkToFit = true;
    bool m_printToPdf       = false;
    bool m_printShowPreview = false;
    QString m_lastPdfDir;   /* folder only, not the filename -- set in loadSettings() */

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (terminal->isEditMode()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "Exit", "Exit editor and quit?",
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::No) {
                event->ignore();
                return;
            }
            terminal->exitEditMode(false);
            {
                QMutexLocker lock(&g_editMutex);
                g_editActive = false;
            }
            g_editCond.wakeAll();
        }
        saveWindowSize();   /* capture final size/position */
        stopEngine();
        event->accept();
    }
};  // end of APLMainWindow

/* ── Entry point ────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("APL Terminal - IBM 2741 Emulator");
    parser.addHelpOption();

    QCommandLineOption logOption("log", "Enable logging of APL input to apl_input.bin");
    parser.addOption(logOption);

    QCommandLineOption testOption("test", "Run in test mode (engine not started)");
    parser.addOption(testOption);

    QCommandLineOption quadOption(QStringList() << "q" << "quad-syntax",
        "Quad-function syntax: both (default; \u2395IO 0 and \u2395IO\u21900), "
        "warn (both, and )ENTER notes each old-form use), "
        "new (only \u2395IO\u21900).",
        "mode", "both");
    parser.addOption(quadOption);

    parser.addPositionalArgument("workspace",
        "Workspace to load at startup (default: mpl)", "[workspace]");
    parser.addPositionalArgument("args",
        "Values passed through to \u2295ARGS", "[args...]");

    parser.process(app);

    bool enableLogging = parser.isSet(logOption);
    bool testMode      = parser.isSet(testOption);

    {
        const QString q = parser.value(quadOption);
        if (q == "both")      quad_syntax = 0;  /* QSYN_BOTH */
        else if (q == "warn") quad_syntax = 1;  /* QSYN_WARN */
        else if (q == "new")  quad_syntax = 2;  /* QSYN_NEW */
        else {
            qWarning("aplterm: -q must be both, warn or new, not '%s'",
                     q.toLocal8Bit().constData());
            parser.showHelp(2);
        }
    }

    QStringList pos = parser.positionalArguments();
    g_startupWsName = (pos.isEmpty() ? QStringLiteral("mpl") : pos.takeFirst()).toLocal8Bit();
    for (const QString &a : pos)
        g_startupArgStorage.append(a.toLocal8Bit());
    for (QByteArray &b : g_startupArgStorage)
        g_startupArgPtrs.append(b.data());

    g_appTitle = QString("RetroAPL %1.%2").arg(get_version()).arg(FRONTEND_VERSION);

    APLMainWindow window(enableLogging);
    window.setWindowTitle(g_appTitle);
    window.show();

    window.reportPendingCrash();   /* before the engine: see its comment */

    if (!testMode)
        window.startEngine();

    return app.exec();
}

#include "main.moc"

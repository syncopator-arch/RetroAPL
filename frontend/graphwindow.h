#ifndef GRAPHWINDOW_H
#define GRAPHWINDOW_H

#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QStringList>

class QPaintEvent;
class QCloseEvent;
class QMouseEvent;

// One instance per open APL graphics window (WOPEN). Owns a retained
// backing QImage; paintEvent() blits it -- drawing ops (Phase 2) will
// paint onto the image via QPainter, not directly onto the widget, so
// content survives resize/minimize/expose events without needing to be
// replayed from a command list.
//
// GraphWindow does not know about the window-number registry. On an
// OS-level close (title-bar close button) it emits closing(handle) and
// lets the owning registry (GraphManager, next file) remove it from the
// QMap<int, GraphWindow*> -- keeps this class usable/testable standalone
// and avoids a back-pointer to the registry.
//
// Threading: this class must only be constructed/destroyed/touched on the
// GUI thread. The engine thread never calls into it directly -- see the
// gw_open()/gw_close()/etc. marshaling layer (graphmanager.cpp, next file)
// for how engine-thread calls reach it via QMetaObject::invokeMethod.

class GraphWindow : public QWidget
{
    Q_OBJECT

public:
    explicit GraphWindow(int handle, const QString &title, int w, int h,
                          QWidget *parent = nullptr);

    // WILOAD. Backing image comes from an already-decoded QImage rather
    // than a blank w,h fill -- window size is whatever the image's size
    // is, not caller-specified. Converted to Format_RGB32 (dropping any
    // alpha channel) so this window's invariant matches every other
    // window's: POLY/WPOINTS/FPOLY/WPIXEL/etc. all assume no alpha.
    // This is also exactly what keeps WILOAD out of any transparency
    // question -- the loaded image simply *becomes* the window's whole
    // content, alpha and all else discarded, same as how a blank WOPEN
    // fill isn't composited onto anything either. See
    // apl_graphics_other_ideas_notes_v1.md's Image Load section.
    explicit GraphWindow(int handle, const QString &title,
                          const QImage &image, QWidget *parent = nullptr);
    ~GraphWindow() override = default;

    int handle() const { return m_handle; }

    QImage &image() { return m_image; }

    // Per-window pen state (Phase 2). PEN sets these; POLY and WPOINTS
    // (for its N x2 / current-size-always case) read them. Explicit
    // r,g,b in, not a packed QRgb -- avoids the QColor(QRgb) alpha-byte
    // trap noted in the context doc (QRgb is 0xAARRGGBB; a bare 0xFF0000
    // would paint fully transparent). Width doubles as POLY's line width
    // and WPOINTS' point size, per the spec -- one PEN call drives both.
    QColor penColor() const { return m_penColor; }
    int penWidth() const { return m_penWidth; }
    void setPen(const QColor &color, int width);

    // POLY's move/draw position, per window. WOPEN's construction leaves
    // this at (0,0); PEN does not touch it -- only POLY's move (p == -1)
    // and draw steps update it.
    QPoint penPos() const { return m_penPos; }
    void setPenPos(const QPoint &pos) { m_penPos = pos; }

    // WTAG / WTAGOF. Free-form label, independent of the window's title
    // -- set via ⎕WTAG, read back via ⎕WTAGOF, matched by prefix or
    // exact string in GraphManager::nums(pattern). Defaults to an empty
    // QString (untagged) with no constructor work needed, same as every
    // other GraphWindow ctor already leaves things like m_penColor
    // defaulted via the member-init list rather than an explicit branch.
    QString tag() const { return m_tag; }
    void setTag(const QString &tag) { m_tag = tag; }

    // WFONT / TWID. Per-window text size, in PIXELS, not points --
    // deliberately: a point size is converted to pixels using the DPI of
    // the paint device, and this window's paint device is m_image, whose
    // DPI is whatever the image carries. A WOPEN window's fresh QImage
    // gets the default (96 in testing), but a WILOAD/WIRELOAD image keeps
    // its file's own DPI metadata -- confirmed in a PySide6 sandbox that
    // 14pt text in a 300-dpi image draws ~3x wider than in a 96-dpi one.
    // Pixel sizing makes the same WFONT setting look the same in every
    // window, and makes TWID's result mean exactly what it says.
    //
    // kDefaultFontPixelSize (14) reproduces the old hardcoded 14pt on the
    // person's own display, which reports 72 logical dpi (14pt == 14px
    // there). The old 14pt was never a fixed pixel size: a fresh QImage
    // takes the screen's logical DPI, so it came out 14px at 72 dpi and
    // 19px at 96 -- confirmed by running the old painter.font()/
    // setPointSize(14) path under QT_FONT_DPI=72 vs. the default 96. 19
    // was first chosen from the sandbox's 96 and was visibly too large
    // on the real display. A fixed pixel default is now the same on
    // every machine, which the old one wasn't. In-
    // class initialized rather than added to both constructors' init
    // lists, same as GraphManager's own m_useNativeFileDialog; no
    // sentinel awareness here -- WFONT's -1 "reset to default" is
    // resolved in GraphManager::setFontSize(), same division of labor as
    // setPen()'s -1 sentinels.
    static constexpr int kDefaultFontPixelSize = 14;
    int fontPixelSize() const { return m_fontPixelSize; }
    void setFontPixelSize(int px) { m_fontPixelSize = px; }

    // WFONT face: an ordered fallback list of family names, applied via
    // QFont::setFamilies(). Empty = the app's default face (the state a
    // window starts in, and what WFONT's 'DEFAULT' restores). Stored as
    // given (already split/trimmed by GraphManager::setFontFace()), not
    // as whatever Qt resolved it to -- resolution happens each time the
    // font is built, same as the size.
    QStringList fontFamilies() const { return m_fontFamilies; }
    void setFontFamilies(const QStringList &families) { m_fontFamilies = families; }

    // Hidden windows (WOPEN/WILOAD's optional visibility element, WCTRL
    // 5/6). Visibility itself is NOT tracked here -- QWidget's own
    // isHidden()/isMinimized() are the source of truth, same as they
    // already are for WCTRL's minimize/restore. The one thing Qt can't
    // report is whether this window has a real position yet: a window
    // opened hidden with x,y == -1,-1 has never been moved and never been
    // shown (so the window manager hasn't placed it either), and its
    // pos() is just Qt's meaningless 0,0 default. GraphManager::size()
    // reports -1,-1 for x,y in exactly that state, so WSIZE's x,y can
    // still be fed back into a later WOPEN and mean "default placement."
    // Set by GraphManager whenever it moves the window (explicit WOPEN/
    // WILOAD x,y, or WCTRL 4) or shows it (initial show or WCTRL 6);
    // never cleared -- hiding a window that has been placed keeps its
    // position (confirmed in the sandbox: pos() survives hide()).
    // In-class initialized, same as m_fontPixelSize below.
    bool isPlaced() const { return m_placed; }
    void markPlaced() { m_placed = true; }

    // WCTRL 6 on a window that was hidden while minimized. Throws away
    // this widget's OS-level (native) window and clears the minimized
    // flag, so the next show() creates a brand-new native window with no
    // state carried over -- exactly the path a first show takes, which
    // is confirmed to behave correctly under GNOME Shell 42. Everything
    // on the Qt side (m_image, pen, font, tag, handle, geometry, widget
    // attributes) is untouched; only the X/OS window underneath is
    // replaced. Must only be called while the window is hidden. Lives
    // here, not in GraphManager, because QWidget::destroy() is protected.
    // See GraphManager::reveal() for why this exists.
    void recreateNativeWindow();

    // WCTRL 5. Records the content-area geometry, then hides. Recorded
    // BEFORE hide() because it doesn't stay accurate afterwards: when the
    // window manager releases a hidden window it reports it back at its
    // old FRAME position, and Qt records that as the content position
    // (openbox: content 51,70 -> 50,50; GNOME 46: 50,87 -> 50,50 --
    // Xvfb tests). A plain show() self-corrects, since the reused native
    // window keeps the WM's frame knowledge, but recreateNativeWindow()
    // would bake the wrong position in -- so it uses this instead.
    // Recorded only if the window is currently shown (a window that was
    // never shown has no WM-reported geometry to go wrong).
    void hideRememberingGeometry();

    // Whether the window was minimized at the moment hideRememberingGeometry()
    // hid it -- the test GraphManager::reveal() uses to decide on
    // recreateNativeWindow(). Recorded at hide time because Qt's own
    // isMinimized() is NOT reliable by show time: under GNOME Shell 46
    // (Xvfb) the WM sometimes reported the withdrawn window as
    // un-minimized, clearing Qt's flag, while still remembering the
    // minimize on that X window -- a plain show() then brought it back
    // minimized. Timing-dependent: some runs kept the flag, some didn't.
    // Cleared by recreateNativeWindow().
    bool wasMinimizedWhenHidden() const { return m_minimizedAtHide; }

    // WSIZE on a hidden window. The frame position recorded by
    // hideRememberingGeometry(), for the same reason the geometry is
    // recorded there: after the hide, Qt's own report is off by the
    // title-bar height under GNOME (37px on the person's GNOME 42.9 --
    // confirmed by manual test; 50,13 vs 50,50 on GNOME 46 in Xvfb).
    // Returns false if nothing is recorded (never shown before being
    // hidden), in which case the current position is already right --
    // the window is either unplaced or exactly where WOPEN's move() put
    // it.
    bool hiddenFramePos(QPoint *out) const
    {
        if (!m_hasFramePosBeforeHide) return false;
        *out = m_framePosBeforeHide;
        return true;
    }

    // WCTRL 4 on a hidden window. An explicit move supersedes what
    // hideRememberingGeometry() recorded: the recorded geometry is
    // dropped, so recreateNativeWindow() falls back to the widget's
    // current geometry (which move() just set, the same way a
    // pre-first-show WOPEN placement works), and the reported frame
    // position becomes the moved-to x,y. Needed because after a move on
    // a hidden, previously-shown window, Qt's own position report stayed
    // stale until the next show (Xvfb: openbox reported 51,70 and GNOME
    // 46 reported 50,50 for a move to 300,200 -- the window itself did
    // show at 300,200).
    void movedWhileHidden(const QPoint &framePos)
    {
        m_geometryBeforeHide = QRect();
        m_framePosBeforeHide = framePos;
        m_hasFramePosBeforeHide = true;
    }

    void flush();

signals:
    void closing(int handle);

    // WCLICK. Left-button press only -- other buttons are simply not
    // reported (decided scope; see apl_graphics_mouse_input_notes_v1.md's
    // button-filter open question). Fires on press, not release, per the
    // same doc's trigger decision. x,y are widget-local pixel coordinates,
    // same space WPOINTS/POLY/etc. already draw in. GraphManager connects
    // to this at window-creation time (open()) for every window, but only
    // acts on it while that handle is in the currently-armed set a
    // WCLICK call populated -- see GraphManager::click().
    void clicked(int handle, int x, int y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int    m_handle;
    QImage m_image;
    QColor m_penColor;
    int    m_penWidth;
    QPoint m_penPos;
    QString m_tag;
    int    m_fontPixelSize = kDefaultFontPixelSize;
    QStringList m_fontFamilies;
    bool   m_placed = false;
    QRect  m_geometryBeforeHide;   // null = nothing recorded
    QPoint m_framePosBeforeHide;   // valid only if m_hasFramePosBeforeHide
    bool   m_hasFramePosBeforeHide = false;
    bool   m_minimizedAtHide = false;
};

#endif // GRAPHWINDOW_H


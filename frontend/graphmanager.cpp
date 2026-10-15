#include "graphmanager.h"
#include "graphwindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPolygon>
#include <QTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QFileDialog>
#include <QUrl>
#include <QDir>
#include <QWindow>

#include "evhub.h"      /* the event hub (backend): every click and
                           window close is reported there; WCLICK's wait
                           lives there since Phase 7a, not here. Until
                           then this file also included depend.h/parse.h/
                           mplmem.h/globals.h, for ABORTSIG alone. */

GraphManager *g_graphManager = nullptr;

GraphManager::GraphManager(QObject *parent)
    : QObject(parent)
{
}

int GraphManager::nextFreeHandle() const
{
    // Lowest unused handle, not a monotonic counter -- QMap<int,...> keeps
    // keys sorted, so this is just walking up from 1 past whatever's
    // already taken. A loop that opens/closes windows repeatedly reuses
    // the same handful of numbers instead of counting up without bound.
    int handle = 1;
    while (m_windows.contains(handle)) handle++;
    return handle;
}

long GraphManager::registerWindow(GraphWindow *win, long x, long y)
{
    int handle = win->handle();

    connect(win, &GraphWindow::closing, this, &GraphManager::onWindowClosing);
    connect(win, &GraphWindow::clicked, this, &GraphManager::onWindowClicked);
    connect(win, &GraphWindow::escapePressed, this, &GraphManager::onWindowEscape);

    // x,y == -1,-1: system-default placement -- skip move(), let the
    // window manager place it. Confirmed real usage (GTEST), not
    // hypothetical -- see context doc. For a window opened hidden, the
    // same rule holds: an explicit x,y is recorded now and used when
    // WCTRL 6 shows it (move() on a hidden widget just stores the
    // position -- confirmed in the sandbox); -1,-1 defers placement to
    // the window manager at show time.
    if (!(x == -1 && y == -1)) {
        win->move((int)x, (int)y);
        win->markPlaced();
    }

    m_windows.insert(handle, win);
    m_creationOrder.append(handle);

    // Graphics windows are output-only -- they should never steal
    // keyboard focus from whatever the user was typing into (the APL
    // terminal, most likely) just by virtue of opening. Two hints set
    // before show() (WA_ShowWithoutActivating so the initial map doesn't
    // activate the window, NoFocus so a later click on it doesn't grab
    // focus either), plus a deferred reclaim in reveal() that hands focus
    // back to the terminal after the platform's own show/activate
    // sequence has run, in case the hints get overridden anyway. Set
    // here, at registration, rather than in reveal(), so a window opened
    // hidden already carries them by the time WCTRL 6 first shows it.
    //
    // KNOWN LIMITATION (Linux/GNOME/Mutter only, confirmed via manual
    // testing): if a graphics window's placement overlaps the terminal
    // window AT MAP TIME, Mutter grants it focus regardless of
    // WA_ShowWithoutActivating, WA_X11DoNotAcceptFocus, or this reclaim
    // -- none of them prevent it. Overlap created AFTER mapping (e.g.
    // resizing the terminal into the graphics window's existing
    // position) does NOT trigger this, so it's specifically a map-time
    // heuristic, not a general overlap rule -- and it's not pointer-
    // position-dependent either. No client-side fix found; workaround is
    // positioning graphics windows so they don't overlap the terminal at
    // open time. CONFIRMED NOT PRESENT ON WINDOWS: focus stays with the
    // terminal correctly regardless of overlap, no workaround needed
    // there -- this really does look like Mutter-specific placement/
    // focus logic rather than anything general about window overlap.
    // Expected (not yet confirmed) to apply equally to a WCTRL 6 show,
    // since that's the same map-time event, just later.
    win->setAttribute(Qt::WA_ShowWithoutActivating);
    win->setFocusPolicy(Qt::NoFocus);

    return handle;
}

void GraphManager::reveal(GraphWindow *win)
{
    QPointer<QWidget> target = m_focusTarget;
    QTimer::singleShot(0, this, [target]() {
        if (!target) return;
        // Don't reclaim focus onto a minimized window -- activateWindow()
        // on a minimized window un-minimizes it as part of activating it
        // (ordinary Qt/X11/Windows behavior, not something disable-able
        // via a flag), which would silently undo an intentional ⎕CTRL 1 0
        // (or the user's own manual minimize) purely as a side effect of
        // opening a graphics window. Checked here, at fire time, not when
        // this lambda was scheduled -- the window's minimized state can
        // change in the 0ms gap between scheduling and firing (unlikely,
        // but this is the correct place to check regardless).
        if (target->window()->isMinimized()) return;
        target->window()->activateWindow();
        target->setFocus();
    });

    // A window hidden while minimized still carries Qt's WindowMinimized
    // flag, and a plain show() maps it asking the window manager for the
    // iconic (minimized) state. On the person's GNOME Shell 42.9 (X11,
    // Qt 6.2.4), the window came back visibly normal while Qt still
    // believed it minimized (WVIS 2); GNOME Shell's refusal of
    // programmatic un-minimize (Phase 4 limitation) kept that stale, and
    // WCTRL 0 then did nothing.
    //
    // First fix tried: showNormal() here instead of show(). Fixed the
    // WVIS report, and worked on openbox, plain Mutter 46 and GNOME Shell
    // 46 with both Qt 6.4 and 6.2.4 -- but on the person's GNOME 42.9 a
    // later WCTRL 0 was still refused until the user minimized once via
    // the title bar. Not reproducible here, so the exact GNOME 42 cause
    // is unknown.
    //
    // Current approach: discard the native window entirely and let show()
    // create a fresh one (GraphWindow::recreateNativeWindow()). That's
    // the same path as a first show, which the person confirmed works on
    // GNOME 42.9 (WCTRL 5, 6, 0 on a never-minimized window minimizes
    // fine). Nothing on the Qt side is lost. Side effect: GNOME may
    // adjust the position slightly when the window is re-created (seen
    // near the top bar in testing: y 50 -> 32).
    //
    // Rejected earlier: clearing the flag at HIDE time -- on X11 it raced
    // the unmap and left the window mapped at the X level while Qt
    // thought it was hidden.
    //
    // The test is "was minimized when WCTRL 5 hid it" (recorded then),
    // not isMinimized() now: GNOME Shell sometimes clears Qt's flag
    // during the withdraw while still remembering the minimize on that
    // X window, so testing only isMinimized() here let a plain show()
    // bring the window back minimized (Xvfb + GNOME Shell 46; timing-
    // dependent). isMinimized() is still checked too, in case the flag
    // is set by some path that didn't go through hideRememberingGeometry().
    if (win->wasMinimizedWhenHidden() || win->isMinimized())
        win->recreateNativeWindow();
    win->show();
    // Once shown, the window manager has given the window a real
    // position even if the caller never supplied one -- see
    // GraphWindow::isPlaced().
    win->markPlaced();
}

long GraphManager::open(const QString &title, long x, long y, long w, long h,
                        bool visible)
{
    if (w <= 0 || h <= 0) return -1;

    int handle = nextFreeHandle();
    GraphWindow *win = new GraphWindow(handle, title, (int)w, (int)h);
    registerWindow(win, x, y);
    if (visible) reveal(win);
    return handle;
}

long GraphManager::openImage(const QString &title, const QImage &image, long x, long y,
                             bool visible)
{
    if (image.isNull()) return -1;

    int handle = nextFreeHandle();
    GraphWindow *win = new GraphWindow(handle, title, image);
    registerWindow(win, x, y);
    if (visible) reveal(win);
    return handle;
}

void GraphManager::close(long handle)
{
    auto it = m_windows.find((int)handle);
    if (it == m_windows.end()) return;

    GraphWindow *win = it.value();
    m_windows.erase(it);
    removeFromCreationOrder((int)handle);
    evhub_closed(handle);   // ends a WCLICK wait on it, if any

    // Deliberately not going through win->close(): that would re-enter
    // closeEvent(), which would emit closing() for a handle we've already
    // removed from the map (onWindowClosing() would then no-op on it, but
    // it's simpler to just not double-run the teardown path at all). This
    // WCLOSE path owns tearing the widget down itself; closeEvent()/
    // closing() is only for the OS-level close path.
    win->disconnect(this);
    win->deleteLater();
}

QVector<long> GraphManager::nums() const
{
    QVector<long> result;
    result.reserve(m_creationOrder.size());
    for (int handle : m_creationOrder) result.append(handle);
    return result;
}

QVector<long> GraphManager::nums(const QString &pattern) const
{
    QVector<long> result;
    bool prefixMatch = pattern.endsWith(QLatin1Char('*'));
    QString needle = prefixMatch ? pattern.left(pattern.size() - 1) : pattern;

    result.reserve(m_creationOrder.size());
    for (int handle : m_creationOrder) {
        GraphWindow *win = m_windows.value(handle, nullptr);
        if (!win) continue;   // m_creationOrder/m_windows are kept in
                               // sync by close()/onWindowClosing(), but
                               // don't dereference on the strength of
                               // that invariant alone.
        const QString &tag = win->tag();
        bool matches = prefixMatch ? tag.startsWith(needle) : (tag == needle);
        if (matches) result.append(handle);
    }
    return result;
}

QVector<long> GraphManager::size(long handle) const
{
    QVector<long> result;

    // -1 sentinel: no handle given at all (WSIZE called niladically) ->
    // desktop dimensions, position 0,0 (the desktop has no "position" of
    // its own -- 0,0 is just the fixed origin of the coordinate system
    // WOPEN's x,y placement and this same function's window case both
    // report into). 0 is NOT special-cased here -- it's just an ordinary
    // handle nothing has ever opened, so it falls through to the "not
    // currently open" empty-result path below like any other unrecognized
    // handle.
    if (handle == -1) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QSize s = screen->size();
            result << (long)s.width() << (long)s.height();
        } else {
            result << 0 << 0;
        }
        result << 0 << 0;   // desktop position
        return result;
    }

    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return result;   // empty -- signals failure to gw_size()

    QSize s = win->image().size();
    result << (long)s.width() << (long)s.height();

    // Never placed (opened hidden at -1,-1, never moved or shown since):
    // report -1,-1 rather than Qt's meaningless 0,0 for an unplaced
    // widget -- see this function's header comment.
    if (!win->isPlaced()) {
        result << -1L << -1L;
        return result;
    }

    // Frame position -- the exact inverse of registerWindow()'s
    // win->move((int)x,(int)y) (a top-level QWidget's move()/pos() are
    // both frame positions), since the whole point is that WSIZE's x,y
    // round-trips through the same coordinate system WOPEN/WILOAD's
    // placement argument uses.
    //
    // Read from the QWindow's framePosition() when a native window
    // exists, not QWidget::pos(). The two agree in every case tested
    // EXCEPT after GraphWindow::recreateNativeWindow(): under GNOME Shell
    // the widget's cached frame margins aren't refreshed for the new
    // native window, so pos() reported the content-area origin (one
    // title bar too low, 50,87 vs the real 50,50 -- _NET_FRAME_EXTENTS
    // confirmed the title bar was there), while framePosition(), which
    // asks the platform window, stayed correct (Xvfb + GNOME Shell 46
    // test). pos() remains the fallback for a window that has never had
    // a native window (opened hidden, never shown).
    //
    // Hidden after having been shown: neither is right -- the window
    // manager reports a hidden window back at its old frame position and
    // Qt misreads that as the content position, so both come out one
    // title bar too high under GNOME (37px on the person's GNOME 42.9).
    // Use the position recorded just before the hide instead (see
    // GraphWindow::hiddenFramePos()).
    QPoint p;
    if (!(win->isHidden() && win->hiddenFramePos(&p))) {
        p = win->windowHandle() ? win->windowHandle()->framePosition()
                                : win->pos();
    }
    result << (long)p.x() << (long)p.y();
    return result;
}

QVector<long> GraphManager::pixel(long handle, const QVector<long> &points) const
{
    QVector<long> result;

    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return result;   // empty -- signals failure to gw_pixel(),
                                // same convention size() already uses

    const QImage &img = win->image();
    long n = points.size() / 2;
    result.reserve(n);
    for (long i = 0; i < n; ++i) {
        long x = points[i * 2 + 0];
        long y = points[i * 2 + 1];
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) {
            result.append(-1);
            continue;
        }
        QRgb rgb = img.pixel((int)x, (int)y);
        // Format_RGB32 always reports full opacity (confirmed, not
        // assumed -- see GraphWindow's constructor), so masking off the
        // top byte here only ever discards an always-0xFF alpha value,
        // never real color data. What's left is exactly 0xRRGGBB --
        // the same packing unpackColor() uses, just going the other
        // direction.
        result.append((long)(rgb & 0x00FFFFFFu));
    }
    return result;
}

void GraphManager::setTitle(long handle, const QString &title)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (win) win->setWindowTitle(title);
}

void GraphManager::ctrl(long handle, long opcode, long x, long y)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;
    switch (opcode) {
        case 0:   /* minimize -- no-op while hidden (see header) */
            if (!win->isHidden()) win->showMinimized();
            break;
        case 1:   /* restore -- no-op while hidden (see header) */
            if (!win->isHidden()) win->showNormal();
            break;
        case 2: win->raise();              break;
        case 3: win->lower();              break;
        case 4:
            win->move((int)x,(int)y);
            win->markPlaced();
            // Hidden: the explicit position supersedes anything recorded
            // at hide time, for both re-creation and WSIZE (see
            // GraphWindow::movedWhileHidden()).
            if (win->isHidden()) win->movedWhileHidden(QPoint((int)x,(int)y));
            break;
        case 5: win->hideRememberingGeometry(); break;
        case 6:   /* show -- no-op if not hidden (see header) */
            if (win->isHidden()) reveal(win);
            break;
        default: break;   /* grfuncs.c already validates; defensive only */
    }
}

void GraphManager::setTag(long handle, const QString &tag)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (win) win->setTag(tag);
}

QString GraphManager::tagOf(long handle) const
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    return win ? win->tag() : QString();
}

QColor GraphManager::unpackColor(long packed)
{
    int r = (packed >> 16) & 0xFF;
    int g = (packed >> 8) & 0xFF;
    int b = packed & 0xFF;
    return QColor(r, g, b);
}

void GraphManager::setPen(long handle, int r, int g, int b, int width)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;
    // width == -1: "leave unchanged" sentinel (real widths are always
    // >=1). Read the current width straight off the GraphWindow and
    // re-supply it -- safe, this slot already runs on the GUI thread,
    // same as win itself. GraphWindow::setPen() stays a plain
    // unconditional "set both," no sentinel awareness pushed down into it.
    //
    // r == -1: the same sentinel, now also covering color -- "leave
    // color unchanged, set width only" (PEN's new re-length-1,
    // non-query-value case; g,b are -1 too by construction from
    // grfuncs.c, but only r is actually checked here, same as gw_pen()'s
    // own header comment specifies). Reads the current color back off
    // the window and re-supplies it, exactly mirroring the width
    // handling immediately above -- the two sentinels are independent
    // and can't both be real width/color-component values at once
    // (real r/g/b are always >=0), so checking them separately here
    // (rather than requiring both or neither) is safe and lets a future
    // caller in principle set one without the other via two separate
    // calls, same as PEN's length-3 case already lets color be set
    // without touching width.
    QColor c = (r == -1) ? win->penColor() : QColor(r, g, b);
    int w = (width == -1) ? win->penWidth() : width;
    win->setPen(c, w);
}

QVector<long> GraphManager::pen(long handle) const
{
    QVector<long> result;

    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return result;   // empty -- signals failure to
                                // gw_pen_query(), same convention
                                // size()/pixel() already use

    QColor c = win->penColor();
    result << (long)c.red() << (long)c.green() << (long)c.blue()
           << (long)win->penWidth();
    return result;
}

void GraphManager::poly(long handle, const QVector<long> &ops)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;

    QPainter painter(&win->image());
    QPen pen(win->penColor(), win->penWidth());
    painter.setPen(pen);

    long n = ops.size() / 3;
    for (long i = 0; i < n; ++i) {
        long p = ops[i * 3 + 0];
        long x = ops[i * 3 + 1];
        long y = ops[i * 3 + 2];
        QPoint pt((int)x, (int)y);
        if (p == -1) {
            win->setPenPos(pt);
        } else {
            painter.drawLine(win->penPos(), pt);
            win->setPenPos(pt);
        }
    }
    win->flush();
}

void GraphManager::points(long handle, const QVector<long> &data, int ncols)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win || (ncols != 2 && ncols != 3)) return;

    QPainter painter(&win->image());
    int width = win->penWidth();
    long n = data.size() / ncols;

    for (long i = 0; i < n; ++i) {
        long x = data[i * ncols + 0];
        long y = data[i * ncols + 1];
        QColor color = (ncols == 3) ? unpackColor(data[i * ncols + 2])
                                     : win->penColor();
        QRect r((int)x - width / 2, (int)y - width / 2, width, width);
        painter.fillRect(r, color);
    }
    win->flush();
}

void GraphManager::clear(long handle, long packedColor)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;
    win->image().fill(unpackColor(packedColor));
    win->flush();
}

void GraphManager::fpoly(long handle, const QVector<long> &ops)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;

    long n = ops.size() / 2;
    QPolygon poly;
    poly.reserve(n);
    for (long i = 0; i < n; ++i) {
        long x = ops[i * 2 + 0];
        long y = ops[i * 2 + 1];
        poly << QPoint((int)x, (int)y);
    }

    QPainter painter(&win->image());
    // No border stroke -- fill only. Qt's default pen would otherwise
    // draw a 1px black outline around the fill, which nobody asked for
    // and wasn't part of the spec (fill color == current PEN color; a
    // same-color border would be invisible anyway, so the design is
    // fill-only, not "invisible border").
    painter.setPen(Qt::NoPen);
    painter.setBrush(win->penColor());
    // OddEvenFill (Qt's own default -- named explicitly here rather than
    // relying on the default, since which rule applies is a real design
    // decision, not an incidental one): correctly fills self-intersecting
    // shapes like a figure-8 with no extra validation needed on our end.
    painter.drawPolygon(poly, Qt::OddEvenFill);
    win->flush();
}

QFont GraphManager::textFont(const GraphWindow *win)
{
    // Default-constructed QFont is the application default font -- the
    // same thing text()'s old `painter.font()` on a fresh QPainter over
    // the window's QImage resolved to, so the family is unchanged.
    // setPixelSize(), not setPointSize(): see GraphWindow::
    // fontPixelSize()'s comment for why (point sizes scale with the
    // backing image's DPI, which WILOAD/WIRELOAD images carry from their
    // files).
    QFont font;
    const QStringList families = win->fontFamilies();
    if (!families.isEmpty()) font.setFamilies(families);
    font.setPixelSize(win->fontPixelSize());
    return font;
}

void GraphManager::text(long handle, const QString &text, long x, long y)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;

    QPainter painter(&win->image());
    // On for this call specifically -- a different quality/cost tradeoff
    // than WPOINTS' deliberately-off antialiasing for bulk rect plotting
    // (see that function's own comment); text is infrequent and benefits
    // visually from it.
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    // Window's current WFONT size, via the same helper textWidth() (TWID)
    // measures with. Was a hardcoded 14pt before WFONT existed; the
    // default pixel size (14) reproduces that on a 72-logical-dpi display
    // -- see GraphWindow::kDefaultFontPixelSize.
    painter.setFont(textFont(win));
    // Current PEN color, same source POLY/WPOINTS/FPOLY already use --
    // no separate color argument for WTEXT.
    painter.setPen(win->penColor());
    // Native Qt baseline semantics, taken as-is rather than adjusted to
    // a top-left anchor -- (x,y) is where the text sits on its baseline,
    // not its top-left corner.
    painter.drawText((int)x, (int)y, text);
    win->flush();
}

long GraphManager::fontSize(long handle) const
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return -1;
    return (long)win->fontPixelSize();
}

long GraphManager::setFontSize(long handle, int pixelSize)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return -1;

    long prior = (long)win->fontPixelSize();
    if (pixelSize == -1) pixelSize = GraphWindow::kDefaultFontPixelSize;
    win->setFontPixelSize(pixelSize);
    return prior;
}

long GraphManager::setFontFace(long handle, const QString &spec)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return -1;

    QStringList families;
    if (spec.trimmed().compare(QLatin1String("DEFAULT"), Qt::CaseInsensitive) != 0) {
        const QStringList parts = spec.split(QLatin1Char(','));
        for (const QString &part : parts) {
            QString name = part.trimmed();
            if (!name.isEmpty()) families.append(name);
        }
    }
    // families empty here means DEFAULT (grfuncs.c has already rejected a
    // spec that names nothing at all, so that's the only way to get here
    // with an empty list).
    win->setFontFamilies(families);
    return (long)win->fontPixelSize();
}

QString GraphManager::fontFace(long handle) const
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return QString();
    return QFontInfo(textFont(win)).family();
}

long GraphManager::textWidth(long handle, const QString &text) const
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return -1;

    // Measured against the window's own backing image as the paint
    // device -- the same device text()'s QPainter draws on. With a pixel-
    // sized font the device's DPI doesn't change the result (checked in
    // the sandbox), but naming the real device keeps this correct even if
    // textFont() ever goes back to a point-based size. Whole string, not
    // per character: kerning means width("AV") != width("A")+width("V").
    QFontMetricsF fm(textFont(win), &win->image());
    return (long)qRound(fm.horizontalAdvance(text));
}

void GraphManager::fill(long handle, long x, long y, long tol)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return;

    QImage &img = win->image();
    int w = img.width();
    int h = img.height();
    if (x < 0 || y < 0 || x >= w || y >= h) return;   // seed out of bounds

    if (tol < 0) tol = 0;   // permissive clamp, same spirit as PEN's width

    QRgb seed = img.pixel((int)x, (int)y) & 0x00FFFFFFu;   // strip the
        // always-0xFF alpha byte, same mask pixel()/unpackColor() already
        // use elsewhere in this file
    int seedR = qRed(seed), seedG = qGreen(seed), seedB = qBlue(seed);

    QColor pen = win->penColor();
    QRgb fillColor = qRgb(pen.red(), pen.green(), pen.blue());

    // Nothing visibly changes if the seed is already the fill color --
    // skip the whole traversal rather than doing real work for a no-op.
    if (qRgb(seedR, seedG, seedB) == fillColor) return;

    // Plain visited-set flood fill, not a scanline-optimized one --
    // matches this module's existing simplicity-over-raw-performance
    // choices elsewhere (pixel()/WPIXEL already loops QImage::pixel()
    // one point at a time). An explicit stack (not recursion) avoids any
    // call-depth risk on a large contiguous region.
    QVector<bool> visited(w * h, false);
    QVector<QPoint> stack;
    stack.reserve(256);
    stack.append(QPoint((int)x, (int)y));
    visited[(int)y * w + (int)x] = true;

    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };

    while (!stack.isEmpty()) {
        QPoint p = stack.takeLast();
        img.setPixel(p, fillColor);
        for (int i = 0; i < 4; ++i) {
            int nx = p.x() + dx[i];
            int ny = p.y() + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            int idx = ny * w + nx;
            if (visited[idx]) continue;
            QRgb c = img.pixel(nx, ny) & 0x00FFFFFFu;
            // Always compared against the ORIGINAL seed color, never the
            // neighbor that led here -- standard bucket-fill tolerance
            // definition, avoids drifting across a smooth gradient one
            // small step at a time.
            if (qAbs(qRed(c) - seedR) > tol) continue;
            if (qAbs(qGreen(c) - seedG) > tol) continue;
            if (qAbs(qBlue(c) - seedB) > tol) continue;
            visited[idx] = true;
            stack.append(QPoint(nx, ny));
        }
    }
    win->flush();
}

void GraphManager::blit(long dst, long src, long dx, long dy,
                        long sx, long sy, long sw, long sh)
{
    GraphWindow *dstWin = m_windows.value((int)dst, nullptr);
    GraphWindow *srcWin = m_windows.value((int)src, nullptr);
    if (!dstWin || !srcWin) return;

    const QImage &simg = srcWin->image();

    // -1,-1 size: whole source image (internal sentinel, see header).
    QRect want = (sw == -1 && sh == -1)
               ? simg.rect()
               : QRect((int)sx, (int)sy, (int)sw, (int)sh);

    // Clip to the source first. Whatever got trimmed off the left/top
    // moves the destination point by the same amount, so the surviving
    // pixels land exactly where they would have without clipping.
    QRect have = want & simg.rect();
    if (have.isEmpty()) return;
    QPoint to((int)dx + (have.x() - want.x()),
              (int)dy + (have.y() - want.y()));

    // Copy out first: makes src == dst (overlapping regions) safe, since
    // the painter never reads from the image it's writing.
    QImage piece = simg.copy(have);

    QPainter painter(&dstWin->image());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(to, piece);   // QPainter clips against dst's bounds
    painter.end();
    dstWin->flush();
}

long GraphManager::reloadImage(long handle, const QImage &image)
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return -2;
    if (win->image().size() != image.size()) return -3;

    // Same conversion GraphWindow's own image-based constructor already
    // applies (see graphwindow.h) -- keeps a reloaded window's invariant
    // identical to a freshly-WILOAD'd one, so a source file's bit depth/
    // color mode never matters here either, only its pixel dimensions
    // (already confirmed equal by the size() check above).
    win->image() = image.convertToFormat(QImage::Format_RGB32);
    win->flush();
    return handle;
}

QImage GraphManager::imageForSave(long handle) const
{
    GraphWindow *win = m_windows.value((int)handle, nullptr);
    if (!win) return QImage();
    return win->image();
}

void GraphManager::onWindowClosing(int handle)
{
    m_windows.remove(handle);
    removeFromCreationOrder(handle);
    evhub_closed(handle);   // ends a WCLICK wait on it, if any
}

void GraphManager::removeFromCreationOrder(int handle)
{
    m_creationOrder.removeOne(handle);
}

// Every left click is reported to the event hub, waited for or not; the
// hub drops it unless a WCLICK is waiting on this window (Phase 7a; until
// then GraphManager::click() ran the wait here, in a nested QEventLoop).
void GraphManager::onWindowClicked(int handle, int x, int y)
{
    evhub_click(handle, x, y);
}

// Escape with a graphics window active (Phase 7b): ends a WCLICK/EVWAIT
// wait in progress; with nothing waiting, nothing happens, as in the
// terminal.
void GraphManager::onWindowEscape()
{
    evhub_escape();
}

QVector<long> GraphManager::visState(const QVector<long> &handles) const
{
    QVector<long> result;
    result.reserve(handles.size());
    for (long h : handles) {
        GraphWindow *win = m_windows.value((int)h, nullptr);
        if (!win) {
            result.append(-1);          // not open
        } else if (win->isHidden()) {
            result.append(0);           // hidden -- checked before
                                        // minimized on purpose: a window
                                        // hidden while minimized keeps
                                        // Qt's minimized flag, but isn't
                                        // on screen in any form
        } else if (win->isMinimized()) {
            result.append(2);           // minimized
        } else {
            result.append(1);           // shown
        }
    }
    return result;
}

// Mirrors APLTerminalWidget::getFnsDirectory()'s fallback chain exactly
// ($APLHOME -> $HOME/apl -> current directory), just without appending a
// subfolder -- FDIALOG's sidebar shortcut is meant to reach $APLHOME
// itself, not any one particular thing under it (a file dialog isn't
// only ever used for images, or any other single subfolder). Same
// reason this is a C++-side mirror of get_aplhome_dir() rather than a
// call across to the engine's own copy -- established precedent
// (getFnsDirectory()'s own comment points at get_aplhome_dir() in
// linux_platform.c), not a new pattern introduced here.
static QString aplHomeDir()
{
    QByteArray aplHome = qgetenv("APLHOME");
    if (!aplHome.isEmpty())
        return QString::fromLocal8Bit(aplHome);

    QByteArray home = qgetenv("HOME");
    if (!home.isEmpty())
        return QString::fromLocal8Bit(home) + "/apl";

    return QString(".");
}

QString GraphManager::fileDialog(const QString &title, const QString &filter)
{
    // Parented to m_focusTarget when set (see this method's header
    // comment) -- same QPointer open()/openImage() already reclaim
    // focus through, null-checked the same permissive way (a
    // parentless dialog still works, just isn't modal to a specific
    // top-level window).
    QFileDialog dlg(m_focusTarget ? m_focusTarget.data() : nullptr, title);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setAcceptMode(QFileDialog::AcceptOpen);
    if (!filter.isEmpty()) dlg.setNameFilter(filter);

    // DontUseNativeDialog: opts out of the platform's own file-chooser
    // plugin (GTK's, via xdg-desktop-portal, under GNOME) in favor of
    // Qt's own drawn dialog -- an ordinary top-level QWidget the window
    // manager treats like any other window, freely movable/resizable,
    // no portal involvement, and one that picks up this app's own
    // Dark Mode stylesheet (kDarkStyleSheet in main.cpp explicitly
    // targets QDialog) since it's a real widget in this same
    // QApplication rather than a separate GTK process. Added
    // specifically because the native dialog was confirmed (by the
    // person, on a real GNOME session, not a test sandbox) to render
    // oversized and effectively "attached" near its parent, resistant
    // to being moved out of the way -- same category of confirmed,
    // GNOME-specific desktop-integration quirk as WCTRL's
    // restore-under-GNOME-Shell limitation elsewhere in this file, and
    // independently corroborated by main.cpp's own PDF-save dialog,
    // which already had to reach for this same flag for a different
    // confirmed native-dialog quirk (ignoring the app-provided starting
    // directory in favor of its own remembered history).
    //
    // Now a person-facing Settings toggle (m_useNativeFileDialog,
    // default false) rather than hardcoded, so someone who'd rather
    // have native OS integration (bookmarks, recent files) back, and
    // doesn't mind the sizing/movability quirk, can opt back into it --
    // see setUseNativeFileDialog()'s own comment.
    dlg.setOption(QFileDialog::DontUseNativeDialog, !m_useNativeFileDialog);

    // $APLHOME sidebar shortcut -- prepended to Qt's own stock sidebar
    // entries (Home, Desktop, etc.), not replacing them, so ordinary
    // navigation still works alongside this one. Only added if the
    // resolved directory actually exists, so someone who's never set
    // $APLHOME and has no ~/apl yet doesn't get a shortcut to nowhere.
    // Deliberately just $APLHOME itself, not $APLHOME/images or any
    // other subfolder -- FDIALOG is general-purpose, per the person's
    // own reasoning, not image-loading-specific the way WILOAD/WISAVE's
    // own default directory is.
    QString home = aplHomeDir();
    if (QDir(home).exists()) {
        QList<QUrl> sidebars = dlg.sidebarUrls();
        sidebars.prepend(QUrl::fromLocalFile(home));
        dlg.setSidebarUrls(sidebars);
    }

    if (dlg.exec() != QDialog::Accepted) return QString();

    QStringList files = dlg.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

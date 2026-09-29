#include "graph_bridge.h"
#include "graphmanager.h"

#include <QMetaObject>
#include <QString>
#include <QVector>
#include <QImage>
#include <QFileInfo>
#include <QSemaphore>

#include "depend.h"     /* prerequisites for globals.h -- same include
                           order graphmanager.cpp/main.cpp already use */
#include "parse.h"
#include "mplmem.h"
#include "globals.h"    /* ABORTSIG -- read (never cleared) by
                           postDrawing() below */

/*
 * gw_open/gw_close/gw_nums/gw_size/gw_title (Phase 1) all run on the
 * engine thread and block on Qt::BlockingQueuedConnection until the
 * corresponding GraphManager slot has run on the GUI thread -- so
 * capturing locals by reference in those lambdas is safe: this stack
 * frame is still alive for the whole duration of the call, exactly as if
 * it were a normal synchronous function call.
 *
 * gw_pen/gw_poly/gw_points/gw_clear (Phase 2) are different: they use
 * Qt::QueuedConnection instead, since none of them have a result the
 * engine thread needs back. That means this stack frame is NOT
 * guaranteed to still exist when the lambda actually runs -- so those
 * lambdas capture everything they need BY VALUE, and any array argument
 * (ops/data) is copied into a QVector before invokeMethod() is even
 * called, rather than captured as a raw pointer into caller-owned memory
 * (e.g. LSCRATCH, which the engine thread is free to overwrite as soon as
 * this function returns).
 *
 * Every fire-and-forget call goes through postDrawing() below, which
 * limits how many can be waiting on the GUI thread at once.
 */

/* Back-pressure for the fire-and-forget drawing calls (gw_pen, gw_poly,
 * gw_points, gw_clear, gw_fpoly, gw_text, gw_fill, gw_blit).
 *
 * Without a limit, a tight APL drawing loop posts jobs far faster than
 * the GUI thread can run them, and the backlog grows without bound. The
 * GUI thread also handles Ctrl+C, Escape and window close boxes, so those
 * end up waiting behind the whole backlog -- found in manual testing as a
 * hard hang in a WBLIT animation loop without ⎕DL. Measured headless (Qt
 * 6.4, 800x600 windows): a clear+draw loop with no WBLIT posted 1.8
 * million frames in 3 s and needed ~2 minutes to drain, i.e. ~2 minutes
 * before a keypress would be seen. With this limit: worst GUI stall 2 ms
 * (clear+draw) and 6 ms (with a full-window WBLIT each frame), at ~2,500
 * blitted frames/s.
 *
 * Same problem the terminal already solved for text output -- see
 * qt_nl() in main.cpp, which makes each newline blocking "to prevent the
 * Qt event queue from flooding" and leave room for Ctrl+C. A bounded queue
 * is used here instead of making every drawing call blocking, so drawing
 * keeps overlapping with computation and only waits when too far ahead.
 *
 * kMaxQueuedDrawCalls is a judgment call: small enough that even
 * full-window blits keep input latency to a few ms, large enough that
 * ordinary drawing never waits.
 *
 * While waiting for a slot, ABORTSIG is re-checked every 50 ms; if an
 * interrupt has been flagged, the call is dropped (not posted) so the
 * interpreter can reach its own abort check promptly -- a lost frame is
 * irrelevant when the program is being interrupted. This also keeps a
 * thread waiting here from sitting through stopEngine()'s 3 s wait
 * whenever ABORTSIG gets set. ABORTSIG is only read, never cleared (that
 * stays exec_apl_line()'s job, same rule WCLICK's wait loop follows). */
static const int kMaxQueuedDrawCalls = 64;
static QSemaphore g_drawSlots(kMaxQueuedDrawCalls);

template <typename F>
static void postDrawing(F f)
{
    // Once an interrupt is flagged, don't wait at all: take a slot only
    // if one is free right now, otherwise drop the call immediately.
    // (Checking only after each 50 ms wait cost every further drawing
    // call up to 50 ms after Ctrl+C -- found in the sandbox test.)
    for (;;) {
        if (g_drawSlots.tryAcquire(1, ABORTSIG ? 0 : 50)) break;
        if (ABORTSIG) return;
    }
    bool posted = QMetaObject::invokeMethod(g_graphManager, [f]() {
        f();
        g_drawSlots.release();
    }, Qt::QueuedConnection);
    // Couldn't post at all (shouldn't happen while g_graphManager
    // exists): give the slot back rather than leaking it.
    if (!posted) g_drawSlots.release();
}

long gw_open(const char *title, long x, long y, long w, long h, long visible)
{
    if (!g_graphManager) return -1;
    QString qtitle = title ? QString::fromLatin1(title) : QString();
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->open(qtitle, x, y, w, h, visible != 0);
    }, Qt::BlockingQueuedConnection);
    return result;
}

long gw_load_image(const char *filename, long x, long y, long visible)
{
    if (!g_graphManager || !filename || !*filename) return -1;

    // Decode directly on the calling (engine) thread -- see this
    // function's header comment for why that's safe. A failed load
    // (bad path, corrupt file, unsupported format) returns -1 here
    // without ever touching the GUI thread.
    QString qfilename = QString::fromLatin1(filename);
    QImage image(qfilename);
    if (image.isNull()) return -1;

    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->openImage(qfilename, image, x, y, visible != 0);
    }, Qt::BlockingQueuedConnection);
    return result;
}

void gw_close(long handle)
{
    if (!g_graphManager) return;
    QMetaObject::invokeMethod(g_graphManager, [handle]() {
        g_graphManager->close(handle);
    }, Qt::BlockingQueuedConnection);
}

long gw_nums(int32_t *buf, long maxCount)
{
    if (!g_graphManager || !buf || maxCount <= 0) return 0;
    QVector<long> handles;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        handles = g_graphManager->nums();
    }, Qt::BlockingQueuedConnection);
    long n = handles.size();
    if (n > maxCount) n = maxCount;
    for (long i = 0; i < n; ++i) buf[i] = (int32_t)handles[i];
    return n;
}

long gw_nums_matching(const char *pattern, int32_t *buf, long maxCount)
{
    if (!g_graphManager || !buf || maxCount <= 0) return 0;
    QString qpattern = pattern ? QString::fromLatin1(pattern) : QString();

    QVector<long> handles;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        handles = g_graphManager->nums(qpattern);
    }, Qt::BlockingQueuedConnection);

    long n = handles.size();
    if (n > maxCount) n = maxCount;
    for (long i = 0; i < n; ++i) buf[i] = (int32_t)handles[i];
    return n;
}

long gw_size(long handle, int32_t *buf)
{
    if (!buf) return 0;
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    if (!g_graphManager) return 0;
    QVector<long> dims;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        dims = g_graphManager->size(handle);
    }, Qt::BlockingQueuedConnection);
    if (dims.size() >= 4) {
        buf[0] = (int32_t)dims[0];
        buf[1] = (int32_t)dims[1];
        buf[2] = (int32_t)dims[2];
        buf[3] = (int32_t)dims[3];
        return 1;
    }
    return 0;
}

void gw_title(long handle, const char *title)
{
    if (!g_graphManager) return;
    QString qtitle = title ? QString::fromLatin1(title) : QString();
    QMetaObject::invokeMethod(g_graphManager, [handle, qtitle]() {
        g_graphManager->setTitle(handle, qtitle);
    }, Qt::BlockingQueuedConnection);
}

void gw_tag(long handle, const char *tag)
{
    if (!g_graphManager) return;
    QString qtag = tag ? QString::fromLatin1(tag) : QString();
    QMetaObject::invokeMethod(g_graphManager, [handle, qtag]() {
        g_graphManager->setTag(handle, qtag);
    }, Qt::BlockingQueuedConnection);
}

void gw_ctrl(long handle, long opcode, long x, long y)
{
    if (!g_graphManager) return;
    QMetaObject::invokeMethod(g_graphManager, [handle, opcode, x, y]() {
        g_graphManager->ctrl(handle, opcode, x, y);
    }, Qt::BlockingQueuedConnection);
}

void gw_tagof(long handle, char *buf, long bufsize)
{
    if (!buf || bufsize <= 0) return;
    buf[0] = '\0';
    if (!g_graphManager) return;

    QString tag;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        tag = g_graphManager->tagOf(handle);
    }, Qt::BlockingQueuedConnection);

    QByteArray bytes = tag.toLatin1();
    long n = bytes.size();
    if (n > bufsize - 1) n = bufsize - 1;
    const char *src = bytes.constData();
    long i = 0;
    for (; i < n; ++i) buf[i] = src[i];
    buf[i] = '\0';
}

void gw_pen(long handle, long r, long g, long b, long width)
{
    if (!g_graphManager) return;
    // width == -1 ("leave unchanged") is just forwarded through as-is --
    // GraphManager::setPen() is what actually interprets the sentinel,
    // same division of labor as every other marshaling function in this
    // file (this layer copies/forwards, GraphManager owns behavior).
    postDrawing([handle, r, g, b, width]() {
        g_graphManager->setPen(handle, (int)r, (int)g, (int)b, (int)width);
    });
}

long gw_pen_query(long handle, int32_t *buf)
{
    if (!buf) return 0;
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    if (!g_graphManager) return 0;
    QVector<long> pen;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        pen = g_graphManager->pen(handle);
    }, Qt::BlockingQueuedConnection);
    if (pen.size() >= 4) {
        buf[0] = (int32_t)pen[0];
        buf[1] = (int32_t)pen[1];
        buf[2] = (int32_t)pen[2];
        buf[3] = (int32_t)pen[3];
        return 1;
    }
    return 0;
}

void gw_poly(long handle, const int32_t *ops, long count)
{
    if (!g_graphManager || !ops || count <= 0) return;

    QVector<long> opsCopy;
    opsCopy.reserve(count);
    for (long i = 0; i < count; ++i) opsCopy.append((long)ops[i]);

    postDrawing([handle, opsCopy]() {
        g_graphManager->poly(handle, opsCopy);
    });
}

void gw_points(long handle, const int32_t *data, long count, long ncols)
{
    if (!g_graphManager || !data || count <= 0) return;

    QVector<long> dataCopy;
    dataCopy.reserve(count);
    for (long i = 0; i < count; ++i) dataCopy.append((long)data[i]);

    postDrawing([handle, dataCopy, ncols]() {
        g_graphManager->points(handle, dataCopy, (int)ncols);
    });
}

void gw_clear(long handle, long packedColor)
{
    if (!g_graphManager) return;
    postDrawing([handle, packedColor]() {
        g_graphManager->clear(handle, packedColor);
    });
}

void gw_fpoly(long handle, const int32_t *ops, long count)
{
    if (!g_graphManager || !ops || count <= 0) return;

    QVector<long> opsCopy;
    opsCopy.reserve(count);
    for (long i = 0; i < count; ++i) opsCopy.append((long)ops[i]);

    postDrawing([handle, opsCopy]() {
        g_graphManager->fpoly(handle, opsCopy);
    });
}

long gw_click(const int32_t *handles, long count, int32_t *buf)
{
    if (!g_graphManager || !handles || count <= 0 || !buf) return 0;

    QVector<long> handleList;
    handleList.reserve(count);
    for (long i = 0; i < count; ++i) handleList.append((long)handles[i]);

    // BlockingQueuedConnection here, same as gw_open()/gw_nums()/etc.,
    // but GraphManager::click() itself runs a nested QEventLoop that can
    // block for an arbitrarily long, user-paced time -- not the instant
    // GUI round-trip the rest of Phase 1's calls make. That's fine: this
    // call just blocks until click()'s slot returns, however long that
    // takes; the stack frame stays alive throughout, so capturing
    // handleList/result by reference is safe for the same reason noted
    // at the top of this file for the Phase 1 calls.
    QVector<long> result;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->click(handleList);
    }, Qt::BlockingQueuedConnection);

    if (result.size() < 3) return 0;   // cancelled/closed/aborted
    buf[0] = (int32_t)result[0];
    buf[1] = (int32_t)result[1];
    buf[2] = (int32_t)result[2];
    return 1;
}

long gw_vis(const int32_t *handles, long count, int32_t *buf)
{
    if (!handles || !buf || count <= 0) return 0;

    // No graphics manager means nothing can be open -- every entry is
    // the "not open" sentinel, same answer visState() would give.
    if (!g_graphManager) {
        for (long i = 0; i < count; ++i) buf[i] = -1;
        return count;
    }

    QVector<long> handleList;
    handleList.reserve(count);
    for (long i = 0; i < count; ++i) handleList.append((long)handles[i]);

    // Plain, instant GUI-thread round trip, same as gw_pixel().
    QVector<long> result;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->visState(handleList);
    }, Qt::BlockingQueuedConnection);

    // visState() always returns exactly one entry per handle; the bound
    // is just defensive.
    long n = result.size();
    if (n > count) n = count;
    for (long i = 0; i < n; ++i) buf[i] = (int32_t)result[i];
    for (long i = n; i < count; ++i) buf[i] = -1;
    return count;
}

long gw_pixel(long handle, const int32_t *points, long count, int32_t *buf)
{
    if (!g_graphManager || !points || count <= 0 || !buf) return 0;

    QVector<long> pointsCopy;
    pointsCopy.reserve(count);
    for (long i = 0; i < count; ++i) pointsCopy.append((long)points[i]);

    // BlockingQueuedConnection like gw_size()/gw_open() -- a plain,
    // instant GUI-thread round trip, not gw_click()'s indefinite wait.
    QVector<long> result;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->pixel(handle, pointsCopy);
    }, Qt::BlockingQueuedConnection);

    // Empty result is GraphManager::pixel()'s "handle isn't open at all"
    // signal (same convention size() already uses) -- count > 0 above
    // guarantees a real request always gets a non-empty result back
    // otherwise, so there's no ambiguity between "genuinely zero points"
    // and "handle not open" here.
    if (result.isEmpty()) return 0;
    long n = result.size();
    for (long i = 0; i < n; ++i) buf[i] = (int32_t)result[i];
    return 1;
}

void gw_text(const char *text, long handle, long x, long y)
{
    if (!g_graphManager) return;
    QString qtext = text ? QString::fromLatin1(text) : QString();
    // Same Phase 2 fire-and-forget pattern as gw_pen()/gw_clear(): no
    // result the engine thread needs back, so QueuedConnection, and the
    // string is captured by value (already converted to a QString above,
    // not a raw pointer into LSCRATCH) before invokeMethod() -- same
    // reasoning as gw_title()'s existing BlockingQueuedConnection case,
    // just queued instead of blocking here.
    postDrawing([handle, qtext, x, y]() {
        g_graphManager->text(handle, qtext, x, y);
    });
}

long gw_font_query(long handle)
{
    if (!g_graphManager) return -1;
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->fontSize(handle);
    }, Qt::BlockingQueuedConnection);
    return result;
}

long gw_font(long handle, long size)
{
    if (!g_graphManager) return -1;
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->setFontSize(handle, (int)size);
    }, Qt::BlockingQueuedConnection);
    return result;
}

long gw_font_face(long handle, const char *spec)
{
    if (!g_graphManager) return -1;
    QString qspec = spec ? QString::fromLatin1(spec) : QString();
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->setFontFace(handle, qspec);
    }, Qt::BlockingQueuedConnection);
    return result;
}

// Byte-copy tail is identical to gw_tagof()'s -- same "possibly-empty
// QString result into a caller buffer, truncated to bufsize" convention.
void gw_font_face_query(long handle, char *buf, long bufsize)
{
    if (!buf || bufsize <= 0) return;
    buf[0] = '\0';
    if (!g_graphManager) return;

    QString face;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        face = g_graphManager->fontFace(handle);
    }, Qt::BlockingQueuedConnection);

    QByteArray bytes = face.toLatin1();
    long n = bytes.size();
    if (n > bufsize - 1) n = bufsize - 1;
    const char *src = bytes.constData();
    long i = 0;
    for (; i < n; ++i) buf[i] = src[i];
    buf[i] = '\0';
}

long gw_text_width(const char *text, long handle)
{
    if (!g_graphManager) return -1;
    // Same conversion gw_text() uses -- must stay identical, or TWID
    // would measure a different string than WTEXT draws.
    QString qtext = text ? QString::fromLatin1(text) : QString();
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->textWidth(handle, qtext);
    }, Qt::BlockingQueuedConnection);
    return result;
}

void gw_fill(long handle, long x, long y, long tol)
{
    if (!g_graphManager) return;
    // All scalars, nothing to capture by value beyond the arguments
    // themselves -- same fire-and-forget Phase 2 pattern as gw_clear().
    postDrawing([handle, x, y, tol]() {
        g_graphManager->fill(handle, x, y, tol);
    });
}

void gw_blit(long dst, long src, long dx, long dy,
             long sx, long sy, long sw, long sh)
{
    if (!g_graphManager) return;
    // All scalars, captured by value -- same fire-and-forget pattern as
    // gw_fill()/gw_clear().
    postDrawing([dst, src, dx, dy, sx, sy, sw, sh]() {
        g_graphManager->blit(dst, src, dx, dy, sx, sy, sw, sh);
    });
}

long gw_reload_image(long handle, const char *filename)
{
    if (!g_graphManager || !filename || !*filename) return -1;

    // Decode directly on the calling (engine) thread first -- same
    // reasoning as gw_load_image()'s own header comment. A bad path,
    // corrupt file, or unsupported format reports back here directly,
    // without ever touching the GUI thread.
    QString qfilename = QString::fromLatin1(filename);
    QImage image(qfilename);
    if (image.isNull()) return -1;

    // -2 (handle not open) / -3 (dimension mismatch) can only be
    // determined on the GUI thread, where the target window's CURRENT
    // size actually lives -- resolved inside this one
    // BlockingQueuedConnection hop (GraphManager::reloadImage() does the
    // size compare and the swap-in together), not a separate
    // query-then-swap round trip.
    long result = -1;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        result = g_graphManager->reloadImage(handle, image);
    }, Qt::BlockingQueuedConnection);
    return result;
}

long gw_query_image(const char *filename, int32_t *buf)
{
    if (!filename || !*filename || !buf) return 0;

    // No GraphManager/GUI-thread involvement at all -- there's no window
    // to check against, just a file to decode and measure. Same decode
    // call gw_load_image()/gw_reload_image() use for their own first
    // step.
    QImage image(QString::fromLatin1(filename));
    if (image.isNull()) return 0;

    buf[0] = (int32_t)image.width();
    buf[1] = (int32_t)image.height();
    return 1;
}

long gw_save_image(long handle, const char *filename, long overwrite)
{
    if (!g_graphManager || !filename || !*filename) return -1;

    QString qfilename = QString::fromLatin1(filename);

    // No GUI-thread involvement needed just to check whether a file
    // already exists -- same reasoning gw_query_image()'s decode-only
    // check already uses; existence has nothing to do with any window's
    // state.
    if (!overwrite && QFileInfo::exists(qfilename)) return -3;

    // Grab the target window's backing image via ONE
    // BlockingQueuedConnection call -- deliberately just a grab, not the
    // disk write itself (see this function's header comment for why).
    // A null QImage means handle isn't currently open, mirroring
    // gw_size()'s own "empty/null signals failure" convention.
    QImage image;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        image = g_graphManager->imageForSave(handle);
    }, Qt::BlockingQueuedConnection);
    if (image.isNull()) return -2;

    // The actual write happens here, back on the engine thread, AFTER
    // the GUI-thread hop above has already returned -- so a slow save
    // never stalls the GUI thread. Format is inferred from qfilename's
    // extension by QImage::save() itself.
    if (!image.save(qfilename)) return -1;
    return handle;
}

// FDIALOG. BlockingQueuedConnection, same family as gw_size()/
// gw_tagof() -- but the block here lasts as long as the dialog stays
// open, not a near-instant hop; see this function's header comment in
// graph_bridge.h for why that's confirmed safe (threading spike,
// GRAPHICS_NOTES.md). Byte-copy tail is identical to gw_tagof()'s own
// -- both write a possibly-empty QString result into a caller buffer
// with the same truncate-to-bufsize convention.
void gw_file_dialog(const char *title, const char *filter, char *buf, long bufsize)
{
    if (!buf || bufsize <= 0) return;
    buf[0] = '\0';
    if (!g_graphManager) return;

    QString qtitle = title ? QString::fromLatin1(title) : QString();
    QString qfilter = filter ? QString::fromLatin1(filter) : QString();

    QString path;
    QMetaObject::invokeMethod(g_graphManager, [&]() {
        path = g_graphManager->fileDialog(qtitle, qfilter);
    }, Qt::BlockingQueuedConnection);

    QByteArray bytes = path.toLatin1();
    long n = bytes.size();
    if (n > bufsize - 1) n = bufsize - 1;
    const char *src = bytes.constData();
    long i = 0;
    for (; i < n; ++i) buf[i] = src[i];
    buf[i] = '\0';
}

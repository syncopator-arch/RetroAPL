#include "APLTerminalWidget.h"
#include <QFontDatabase>
#include <QFont>
#include <QTextCursor>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QTextBlock>
#include <QMimeData>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include "kapl_unicode_mapping.hpp"
#include <QTextDocument>
#include "depend.h"   // BACKEND_VERSION, FRONTEND_VERSION
#include "graphmanager.h"   // g_graphicsWaitActive, g_graphicsWaitCancelled

static const int INDENT = 5;

// =============================================================================
// Clipboard translation (KAPL font-index <-> real Unicode APL glyphs)
// =============================================================================
//
// Screen codes and .fn file codes are completely untouched by this — those
// keep using the raw 0-255 font-index bytes exactly as before (OrigTrans /
// AsciiTrans / displayLineToStorage / storageLineToDisplay all unchanged).
// This translation exists ONLY at the clipboard boundary: what goes out via
// copy/cut/drag (createMimeDataFromSelection, below) and what comes in via
// paste (insertFromMimeData). See kapl_unicode_mapping.hpp for the table
// and kapl_unicode_mapping.md for how each entry was derived.
// -----------------------------------------------------------------------------

// Incoming clipboard text (from this app, another app, a website, etc.) ->
// KAPL font-index bytes, ready to hand to the existing insertFromMimeData
// logic exactly as if it were typed/loaded today.
static QString translateClipboardTextToKapl(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (QChar ch : text) {
        uint code = ch.unicode();
        if (code < 0x80) {
            // Plain ASCII (letters, digits, common punctuation, control
            // chars like \n) is already identical whether interpreted as
            // raw KAPL index or literal ASCII -- pass through unchanged.
            result.append(ch);
            continue;
        }
        int kaplByte = unicodeToKaplByte(static_cast<char32_t>(code));
        if (kaplByte >= 0) {
            result.append(QChar(static_cast<ushort>(kaplByte)));
        } else if (code > 0xFF) {
            // Not a byte-storable value and not a recognized APL glyph.
            // displayLineToStorage() masks with "& 0xFF", so left alone this
            // could silently fold an unrelated character (emoji, foreign
            // text, etc.) into a byte that *looks* like a valid, different
            // screen code. Substitute a visible placeholder instead of
            // risking silent corruption.
            result.append(QChar('?'));
        } else {
            // Already a raw 0x80-0xFF byte with no clipboard mapping
            // (e.g. pasted from another instance of this same app, or one
            // of the not-currently-reachable leftover glyphs) -- pass
            // through unchanged, matching today's existing behavior.
            result.append(ch);
        }
    }
    return result;
}

static QString formatLineNumber(int lineNum)
{
    // Fixed width of 5 characters: [n] followed by space(s)
    if (lineNum < 10) {
        return QString("[%1]  ").arg(lineNum);   // 5 chars: [0] + space + space
    } else if (lineNum < 100) {
        return QString("[%1] ").arg(lineNum);    // 5 chars: [99] + space
    } else {
        return QString("[%1]").arg(lineNum);      // 5 chars: [100] (no extra space)
    }
}

APLTerminalWidget::APLTerminalWidget(QWidget *parent)
    : QTextEdit(parent)
    , m_overstrikeMode(false)
    , m_overstrikePending(false)
    , m_pendingOverstrike(0)
    , m_lastFontChar(0)
    , m_lastKeyPressed('\0')
    , m_prompt("     ")
    , m_promptLength(INDENT)
    , m_lineStartPos(0)
	, m_inputBlockNumber(0)
    , m_inputEnabled(true)
    , m_keyboardLayout(ASCII)
    , m_fontSize(14)
    , m_editMode(false)
    , m_editorModified(false)
    , m_lastEditLine(0)
    , m_pendEdit(false)
    , m_currentFilePath("")
    , m_isNewFunction(true) 
    , m_backupTakenThisSession(false)
	, m_backgroundBrightness(190)
	, m_overwriteMode(false)
	, m_clipboardUnicodeTranslationEnabled(true)
{
    setAcceptRichText(true);
    /* This widget has its own custom key translation / overstrike system and
     * never uses system input-method composition.  Left enabled, every
     * insertText()/cursor move (including the per-character calls in
     * printOutputOverwrite() during display loops) notifies the platform IME
     * of the new cursor position -- under ibus this floods its IPC channel
     * during rapid-redraw programs like LIFEITER and makes real keyboard
     * input back up for many seconds after the flood ends. */
    setAttribute(Qt::WA_InputMethodEnabled, false);
    /* Undo/redo is meaningless for terminal output (nobody expects Ctrl+Z on
     * program output) and left enabled it accumulates without limit during
     * continuous output -- each edit gets progressively slower as the
     * uncleared undo history grows, which stalls the GUI thread inside the
     * BlockingQueuedConnection calls from the engine thread and makes
     * keyboard input (including Ctrl-C) appear to hang.  Only meaningful in
     * edit mode, where it's turned on explicitly. */
    setUndoRedoEnabled(false);
    setLineWrapMode(QTextEdit::NoWrap);
    
    for (int i = 0; i < 256; ++i) {
        m_asciiInv[i] = static_cast<char>(i);
        m_origInv[i] = static_cast<char>(i);
    }
}

APLTerminalWidget::~APLTerminalWidget() {}

QString APLTerminalWidget::getVersion()
{
    return QString(FRONTEND_VERSION);
}

QString APLTerminalWidget::getEditorTag()
{
    // Mirrors the tag saveToFile() appends to line 1 of every .fn file.
    return QString(" \xE3 ") + APL_PRODUCT_NAME " v" + QString(BACKEND_VERSION);
}

bool APLTerminalWidget::initialize()
{
    if (!loadAPLFont()) {
        qWarning() << "Failed to load KAPL.ttf font";
        return false;
    }
    setupTextFormats();
    buildInverseTables();
	document()->setMaximumBlockCount(1000);
    clearScreen();
    return true;
}

bool APLTerminalWidget::forceSaveToFile(const QString &filename)
{
    if (saveToFile(filename)) {
        m_currentFilePath = filename;
        m_isNewFunction = false;
        QFileInfo info(filename);
        // Files are always saved lowercase (see the Save/Save-As dialogs in
        // main.cpp, which build the path via .toLower()), but the function
        // name itself is kept uppercase everywhere else (enterEditMode()'s
        // callers all pass .toUpper()) -- that's what )ENTER and the rest
        // of the app expect. Without .toUpper() here, this line was
        // silently lowercasing m_currentFunctionName on every save.
        m_currentFunctionName = info.baseName().toUpper();
        // Update the header line to reflect the new filename? No - preserve user's header
        // But we do need to update the stored content if the function name changed?
        // For now, just update the tracking variables
        return true;
    }
    return false;
}

void APLTerminalWidget::insertFromMimeData(const QMimeData *source)
{
    if (!m_inputEnabled) {
        QTextEdit::insertFromMimeData(source);
        return;
    }
    
    if (!source->hasText()) {
        QTextEdit::insertFromMimeData(source);
        return;
    }
    
    QString text = m_clipboardUnicodeTranslationEnabled
                   ? translateClipboardTextToKapl(source->text())
                   : source->text();
    
    if (m_editMode) {
        QTextCursor cursor = textCursor();
        int blockNum = cursor.blockNumber();
        
        // Remove any selected text first
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        }
        
        // Split by newlines to handle multi-line paste
        QStringList lines = text.split('\n', Qt::KeepEmptyParts);
        
		if (lines.size() == 1) {
            // Single line - insert directly
            cursor.insertText(text);
            
            // Update editor content — blockNum may equal m_editorContent.size()
            // if the cursor was on the trailing empty display line, in which case
            // we append a new entry rather than updating an existing one.
            if (blockNum >= 0) {
                QString displayContent = getCurrentLine();
                if (blockNum < m_editorContent.size()) {
                    m_editorContent[blockNum] = displayLineToStorage(displayContent);
                } else if (blockNum == m_editorContent.size()) {
                    m_editorContent.append(displayLineToStorage(displayContent));
                }
            }
		} else {
            // Multi-line paste. If these lines came from a )LIST cut in
            // the terminal, each one may be carrying its display line
            // number along as plain text -- strip that off per line so
            // the editor's own auto-numbering takes over, same as if the
            // lines had been typed fresh. Judged independently per line
            // (not requiring the whole pasted block to be uniformly
            // numbered), and only done here for multi-line pastes -- a
            // single pasted line is much more likely to be a deliberate
            // code fragment that happens to start with a bracket.
            for (QString &line : lines) {
                line = stripPastedLineNumber(line);
            }

            // The cursor may be sitting on the trailing auto-generated
            // blank "next line" display row, which has no backing
            // m_editorContent entry — that's a valid position to append
            // after (index == m_editorContent.size()), NOT something to
            // clamp down into the last real line. Clamping to size()-1
            // here used to make a multi-line paste at the very end of a
            // function silently merge into the last real line and shove
            // it down to the bottom of the pasted block instead of
            // leaving it where it was.
            int safeBlockNum = qMin(blockNum, m_editorContent.size());
            if (safeBlockNum < 0) safeBlockNum = 0;

            QString currentFullLine = (safeBlockNum < m_editorLines.size()) ? 
                                       stripLineNumber(m_editorLines[safeBlockNum]) : "";
            int cursorPosInLine = cursor.columnNumber() - 5;
            if (cursorPosInLine < 0) cursorPosInLine = 0;
            
            QString beforeCursor = currentFullLine.left(cursorPosInLine);
            QString afterCursor = currentFullLine.mid(cursorPosInLine);
            
            // Build new content
            QList<QByteArray> newContent;
            
            // Add all lines before current line
            for (int i = 0; i < safeBlockNum; ++i) {
                newContent.append(m_editorContent[i]);
            }
            
            // First pasted line combines with content before cursor
            newContent.append(displayLineToStorage(beforeCursor + lines[0]));
            
            // Middle pasted lines
            for (int i = 1; i < lines.size() - 1; ++i) {
                newContent.append(displayLineToStorage(lines[i]));
            }
            
            // Last pasted line combines with content after cursor
            if (lines.size() > 1) {
                newContent.append(displayLineToStorage(lines[lines.size() - 1] + afterCursor));
            }
            
            // Add all lines after current line
            for (int i = safeBlockNum + 1; i < m_editorContent.size(); ++i) {
                newContent.append(m_editorContent[i]);
            }
            
            // Replace content and rebuild
            m_editorContent = newContent;
            rebuildFromEditorContent();
            
            // Position cursor at the end of the pasted content
            cursor = textCursor();
            int targetLine = safeBlockNum + lines.size() - 1;
            if (targetLine >= 0 && targetLine < m_editorLines.size()) {
                cursor.movePosition(QTextCursor::Start);
                for (int i = 0; i < targetLine; ++i) {
                    if (!cursor.movePosition(QTextCursor::Down)) break;
                }
                cursor.movePosition(QTextCursor::StartOfBlock);
                // Position at end of the pasted text on that line
                QString targetLineContent = stripLineNumber(m_editorLines[targetLine]);
                int targetColumn = (lines.size() == 1) ? 
                                   beforeCursor.length() + lines[0].length() :
                                   (lines.size() > 1 && targetLine == safeBlockNum + lines.size() - 1) ?
                                   lines[lines.size() - 1].length() : 
                                   lines[targetLine - safeBlockNum].length();
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                                   qMin(5 + targetColumn, m_editorLines[targetLine].length()));
                setTextCursor(cursor);
            }
        }
        
        markModified();
        return;
    }
    
    // Terminal mode - simple insert
    QTextCursor cursor = textCursor();
    cursor.insertText(text);
    ensureCursorVisible();
}

QMimeData *APLTerminalWidget::createMimeDataFromSelection() const
{
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return QTextEdit::createMimeDataFromSelection();
    }

    QString rawSelection;

    if (m_editMode) {
        // Build the copied text straight from each line's real content
        // (via stripLineNumber()), not the raw on-screen selection. A
        // mouse selection that starts at column 0 — i.e. grabbing "whole
        // lines" by dragging from before the "[N]" prefix — otherwise
        // carries those literal bracket/number characters onto the
        // clipboard, and pasting them back re-inserts them as content
        // alongside the editor's own auto-numbering (doubled-up line
        // numbers). Clamp exactly like the deletion path does: a
        // selection edge anywhere within the 5-char prefix is treated as
        // starting/ending right after it, at column 5. This applies
        // regardless of the Unicode-translation setting below — the
        // prefix is never meant to be copyable content either way.
        const QTextDocument *doc = document();
        int selStart = cursor.selectionStart();
        int selEnd   = cursor.selectionEnd();
        QTextBlock startBlockObj = doc->findBlock(selStart);
        QTextBlock endBlockObj   = doc->findBlock(selEnd);

        int startCol = qMax(5, selStart - startBlockObj.position());
        int endCol   = qMax(5, selEnd   - endBlockObj.position());

        if (startBlockObj == endBlockObj) {
            QString lineText = stripLineNumber(startBlockObj.text());
            int from = qMax(0, startCol - 5);
            int to   = qMax(from, endCol - 5);
            rawSelection = lineText.mid(from, to - from);
        } else {
            QStringList lines;
            QString firstLine = stripLineNumber(startBlockObj.text());
            lines.append(firstLine.mid(qMax(0, startCol - 5)));

            for (QTextBlock b = startBlockObj.next(); b.isValid() && b != endBlockObj; b = b.next()) {
                lines.append(stripLineNumber(b.text()));
            }

            QString lastLine = stripLineNumber(endBlockObj.text());
            lines.append(lastLine.left(qMax(0, endCol - 5)));

            rawSelection = lines.join('\n');
        }
    } else {
        rawSelection = cursor.selectedText();
        // QTextCursor::selectedText() uses U+2029 (paragraph separator) for
        // line breaks in a multi-block selection, not '\n'. Normalize so
        // that what we put on the clipboard round-trips correctly through
        // insertFromMimeData (and through any plain-text editor the user
        // pastes into).
        rawSelection.replace(QChar(0x2029), QChar('\n'));
    }

    if (!m_clipboardUnicodeTranslationEnabled) {
        QMimeData *mimeData = new QMimeData();
        mimeData->setText(rawSelection);
        return mimeData;
    }

    QString unicodeText;
    unicodeText.reserve(rawSelection.size());
    for (QChar ch : rawSelection) {
        if (ch.unicode() > 0xFF) {
            // Shouldn't occur today (display buffer is raw 0-255 bytes),
            // but pass through untouched rather than mangle it.
            unicodeText.append(ch);
            continue;
        }
        char32_t u = kaplByteToClipboardUnicode(static_cast<uint8_t>(ch.unicode()));
        unicodeText.append(QChar(static_cast<ushort>(u)));
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setText(unicodeText);
    return mimeData;
}


void APLTerminalWidget::clearUndoStack()
{
    document()->clearUndoRedoStacks();
}

void APLTerminalWidget::buildInverseTables()
{
    for (int i = 0; i < 256; ++i) {
        m_asciiInv[AsciiTrans[i]] = static_cast<char>(i);
        m_origInv[OrigTrans[i]] = static_cast<char>(i);
    }
}

bool APLTerminalWidget::loadAPLFont()
{
	int fontId = -1;
    const char *aplhome = getenv("APLHOME");

    if (aplhome && *aplhome) {
        QDir dir(QString::fromLocal8Bit(aplhome));
        QString fontPath = dir.filePath("fonts/kapl.ttf");
        fontId = QFontDatabase::addApplicationFont(fontPath);
    }

/*
	if (aplhome && *aplhome) {
        QDir dir(QString::fromLocal8Bit(aplhome));
        fontId = QFontDatabase::addApplicationFont(
            dir.filePath("fonts/kapl.ttf"));
    }
*/

/*
    if (fontId == -1) fontId = QFontDatabase::addApplicationFont("kapl.ttf");
    if (fontId == -1) fontId = QFontDatabase::addApplicationFont("/usr/share/fonts/truetype/kapl/kapl.ttf");
    if (fontId == -1) fontId = QFontDatabase::addApplicationFont("../fonts/kapl.ttf");
    if (fontId == -1) fontId = QFontDatabase::addApplicationFont("./fonts/kapl.ttf");
*/
    
    if (fontId != -1) {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        if (!fontFamilies.isEmpty()) {
            m_fontFamily = fontFamilies.at(0);
            setFontSize(14);
//            qDebug() << "Loaded KAPL font:" << m_fontFamily;
            return true;
        }
    }
    return false;
}

/* ── 2. Replace setupTextFormats() ─────────────────────────────────────────
 *
 * Old version had QColor(190, 190, 190) and Qt::black hardcoded.
 * New version reads m_backgroundBrightness and derives text colour from it
 * so that dark backgrounds automatically get white text.
 */
void APLTerminalWidget::setupTextFormats()
{
    int v = m_backgroundBrightness;
    QColor bg(v, v, v);
    QColor fg = (v < 128) ? Qt::white : Qt::black;

    // Explicit widget stylesheet wins over any app-level dark-mode cascade
    setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; }")
                  .arg(bg.name()).arg(fg.name()));

    QPalette p = palette();
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::Text, fg);
    setPalette(p);

    setTextColor(fg);
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    setCurrentCharFormat(fmt);
}

void APLTerminalWidget::setPrompt(const QString &prompt)
{
    m_prompt = prompt;
    m_promptLength = prompt.length();
}

void APLTerminalWidget::clearScreen()
{
    m_overwriteMode = false;   // clear resets overwrite mode
    clear();
    m_lineStartPos = 0;
    m_overstrikeMode = false;
    m_overstrikePending = false;
    m_pendingOverstrike = 0;
    m_lastFontChar = 0;
}

void APLTerminalWidget::home()
{
    m_overwriteMode = true;
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::Start);
    setTextCursor(cursor);
}

void APLTerminalWidget::end()
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
}

QString APLTerminalWidget::getCurrentLine() const
{
    QTextCursor cursor = textCursor();
    int blockNum = cursor.blockNumber();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QString line = cursor.selectedText();

    if (m_editMode) {
        if (line.length() >= 5 && line[0] == '[') return line.mid(5);
        return line;
    }

    int stripLen = (blockNum == m_inputBlockNumber) ? m_lineStartPos : INDENT;
    if (line.length() > stripLen) return line.mid(stripLen);
    return QString();
}

void APLTerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();

    if (m_editMode) {
        // The standard Cut/Delete actions Qt just added above are wired to
        // QTextEdit's own built-in cut()/delete implementations, which act
        // directly on the QTextDocument and know nothing about
        // m_editorContent. That's the "Cut from the right-click menu
        // doesn't stick" bug: the visible lines disappear because Qt did
        // remove them from the document, but the array saveToFile()/)LIST
        // read from was never updated, so the lines reappear next time the
        // function is (re)loaded.
        //
        // Pull the built-ins out of the menu entirely and swap in fresh
        // QActions wired to handleDelete(), which now keeps m_editorContent
        // in sync for selections of any size. Matched by objectName
        // ("edit-cut"/"edit-delete"), not text() — Qt appends a
        // tab-separated shortcut suffix to text() for actions that have
        // one (e.g. "Cu&t\tCtrl+X" for Cut vs. plain "Delete", which has
        // none), which silently broke an earlier text()-based match for
        // Cut specifically. objectName is stable regardless of that.
        QAction *builtinCut = nullptr;
        QAction *builtinDelete = nullptr;
        for (QAction *action : menu->actions()) {
            if (action->objectName() == "edit-cut") builtinCut = action;
            else if (action->objectName() == "edit-delete") builtinDelete = action;
        }

        if (builtinCut) {
            QAction *newCut = new QAction("Cut", menu);
            newCut->setEnabled(builtinCut->isEnabled());
            newCut->setShortcut(builtinCut->shortcut());
            connect(newCut, &QAction::triggered, this, [this]() {
                if (textCursor().hasSelection()) copy();
                handleDelete();
            });
            menu->insertAction(builtinCut, newCut);
            menu->removeAction(builtinCut);
        }
        if (builtinDelete) {
            QAction *newDelete = new QAction("Delete", menu);
            newDelete->setEnabled(builtinDelete->isEnabled());
            newDelete->setShortcut(builtinDelete->shortcut());
            connect(newDelete, &QAction::triggered, this, [this]() {
                handleDelete();
            });
            menu->insertAction(builtinDelete, newDelete);
            menu->removeAction(builtinDelete);
        }

        menu->addSeparator();
        
        QAction *insertAboveAction = menu->addAction("Insert Line Above");
        QAction *insertBelowAction = menu->addAction("Insert Line Below");
        QAction *deleteLineAction = menu->addAction("Delete Current Line");
        menu->addSeparator();
        QAction *gotoLineAction = menu->addAction("Go to Line...");
        
        QTextCursor cursor = textCursor();
        int currentLine = cursor.blockNumber();
        
        connect(insertAboveAction, &QAction::triggered, [this, currentLine]() {
            if (currentLine >= 0 && currentLine <= m_editorContent.size()) {
                m_editorContent.insert(currentLine, QByteArray());
                rebuildFromEditorContent();
                
                // Move cursor to the new line at column 5
                QTextCursor newCursor = textCursor();
                newCursor.movePosition(QTextCursor::Start);
                for (int i = 0; i < currentLine; ++i) {
                    newCursor.movePosition(QTextCursor::Down);
                }
                newCursor.movePosition(QTextCursor::StartOfBlock);
                newCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(newCursor);
                markModified();
            }
        });
        
        connect(insertBelowAction, &QAction::triggered, [this, currentLine]() {
            if (currentLine >= 0 && currentLine <= m_editorContent.size()) {
                m_editorContent.insert(currentLine + 1, QByteArray());
                rebuildFromEditorContent();
                
                // Move cursor to the new line at column 5
                QTextCursor newCursor = textCursor();
                newCursor.movePosition(QTextCursor::Start);
                for (int i = 0; i <= currentLine; ++i) {
                    newCursor.movePosition(QTextCursor::Down);
                }
                newCursor.movePosition(QTextCursor::StartOfBlock);
                newCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(newCursor);
                markModified();
            }
        });
        
        connect(deleteLineAction, &QAction::triggered, [this, currentLine]() {
            if (currentLine >= 0 && currentLine < m_editorContent.size()) {
                deleteLine(currentLine);
            }
        });
        
        connect(gotoLineAction, &QAction::triggered, [this]() {
            bool ok;
            int lineNum = QInputDialog::getInt(this, "Go to Line", 
                "Enter line number (0-" + QString::number(m_editorLines.size() - 1) + "):",
                0, 0, m_editorLines.size() - 1, 1, &ok);
            if (ok) {
                QTextCursor cursor = textCursor();
                cursor.movePosition(QTextCursor::Start);
                for (int i = 0; i < lineNum; ++i) {
                    cursor.movePosition(QTextCursor::Down);
                }
                cursor.movePosition(QTextCursor::StartOfBlock);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(cursor);
                ensureCursorVisible();
            }
        });
    }
    
    menu->exec(event->globalPos());
    delete menu;
}

void APLTerminalWidget::printOutput(const QString &text)
{
    if (m_overwriteMode) {
        printOutputOverwrite(text);
        return;
    }
    QTextCursor cursor = textCursor();
    bool wasAtEnd = cursor.atEnd();
    insertPlainText(text);
    m_lineStartPos = textCursor().positionInBlock();
	m_inputBlockNumber = textCursor().blockNumber();
    if (wasAtEnd) scrollToBottom();
}

void APLTerminalWidget::printOutputOverwrite(const QString &text)
{
    QTextCursor cursor = textCursor();

    for (int i = 0; i < text.length(); ++i) {
        QChar ch = text[i];

        if (ch == '\n') {
            /* Advance to next block; if none, insert one and switch to
             * normal append mode for any remaining text in this call. */
            if (!cursor.movePosition(QTextCursor::NextBlock)) {
                cursor.movePosition(QTextCursor::End);
                cursor.insertBlock();
                if (i + 1 < text.length())
                    cursor.insertText(text.mid(i + 1));
                m_overwriteMode = false;
                setTextCursor(cursor);
                m_lineStartPos = cursor.positionInBlock();
				m_inputBlockNumber = cursor.blockNumber();
                ensureCursorVisible();
                return;
            }
        } else {
            /* Replace the character under the cursor if one exists,
             * otherwise append to the end of the current block. */
            if (!cursor.atBlockEnd()) {
                cursor.movePosition(QTextCursor::NextCharacter,
                                    QTextCursor::KeepAnchor);
            }
            cursor.insertText(QString(ch));
        }
    }

    setTextCursor(cursor);
    m_lineStartPos = cursor.positionInBlock();
	m_inputBlockNumber = cursor.blockNumber();
    ensureCursorVisible();
}

void APLTerminalWidget::printInput(const QString &text)
{
    QTextCursor cursor = textCursor();
    bool wasAtEnd = cursor.atEnd();
    insertPlainText(text);
    m_lineStartPos = textCursor().positionInBlock();
	m_inputBlockNumber = textCursor().blockNumber();
    if (wasAtEnd) scrollToBottom();
}

void APLTerminalWidget::scrollToBottom()
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void APLTerminalWidget::setInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
    setReadOnly(!enabled);
}

void APLTerminalWidget::setFontSize(int pointSize)
{
    m_fontSize = pointSize;
    if (!m_fontFamily.isEmpty()) {
        QFont font(m_fontFamily);
        font.setPointSize(m_fontSize);
        font.setStyleStrategy(QFont::PreferBitmap);
        font.setStyleHint(QFont::TypeWriter);
        setFont(font);
        setTextColor(Qt::black);
    }
}

/* ── 3. New method: setBackgroundBrightness() ──────────────────────────────
 *
 * Called by APLMainWindow::loadSettings(), applyDarkMode(), and live from
 * the brightness slider in APLSettingsDialog.  Safe to call at any time.
 * Add after setFontSize() (around line 429 in the original file).
 */
void APLTerminalWidget::setBackgroundBrightness(int v)
{
    v = qBound(0, v, 255);
    m_backgroundBrightness = v;

    QColor bg(v, v, v);
    QColor fg = (v < 128) ? Qt::white : Qt::black;

    // Explicit widget stylesheet wins over any app-level dark-mode cascade
    setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; }")
                  .arg(bg.name()).arg(fg.name()));

    QPalette p = palette();
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::Text, fg);
    setPalette(p);

    setTextColor(fg);
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    setCurrentCharFormat(fmt);

    viewport()->update();
}

/* ── Printing ─────────────────────────────────────────────────────────────
 *
 * setupTextFormats()/setBackgroundBrightness() above bake the current
 * foreground colour into a QTextCharFormat that governs newly-inserted
 * text, so on-screen content can carry a non-black foreground (e.g. white
 * text in dark mode). Printed output must always be black-on-white
 * regardless of that setting, so this clones the document and overrides
 * colour on the *clone* -- the live widget/document is never touched.
 * A single whole-document select + mergeCharFormat is used rather than
 * walking fragments by hand, so this stays correct even if the terminal
 * ever grows real per-character rich-text colour (e.g. syntax
 * highlighting) instead of today's single whole-widget style.
 */
QTextDocument *APLTerminalWidget::createPrintDocument(int pointSize) const
{
    if (pointSize <= 0) pointSize = m_fontSize;

    QTextDocument *doc = document()->clone();

    QFont printFont(m_fontFamily.isEmpty() ? font().family() : m_fontFamily);
    printFont.setPointSize(pointSize);
    printFont.setStyleStrategy(QFont::PreferBitmap);
    printFont.setStyleHint(QFont::TypeWriter);
    doc->setDefaultFont(printFont);

    QTextCharFormat blackOnWhite;
    blackOnWhite.setForeground(Qt::black);
    blackOnWhite.setBackground(Qt::white);
    blackOnWhite.setFont(printFont);

    QTextCursor cur(doc);
    cur.select(QTextCursor::Document);
    cur.mergeCharFormat(blackOnWhite);

    return doc;
}

QString APLTerminalWidget::widestLine() const
{
    QString widest;
    const QStringList lines = toPlainText().split(QChar('\n'));
    for (const QString &line : lines) {
        if (line.length() > widest.length())
            widest = line;
    }
    return widest;
}

void APLTerminalWidget::setKeyboardLayout(KeyboardLayout layout)
{
    m_keyboardLayout = layout;
}

void APLTerminalWidget::setClipboardUnicodeTranslationEnabled(bool enabled)
{
    m_clipboardUnicodeTranslationEnabled = enabled;
}

void APLTerminalWidget::ensureCursorVisible()
{
    QTextEdit::ensureCursorVisible();
}

void APLTerminalWidget::handleDelete()
{
    QTextCursor cursor = textCursor();
    
    // If there's a selection, delete the selected text
    if (cursor.hasSelection()) {
        QString selectedText = cursor.selectedText();
        if (selectedText.isEmpty()) return;

        m_pendingOverstrike = static_cast<uint8_t>(selectedText.at(0).unicode());
        m_overstrikePending = true;

        if (m_editMode) {
            QTextDocument *doc = document();
            int selStart = cursor.selectionStart();
            int selEnd   = cursor.selectionEnd();
            QTextBlock startBlockObj = doc->findBlock(selStart);
            QTextBlock endBlockObj   = doc->findBlock(selEnd);
            int startBlock = startBlockObj.blockNumber();
            int endBlock   = endBlockObj.blockNumber();

            if (startBlock != endBlock) {
                // Selection spans multiple editor lines. Letting Qt just
                // remove the text merges/removes document *blocks* on its
                // own, but the single-block code path below only ever
                // patches ONE m_editorContent entry (the block the cursor
                // lands on afterwards) — every other line caught in the
                // selection was left sitting in m_editorContent untouched.
                // Since m_editorContent (not the on-screen QTextDocument)
                // is what saveToFile()/)LIST read from, "deleted" lines
                // were never actually gone and came back on next edit or
                // re-)LIST. Splice the model directly instead.
                int startCol = qMax(5, selStart - startBlockObj.position());
                int endCol   = qMax(5, selEnd   - endBlockObj.position());

                QString startLineText = stripLineNumber(startBlockObj.text());
                QString endLineText   = stripLineNumber(endBlockObj.text());

                QString keptPrefix = startLineText.left(qMax(0, startCol - 5));
                QString keptSuffix = endLineText.mid(qMax(0, endCol - 5));

                if (startBlock >= 0 && startBlock < m_editorContent.size()) {
                    m_editorContent[startBlock] = displayLineToStorage(keptPrefix + keptSuffix);
                }
                // Remove the now-consumed interior/end lines.
                for (int i = qMin(endBlock, m_editorContent.size() - 1); i > startBlock; --i) {
                    m_editorContent.removeAt(i);
                }

                rebuildFromEditorContent();

                QTextCursor newCursor = textCursor();
                newCursor.movePosition(QTextCursor::Start);
                for (int i = 0; i < startBlock; ++i) newCursor.movePosition(QTextCursor::Down);
                newCursor.movePosition(QTextCursor::StartOfBlock);
                newCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                                        5 + keptPrefix.length());
                setTextCursor(newCursor);
                markModified();
                return;
            }
        }

        // Selection confined to a single block (or terminal mode) — safe
        // to let Qt remove the text directly and re-sync that one line.
        cursor.removeSelectedText();
        setTextCursor(cursor);

        if (m_editMode) {
            int blockNum = cursor.blockNumber();
            if (blockNum >= 0 && blockNum < m_editorContent.size()) {
                QString displayContent = getCurrentLine();
                m_editorContent[blockNum] = displayLineToStorage(displayContent);
            }
            markModified();
        }
        return;
    }
    
    if (m_editMode) {
        int blockNum = cursor.blockNumber();
        int columnNum = cursor.columnNumber();
        
		if (blockNum < m_editorLines.size()) {
			// Do not allow delete in the 5-char line-number prefix area.
			if (columnNum < 5) return;

			QString lineContent = getCurrentLine();

			// If at end of line, delete the newline (join with next line).
			// columnNum is absolute in the block; content starts at column 5,
			// so end-of-line is column 5 + lineContent.length().
			if (columnNum >= lineContent.length() + 5) {
                if (blockNum < m_editorContent.size() - 1) {
                    // Join current line with next line
                    QByteArray nextLineContent = m_editorContent[blockNum + 1];
                    m_editorContent[blockNum] = m_editorContent[blockNum] + nextLineContent;
                    m_editorContent.removeAt(blockNum + 1);
                    rebuildFromEditorContent();
                    
                    // Restore cursor position at the join point
                    cursor = textCursor();
                    cursor.movePosition(QTextCursor::Start);
                    for (int i = 0; i < blockNum; ++i) cursor.movePosition(QTextCursor::Down);
                    cursor.movePosition(QTextCursor::StartOfBlock);
                    int newColumn = qMin(columnNum, m_editorLines[blockNum].length());
                    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, newColumn);
                    setTextCursor(cursor);
                    markModified();
                }
                return;
            }
            
            // Delete character at cursor (not before)
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            if (!cursor.selectedText().isEmpty()) {
                QString deletedChar = cursor.selectedText();
                m_pendingOverstrike = static_cast<uint8_t>(deletedChar.at(0).unicode());
                m_overstrikePending = true;
                cursor.deleteChar();
                setTextCursor(cursor);
                
                // Update editor content
                QString displayContent = getCurrentLine();
                if (blockNum < m_editorContent.size()) {
                    m_editorContent[blockNum] = displayLineToStorage(displayContent);
                }
                markModified();
            }
        }
    } else {
        // Terminal mode delete - delete character at cursor
        int posInBlock = cursor.positionInBlock();
        if (posInBlock >= m_lineStartPos) {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            if (!cursor.selectedText().isEmpty()) {
                QString deletedChar = cursor.selectedText();
                m_pendingOverstrike = static_cast<uint8_t>(deletedChar.at(0).unicode());
                m_overstrikePending = true;
                cursor.deleteChar();
                setTextCursor(cursor);
            }
        }
    }
    m_lastKeyPressed = QChar(0x7F); // DEL character
}

void APLTerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_inputEnabled) {
        event->ignore();
        return;
    }

    int keyCode = event->key();

	// Add Delete key handling here - before other navigation keys
    if (keyCode == Qt::Key_Delete) {
        handleDelete();
        return;
    }

	if (m_editMode && (event->modifiers() & Qt::ControlModifier) && keyCode == Qt::Key_D) {
		QTextCursor cursor = textCursor();
		int currentLine = cursor.blockNumber();
		if (currentLine >= 0 && currentLine < m_editorContent.size()) {
			deleteLine(currentLine);
		}
		return;
	}

    // Ctrl+Z - Undo
    if ((event->modifiers() & Qt::ControlModifier) && keyCode == Qt::Key_Z) {
        if (m_editMode) {
            undo();
            // m_editorContent must be rebuilt from the ENTIRE document, not
            // just patched at the cursor's line. Qt's undo can restore or
            // remove several lines in one step (a whole cut/paste/insert
            // /delete-line rebuild is now one grouped undo command), so
            // patching only the current line left every other affected
            // line silently stale — the same model/view desync bug we've
            // chased all day, just via Ctrl+Z instead of Cut.
            {
                QTextDocument *doc = document();
                QList<QByteArray> resyncedContent;
                QStringList resyncedLines;
                for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                    resyncedLines.append(b.text());
                    if (b.next().isValid()) {
                        resyncedContent.append(displayLineToStorage(stripLineNumber(b.text())));
                    }
                }
                m_editorContent = resyncedContent;
                m_editorLines = resyncedLines;
            }
            markModified();
        } else {
            undo();
        }
        return;
    }
    
	// Ctrl+Shift+Z - Redo (more standard)
    if ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier) && keyCode == Qt::Key_Z) {
        if (m_editMode) {
            redo();
            // See the Ctrl+Z handler above for why this is a full resync
            // rather than a single-line patch.
            {
                QTextDocument *doc = document();
                QList<QByteArray> resyncedContent;
                QStringList resyncedLines;
                for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                    resyncedLines.append(b.text());
                    if (b.next().isValid()) {
                        resyncedContent.append(displayLineToStorage(stripLineNumber(b.text())));
                    }
                }
                m_editorContent = resyncedContent;
                m_editorLines = resyncedLines;
            }
            markModified();
        } else {
            redo();
        }
        return;
    }
    
    // Ctrl+Y as alternative for Redo
    if ((event->modifiers() & Qt::ControlModifier) && keyCode == Qt::Key_Y) {
        if (m_editMode) {
            redo();
            // See the Ctrl+Z handler above for why this is a full resync
            // rather than a single-line patch.
            {
                QTextDocument *doc = document();
                QList<QByteArray> resyncedContent;
                QStringList resyncedLines;
                for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                    resyncedLines.append(b.text());
                    if (b.next().isValid()) {
                        resyncedContent.append(displayLineToStorage(stripLineNumber(b.text())));
                    }
                }
                m_editorContent = resyncedContent;
                m_editorLines = resyncedLines;
            }
            markModified();
        } else {
            redo();
        }
        return;
    }

    // Ctrl+V - Paste
    if ((event->modifiers() & Qt::ControlModifier) && keyCode == Qt::Key_V) {
        paste();  // QTextEdit's built-in paste method
        return;
    }

    if (m_editMode) {
    	if (keyCode == Qt::Key_Up) {
			QTextCursor cursor = textCursor();
			int currentLine = cursor.blockNumber();
			if (currentLine > 0) {
				cursor.movePosition(QTextCursor::Up);
				// Ensure we're at column 5 (after line number) on the new line
				cursor.movePosition(QTextCursor::StartOfBlock);
				cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
				setTextCursor(cursor);
				ensureCursorVisible();
			}
			return;
		}
		if (keyCode == Qt::Key_Down) {
			QTextCursor cursor = textCursor();
			int currentLine = cursor.blockNumber();
			if (currentLine < m_editorLines.size() - 1) {
				cursor.movePosition(QTextCursor::Down);
				// Ensure we're at column 5 (after line number) on the new line
				cursor.movePosition(QTextCursor::StartOfBlock);
				cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
				setTextCursor(cursor);
				ensureCursorVisible();
			}
			return;
		}

		if (keyCode == Qt::Key_Return || keyCode == Qt::Key_Enter) {
			// If Shift+Enter in edit mode, insert a line above current line (or below? Let's do below but stay)
			if (m_editMode && (event->modifiers() & Qt::ShiftModifier)) {
				QTextCursor cursor = textCursor();
				int currentLine = cursor.blockNumber();
				
				// Insert a new empty line AFTER current line
				if (currentLine >= 0 && currentLine <= m_editorContent.size()) {
					m_editorContent.insert(currentLine + 1, QByteArray());
					rebuildFromEditorContent();
					
					// Stay on the current line (not the new one), cursor at column 5
					cursor = textCursor();
					cursor.movePosition(QTextCursor::Start);
					for (int i = 0; i <= currentLine; ++i) {
						cursor.movePosition(QTextCursor::Down);
					}
					cursor.movePosition(QTextCursor::StartOfBlock);
					cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
					setTextCursor(cursor);
					markModified();
				}
				return;
			}
			
			// Regular Enter - insert line and move to it
			m_overstrikePending = false;
			m_pendingOverstrike = 0;
			handleReturn();
			return;
		}

        if (keyCode == Qt::Key_Backspace) {
            QTextCursor cursor = textCursor();
            int currentLine = cursor.blockNumber();
            QString lineContent = getCurrentLine();
            if (cursor.columnNumber() <= 5 && lineContent.isEmpty()) {
                if (currentLine < m_editorLines.size() - 1) {
                    deleteLine(currentLine);
                    return;
                }
            }
        }
    }
    
    if (keyCode == Qt::Key_Left || keyCode == Qt::Key_Right ||
        keyCode == Qt::Key_Up || keyCode == Qt::Key_Down ||
        keyCode == Qt::Key_Home || keyCode == Qt::Key_End ||
        keyCode == Qt::Key_PageUp || keyCode == Qt::Key_PageDown) {
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
        QTextEdit::keyPressEvent(event);
        return;
    }

    if (keyCode == Qt::Key_Shift || keyCode == Qt::Key_Control ||
        keyCode == Qt::Key_Alt || keyCode == Qt::Key_Meta) {
        event->accept();
        return;
    }

    if (keyCode == Qt::Key_Return || keyCode == Qt::Key_Enter) {
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
        handleReturn();
        return;
    }

/*
    if (keyCode == Qt::Key_Escape || 
        (event->modifiers() & Qt::ControlModifier && keyCode == Qt::Key_C)) {
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
        emit interruptRequested();
        return;
    }
*/
	if (keyCode == Qt::Key_Escape || 
        (event->modifiers() & Qt::ControlModifier && keyCode == Qt::Key_C)) {
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
        // Ctrl+C in terminal (immediate) mode: clear the current input line
        // and reset the cursor to the prompt column before signalling abort.
        // Escape is left as a pure interrupt (no line wipe) since it may be
        // used to interrupt a running computation with no input line present.
        if (!m_editMode &&
            (event->modifiers() & Qt::ControlModifier) &&
            keyCode == Qt::Key_C) {
            clearCurrentInputLine();
        }

        // Escape has a second, narrower meaning while a graphics WCLICK
        // wait is in progress: soft-cancel that one wait (the waiting
        // APL code gets back a recognizable "no click happened" result)
        // rather than aborting the whole running program. Ctrl+C is
        // deliberately NOT given this alternate meaning -- it always
        // aborts, wait or no wait, exactly as before. See
        // apl_graphics_mouse_input_notes_v1.md's "Keyboard-Driven Cancel"
        // section and graphmanager.h's g_graphicsWaitActive/
        // g_graphicsWaitCancelled comments for the full reasoning.
        if (keyCode == Qt::Key_Escape && g_graphicsWaitActive) {
            g_graphicsWaitCancelled = 1;
            return;
        }

        emit interruptRequested();
        return;
    }

    if (keyCode == Qt::Key_Backspace) {
        handleBackspace();
        return;
    }

    QString text = event->text();
    if (text.isEmpty()) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    QChar key = text.at(0);
    if (key.unicode() < 0x20 && key.unicode() != Qt::Key_Tab) {
        event->accept();
        return;
    }
    handleCharacter(key);
}

void APLTerminalWidget::handleBackspace()
{
    QTextCursor cursor = textCursor();
    int cursorPos = cursor.position();
    
    // If there's a selection, delete the selected text
    if (cursor.hasSelection()) {
        QString selectedText = cursor.selectedText();
        if (selectedText.isEmpty()) return;

        m_pendingOverstrike = static_cast<uint8_t>(selectedText.at(0).unicode());
        m_overstrikePending = true;

        if (m_editMode) {
            QTextDocument *doc = document();
            int selStart = cursor.selectionStart();
            int selEnd   = cursor.selectionEnd();
            QTextBlock startBlockObj = doc->findBlock(selStart);
            QTextBlock endBlockObj   = doc->findBlock(selEnd);
            int startBlock = startBlockObj.blockNumber();
            int endBlock   = endBlockObj.blockNumber();

            if (startBlock != endBlock) {
                // See handleDelete() for the full explanation — same
                // multi-line-selection model-desync bug, reachable here via
                // Backspace instead of Delete.
                int startCol = qMax(5, selStart - startBlockObj.position());
                int endCol   = qMax(5, selEnd   - endBlockObj.position());

                QString startLineText = stripLineNumber(startBlockObj.text());
                QString endLineText   = stripLineNumber(endBlockObj.text());

                QString keptPrefix = startLineText.left(qMax(0, startCol - 5));
                QString keptSuffix = endLineText.mid(qMax(0, endCol - 5));

                if (startBlock >= 0 && startBlock < m_editorContent.size()) {
                    m_editorContent[startBlock] = displayLineToStorage(keptPrefix + keptSuffix);
                }
                for (int i = qMin(endBlock, m_editorContent.size() - 1); i > startBlock; --i) {
                    m_editorContent.removeAt(i);
                }

                rebuildFromEditorContent();

                QTextCursor newCursor = textCursor();
                newCursor.movePosition(QTextCursor::Start);
                for (int i = 0; i < startBlock; ++i) newCursor.movePosition(QTextCursor::Down);
                newCursor.movePosition(QTextCursor::StartOfBlock);
                newCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                                        5 + keptPrefix.length());
                setTextCursor(newCursor);
                markModified();
                return;
            }
        }

        cursor.removeSelectedText();
        setTextCursor(cursor);

        if (m_editMode) {
            int blockNum = cursor.blockNumber();
            if (blockNum >= 0 && blockNum < m_editorContent.size()) {
                QString displayContent = getCurrentLine();
                m_editorContent[blockNum] = displayLineToStorage(displayContent);
            }
            markModified();
        }
        return;
    }
    
    if (m_editMode) {
        int blockNum = cursor.blockNumber();
        if (blockNum < m_editorLines.size()) {
            int lineStart = cursor.position() - cursor.columnNumber();
            if (cursorPos <= lineStart + 5) return; // Can't delete line number or prompt
        }
	} else {
        // Compare within-block position against the prompt boundary
        int posInBlock = cursor.positionInBlock();
        if (posInBlock <= m_lineStartPos) return;
    }

    cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
    QString deletedChar = cursor.selectedText();
    
    if (!deletedChar.isEmpty()) {
        m_pendingOverstrike = static_cast<uint8_t>(deletedChar.at(0).unicode());
        m_overstrikePending = true;
        cursor.deleteChar();
        setTextCursor(cursor);
        
        if (m_editMode) {
            int currentLine = cursor.blockNumber();
            if (currentLine >= 0 && currentLine < m_editorContent.size()) {
                QString displayContent = getCurrentLine();
                m_editorContent[currentLine] = displayLineToStorage(displayContent);
            }
            markModified();
        }
    }
    m_lastKeyPressed = '\b';
}

void APLTerminalWidget::clearCurrentInputLine()
{
    // Erase everything the user typed after the prompt on the current line,
    // leaving the cursor parked at the prompt column.
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfBlock);
    // Skip past the prompt characters (m_lineStartPos is the prompt width)
    cursor.movePosition(QTextCursor::NextCharacter,
                        QTextCursor::MoveAnchor, m_lineStartPos);
    // Select to end of block and delete
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    setTextCursor(cursor);
    ensureCursorVisible();
}

void APLTerminalWidget::handleCharacter(QChar key)
{
    if (key.unicode() < 0x20 || key.unicode() == 0x7F) return;
    
    uint8_t fontChar = translateKeyToFont(key);
    QChar displayChar;
    
    if (m_overstrikePending && m_pendingOverstrike != 0) {
        uint8_t composedChar = applyOverstrike(m_pendingOverstrike, fontChar);
        displayChar = fontIndexToChar(composedChar);
        insertCharAtCursor(displayChar);
        m_lastFontChar = composedChar;
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
    } else {
        displayChar = fontIndexToChar(fontChar);
        insertCharAtCursor(displayChar);
        m_lastFontChar = fontChar;
        m_overstrikePending = false;
        m_pendingOverstrike = 0;
    }
    
    if (m_editMode) {
        QTextCursor cursor = textCursor();
        int currentLine = cursor.blockNumber();
        if (currentLine >= 0 && currentLine < m_editorLines.size()) {
            QString displayContent = getCurrentLine();
            if (currentLine < m_editorContent.size()) {
                m_editorContent[currentLine] = displayLineToStorage(displayContent);
            } else if (currentLine == m_editorContent.size()) {
                m_editorContent.append(displayLineToStorage(displayContent));
            }
        }
        markModified();
    }
    m_lastKeyPressed = key;
}

void APLTerminalWidget::handleReturn()
{
    m_overstrikePending = false;
    m_pendingOverstrike = 0;
    
    if (m_editMode) {
        QTextCursor cursor = textCursor();
        int currentLine = cursor.blockNumber();
        
        // Add bounds checking
        if (currentLine < 0) currentLine = 0;
        
        // Get the content of the current line (without line number)
        QString currentContent = getCurrentLine();
        
        // Check if we're on the last empty line (the one at the bottom)
        bool isLastEmptyLine = (currentLine == m_editorLines.size() - 1 && currentContent.isEmpty());
        
        if (isLastEmptyLine) {
            // On the last empty line - just stay there, don't add another line
            // Position cursor at column 5
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
            setTextCursor(cursor);
            return;
        }
        
        // Check if current line is empty (but not the last line)
        if (currentContent.isEmpty() && currentLine < m_editorContent.size()) {
            // On an empty line that's not the last line
            // Just move to the next line instead of inserting a new one
            if (currentLine + 1 < m_editorLines.size()) {
                cursor.movePosition(QTextCursor::NextBlock);
                cursor.movePosition(QTextCursor::StartOfBlock);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(cursor);
            }
            return;
        }
        
#ifdef CTLENTER  // ctl-enter must be used to insert a newline with this original version
        // Normal case: Insert a new empty line after current line
        if (currentLine >= 0 && currentLine <= m_editorContent.size()) {
            // Insert a new empty line after current line
            m_editorContent.insert(currentLine + 1, QByteArray());
            rebuildFromEditorContent();
            
            // Move cursor to the new line (currentLine + 1) at column 5
            cursor = textCursor();
            cursor.movePosition(QTextCursor::Start);
            for (int i = 0; i <= currentLine; ++i) {
                cursor.movePosition(QTextCursor::Down);
            }
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
            setTextCursor(cursor);
        }
#else	// enter at beginning of line will insert rather than move down with this alt version
		// Normal case: Insert a new empty line.
        if (currentLine >= 0 && currentLine <= m_editorContent.size()) {
            if (cursor.columnNumber() == 5 && currentLine > 0) {
                // At the very start of a non-header line: insert blank ABOVE
                // (current line drops down) and follow the content line down.
                m_editorContent.insert(currentLine, QByteArray());
                rebuildFromEditorContent();
                // Cursor goes to currentLine + 1 — the original (now shifted) line.
                cursor = textCursor();
                cursor.movePosition(QTextCursor::Start);
                for (int i = 0; i <= currentLine; ++i) {
                    cursor.movePosition(QTextCursor::Down);
                }
                cursor.movePosition(QTextCursor::StartOfBlock);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(cursor);
            } else {
                // Mid-line (also covers header line, and end-of-line, which
                // this naturally degenerates to): actually split the line's
                // content at the cursor, rather than just inserting a blank
                // line after it. Text before the cursor stays here; text
                // from the cursor onward becomes the new line below.
                QString content = getCurrentLine();
                int splitIndex = cursor.columnNumber() - 5;   // past the "[nnn " prefix
                if (splitIndex < 0) splitIndex = 0;
                if (splitIndex > content.length()) splitIndex = content.length();
                QString before = content.left(splitIndex);
                QString after  = content.mid(splitIndex);

                m_editorContent[currentLine] = displayLineToStorage(before);
                m_editorContent.insert(currentLine + 1, displayLineToStorage(after));
                rebuildFromEditorContent();
                cursor = textCursor();
                cursor.movePosition(QTextCursor::Start);
                for (int i = 0; i <= currentLine; ++i) {
                    cursor.movePosition(QTextCursor::Down);
                }
                cursor.movePosition(QTextCursor::StartOfBlock);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
                setTextCursor(cursor);
            }
        }
#endif
        markModified();
        return;
    }
    
    // Terminal mode
    QString line = getCurrentLine();
    end();
    insertPlainText("\n");
	m_lineStartPos = textCursor().positionInBlock();
	m_inputBlockNumber = textCursor().blockNumber();
    ensureCursorVisible();
    emit lineEntered(line);
    m_overstrikeMode = false;
    m_lastFontChar = 0;
}

uint8_t APLTerminalWidget::translateKeyToFont(QChar key)
{
    uint8_t ascii = static_cast<uint8_t>(key.toLatin1());
    return (m_keyboardLayout == Classic) ? OrigTrans[ascii] : AsciiTrans[ascii];
}

QChar APLTerminalWidget::fontIndexToChar(uint8_t index)
{
    return QChar(index);
}

void APLTerminalWidget::moveCursorLeft()
{
    QTextCursor cursor = textCursor();
    if (m_editMode) {
        if (cursor.blockNumber() < m_editorLines.size() && cursor.columnNumber() > 5) {
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
        }
    } else if (cursor.position() > m_lineStartPos + m_promptLength) {
        cursor.movePosition(QTextCursor::Left);
        setTextCursor(cursor);
    }
}

void APLTerminalWidget::deleteCharAtCursor()
{
    QTextCursor cursor = textCursor();
    if (m_editMode) {
        if (cursor.blockNumber() < m_editorLines.size() && cursor.columnNumber() > 5) {
            cursor.deletePreviousChar();
            setTextCursor(cursor);
            markModified();
        }
    } else if (cursor.position() > m_lineStartPos + m_promptLength) {
        cursor.deletePreviousChar();
        setTextCursor(cursor);
    }
}

void APLTerminalWidget::insertCharAtCursor(QChar ch)
{
    insertPlainText(QString(ch));
    ensureCursorVisible();
}

int APLTerminalWidget::getCursorPosition() const
{
    return textCursor().position();
}

void APLTerminalWidget::setCursorPosition(int pos)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(pos);
    setTextCursor(cursor);
}

QByteArray APLTerminalWidget::getInternalRepresentation(const QString &displayLine) const
{
    QByteArray result;
    for (QChar ch : displayLine) {
        result.append(static_cast<char>(ch.unicode()));
    }
    return result;
}

QString APLTerminalWidget::getFnsDirectory()
{
    // Mirror get_aplhome_dir() logic from linux_platform.c:
    // 1. $APLHOME/fns/
    // 2. $HOME/apl/fns/
    // 3. ./fns/
    QByteArray aplHome = qgetenv("APLHOME");
    if (!aplHome.isEmpty())
        return QString::fromLocal8Bit(aplHome) + "/fns/";

    QByteArray home = qgetenv("HOME");
    if (!home.isEmpty())
        return QString::fromLocal8Bit(home) + "/apl/fns/";

    return QString("./fns/");
}

QStringList APLTerminalWidget::loadFnFile(const QString &filename, KeyboardLayout layout)
{
    Q_UNUSED(layout);
    
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) return QStringList();
    
    QByteArray rawData = file.readAll();
    file.close();
    
    QStringList lines;
    int start = 0;
    int len = rawData.size();
    
    for (int i = 0; i < len; ++i) {
        if (rawData[i] == '\n') {
            QByteArray line = rawData.mid(start, i - start);
            if (line.endsWith('\r')) line.chop(1);
            QString displayLine;
            for (char c : line) {
                uint8_t byte = static_cast<uint8_t>(c);
                displayLine.append(QChar(OrigTrans[byte]));
            }
            lines.append(displayLine);
            start = i + 1;
        }
    }
    
    if (start < len) {
        QByteArray line = rawData.mid(start);
        if (line.endsWith('\r')) line.chop(1);
        QString displayLine;
        for (char c : line) {
            uint8_t byte = static_cast<uint8_t>(c);
            displayLine.append(QChar(OrigTrans[byte]));
        }
        lines.append(displayLine);
    }
    
    return lines;
}

// Edit mode conversion
//
// m_editorContent holds raw font index bytes (kapl.ttf indices) — the same
// values that are in the QTextEdit display.  displayLineToStorage is therefore
// identity: the display QString characters ARE the storage bytes.
// storageLineToDisplay is likewise identity.
//
// Translation to/from .fn file format happens only in loadFnFile() (OrigTrans)
// and saveToFile() (m_origInv).  Nothing else should touch the encoding.
QByteArray APLTerminalWidget::displayLineToStorage(const QString &displayLine) const
{
    QByteArray result;
    for (QChar ch : displayLine) {
        result.append(static_cast<char>(ch.unicode() & 0xFF));
    }
    return result;
}

QString APLTerminalWidget::storageLineToDisplay(const QByteArray &storageLine) const
{
    QString result;
    for (unsigned char c : storageLine) {
        result.append(QChar(c));
    }
    return result;
}

// Edit mode
bool APLTerminalWidget::enterEditMode(const QString &functionName, const QStringList &lines)
{
    if (m_editMode) exitEditMode(false);

	document()->setMaximumBlockCount(0);  // 0 = unlimited
	setUndoRedoEnabled(true);   // meaningful here; terminal mode keeps it off
    
    m_editMode = true;
    m_currentFunctionName = functionName;
    m_editorModified = false;
    m_backupTakenThisSession = false;
    m_pendEdit = true;
    
    clear();
    setLineWrapMode(QTextEdit::NoWrap);
    m_editorContent.clear();
    
    // Check if this function exists on disk
    QString fnsDir = getFnsDirectory();
    QString expectedPath = QString("%1/%2.fn").arg(fnsDir).arg(functionName.toLower());
    QFileInfo fileInfo(expectedPath);
    
    if (!lines.isEmpty() && lines.size() > 0) {
        // Loading from provided lines (Menu -> Open Function)
        for (const QString &line : lines) {
            m_editorContent.append(displayLineToStorage(line));
        }
        m_currentFilePath = expectedPath;
        m_isNewFunction = false;
    } else if (fileInfo.exists()) {
        // Backend )EDIT on existing function
        QStringList fileLines = loadFnFile(expectedPath, m_keyboardLayout);
        if (!fileLines.isEmpty()) {
            for (const QString &line : fileLines) {
                m_editorContent.append(displayLineToStorage(line));
            }
            m_currentFilePath = expectedPath;
            m_isNewFunction = false;
        } else {
            // Failed to load, create new
            m_editorContent.append(displayLineToStorage(functionName.toUpper()));
            m_currentFilePath = "";
            m_isNewFunction = true;
        }
    } else {
        // Backend )EDIT on new function
        m_editorContent.append(displayLineToStorage(functionName.toUpper()));

        m_currentFilePath = expectedPath;
        m_isNewFunction = false;  // Even though it's new, we know where to save it
    }
    
    rebuildFromEditorContent();
   
	// New function: cursor to line 1 (first editable line after header).
    // Existing function: cursor to the last empty line at the bottom,
    // ready to append a new line.
    QTextCursor cursor = textCursor();
    if (m_editorContent.size() > 0) {
        int targetLine;
        if (m_isNewFunction) {
            targetLine = (m_editorContent.size() > 1) ? 1 : 0;
        } else {
            // Last line in m_editorLines is the trailing empty entry line
            targetLine = m_editorLines.size() - 1;
        }
        cursor.movePosition(QTextCursor::Start);
        for (int i = 0; i < targetLine; ++i) {
            cursor.movePosition(QTextCursor::Down);
        }
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
        setTextCursor(cursor);
        ensureCursorVisible();
    } 
    
    setInputEnabled(true);
    setReadOnly(false);
    emit editModeEntered(functionName);
    return true;
}

void APLTerminalWidget::exitEditMode(bool compile)
{
    Q_UNUSED(compile);
    if (!m_editMode) return;
    
    m_editMode = false;
    m_editorModified = false;
    m_pendEdit = false;
    m_editorLines.clear();
    m_editorContent.clear();
    m_currentFunctionName.clear();
    clear();
    document()->setMaximumBlockCount(1000);
    setUndoRedoEnabled(false);   // back to terminal mode; see constructor comment
    setLineWrapMode(QTextEdit::NoWrap);
    emit editModeExited();
}

bool APLTerminalWidget::saveCurrentFunction()
{
    if (!m_editMode || m_currentFunctionName.isEmpty()) return false;
    
    // If no file path (new function from menu), do Save As instead
    if (m_currentFilePath.isEmpty() || m_isNewFunction) {
        // Emit signal to main window to show Save As dialog
        emit saveAsRequested();
        return false;  // Let Save As handle it
    }
    
    // Save to existing file path (no warning - expected behavior)
    return saveToFile(m_currentFilePath);
}

bool APLTerminalWidget::saveCurrentFunctionAs(const QString &filename)
{
    if (!m_editMode) return false;
    
    // Check if file exists
    if (QFile::exists(filename)) {
        // Ask main window to show overwrite dialog
        emit overwriteRequested(filename);
        return false;  // Will retry after user confirms
    }
    
    // Save to new file
    if (saveToFile(filename)) {
        m_currentFilePath = filename;
        m_isNewFunction = false;
        QFileInfo info(filename);
        // See the matching comment in forceSaveToFile(): baseName() is the
        // lowercase on-disk stem, not the app's uppercase function-name
        // convention.
        m_currentFunctionName = info.baseName().toUpper();
        return true;
    }
    return false;
}

bool APLTerminalWidget::saveToFile(const QString &filename)
{
    // Tag lives in .fn file-byte space only — never in m_editorContent.
    // 0xE3 = lamp (⍝) in .fn encoding; rest is plain ASCII.
    // Strip-and-rewrite every save: always exactly one tag, no state needed.
    //
    // Stripping is now tag-text-agnostic: truncate line 1 at its first lamp
    // byte, period, rather than matching a specific anchor string. This is
    // what lets an old-format tag (apl/kd, mixed-case RetroAPL, whatever
    // came before) get cleanly replaced on next save instead of surviving
    // untouched and getting a second tag appended after it. Relies on line
    // 1's comment slot never legitimately holding anything but this tag —
    // true today since this is the only code that ever writes a comment
    // there, but worth re-checking if that ever changes.
    static const QByteArray kTag = QByteArrayLiteral(" \xE3 " APL_PRODUCT_NAME " v") + QByteArrayLiteral(BACKEND_VERSION);

    // One backup per edit session, not per save: the first save of a
    // session preserves whatever was on disk before this session's edits
    // (the ".fn.bak" is renamed from, so it's the pre-session content, not
    // last save's), and every later save in the same session leaves that
    // backup alone rather than overwriting it with more of this session's
    // own work. m_backupTakenThisSession is reset in enterEditMode(). A
    // failed rename doesn't block the save itself -- it's a safety net, not
    // a requirement -- but is reported so it doesn't fail silently.
    if (!m_backupTakenThisSession) {
        if (QFile::exists(filename)) {
            QString backupPath = filename + ".bak";
            QFile::remove(backupPath);   // drop any leftover .bak from a previous session; best-effort
            if (!QFile::rename(filename, backupPath)) {
                emit backupFailed(QString("Could not back up %1 before saving")
                                       .arg(QFileInfo(filename).fileName()));
            }
        }
        m_backupTakenThisSession = true;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(nullptr, "Save Failed",
            QString("Cannot write to:\n%1\n\n%2").arg(filename).arg(file.errorString()));
        return false;
    }

    // rebuildFromEditorContent() deliberately leaves exactly one blank
    // entry at the end of m_editorContent — the "line currently being
    // composed" placeholder that makes the editor always have somewhere
    // to type next. That's a display/editing convenience, not something
    // the user actually wrote, so it shouldn't be persisted: writing it
    // out adds a spurious blank final line to the .fn file (and the extra
    // trailing newline a hex dump would show). Trim any trailing blank
    // lines here, at save time only — the in-memory m_editorContent (and
    // the visible editor) keep their own copy of that placeholder
    // untouched for continued editing.
    int lineCount = m_editorContent.size();
    while (lineCount > 1 && m_editorContent[lineCount - 1].isEmpty()) {
        --lineCount;
    }

    for (int i = 0; i < lineCount; ++i) {
        // Translate storage (font-index) bytes → .fn file bytes
        QByteArray fileLine;
        fileLine.reserve(m_editorContent[i].size() + kTag.size());
        for (unsigned char fontByte : m_editorContent[i]) {
            fileLine.append(static_cast<char>(m_origInv[fontByte]));
        }

        if (i == 0) {
            // Now in file-byte space: strip everything from the first
            // lamp onward — any tag, any format, any era — then re-add.
            // 0xE3 here is always the lamp file byte, unambiguous.
            int tagPos = fileLine.indexOf('\xE3');

			if (tagPos != -1) {
				fileLine.truncate(tagPos);
				// Truncating at the lamp leaves the space that separated
				// the header from the old tag still in the buffer; kTag
				// supplies its own leading space, so without this the gap
				// grows by one character on every single save.
				while (fileLine.endsWith(' '))
					fileLine.chop(1);
			}

            fileLine += kTag;
        }

        file.write(fileLine);
        file.write("\n", 1);
    }
    file.close();

    m_editorModified = false;
    return true;
}

void APLTerminalWidget::insertLine(int position)
{
    if (!m_editMode) return;
    if (position < 0 || position > m_editorContent.size()) return;
    
    m_editorContent.insert(position, QByteArray());
    rebuildFromEditorContent();
    
    // Position cursor at the 5-space entry point of the newly inserted line
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < position; ++i) {
        cursor.movePosition(QTextCursor::Down);
    }
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
    setTextCursor(cursor);
    
    markModified();
}

void APLTerminalWidget::deleteLine(int position)
{
    if (!m_editMode) return;
    if (position < 0 || position >= m_editorContent.size()) return;
    
    m_editorContent.removeAt(position);
    rebuildFromEditorContent();
    
    // Position cursor at the 5-space entry point of the line that took its place
    // If we deleted the last line, go to the new last line
    int targetLine = qMin(position, m_editorContent.size() - 1);
    if (targetLine < 0) targetLine = 0;
    
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < targetLine; ++i) {
        cursor.movePosition(QTextCursor::Down);
    }
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
    setTextCursor(cursor);
    
    markModified();
}

void APLTerminalWidget::rebuildFromEditorContent()
{
    // Strip trailing empty lines from editor content (except keep one)
    while (m_editorContent.size() > 1 && m_editorContent.last().isEmpty()) {
        m_editorContent.removeLast();
    }
    
    m_editorLines.clear();
    for (int i = 0; i < m_editorContent.size(); ++i) {
        m_editorLines.append(formatLineNumber(i) + storageLineToDisplay(m_editorContent[i]));
    }
    // Add the empty line at the bottom with the NEXT line number
    m_editorLines.append(formatLineNumber(m_editorContent.size()) + "");
    updateDisplayFromEditorLines();
}

void APLTerminalWidget::updateDisplayFromEditorLines()
{
    QTextCursor cursor = textCursor();
    int savedBlock = cursor.blockNumber();
    int savedColumn = cursor.columnNumber();
    bool wasAtEnd = cursor.atEnd();

    // Group the full replace into ONE undo/redo step, and do it without
    // QTextEdit::clear() — clear() calls QTextDocument::clear(), which Qt
    // documents as also wiping the ENTIRE undo/redo history, not just the
    // visible text. That's fine the one time a function is first loaded
    // into the editor, but this function runs on every rebuild (cut,
    // paste, insert/delete line, ...), so calling clear() here nuked all
    // prior undo history on every single edit: grouping the reinsertion
    // into one command (the fix above) then meant the very first Ctrl+Z
    // after ANY edit undid straight down to an empty buffer, because
    // there was nothing left in the stack before that one grouped
    // "insert everything" command. Selecting the whole document and
    // removing it via a cursor is an ordinary, undoable edit and has no
    // such side effect.
    cursor.beginEditBlock();

    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    for (const QString &line : m_editorLines) {
        cursor.insertText(line);
        cursor.insertText("\n");
    }
    
    // Remove the last newline
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
    if (cursor.selectedText() == "\n") cursor.deleteChar();

    cursor.endEditBlock();
    
    // CRITICAL FIX: Ensure savedBlock is within valid range
    int maxBlock = m_editorLines.size() - 1;
    if (savedBlock > maxBlock) {
        savedBlock = maxBlock;
    }
    
    if (savedBlock >= 0 && savedBlock < m_editorLines.size()) {
        cursor = textCursor();
        cursor.movePosition(QTextCursor::Start);
        for (int i = 0; i < savedBlock; ++i) {
            if (!cursor.movePosition(QTextCursor::Down)) break;
        }
        cursor.movePosition(QTextCursor::StartOfBlock);
        int maxColumn = m_editorLines[savedBlock].length();
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 
                           qMin(savedColumn, maxColumn));
        setTextCursor(cursor);
    } else if (wasAtEnd || savedBlock >= m_editorLines.size()) {
        scrollToBottom();
    }
}

QString APLTerminalWidget::stripLineNumber(const QString &line) const
{
    if (line.length() >= 5 && line[0] == '[') return line.mid(5);
    return line;
}

// Line-number prefix a *pasted* line might be carrying in from a )LIST cut
// (copied straight out of the terminal, brackets and all). Deliberately
// stricter than stripLineNumber() above, which just assumes the editor's
// own fixed-width "[nnn] " convention regardless of what's actually
// between the brackets: this requires the bracket to contain digits only,
// so a genuine line of APL starting with real bracket-indexing (e.g.
// "[I;J]...") -- syntactically odd standing alone at the very start of a
// line with no array name before it, but not something confirmed
// impossible here -- is left alone rather than mistaken for a line
// number. Once validated, it strips the same fixed 5 characters
// stripLineNumber() does: formatLineNumber() pads with trailing spaces
// after "]" (not inside the brackets) so the whole prefix is always 5
// columns wide regardless of the number's digit count -- "[1]  ", "[12] ",
// "[123]" -- so a plain "consume one optional space after ']'" undercounts
// the padding for 1- and 2-digit lines. Only ever called for multi-line
// pastes; see the call site in insertFromMimeData().
QString APLTerminalWidget::stripPastedLineNumber(const QString &line) const
{
    if (line.length() < 5 || line[0] != '[') return line;
    int closeBracket = line.indexOf(']');
    if (closeBracket <= 1) return line;   // no content between [ and ]
    QString digits = line.mid(1, closeBracket - 1);
    for (QChar c : digits) {
        if (!c.isDigit()) return line;
    }
    return line.mid(5);
}

int APLTerminalWidget::extractLineNumber(const QString &line) const
{
    if (line.length() >= 3 && line[0] == '[') {
        int bracketPos = line.indexOf(']');
        if (bracketPos > 1) return line.mid(1, bracketPos - 1).toInt();
    }
    return -1;
}

void APLTerminalWidget::markModified()
{
    if (!m_pendEdit) m_editorModified = true;
    m_pendEdit = false;
}

QStringList APLTerminalWidget::getFunctionContent() const
{
    QStringList result;
    for (const QByteArray &storageLine : m_editorContent) {
        result.append(storageLineToDisplay(storageLine));
    }
    return result;
}

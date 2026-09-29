#ifndef APLTERMINALWIDGET_H
#define APLTERMINALWIDGET_H

#include <QTextEdit>
#include <QKeyEvent>
#include <QTextCharFormat>
#include <QColor>
#include <QDir>
#include <QByteArray>
#include "mappings.hpp"

class QTextDocument;

class APLTerminalWidget : public QTextEdit
{
    Q_OBJECT

public:
    explicit APLTerminalWidget(QWidget *parent = nullptr);
    ~APLTerminalWidget();

    // Initialize
    bool initialize();

    // Basic terminal operations
    void setPrompt(const QString &prompt);
    void clearScreen();
    void home();
    void end();
    QString getCurrentLine() const;
    void printOutput(const QString &text);
    void printInput(const QString &text);
    void setInputEnabled(bool enabled);
    void setFontSize(int pointSize);
    void scrollToBottom();
    QByteArray getInternalRepresentation(const QString &displayLine) const;

	void setBackgroundBrightness(int v);   // 0 = black … 255 = white
 	int  getBackgroundBrightness() const { return m_backgroundBrightness; }
 	int  getFontSize()             const { return m_fontSize; }

    // Printing -----------------------------------------------------------
    // Returns a new heap-allocated QTextDocument (caller owns it and must
    // delete it) mirroring this widget's current content, with every run's
    // foreground forced to black and background to white -- printed output
    // always ignores the on-screen background-brightness/dark-mode setting.
    // Font is always the loaded APL font (kapl.ttf); pointSize <= 0 means
    // "use the current on-screen font size" (m_fontSize).
    QTextDocument *createPrintDocument(int pointSize = -1) const;

    // Actual text of the widest line currently displayed (not just its
    // character count) -- used by the print flow's shrink-to-fit sizing so
    // it measures real glyph widths rather than assuming every character
    // is as wide as 'M'. kapl.ttf's non-ASCII glyphs (overstrikes, APL
    // symbols) aren't guaranteed to share the ASCII characters' advance.
    QString widestLine() const;

    QString fontFamily() const { return m_fontFamily; }

    // Clipboard Unicode translation (copy/paste only — screen codes and
    // .fn files are unaffected either way; see kapl_unicode_mapping.hpp)
    void setClipboardUnicodeTranslationEnabled(bool enabled);
    bool isClipboardUnicodeTranslationEnabled() const { return m_clipboardUnicodeTranslationEnabled; }

    // Keyboard layout
    enum KeyboardLayout { Classic, ASCII };
    void setKeyboardLayout(KeyboardLayout layout);
    KeyboardLayout getKeyboardLayout() const { return m_keyboardLayout; }

    // Static file I/O (public for main window to use)
    static QString getFnsDirectory();
    static QStringList loadFnFile(const QString &filename, KeyboardLayout layout);
    static bool saveFnFile(const QString &filename, const QStringList &lines, KeyboardLayout layout);

    // Edit mode
    bool enterEditMode(const QString &functionName, const QStringList &lines = QStringList());
    void exitEditMode(bool compile = true);
    bool saveCurrentFunction();
    bool saveCurrentFunctionAs(const QString &filename);
    bool isEditMode() const { return m_editMode; }
    bool hasUnsavedChanges() const { return m_editMode && m_editorModified; }
    QString getCurrentFunctionName() const { return m_currentFunctionName; }
	QString getCurrentFilePath() const { return m_currentFilePath; }
    void insertLine(int position);
    void deleteLine(int position);
    QStringList getFunctionContent() const;

    // Frontend version (FRONTEND_VERSION in depend.h), e.g. "2.1" -- distinct
    // from the backend/interpreter VERSION shown alongside it in the window title.
    static QString getVersion();
    // The trailing tag appended to saved .fn lines (see displayLineToStorage /
    // saveToFile), exposed here for anything outside the widget that needs to
    // recognize or reproduce it, e.g. " \xE3 apl/kd v4.1".
    static QString getEditorTag();
	bool forceSaveToFile(const QString &filename);

public slots:
    void clearUndoStack();

signals:
    void lineEntered(const QString &line);
    void interruptRequested();
    void editModeEntered(const QString &functionName);
    void editModeExited();
    void saveAsRequested();
    void overwriteRequested(const QString &filename);
    void editRequested(const QString &functionName);
    void backupFailed(const QString &reason);   // .bak rename didn't work; save proceeds anyway

protected:
    void keyPressEvent(QKeyEvent *event) override;
	void insertFromMimeData(const QMimeData *source) override;
	QMimeData *createMimeDataFromSelection() const override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	bool saveToFile(const QString &filename);

    // Character translation
    uint8_t translateKeyToFont(QChar key);
    uint8_t translateFontToInternal(uint8_t fontIndex);
    QChar fontIndexToChar(uint8_t index);

    // Overstrike state
    bool m_overstrikeMode;
    bool m_overstrikePending;
    uint8_t m_pendingOverstrike;
    uint8_t m_lastFontChar;
    QChar m_lastKeyPressed;

    // Prompt
    QString m_prompt;
    int m_promptLength;
    int m_lineStartPos;
	int m_inputBlockNumber;

    // Input state
    bool m_inputEnabled;

    // Keyboard layout
    KeyboardLayout m_keyboardLayout;
    
    // Inverse translation tables
    char m_asciiInv[256];
    char m_origInv[256];

    // Font
    QString m_fontFamily;
    int m_fontSize;

    // Font setup
    bool loadAPLFont();
    void setupTextFormats();
    void buildInverseTables();
    
    void ensureCursorVisible();
    void moveCursorLeft();
    void deleteCharAtCursor();
    void insertCharAtCursor(QChar ch);
    int getCursorPosition() const;
    void setCursorPosition(int pos);

    // Line management
    void handleReturn();
    void handleBackspace();
	void handleDelete();
    void handleCharacter(QChar ch);
	void clearCurrentInputLine();
    
    // Edit mode conversion (use m_keyboardLayout)
    QByteArray displayLineToStorage(const QString &displayLine) const;
    QString storageLineToDisplay(const QByteArray &storageLine) const;
    
    // Edit mode state
    bool m_editMode;
    QString m_currentFunctionName;
    QStringList m_editorLines;
    QList<QByteArray> m_editorContent;
    bool m_editorModified;
    int m_lastEditLine;
    bool m_pendEdit;
    
    // Edit mode helpers
    void updateDisplayFromEditorLines();
    void rebuildFromEditorContent();
    QString stripLineNumber(const QString &line) const;
    int extractLineNumber(const QString &line) const;
    QString stripPastedLineNumber(const QString &line) const;
    void markModified();

	QString m_currentFilePath;  // Current file path (empty if never saved)
    bool m_isNewFunction;       // True if created via New (not loaded from disk)
    // True once this edit session has made its one pre-session .bak backup
    // (see saveToFile()) -- reset each time enterEditMode() starts a new
    // session, so later saves within the same session don't keep
    // overwriting it with the function's own more-recent state.
    bool m_backupTakenThisSession;
	int  m_backgroundBrightness;           // grey level of terminal bg

	// overwrite mode
	bool m_overwriteMode;
	void printOutputOverwrite(const QString &text);

	// clipboard Unicode translation (default on)
	bool m_clipboardUnicodeTranslationEnabled;

};

#endif // APLTERMINALWIDGET_H

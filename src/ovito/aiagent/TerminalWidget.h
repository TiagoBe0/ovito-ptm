////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once


#include <ovito/aiagent/AIAgent.h>

namespace Ovito {

/**
 * \brief A minimal xterm-compatible terminal emulator widget.
 *
 * It implements the subset of the VT100/xterm control sequences that interactive
 * command line agents actually use: cursor movement and addressing, erase operations,
 * insert/delete of lines and characters, scroll regions, the alternate screen buffer,
 * SGR text attributes including 256-color and 24-bit color, bracketed paste, and the
 * device status reports that TUI frameworks rely on for cursor position queries.
 *
 * The widget only converts bytes to a screen and key events back to bytes. Feeding it
 * and consuming its output is the caller's job, which keeps it independent of how the
 * child process is run (see PtyProcess).
 */
class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:

    /// Constructor.
    explicit TerminalWidget(QWidget* parent = nullptr);

    /// Processes a chunk of output received from the terminal application.
    void feed(const QByteArray& data);

    /// Discards all screen contents and scrollback and resets the emulator state.
    void reset();

    /// Returns the current width of the screen in character cells.
    int columns() const { return _columns; }

    /// Returns the current height of the screen in character cells.
    int rows() const { return _rows; }

    /// Returns the text currently selected with the mouse.
    QString selectedText() const;

    /// Appends a line of text to the screen as if the application had printed it.
    /// Used to show status messages of the embedded session itself.
    void appendMessage(const QString& text);

Q_SIGNALS:

    /// Emitted when the emulator wants to send bytes back to the terminal application,
    /// either because the user typed something or because the application asked a question.
    void dataToSend(const QByteArray& data);

    /// Emitted when the screen size changed, e.g. because the widget was resized.
    void windowSizeChanged(int columns, int rows);

    /// Emitted when the application set the window title via an OSC sequence.
    void titleChanged(const QString& title);

protected:

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual void inputMethodEvent(QInputMethodEvent* event) override;

private:

    /// Text attributes of a single character cell.
    enum CellFlag {
        NoCellFlags   = 0,
        BoldCell      = (1 << 0),
        DimCell       = (1 << 1),
        ItalicCell    = (1 << 2),
        UnderlineCell = (1 << 3),
        InverseCell   = (1 << 4),
        HiddenCell    = (1 << 5),
        StrikeCell    = (1 << 6),
        WidePadding   = (1 << 7)    ///< Right half of a double-width character.
    };

    /// One character cell of the screen.
    struct Cell {
        char32_t ch = U' ';
        quint32 fg = DefaultColor;
        quint32 bg = DefaultColor;
        quint16 flags = NoCellFlags;
    };

    /// Sentinel value for "use the default foreground/background color".
    static constexpr quint32 DefaultColor = 0xFFFFFFFFu;

    /// One line of the screen or of the scrollback buffer.
    using Line = std::vector<Cell>;

    /// The states of the control sequence parser.
    enum ParserState {
        GroundState,        ///< Printable characters and C0 controls.
        EscapeState,        ///< ESC seen, waiting for the sequence type.
        EscapeIntermediate, ///< ESC plus an intermediate byte, e.g. a charset designator.
        CsiState,           ///< Inside a CSI sequence, collecting parameters.
        OscState,           ///< Inside an OSC string, collecting the payload.
        StringState         ///< Inside a DCS/PM/APC string, which we discard.
    };

    /// Feeds a single decoded code point into the emulator.
    void processCodePoint(char32_t cp);

    /// Handles a C0 control character in the ground state.
    void processControlCharacter(char32_t cp);

    /// Handles the final byte of an escape sequence introduced by ESC.
    void processEscapeFinal(char32_t cp);

    /// Handles the final byte of a CSI sequence, using the collected parameters.
    void processCsiFinal(char32_t finalByte);

    /// Handles a completed OSC string.
    void processOscString();

    /// Writes a printable character at the cursor position.
    void putCharacter(char32_t cp, int width);

    /// Applies an SGR (select graphic rendition) parameter list.
    void applySgr();

    /// Sets or resets DEC private modes and ANSI modes.
    void setMode(bool enabled, bool privateMode);

    /// Returns the numeric CSI parameter at the given index, or the default value if absent.
    int param(int index, int defaultValue) const;

    /// Moves the cursor to the given position, clamped to the screen.
    void setCursorPosition(int column, int row);

    /// Advances the cursor to the next line, scrolling the scroll region if needed.
    void lineFeed();

    /// Moves the cursor up one line, scrolling the scroll region backwards if needed.
    void reverseLineFeed();

    /// Scrolls the scroll region up by the given number of lines, pushing lines to the scrollback.
    void scrollUp(int count);

    /// Scrolls the scroll region down by the given number of lines.
    void scrollDown(int count);

    /// Erases a range of cells in the given line.
    void eraseCells(Line& line, int from, int to);

    /// Returns a blank cell carrying the current background color.
    Cell blankCell() const;

    /// Switches between the primary and the alternate screen buffer.
    void useAlternateScreen(bool enabled, bool clear);

    /// Recomputes the screen size from the widget size and notifies the application.
    void updateScreenSize();

    /// Updates the range and position of the vertical scrollbar.
    void updateScrollbar();

    /// Scrolls the view to the bottom, e.g. when the user types.
    void scrollToBottom();

    /// Returns the total number of lines that can be displayed, including scrollback.
    int totalLineCount() const { return (int)_scrollback.size() + _rows; }

    /// Returns the line at the given absolute index (scrollback first, then screen).
    const Line& lineAt(int absoluteIndex) const;

    /// Converts a widget position to a cell position in absolute line coordinates.
    QPoint cellAtPosition(const QPoint& pos) const;

    /// Sends the given bytes to the application, scrolling the view to the bottom first.
    void sendToApplication(const QByteArray& data);

    /// Sends text as if it had been pasted, honoring bracketed paste mode.
    void pasteText(const QString& text);

    /// Returns the actual QColor for a stored color value.
    QColor resolveColor(quint32 color, bool foreground, quint16 flags) const;

    /// The visible screen, always _rows lines of _columns cells.
    std::vector<Line> _screen;

    /// Lines that scrolled off the top of the primary screen.
    std::deque<Line> _scrollback;

    /// Saved primary screen while the alternate screen is active.
    std::vector<Line> _savedScreen;

    /// Maximum number of lines kept in the scrollback buffer.
    int _scrollbackLimit = 10000;

    /// Screen dimensions in character cells.
    int _columns = 80;
    int _rows = 24;

    /// Cursor position, zero-based.
    int _cursorColumn = 0;
    int _cursorRow = 0;

    /// Saved cursor position and attributes (DECSC/DECRC).
    int _savedCursorColumn = 0;
    int _savedCursorRow = 0;
    Cell _savedAttributes;

    /// The scroll region, zero-based and inclusive.
    int _scrollTop = 0;
    int _scrollBottom = 23;

    /// The attributes applied to newly written characters.
    Cell _currentAttributes;

    /// State of the control sequence parser.
    ParserState _parserState = GroundState;

    /// Collected numeric parameters of the CSI sequence being parsed.
    std::vector<int> _csiParams;

    /// Private/intermediate marker byte of the CSI sequence being parsed, or 0.
    char _csiPrefix = 0;

    /// Payload of the OSC string being parsed.
    QString _oscString;

    /// Partial UTF-8 sequence left over from the previous chunk of input.
    QByteArray _utf8Remainder;

    /// Terminal modes.
    bool _autoWrap = true;
    bool _wrapPending = false;      ///< Cursor sits past the last column, waiting to wrap.
    bool _cursorVisible = true;
    bool _applicationCursorKeys = false;
    bool _applicationKeypad = false;
    bool _bracketedPaste = false;
    bool _originMode = false;
    bool _alternateScreen = false;
    bool _insertMode = false;

    /// Whether the widget currently has the keyboard focus.
    bool _hasFocus = false;

    /// Blink state of the cursor.
    bool _cursorBlinkOn = true;
    QTimer _blinkTimer;

    /// Coalesces repaints while a large burst of output is being processed.
    QTimer _repaintTimer;

    /// Metrics of the fixed-width font used for rendering.
    qreal _charWidth = 8.0;
    qreal _lineHeight = 16.0;
    qreal _fontAscent = 12.0;

    /// Mouse selection, in absolute line coordinates. Column, then line.
    QPoint _selectionAnchor{-1, -1};
    QPoint _selectionEnd{-1, -1};
    bool _selecting = false;

    /// The color palette: 16 ANSI colors followed by the 240 extended xterm colors.
    QColor _palette[256];
    QColor _defaultForeground;
    QColor _defaultBackground;
};

}   // End of namespace

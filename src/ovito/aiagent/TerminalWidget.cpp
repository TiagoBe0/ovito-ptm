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

#include <ovito/aiagent/AIAgent.h>
#include "TerminalWidget.h"

#include <QClipboard>
#include <QScrollBar>
#include <QTextStream>

#ifdef Q_OS_UNIX
    #include <wchar.h>
#endif

namespace Ovito {

/******************************************************************************
* Returns the number of character cells occupied by a code point.
******************************************************************************/
static int codePointWidth(char32_t cp)
{
    if(cp < 0x20)
        return 0;
#ifdef Q_OS_UNIX
    // wcwidth() knows about combining marks (width 0) and East Asian wide characters (width 2).
    int width = ::wcwidth((wchar_t)cp);
    if(width >= 0)
        return width;
#endif
    // Fall back to a rough classification for code points the C library does not know,
    // which mostly matters for emoji: terminals render them double-width.
    if((cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF))
        return 2;
    return 1;
}

/******************************************************************************
* Constructor.
******************************************************************************/
TerminalWidget::TerminalWidget(QWidget* parent) : QAbstractScrollArea(parent)
{
    // A fixed-width font is a hard requirement: the emulator addresses the screen in cells.
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() : 10.0);
    font.setStyleHint(QFont::Monospace, QFont::PreferMatch);
    font.setFixedPitch(true);
    setFont(font);

    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAttribute(Qt::WA_InputMethodEnabled, true);

    // The standard xterm palette. Bright variants share the hue but are lighter.
    static const QRgb basicColors[16] = {
        qRgb(0x00,0x00,0x00), qRgb(0xCD,0x31,0x31), qRgb(0x0D,0xBC,0x79), qRgb(0xE5,0xE5,0x10),
        qRgb(0x24,0x72,0xC8), qRgb(0xBC,0x3F,0xBC), qRgb(0x11,0xA8,0xCD), qRgb(0xCC,0xCC,0xCC),
        qRgb(0x66,0x66,0x66), qRgb(0xF1,0x4C,0x4C), qRgb(0x23,0xD1,0x8B), qRgb(0xF5,0xF5,0x43),
        qRgb(0x3B,0x8E,0xEA), qRgb(0xD6,0x70,0xD6), qRgb(0x29,0xB8,0xDB), qRgb(0xFF,0xFF,0xFF)
    };
    for(int i = 0; i < 16; i++)
        _palette[i] = QColor(basicColors[i]);
    // The 6x6x6 color cube.
    static const int cubeLevels[6] = {0, 95, 135, 175, 215, 255};
    for(int r = 0; r < 6; r++)
        for(int g = 0; g < 6; g++)
            for(int b = 0; b < 6; b++)
                _palette[16 + r*36 + g*6 + b] = QColor(cubeLevels[r], cubeLevels[g], cubeLevels[b]);
    // The 24 grayscale steps.
    for(int i = 0; i < 24; i++) {
        int level = 8 + i * 10;
        _palette[232 + i] = QColor(level, level, level);
    }

    // Follow the application's color scheme so the terminal does not glare in a dark UI.
    bool darkUi = palette().color(QPalette::Window).lightness() < 128;
    _defaultBackground = darkUi ? QColor(0x1E,0x1E,0x1E) : QColor(0xFC,0xFC,0xFC);
    _defaultForeground = darkUi ? QColor(0xD4,0xD4,0xD4) : QColor(0x20,0x20,0x20);

    _blinkTimer.setInterval(600);
    connect(&_blinkTimer, &QTimer::timeout, this, [this]() {
        _cursorBlinkOn = !_cursorBlinkOn;
        viewport()->update();
    });

    // Output arrives in many small chunks; repainting on each of them would starve the event loop.
    _repaintTimer.setSingleShot(true);
    _repaintTimer.setInterval(16);
    connect(&_repaintTimer, &QTimer::timeout, this, [this]() {
        updateScrollbar();
        viewport()->update();
    });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); });

    reset();
}

/******************************************************************************
* Discards all screen contents and resets the emulator state.
******************************************************************************/
void TerminalWidget::reset()
{
    _scrollback.clear();
    _savedScreen.clear();
    _currentAttributes = Cell();
    _screen.assign(std::max(_rows, 1), Line(std::max(_columns, 1), blankCell()));
    _cursorColumn = _cursorRow = 0;
    _savedCursorColumn = _savedCursorRow = 0;
    _scrollTop = 0;
    _scrollBottom = _rows - 1;
    _parserState = GroundState;
    _csiParams.clear();
    _csiPrefix = 0;
    _oscString.clear();
    _utf8Remainder.clear();
    _autoWrap = true;
    _wrapPending = false;
    _cursorVisible = true;
    _applicationCursorKeys = false;
    _applicationKeypad = false;
    _bracketedPaste = false;
    _originMode = false;
    _alternateScreen = false;
    _insertMode = false;
    _selectionAnchor = _selectionEnd = QPoint(-1, -1);
    updateScrollbar();
    viewport()->update();
}

/******************************************************************************
* Returns a blank cell carrying the current background color.
******************************************************************************/
TerminalWidget::Cell TerminalWidget::blankCell() const
{
    Cell cell;
    cell.bg = _currentAttributes.bg;
    return cell;
}

/******************************************************************************
* Processes a chunk of output received from the terminal application.
******************************************************************************/
void TerminalWidget::feed(const QByteArray& data)
{
    QByteArray bytes = _utf8Remainder + data;
    _utf8Remainder.clear();

    // Decode UTF-8 by hand rather than through QString, because a chunk may end in the
    // middle of a multi-byte sequence and the remainder has to survive until the next chunk.
    int i = 0;
    const int n = bytes.size();
    while(i < n) {
        unsigned char b = (unsigned char)bytes[i];
        char32_t cp;
        int sequenceLength;
        if(b < 0x80)        { cp = b;         sequenceLength = 1; }
        else if(b < 0xC0)   { cp = 0xFFFD;    sequenceLength = 1; }  // Stray continuation byte.
        else if(b < 0xE0)   { cp = b & 0x1F;  sequenceLength = 2; }
        else if(b < 0xF0)   { cp = b & 0x0F;  sequenceLength = 3; }
        else if(b < 0xF8)   { cp = b & 0x07;  sequenceLength = 4; }
        else                { cp = 0xFFFD;    sequenceLength = 1; }

        if(i + sequenceLength > n) {
            // Incomplete sequence at the end of the chunk: keep it for the next call.
            _utf8Remainder = bytes.mid(i);
            break;
        }
        for(int k = 1; k < sequenceLength; k++) {
            unsigned char cb = (unsigned char)bytes[i + k];
            if((cb & 0xC0) != 0x80) { cp = 0xFFFD; sequenceLength = k; break; }
            cp = (cp << 6) | (cb & 0x3F);
        }
        i += sequenceLength;
        processCodePoint(cp);
    }

    if(!_repaintTimer.isActive())
        _repaintTimer.start();
}

/******************************************************************************
* Feeds a single decoded code point into the emulator.
******************************************************************************/
void TerminalWidget::processCodePoint(char32_t cp)
{
    switch(_parserState) {

    case GroundState:
        if(cp == 0x1B) { _parserState = EscapeState; return; }
        if(cp < 0x20 || cp == 0x7F) { processControlCharacter(cp); return; }
        putCharacter(cp, codePointWidth(cp));
        return;

    case EscapeState:
        if(cp == '[') { _parserState = CsiState; _csiParams.clear(); _csiPrefix = 0; return; }
        if(cp == ']') { _parserState = OscState; _oscString.clear(); return; }
        if(cp == 'P' || cp == 'X' || cp == '^' || cp == '_') { _parserState = StringState; return; }
        if(cp == '(' || cp == ')' || cp == '*' || cp == '+' || cp == '#' || cp == '%') { _parserState = EscapeIntermediate; return; }
        processEscapeFinal(cp);
        _parserState = GroundState;
        return;

    case EscapeIntermediate:
        // Charset designators and similar two-byte sequences: we accept and ignore them.
        _parserState = GroundState;
        return;

    case CsiState:
        if(cp >= '0' && cp <= '9') {
            if(_csiParams.empty()) _csiParams.push_back(0);
            _csiParams.back() = std::min(_csiParams.back() * 10 + (int)(cp - '0'), 65535);
            return;
        }
        if(cp == ';' || cp == ':') { _csiParams.push_back(0); return; }
        if(cp == '?' || cp == '>' || cp == '<' || cp == '=' || cp == '!' || cp == '$' || cp == '"' || cp == ' ' || cp == '\'') {
            _csiPrefix = (char)cp;
            return;
        }
        processCsiFinal(cp);
        _parserState = GroundState;
        return;

    case OscState:
        if(cp == 0x07) { processOscString(); _parserState = GroundState; return; }
        if(cp == 0x1B) { _parserState = StringState; processOscString(); return; }   // ESC \ terminator.
        if(cp >= 0x20) _oscString.append(QChar((uint)cp));
        return;

    case StringState:
        // Wait for the string terminator; everything in between is discarded.
        if(cp == 0x07 || cp == '\\') _parserState = GroundState;
        else if(cp == 0x1B) _parserState = StringState;
        return;
    }
}

/******************************************************************************
* Handles a C0 control character in the ground state.
******************************************************************************/
void TerminalWidget::processControlCharacter(char32_t cp)
{
    switch(cp) {
    case 0x07:  // BEL
        break;
    case 0x08:  // BS
        if(_wrapPending) _wrapPending = false;
        else if(_cursorColumn > 0) _cursorColumn--;
        break;
    case 0x09: { // HT: advance to the next multiple of 8.
        int next = std::min(((_cursorColumn / 8) + 1) * 8, _columns - 1);
        _cursorColumn = next;
        _wrapPending = false;
        break;
    }
    case 0x0A:  // LF
    case 0x0B:  // VT
    case 0x0C:  // FF
        lineFeed();
        break;
    case 0x0D:  // CR
        _cursorColumn = 0;
        _wrapPending = false;
        break;
    default:
        break;
    }
}

/******************************************************************************
* Handles the final byte of an escape sequence introduced by ESC.
******************************************************************************/
void TerminalWidget::processEscapeFinal(char32_t cp)
{
    switch(cp) {
    case '7':   // DECSC: save cursor and attributes.
        _savedCursorColumn = _cursorColumn;
        _savedCursorRow = _cursorRow;
        _savedAttributes = _currentAttributes;
        break;
    case '8':   // DECRC: restore cursor and attributes.
        _cursorColumn = std::min(_savedCursorColumn, _columns - 1);
        _cursorRow = std::min(_savedCursorRow, _rows - 1);
        _currentAttributes = _savedAttributes;
        _wrapPending = false;
        break;
    case 'D':   // IND: index.
        lineFeed();
        break;
    case 'E':   // NEL: next line.
        _cursorColumn = 0;
        lineFeed();
        break;
    case 'M':   // RI: reverse index.
        reverseLineFeed();
        break;
    case 'c':   // RIS: full reset.
        reset();
        break;
    case '=':
        _applicationKeypad = true;
        break;
    case '>':
        _applicationKeypad = false;
        break;
    default:
        break;
    }
}

/******************************************************************************
* Handles a completed OSC string.
******************************************************************************/
void TerminalWidget::processOscString()
{
    int separator = _oscString.indexOf(QChar(';'));
    if(separator < 0)
        return;
    int command = _oscString.left(separator).toInt();
    QString payload = _oscString.mid(separator + 1);
    // 0 sets icon name and title, 1 the icon name, 2 the title. Everything else
    // (color queries, clipboard access, hyperlinks) is deliberately ignored.
    if(command == 0 || command == 2)
        Q_EMIT titleChanged(payload);
    _oscString.clear();
}

/******************************************************************************
* Writes a printable character at the cursor position.
******************************************************************************/
void TerminalWidget::putCharacter(char32_t cp, int width)
{
    if(width == 0) {
        // A combining mark: attach it to the previous cell if we can represent it there.
        return;
    }
    if(_wrapPending && _autoWrap) {
        _cursorColumn = 0;
        lineFeed();
        _wrapPending = false;
    }
    if(_cursorColumn + width > _columns) {
        if(!_autoWrap) {
            _cursorColumn = _columns - width;
            if(_cursorColumn < 0) return;
        }
        else {
            _cursorColumn = 0;
            lineFeed();
        }
    }

    Line& line = _screen[_cursorRow];
    if(_insertMode) {
        line.insert(line.begin() + _cursorColumn, width, blankCell());
        line.resize(_columns);
    }

    Cell cell = _currentAttributes;
    cell.ch = cp;
    cell.flags &= ~WidePadding;
    line[_cursorColumn] = cell;
    for(int k = 1; k < width && _cursorColumn + k < _columns; k++) {
        Cell padding = _currentAttributes;
        padding.ch = U' ';
        padding.flags |= WidePadding;
        line[_cursorColumn + k] = padding;
    }

    _cursorColumn += width;
    if(_cursorColumn >= _columns) {
        _cursorColumn = _columns - 1;
        _wrapPending = true;
    }
}

/******************************************************************************
* Returns the numeric CSI parameter at the given index, or the default value.
******************************************************************************/
int TerminalWidget::param(int index, int defaultValue) const
{
    // A parameter that is absent or zero means "use the default", which is how the
    // terminal protocol lets applications write CSI A instead of CSI 1 A.
    if(index < 0 || index >= (int)_csiParams.size() || _csiParams[index] == 0)
        return defaultValue;
    return _csiParams[index];
}

/******************************************************************************
* Moves the cursor to the given position, clamped to the screen.
******************************************************************************/
void TerminalWidget::setCursorPosition(int column, int row)
{
    _cursorColumn = std::clamp(column, 0, _columns - 1);
    if(_originMode)
        _cursorRow = std::clamp(row + _scrollTop, _scrollTop, _scrollBottom);
    else
        _cursorRow = std::clamp(row, 0, _rows - 1);
    _wrapPending = false;
}

/******************************************************************************
* Erases a range of cells in the given line.
******************************************************************************/
void TerminalWidget::eraseCells(Line& line, int from, int to)
{
    from = std::max(from, 0);
    to = std::min(to, (int)line.size() - 1);
    for(int i = from; i <= to; i++)
        line[i] = blankCell();
}

/******************************************************************************
* Advances the cursor to the next line, scrolling the scroll region if needed.
******************************************************************************/
void TerminalWidget::lineFeed()
{
    _wrapPending = false;
    if(_cursorRow == _scrollBottom)
        scrollUp(1);
    else if(_cursorRow < _rows - 1)
        _cursorRow++;
}

/******************************************************************************
* Moves the cursor up one line, scrolling the scroll region backwards if needed.
******************************************************************************/
void TerminalWidget::reverseLineFeed()
{
    _wrapPending = false;
    if(_cursorRow == _scrollTop)
        scrollDown(1);
    else if(_cursorRow > 0)
        _cursorRow--;
}

/******************************************************************************
* Scrolls the scroll region up, pushing lines to the scrollback buffer.
******************************************************************************/
void TerminalWidget::scrollUp(int count)
{
    count = std::min(count, _scrollBottom - _scrollTop + 1);
    for(int i = 0; i < count; i++) {
        // Only lines scrolling off the top of the primary screen are worth keeping:
        // the alternate screen belongs to a full-screen application redrawing itself.
        if(!_alternateScreen && _scrollTop == 0) {
            _scrollback.push_back(std::move(_screen[_scrollTop]));
            while((int)_scrollback.size() > _scrollbackLimit)
                _scrollback.pop_front();
        }
        _screen.erase(_screen.begin() + _scrollTop);
        _screen.insert(_screen.begin() + _scrollBottom, Line(_columns, blankCell()));
    }
}

/******************************************************************************
* Scrolls the scroll region down by the given number of lines.
******************************************************************************/
void TerminalWidget::scrollDown(int count)
{
    count = std::min(count, _scrollBottom - _scrollTop + 1);
    for(int i = 0; i < count; i++) {
        _screen.erase(_screen.begin() + _scrollBottom);
        _screen.insert(_screen.begin() + _scrollTop, Line(_columns, blankCell()));
    }
}

/******************************************************************************
* Switches between the primary and the alternate screen buffer.
******************************************************************************/
void TerminalWidget::useAlternateScreen(bool enabled, bool clear)
{
    if(enabled == _alternateScreen)
        return;
    if(enabled) {
        _savedScreen = _screen;
        _alternateScreen = true;
        if(clear)
            _screen.assign(_rows, Line(_columns, blankCell()));
    }
    else {
        _alternateScreen = false;
        _screen = _savedScreen;
        _savedScreen.clear();
        // The saved screen may be stale after an intermediate resize.
        _screen.resize(_rows, Line(_columns, blankCell()));
        for(Line& line : _screen)
            line.resize(_columns, blankCell());
    }
    _scrollTop = 0;
    _scrollBottom = _rows - 1;
    scrollToBottom();
}

/******************************************************************************
* Sets or resets DEC private modes and ANSI modes.
******************************************************************************/
void TerminalWidget::setMode(bool enabled, bool privateMode)
{
    for(size_t i = 0; i < std::max<size_t>(_csiParams.size(), 1); i++) {
        int mode = (i < _csiParams.size()) ? _csiParams[i] : 0;
        if(!privateMode) {
            if(mode == 4) _insertMode = enabled;     // IRM
            continue;
        }
        switch(mode) {
        case 1:     _applicationCursorKeys = enabled; break;
        case 6:     // DECOM: origin mode.
            _originMode = enabled;
            setCursorPosition(0, 0);
            break;
        case 7:     _autoWrap = enabled; break;
        case 25:    _cursorVisible = enabled; break;
        case 47:
        case 1047:  useAlternateScreen(enabled, true); break;
        case 1048:
            if(enabled) { _savedCursorColumn = _cursorColumn; _savedCursorRow = _cursorRow; _savedAttributes = _currentAttributes; }
            else { _cursorColumn = _savedCursorColumn; _cursorRow = _savedCursorRow; _currentAttributes = _savedAttributes; }
            break;
        case 1049:
            // The combination used by virtually all TUIs: save cursor, switch, clear.
            if(enabled) { _savedCursorColumn = _cursorColumn; _savedCursorRow = _cursorRow; _savedAttributes = _currentAttributes; }
            useAlternateScreen(enabled, true);
            if(!enabled) { _cursorColumn = std::min(_savedCursorColumn, _columns - 1); _cursorRow = std::min(_savedCursorRow, _rows - 1); _currentAttributes = _savedAttributes; }
            break;
        case 2004:  _bracketedPaste = enabled; break;
        default:
            // Mouse reporting modes (1000-1006) and others are accepted and ignored:
            // this widget does not forward mouse events to the application.
            break;
        }
    }
}

/******************************************************************************
* Applies an SGR (select graphic rendition) parameter list.
******************************************************************************/
void TerminalWidget::applySgr()
{
    if(_csiParams.empty())
        _csiParams.push_back(0);

    for(size_t i = 0; i < _csiParams.size(); i++) {
        int p = _csiParams[i];
        switch(p) {
        case 0:
            _currentAttributes = Cell();
            break;
        case 1:  _currentAttributes.flags |= BoldCell; break;
        case 2:  _currentAttributes.flags |= DimCell; break;
        case 3:  _currentAttributes.flags |= ItalicCell; break;
        case 4:  _currentAttributes.flags |= UnderlineCell; break;
        case 7:  _currentAttributes.flags |= InverseCell; break;
        case 8:  _currentAttributes.flags |= HiddenCell; break;
        case 9:  _currentAttributes.flags |= StrikeCell; break;
        case 21:
        case 22: _currentAttributes.flags &= ~(BoldCell | DimCell); break;
        case 23: _currentAttributes.flags &= ~ItalicCell; break;
        case 24: _currentAttributes.flags &= ~UnderlineCell; break;
        case 27: _currentAttributes.flags &= ~InverseCell; break;
        case 28: _currentAttributes.flags &= ~HiddenCell; break;
        case 29: _currentAttributes.flags &= ~StrikeCell; break;
        case 39: _currentAttributes.fg = DefaultColor; break;
        case 49: _currentAttributes.bg = DefaultColor; break;
        case 38:
        case 48: {
            // Extended color: 5;<index> selects a palette entry, 2;<r>;<g>;<b> a direct color.
            quint32 color = DefaultColor;
            size_t consumed = 0;
            if(i + 1 < _csiParams.size() && _csiParams[i+1] == 5 && i + 2 < _csiParams.size()) {
                color = (quint32)std::clamp(_csiParams[i+2], 0, 255);
                consumed = 2;
            }
            else if(i + 1 < _csiParams.size() && _csiParams[i+1] == 2 && i + 4 < _csiParams.size()) {
                int r = std::clamp(_csiParams[i+2], 0, 255);
                int g = std::clamp(_csiParams[i+3], 0, 255);
                int b = std::clamp(_csiParams[i+4], 0, 255);
                color = 0x01000000u | ((quint32)r << 16) | ((quint32)g << 8) | (quint32)b;
                consumed = 4;
            }
            if(consumed) {
                if(p == 38) _currentAttributes.fg = color;
                else _currentAttributes.bg = color;
                i += consumed;
            }
            break;
        }
        default:
            if(p >= 30 && p <= 37)        _currentAttributes.fg = (quint32)(p - 30);
            else if(p >= 40 && p <= 47)   _currentAttributes.bg = (quint32)(p - 40);
            else if(p >= 90 && p <= 97)   _currentAttributes.fg = (quint32)(p - 90 + 8);
            else if(p >= 100 && p <= 107) _currentAttributes.bg = (quint32)(p - 100 + 8);
            break;
        }
    }
}

/******************************************************************************
* Handles the final byte of a CSI sequence, using the collected parameters.
******************************************************************************/
void TerminalWidget::processCsiFinal(char32_t finalByte)
{
    const bool privateMode = (_csiPrefix == '?');

    switch(finalByte) {

    case '@': { // ICH: insert blank characters.
        int count = std::min(param(0, 1), _columns - _cursorColumn);
        Line& line = _screen[_cursorRow];
        line.insert(line.begin() + _cursorColumn, count, blankCell());
        line.resize(_columns);
        break;
    }
    case 'A': setCursorPosition(_cursorColumn, _cursorRow - param(0, 1)); break;      // CUU
    case 'B': setCursorPosition(_cursorColumn, _cursorRow + param(0, 1)); break;      // CUD
    case 'C': setCursorPosition(_cursorColumn + param(0, 1), _cursorRow); break;      // CUF
    case 'D': setCursorPosition(_cursorColumn - param(0, 1), _cursorRow); break;      // CUB
    case 'E': setCursorPosition(0, _cursorRow + param(0, 1)); break;                  // CNL
    case 'F': setCursorPosition(0, _cursorRow - param(0, 1)); break;                  // CPL
    case 'G':
    case '`': setCursorPosition(param(0, 1) - 1, _cursorRow); break;                  // CHA / HPA
    case 'H':
    case 'f': setCursorPosition(param(1, 1) - 1, param(0, 1) - 1); break;             // CUP / HVP
    case 'I': { // CHT: forward tabulation.
        for(int i = 0; i < param(0, 1); i++)
            _cursorColumn = std::min(((_cursorColumn / 8) + 1) * 8, _columns - 1);
        break;
    }
    case 'J': { // ED: erase in display.
        int mode = param(0, 0);
        if(mode == 0) {
            eraseCells(_screen[_cursorRow], _cursorColumn, _columns - 1);
            for(int r = _cursorRow + 1; r < _rows; r++) eraseCells(_screen[r], 0, _columns - 1);
        }
        else if(mode == 1) {
            eraseCells(_screen[_cursorRow], 0, _cursorColumn);
            for(int r = 0; r < _cursorRow; r++) eraseCells(_screen[r], 0, _columns - 1);
        }
        else if(mode == 2 || mode == 3) {
            for(int r = 0; r < _rows; r++) eraseCells(_screen[r], 0, _columns - 1);
            if(mode == 3) { _scrollback.clear(); updateScrollbar(); }
        }
        break;
    }
    case 'K': { // EL: erase in line.
        int mode = param(0, 0);
        if(mode == 0)      eraseCells(_screen[_cursorRow], _cursorColumn, _columns - 1);
        else if(mode == 1) eraseCells(_screen[_cursorRow], 0, _cursorColumn);
        else               eraseCells(_screen[_cursorRow], 0, _columns - 1);
        break;
    }
    case 'L': { // IL: insert lines within the scroll region.
        if(_cursorRow < _scrollTop || _cursorRow > _scrollBottom) break;
        int count = std::min(param(0, 1), _scrollBottom - _cursorRow + 1);
        for(int i = 0; i < count; i++) {
            _screen.erase(_screen.begin() + _scrollBottom);
            _screen.insert(_screen.begin() + _cursorRow, Line(_columns, blankCell()));
        }
        break;
    }
    case 'M': { // DL: delete lines within the scroll region.
        if(_cursorRow < _scrollTop || _cursorRow > _scrollBottom) break;
        int count = std::min(param(0, 1), _scrollBottom - _cursorRow + 1);
        for(int i = 0; i < count; i++) {
            _screen.erase(_screen.begin() + _cursorRow);
            _screen.insert(_screen.begin() + _scrollBottom, Line(_columns, blankCell()));
        }
        break;
    }
    case 'P': { // DCH: delete characters.
        int count = std::min(param(0, 1), _columns - _cursorColumn);
        Line& line = _screen[_cursorRow];
        line.erase(line.begin() + _cursorColumn, line.begin() + _cursorColumn + count);
        line.resize(_columns, blankCell());
        break;
    }
    case 'S': scrollUp(param(0, 1)); break;                                           // SU
    case 'T': scrollDown(param(0, 1)); break;                                         // SD
    case 'X': { // ECH: erase characters.
        int count = param(0, 1);
        eraseCells(_screen[_cursorRow], _cursorColumn, _cursorColumn + count - 1);
        break;
    }
    case 'Z': { // CBT: backward tabulation.
        for(int i = 0; i < param(0, 1); i++)
            _cursorColumn = std::max(((_cursorColumn - 1) / 8) * 8, 0);
        break;
    }
    case 'd': setCursorPosition(_cursorColumn, param(0, 1) - 1); break;               // VPA
    case 'h': setMode(true, privateMode); break;                                      // SM / DECSET
    case 'l': setMode(false, privateMode); break;                                     // RM / DECRST
    case 'm': applySgr(); break;                                                      // SGR
    case 'n': { // DSR: device status report.
        int request = param(0, 0);
        if(request == 5)
            sendToApplication(QByteArrayLiteral("\x1b[0n"));
        else if(request == 6)
            sendToApplication(QStringLiteral("\x1b[%1;%2R").arg(_cursorRow + 1).arg(_cursorColumn + 1).toLatin1());
        break;
    }
    case 'r': { // DECSTBM: set scroll region.
        int top = param(0, 1) - 1;
        int bottom = (_csiParams.size() > 1 && _csiParams[1] > 0) ? _csiParams[1] - 1 : _rows - 1;
        if(top < bottom && bottom < _rows) {
            _scrollTop = std::max(top, 0);
            _scrollBottom = bottom;
            setCursorPosition(0, _originMode ? 0 : _scrollTop);
        }
        break;
    }
    case 's':   // SCOSC: save cursor.
        _savedCursorColumn = _cursorColumn;
        _savedCursorRow = _cursorRow;
        break;
    case 'u':   // SCORC: restore cursor.
        _cursorColumn = std::min(_savedCursorColumn, _columns - 1);
        _cursorRow = std::min(_savedCursorRow, _rows - 1);
        break;
    case 'c':   // DA: device attributes. Identify as a VT102.
        if(!privateMode)
            sendToApplication(QByteArrayLiteral("\x1b[?6c"));
        break;
    case 'b': { // REP: repeat the last character.
        int count = param(0, 1);
        if(_cursorColumn > 0) {
            char32_t previous = _screen[_cursorRow][_cursorColumn - 1].ch;
            for(int i = 0; i < count; i++)
                putCharacter(previous, codePointWidth(previous));
        }
        break;
    }
    default:
        // Window manipulation (t), cursor style (q) and other sequences we do not model.
        break;
    }
}

/******************************************************************************
* Returns the actual QColor for a stored color value.
******************************************************************************/
QColor TerminalWidget::resolveColor(quint32 color, bool foreground, quint16 flags) const
{
    QColor result;
    if(color == DefaultColor)
        result = foreground ? _defaultForeground : _defaultBackground;
    else if(color & 0x01000000u)
        result = QColor((int)((color >> 16) & 0xFF), (int)((color >> 8) & 0xFF), (int)(color & 0xFF));
    else {
        quint32 index = color & 0xFFu;
        // Bold text traditionally selects the bright variant of the eight basic colors.
        if(foreground && (flags & BoldCell) && index < 8)
            index += 8;
        result = _palette[index];
    }
    if(foreground && (flags & DimCell))
        result = result.darker(150);
    return result;
}

/******************************************************************************
* Returns the line at the given absolute index (scrollback first, then screen).
******************************************************************************/
const TerminalWidget::Line& TerminalWidget::lineAt(int absoluteIndex) const
{
    static const Line emptyLine;
    if(absoluteIndex < 0)
        return emptyLine;
    if(absoluteIndex < (int)_scrollback.size())
        return _scrollback[absoluteIndex];
    int screenRow = absoluteIndex - (int)_scrollback.size();
    if(screenRow < (int)_screen.size())
        return _screen[screenRow];
    return emptyLine;
}

/******************************************************************************
* Updates the range and position of the vertical scrollbar.
******************************************************************************/
void TerminalWidget::updateScrollbar()
{
    QScrollBar* bar = verticalScrollBar();
    bool wasAtBottom = (bar->value() >= bar->maximum());
    int maximum = std::max(totalLineCount() - _rows, 0);
    bar->setRange(0, maximum);
    bar->setPageStep(_rows);
    bar->setSingleStep(1);
    // Keep the view pinned to the bottom while output is streaming in, unless the user
    // has scrolled up to read something.
    if(wasAtBottom || _alternateScreen)
        bar->setValue(maximum);
}

/******************************************************************************
* Scrolls the view to the bottom.
******************************************************************************/
void TerminalWidget::scrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

/******************************************************************************
* Recomputes the screen size from the widget size and notifies the application.
******************************************************************************/
void TerminalWidget::updateScreenSize()
{
    QFontMetricsF metrics(font());
    _charWidth = metrics.horizontalAdvance(QChar('W'));
    if(_charWidth <= 0.0) _charWidth = 8.0;
    _lineHeight = metrics.height();
    if(_lineHeight <= 0.0) _lineHeight = 16.0;
    _fontAscent = metrics.ascent();

    int newColumns = std::max((int)(viewport()->width() / _charWidth), 2);
    int newRows = std::max((int)(viewport()->height() / _lineHeight), 2);
    if(newColumns == _columns && newRows == _rows && !_screen.empty())
        return;

    // Growing or shrinking the screen keeps the content anchored at the top. Reflowing
    // wrapped lines is intentionally not attempted: full-screen applications redraw
    // themselves after the SIGWINCH anyway.
    _columns = newColumns;
    _rows = newRows;
    for(Line& line : _screen)
        line.resize(_columns, blankCell());
    if((int)_screen.size() > _rows) {
        int excess = (int)_screen.size() - _rows;
        // Prefer discarding empty lines below the cursor over losing content above it.
        int removeFromTop = std::min(excess, std::max(_cursorRow - _rows + 1, 0));
        for(int i = 0; i < removeFromTop; i++) {
            if(!_alternateScreen)
                _scrollback.push_back(std::move(_screen.front()));
            _screen.erase(_screen.begin());
        }
        _screen.resize(_rows);
        _cursorRow -= removeFromTop;
    }
    else {
        _screen.resize(_rows, Line(_columns, blankCell()));
    }
    while((int)_scrollback.size() > _scrollbackLimit)
        _scrollback.pop_front();

    _scrollTop = 0;
    _scrollBottom = _rows - 1;
    _cursorRow = std::clamp(_cursorRow, 0, _rows - 1);
    _cursorColumn = std::clamp(_cursorColumn, 0, _columns - 1);

    updateScrollbar();
    Q_EMIT windowSizeChanged(_columns, _rows);
}

/******************************************************************************
* Handles resizing of the widget.
******************************************************************************/
void TerminalWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScreenSize();
    viewport()->update();
}

/******************************************************************************
* Renders the visible portion of the screen.
******************************************************************************/
void TerminalWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), _defaultBackground);
    painter.setFont(font());

    const int firstLine = verticalScrollBar()->value();
    const int visibleLines = std::min(_rows, totalLineCount() - firstLine);

    // Normalize the selection so that the anchor comes first.
    QPoint selectionStart = _selectionAnchor;
    QPoint selectionEnd = _selectionEnd;
    if(selectionStart.y() > selectionEnd.y() || (selectionStart.y() == selectionEnd.y() && selectionStart.x() > selectionEnd.x()))
        std::swap(selectionStart, selectionEnd);
    const bool hasSelection = (selectionStart.y() >= 0 && selectionEnd.y() >= 0);

    for(int row = 0; row < visibleLines; row++) {
        const Line& line = lineAt(firstLine + row);
        const int absoluteLine = firstLine + row;
        const qreal y = row * _lineHeight;

        for(int column = 0; column < (int)line.size(); column++) {
            const Cell& cell = line[column];
            if(cell.flags & WidePadding)
                continue;

            bool selected = false;
            if(hasSelection) {
                if(absoluteLine > selectionStart.y() && absoluteLine < selectionEnd.y())
                    selected = true;
                else if(absoluteLine == selectionStart.y() && absoluteLine == selectionEnd.y())
                    selected = (column >= selectionStart.x() && column < selectionEnd.x());
                else if(absoluteLine == selectionStart.y())
                    selected = (column >= selectionStart.x());
                else if(absoluteLine == selectionEnd.y())
                    selected = (column < selectionEnd.x());
            }

            bool inverse = (cell.flags & InverseCell) != 0;
            if(selected) inverse = !inverse;
            QColor foreground = resolveColor(inverse ? cell.bg : cell.fg, !inverse, cell.flags);
            QColor background = resolveColor(inverse ? cell.fg : cell.bg, inverse, cell.flags);
            if(inverse && cell.bg == DefaultColor) foreground = _defaultBackground;
            if(inverse && cell.fg == DefaultColor) background = _defaultForeground;

            const int width = (column + 1 < (int)line.size() && (line[column+1].flags & WidePadding)) ? 2 : 1;
            QRectF cellRect(column * _charWidth, y, _charWidth * width, _lineHeight);
            if(background != _defaultBackground)
                painter.fillRect(cellRect, background);

            if(cell.ch == U' ' || (cell.flags & HiddenCell))
                continue;

            QFont cellFont = font();
            if(cell.flags & BoldCell) cellFont.setBold(true);
            if(cell.flags & ItalicCell) cellFont.setItalic(true);
            if(cell.flags & UnderlineCell) cellFont.setUnderline(true);
            if(cell.flags & StrikeCell) cellFont.setStrikeOut(true);
            painter.setFont(cellFont);
            painter.setPen(foreground);
            painter.drawText(QPointF(column * _charWidth, y + _fontAscent), QString::fromUcs4(&cell.ch, 1));
        }
    }

    // Draw the cursor as a filled block, hollow when the widget does not have the focus.
    if(_cursorVisible) {
        int cursorLine = (int)_scrollback.size() + _cursorRow - firstLine;
        if(cursorLine >= 0 && cursorLine < _rows) {
            QRectF cursorRect(_cursorColumn * _charWidth, cursorLine * _lineHeight, _charWidth, _lineHeight);
            if(_hasFocus) {
                if(_cursorBlinkOn) {
                    painter.fillRect(cursorRect, _defaultForeground);
                    const Line& line = lineAt(firstLine + cursorLine);
                    if(_cursorColumn < (int)line.size() && line[_cursorColumn].ch != U' ') {
                        painter.setPen(_defaultBackground);
                        painter.setFont(font());
                        painter.drawText(QPointF(cursorRect.left(), cursorRect.top() + _fontAscent), QString::fromUcs4(&line[_cursorColumn].ch, 1));
                    }
                }
            }
            else {
                painter.setPen(_defaultForeground);
                painter.drawRect(cursorRect.adjusted(0, 0, -1, -1));
            }
        }
    }
}

/******************************************************************************
* Sends the given bytes to the application, scrolling the view to the bottom first.
******************************************************************************/
void TerminalWidget::sendToApplication(const QByteArray& data)
{
    if(data.isEmpty())
        return;
    scrollToBottom();
    Q_EMIT dataToSend(data);
}

/******************************************************************************
* Sends text as if it had been pasted, honoring bracketed paste mode.
******************************************************************************/
void TerminalWidget::pasteText(const QString& text)
{
    if(text.isEmpty())
        return;
    // Terminals send \r for Enter, and a pasted \n would otherwise submit each line
    // separately in applications that accept multi-line input.
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    normalized.replace(QChar('\n'), QChar('\r'));
    QByteArray payload = normalized.toUtf8();
    if(_bracketedPaste)
        payload = QByteArrayLiteral("\x1b[200~") + payload + QByteArrayLiteral("\x1b[201~");
    sendToApplication(payload);
}

/******************************************************************************
* Translates key presses into the byte sequences expected by the application.
******************************************************************************/
void TerminalWidget::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);

    // Clipboard shortcuts are handled by the widget itself and never reach the application.
    if(control && shift && event->key() == Qt::Key_C) {
        QString selection = selectedText();
        if(!selection.isEmpty())
            QGuiApplication::clipboard()->setText(selection);
        return;
    }
    if(control && shift && event->key() == Qt::Key_V) {
        pasteText(QGuiApplication::clipboard()->text());
        return;
    }

    // Scrolling through the scrollback buffer, as long as no full-screen application is running.
    if(shift && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) && !_alternateScreen) {
        verticalScrollBar()->triggerAction(event->key() == Qt::Key_PageUp ?
            QAbstractSlider::SliderPageStepSub : QAbstractSlider::SliderPageStepAdd);
        return;
    }

    // The modifier encoding used by xterm for cursor and function keys.
    int modifierCode = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (control ? 4 : 0);
    auto cursorKey = [&](char letter) -> QByteArray {
        if(modifierCode != 1)
            return QStringLiteral("\x1b[1;%1%2").arg(modifierCode).arg(QChar(letter)).toLatin1();
        return _applicationCursorKeys ?
            QByteArray("\x1bO") + letter :
            QByteArray("\x1b[") + letter;
    };
    auto tildeKey = [&](int number) -> QByteArray {
        if(modifierCode != 1)
            return QStringLiteral("\x1b[%1;%2~").arg(number).arg(modifierCode).toLatin1();
        return QStringLiteral("\x1b[%1~").arg(number).toLatin1();
    };

    QByteArray output;
    switch(event->key()) {
    case Qt::Key_Up:        output = cursorKey('A'); break;
    case Qt::Key_Down:      output = cursorKey('B'); break;
    case Qt::Key_Right:     output = cursorKey('C'); break;
    case Qt::Key_Left:      output = cursorKey('D'); break;
    case Qt::Key_Home:      output = cursorKey('H'); break;
    case Qt::Key_End:       output = cursorKey('F'); break;
    case Qt::Key_Insert:    output = tildeKey(2); break;
    case Qt::Key_Delete:    output = tildeKey(3); break;
    case Qt::Key_PageUp:    output = tildeKey(5); break;
    case Qt::Key_PageDown:  output = tildeKey(6); break;
    case Qt::Key_F1:        output = QByteArrayLiteral("\x1bOP"); break;
    case Qt::Key_F2:        output = QByteArrayLiteral("\x1bOQ"); break;
    case Qt::Key_F3:        output = QByteArrayLiteral("\x1bOR"); break;
    case Qt::Key_F4:        output = QByteArrayLiteral("\x1bOS"); break;
    case Qt::Key_F5:        output = tildeKey(15); break;
    case Qt::Key_F6:        output = tildeKey(17); break;
    case Qt::Key_F7:        output = tildeKey(18); break;
    case Qt::Key_F8:        output = tildeKey(19); break;
    case Qt::Key_F9:        output = tildeKey(20); break;
    case Qt::Key_F10:       output = tildeKey(21); break;
    case Qt::Key_F11:       output = tildeKey(23); break;
    case Qt::Key_F12:       output = tildeKey(24); break;
    case Qt::Key_Backspace: output = control ? QByteArrayLiteral("\x08") : QByteArrayLiteral("\x7f"); break;
    case Qt::Key_Tab:       output = QByteArrayLiteral("\t"); break;
    case Qt::Key_Backtab:   output = QByteArrayLiteral("\x1b[Z"); break;
    case Qt::Key_Escape:    output = QByteArrayLiteral("\x1b"); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Agents use ESC-Enter (what terminals send for Option+Enter) to insert a line
        // break without submitting the prompt. Map Shift+Enter to it as well, which is
        // what users coming from chat interfaces reach for first.
        output = (alt || shift) ? QByteArrayLiteral("\x1b\r") : QByteArrayLiteral("\r");
        break;
    default: {
        QString text = event->text();
        if(text.isEmpty()) {
            QAbstractScrollArea::keyPressEvent(event);
            return;
        }
        output = text.toUtf8();
        // Qt already folds Ctrl+letter into a control character in text(); Alt prefixes an ESC.
        if(alt)
            output.prepend('\x1b');
        break;
    }
    }

    if(!output.isEmpty()) {
        sendToApplication(output);
        event->accept();
    }
}

/******************************************************************************
* Makes sure Tab and Backtab reach the terminal instead of moving the focus.
******************************************************************************/
bool TerminalWidget::event(QEvent* event)
{
    if(event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if(keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }
    else if(event->type() == QEvent::ShortcutOverride) {
        // The agent owns the keyboard while it has the focus: do not let application-wide
        // shortcuts swallow keys such as Ctrl+C, which must interrupt the agent instead.
        event->accept();
        return true;
    }
    return QAbstractScrollArea::event(event);
}

/******************************************************************************
* Handles text composed through an input method.
******************************************************************************/
void TerminalWidget::inputMethodEvent(QInputMethodEvent* event)
{
    if(!event->commitString().isEmpty())
        sendToApplication(event->commitString().toUtf8());
    event->accept();
}

/******************************************************************************
* Converts a widget position to a cell position in absolute line coordinates.
******************************************************************************/
QPoint TerminalWidget::cellAtPosition(const QPoint& pos) const
{
    int column = std::clamp((int)(pos.x() / _charWidth + 0.5), 0, _columns);
    int line = verticalScrollBar()->value() + (int)(pos.y() / _lineHeight);
    line = std::clamp(line, 0, std::max(totalLineCount() - 1, 0));
    return QPoint(column, line);
}

/******************************************************************************
* Starts a mouse selection, or pastes the primary selection on a middle click.
******************************************************************************/
void TerminalWidget::mousePressEvent(QMouseEvent* event)
{
    if(event->button() == Qt::MiddleButton) {
        pasteText(QGuiApplication::clipboard()->text(QClipboard::Selection));
        return;
    }
    if(event->button() != Qt::LeftButton)
        return;
    _selecting = true;
    _selectionAnchor = _selectionEnd = cellAtPosition(event->position().toPoint());
    viewport()->update();
}

/******************************************************************************
* Extends the current mouse selection.
******************************************************************************/
void TerminalWidget::mouseMoveEvent(QMouseEvent* event)
{
    if(!_selecting)
        return;
    _selectionEnd = cellAtPosition(event->position().toPoint());
    viewport()->update();
}

/******************************************************************************
* Finishes a mouse selection and publishes it as the primary selection.
******************************************************************************/
void TerminalWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if(event->button() != Qt::LeftButton || !_selecting)
        return;
    _selecting = false;
    QString text = selectedText();
    if(text.isEmpty())
        _selectionAnchor = _selectionEnd = QPoint(-1, -1);
    else if(QGuiApplication::clipboard()->supportsSelection())
        QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
    viewport()->update();
}

/******************************************************************************
* Returns the text currently selected with the mouse.
******************************************************************************/
QString TerminalWidget::selectedText() const
{
    QPoint start = _selectionAnchor;
    QPoint end = _selectionEnd;
    if(start.y() < 0 || end.y() < 0)
        return {};
    if(start.y() > end.y() || (start.y() == end.y() && start.x() > end.x()))
        std::swap(start, end);

    QString result;
    for(int line = start.y(); line <= end.y(); line++) {
        const Line& cells = lineAt(line);
        int from = (line == start.y()) ? start.x() : 0;
        int to = (line == end.y()) ? end.x() : (int)cells.size();
        QString lineText;
        for(int i = from; i < std::min(to, (int)cells.size()); i++) {
            if(cells[i].flags & WidePadding)
                continue;
            lineText.append(QString::fromUcs4(&cells[i].ch, 1));
        }
        // Trailing blanks are padding, not content.
        while(lineText.endsWith(QChar(' ')))
            lineText.chop(1);
        if(line != start.y())
            result.append(QChar('\n'));
        result.append(lineText);
    }
    return result;
}

/******************************************************************************
* Starts the cursor blinking when the widget receives the keyboard focus.
******************************************************************************/
void TerminalWidget::focusInEvent(QFocusEvent* event)
{
    _hasFocus = true;
    _cursorBlinkOn = true;
    _blinkTimer.start();
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

/******************************************************************************
* Stops the cursor blinking when the widget loses the keyboard focus.
******************************************************************************/
void TerminalWidget::focusOutEvent(QFocusEvent* event)
{
    _hasFocus = false;
    _blinkTimer.stop();
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

/******************************************************************************
* Appends a line of text to the screen as if the application had printed it.
******************************************************************************/
void TerminalWidget::appendMessage(const QString& text)
{
    QString line = text;
    line.replace(QChar('\n'), QStringLiteral("\r\n"));
    feed((QStringLiteral("\r\n") + line + QStringLiteral("\r\n")).toUtf8());
}

}   // End of namespace

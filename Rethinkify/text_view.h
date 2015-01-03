#pragma once

#include "document.h"
#include "resource.h"
#include "ui.h"

static FORMATETC plainTextFormat = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
static FORMATETC plainTextWFormat = { CF_UNICODETEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

class text_view : public CWindowImpl<text_view>, public IDropTarget, public IView
{
private:

    document &_doc;

    volatile unsigned long m_cRef = 0;
    text_location m_ptDropPos;
    bool m_bDragSelection = false;
    bool m_bWordSelection = false;
    bool m_bLineSelection = false;
    bool m_bPreparingToDrag = false;
    bool m_bDropPosVisible = false;
    bool m_bCursorHidden = false;
    UINT m_nDragSelTimer = 0;
    text_location m_ptSavedCaretPos;
    text_selection m_ptDraggedText;
    HFONT _font = nullptr;

    CSize _char_offset;
    CSize _extent;
    CSize _font_extent;

    int m_nScreenLines = 0;
    int m_nScreenChars = 0;

public:

    text_view(document &d) : _doc(d)
    {
    }

    ~text_view()
    {
    }


    BEGIN_MSG_MAP(text_view)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
        MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSysColorChange)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_CHAR, OnChar)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
    END_MSG_MAP()

    LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        RegisterDragDrop(m_hWnd, this);

        LOGFONT lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = ANSI_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        wcscpy_s(lf.lfFaceName, L"Consolas");

        _font = ::CreateFontIndirect(&lf);

        return 0;
    }

    LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        RevokeDragDrop(m_hWnd);
        return 0;
    }

    LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        win_dc hdc(m_hWnd);
        auto pOldFont = hdc.SelectFont(_font);

        CSize font_extent;
        GetTextExtentExPoint(hdc, _T("X"), 1, 1, nullptr, nullptr, &font_extent);
        if (font_extent.cy < 1) font_extent.cy = 1;

        /*
        TEXTMETRIC tm;
        if (hdc->GetTextMetrics(&tm))
        m_nCharWidth -= tm.tmOverhang;
        */

        _extent = CSize(LOWORD(lParam), HIWORD(lParam));
        _font_extent = font_extent;

        layout();

        hdc.SelectFont(pOldFont);


        return 0;
    }

    LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        PAINTSTRUCT ps = { 0 };
        auto hdc = BeginPaint(&ps);
        draw(hdc);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        return 1;
    }

    LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        OnTimer(wParam);
        return 0;
    }
    LRESULT OnSysColorChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        Invalidate(FALSE);
        return 0;
    }
    LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnSetFocus((HWND) wParam);
        return 0;
    }

    LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnKillFocus((HWND) wParam);
        return 0;
    }

    LRESULT OnVScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        auto nScrollCode = (int) LOWORD(wParam);
        auto nPos = (short int) HIWORD(wParam);
        auto hwndScrollBar = (HWND) lParam;
        OnVScroll(nScrollCode, nPos, hwndScrollBar);
        return 0;
    }

    LRESULT OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        auto nScrollCode = (int) LOWORD(wParam);
        auto nPos = (short int) HIWORD(wParam);
        auto hwndScrollBar = (HWND) lParam;
        OnHScroll(nScrollCode, nPos, hwndScrollBar);
        return 0;
    }

    LRESULT OnLButtonDblClk(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnLButtonDblClk(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
        return 0;
    }

    LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnLButtonDown(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
        return 0;
    }

    LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnLButtonUp(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
        return 0;
    }

    LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnMouseMove(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
        return 0;
    }

    LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        auto delta = ((short) HIWORD(wParam)) > 0 ? -2 : 2;
        OnMouseWheel(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), delta);
        return 0;
    }

    LRESULT OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        UINT nChar = wParam;
        UINT nRepCnt = 0xFF & lParam;
        UINT nFlags = lParam;

        if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            return 0;

        if (nChar == VK_RETURN)
        {
            if (_doc.QueryEditable())
            {
                undo_group ug(_doc);
                auto pos = _doc.delete_text(ug, _doc.selection());
                _doc.locate(_doc.insert_text(ug, pos, L'\n'));
            }
        }
        else if (nChar > 31)
        {
            if (_doc.QueryEditable())
            {
                undo_group ug(_doc);
                auto pos = _doc.delete_text(ug, _doc.selection());
                _doc.locate(_doc.insert_text(ug, pos, nChar));
            }
        }

        return 0;
    }

    LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        OnCommand(LOWORD(wParam));
        return 0;
    }

    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        CPoint location(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        CPoint clientLocation(location);
        ScreenToClient(&clientLocation);

        // Find out if we're over any errors
        /*SpError thisError;
        bool bFound = false;

        for (auto split = _errors.begin(); !bFound && split != _errors.end(); ++split)
        {
        if (split->rcArea.Contains(clientPt))
        {
        thisError = *split;
        bFound = true;
        }
        }

        if (!bFound)
        {
        bHandled = FALSE;
        return 0;
        }*/


        auto menu = CreatePopupMenu();

        if (menu)
        {

            auto selection = _doc.select_word(client_to_text(clientLocation));
            auto word = Combine(_doc.text(selection));

            std::map<UINT, std::wstring> replacements;

            if (!selection.empty())
            {
                auto id = 1000U;


                for (auto option : _doc.suggest(word))
                {
                    auto word = Replace(option, L"&", L"&&");
                    AppendMenu(menu, MF_ENABLED, id, word.c_str());
                    replacements[id] = option;
                    id++;
                }

                if (replacements.size() > 0)
                {
                    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                }

                if (_doc.can_add(word))
                {
                    AppendMenu(menu, MF_ENABLED, ID_FILE_NEW, String::Format(L"Add '%s'", word.c_str()).c_str());
                    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                }
            }

            if (_doc.can_undo())
            {
                AppendMenu(menu, MF_ENABLED, ID_EDIT_UNDO, L"undo");
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
            }

            AppendMenu(menu, MF_ENABLED, ID_EDIT_CUT, L"Cut");
            AppendMenu(menu, MF_ENABLED, ID_EDIT_COPY, L"Copy");
            AppendMenu(menu, MF_ENABLED, ID_EDIT_PASTE, L"Paste");
            AppendMenu(menu, MF_ENABLED, ID_EDIT_DELETE, L"erase");
            AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenu(menu, MF_ENABLED, ID_EDIT_SELECT_ALL, L"select All");

            auto result = TrackPopupMenu(menu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_VERNEGANIMATION, location.x, location.y, 0, m_hWnd, NULL);
            DestroyMenu(menu);

            switch (result)
            {
            case ID_EDIT_UNDO:
            case ID_EDIT_CUT:
            case ID_EDIT_COPY:
            case ID_EDIT_PASTE:
            case ID_EDIT_DELETE:
            case ID_EDIT_SELECT_ALL:
                OnCommand(result);
                break;

            case ID_FILE_NEW:
                _doc.add_word(word);
                break;

            default:

                if (replacements.find(result) != replacements.end())
                {
                    _doc.delete_text(selection);
                    _doc.insert_text(selection._start, replacements[result]);
                    _doc.select(_doc.word_selection());
                }
                break;
            }
        }

        return 0;
    }

    void OnCommand(int id)
    {
        switch (id)
        {
        case ID_EDIT_COPY: _doc.Copy(); break;
        case ID_EDIT_SELECT_ALL: _doc.SelectAll(); break;
            //case ID_EDIT_FIND: OnEditFind(); break;
        case ID_EDIT_REPEAT: _doc.OnEditRepeat(); break;
        case ID_EDIT_FIND_PREVIOUS: _doc.OnEditFindPrevious(); break;
        case ID_EDIT_CHAR_LEFT: _doc.MoveLeft(false); break;
        case ID_EDIT_EXT_CHAR_LEFT: _doc.MoveLeft(true); break;
        case ID_EDIT_CHAR_RIGHT: _doc.MoveRight(false); break;
        case ID_EDIT_EXT_CHAR_RIGHT: _doc.MoveRight(true); break;
        case ID_EDIT_WORD_LEFT: _doc.MoveWordLeft(false); break;
        case ID_EDIT_EXT_WORD_LEFT: _doc.MoveWordLeft(true); break;
        case ID_EDIT_WORD_RIGHT: _doc.MoveWordRight(false); break;
        case ID_EDIT_EXT_WORD_RIGHT: _doc.MoveWordRight(true); break;
        case ID_EDIT_LINE_UP: _doc.MoveUp(false); break;
        case ID_EDIT_EXT_LINE_UP: _doc.MoveUp(true); break;
        case ID_EDIT_LINE_DOWN: _doc.MoveDown(false); break;
        case ID_EDIT_EXT_LINE_DOWN: _doc.MoveDown(true); break;
        case ID_EDIT_SCROLL_UP: ScrollUp(); break;
        case ID_EDIT_SCROLL_DOWN: ScrollDown(); break;
        case ID_EDIT_PAGE_UP: move_page(false, false); break;
        case ID_EDIT_EXT_PAGE_UP: move_page(true, false); break;
        case ID_EDIT_PAGE_DOWN: move_page(false, true); break;
        case ID_EDIT_EXT_PAGE_DOWN: move_page(true, true); break;
        case ID_EDIT_LINE_END: _doc.MoveEnd(false); break;
        case ID_EDIT_EXT_LINE_END: _doc.MoveEnd(true); break;
        case ID_EDIT_HOME: _doc.MoveHome(false); break;
        case ID_EDIT_EXT_HOME: _doc.MoveHome(true); break;
        case ID_EDIT_TEXT_BEGIN: _doc.MoveCtrlHome(false); break;
        case ID_EDIT_EXT_TEXT_BEGIN: _doc.MoveCtrlHome(true); break;
        case ID_EDIT_TEXT_END: _doc.MoveCtrlEnd(false); break;
        case ID_EDIT_EXT_TEXT_END: _doc.MoveCtrlEnd(true); break;
        case ID_EDIT_PASTE: _doc.Paste(); break;
        case ID_EDIT_CUT: _doc.Cut(); break;
        case ID_EDIT_DELETE: _doc.OnEditDelete(); break;
        case ID_EDIT_DELETE_BACK: _doc.OnEditDeleteBack(); break;
        case ID_EDIT_UNTAB: _doc.OnEditUntab(); break;
        case ID_EDIT_TAB: _doc.OnEditTab(); break;
        case ID_EDIT_REPLACE: _doc.OnEditReplace(); break;
        case ID_EDIT_UNDO: _doc.OnEditUndo(); break;
        case ID_EDIT_REDO: _doc.OnEditRedo(); break;
        }
    }

    void OnSetFocus(CWindow oldWnd)
    {
        _doc.m_bFocused = true;
        if (_doc.has_selection())
            invalidate_lines(_doc._selection._start.y, _doc._selection._end.y);
        update_caret();
    }

    void OnKillFocus(CWindow newWnd)
    {
        _doc.m_bFocused = false;
        update_caret();
        if (_doc.has_selection())
            invalidate_lines(_doc._selection._start.y, _doc._selection._end.y);
        if (m_bDragSelection)
        {
            ReleaseCapture();
            KillTimer(m_nDragSelTimer);
            m_bDragSelection = false;
        }
    }

    void OnTimer(UINT nIDEvent)
    {
        if (nIDEvent == RETHINKIFY_TIMER_DRAGSEL)
        {
            assert(m_bDragSelection);
            CPoint pt;
            ::GetCursorPos(&pt);
            ScreenToClient(&pt);

            auto rcClient = client_rect();
            auto bChanged = false;
            auto nNewTopLine = _char_offset.cy;
            auto line_count = _doc.size();

            if (pt.y < rcClient.top)
            {
                nNewTopLine--;

                if (pt.y < rcClient.top - _font_extent.cy)
                    nNewTopLine -= 2;
            }
            else if (pt.y >= rcClient.bottom)
            {
                nNewTopLine++;

                if (pt.y >= rcClient.bottom + _font_extent.cy)
                    nNewTopLine += 2;
            }

            nNewTopLine = Clamp(nNewTopLine, 0, line_count - 1);

            if (_char_offset.cy != nNewTopLine)
            {
                ScrollToLine(nNewTopLine, true);
                bChanged = true;
            }

            //	Scroll horizontally, if necessary
            auto nNewOffsetChar = _char_offset.cx;
            auto nMaxLineLength = _doc.max_line_length();

            if (pt.x < rcClient.left)
            {
                nNewOffsetChar--;
            }
            else if (pt.x >= rcClient.right)
            {
                nNewOffsetChar++;
            }

            nNewOffsetChar = Clamp(nNewOffsetChar, 0, nMaxLineLength - 1);

            if (_char_offset.cx != nNewOffsetChar)
            {
                ScrollToChar(nNewOffsetChar, true);
                update_caret();
                bChanged = true;
            }

            //	Fix changes
            if (bChanged)
            {
                text_location ptNewCursorPos = client_to_text(pt);

                if (ptNewCursorPos != _doc.m_ptCursorPos)
                {
                    _doc.m_ptCursorPos = ptNewCursorPos;
                    update_caret();
                }
                _doc.select(text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos));
            }
        }
    }

    void OnLButtonDown(const CPoint &point, UINT nFlags)
    {
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        SetFocus();

        if (point.x < _doc.margin_width())
        {
            if (bControl)
            {
                _doc.SelectAll();
            }
            else
            {
                _doc.m_ptCursorPos = client_to_text(point);
                _doc.m_ptCursorPos.x = 0;				//	Force beginning of the line
                if (!bShift)
                    _doc.m_ptAnchor = _doc.m_ptCursorPos;

                text_location ptStart, ptEnd;
                ptStart = _doc.m_ptAnchor;
                if (ptStart.y == _doc.size() - 1)
                    ptStart.x = _doc[ptStart.y].size();
                else
                {
                    ptStart.y++;
                    ptStart.x = 0;
                }

                ptEnd = _doc.m_ptCursorPos;
                ptEnd.x = 0;

                _doc.m_ptCursorPos = ptEnd;
                ensure_visible(_doc.m_ptCursorPos);
                _doc.select(text_selection(ptStart, ptEnd));

                SetCapture();
                m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
                assert(m_nDragSelTimer != 0);
                m_bWordSelection = false;
                m_bLineSelection = true;
                m_bDragSelection = true;
            }
        }
        else
        {
            auto ptText = client_to_text(point);

            if (_doc.is_inside_selection(ptText))				// And D&D Not Disabled
            {
                m_bPreparingToDrag = true;
            }
            else
            {
                _doc.m_ptCursorPos = ptText;

                if (!bShift)
                    _doc.m_ptAnchor = _doc.m_ptCursorPos;

                auto selection = bControl ? _doc.word_selection() : text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos);

                _doc.m_ptCursorPos = selection._end;
                ensure_visible(_doc.m_ptCursorPos);
                _doc.select(selection);

                SetCapture();
                m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
                assert(m_nDragSelTimer != 0);
                m_bWordSelection = bControl;
                m_bLineSelection = false;
                m_bDragSelection = true;
            }
        }
    }

    void OnMouseWheel(const CPoint &point, int zDelta)
    {
        ScrollToLine(Clamp(_char_offset.cy + zDelta, 0, _doc.size()), true);
    }

    void OnMouseMove(const CPoint &point, UINT nFlags)
    {
        if (m_bDragSelection)
        {
            bool bOnMargin = point.x < _doc.margin_width();
            text_location ptNewCursorPos = client_to_text(point);
            text_location ptStart, ptEnd;

            if (m_bLineSelection)
            {
                if (bOnMargin)
                {
                    if (ptNewCursorPos.y < _doc.m_ptAnchor.y ||
                        ptNewCursorPos.y == _doc.m_ptAnchor.y && ptNewCursorPos.x < _doc.m_ptAnchor.x)
                    {
                        ptEnd = _doc.m_ptAnchor;
                        if (ptEnd.y == _doc.size() - 1)
                        {
                            ptEnd.x = _doc[ptEnd.y].size();
                        }
                        else
                        {
                            ptEnd.y++;
                            ptEnd.x = 0;
                        }
                        ptNewCursorPos.x = 0;
                        _doc.m_ptCursorPos = ptNewCursorPos;
                    }
                    else
                    {
                        ptEnd = _doc.m_ptAnchor;
                        ptEnd.x = 0;
                        _doc.m_ptCursorPos = ptNewCursorPos;
                        if (ptNewCursorPos.y == _doc.size() - 1)
                        {
                            ptNewCursorPos.x = _doc[ptNewCursorPos.y].size();
                        }
                        else
                        {
                            ptNewCursorPos.y++;
                            ptNewCursorPos.x = 0;
                        }
                        _doc.m_ptCursorPos.x = 0;
                    }
                    update_caret();
                    _doc.select(text_selection(ptNewCursorPos, ptEnd));
                    return;
                }

                //	Moving to normal selection mode
                ::SetCursor(::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM)));
                m_bLineSelection = m_bWordSelection = false;
            }

            if (m_bWordSelection)
            {
                if (ptNewCursorPos.y < _doc.m_ptAnchor.y ||
                    (ptNewCursorPos.y == _doc.m_ptAnchor.y && ptNewCursorPos.x < _doc.m_ptAnchor.x))
                {
                    ptStart = _doc.WordToLeft(ptNewCursorPos);
                    ptEnd = _doc.WordToRight(_doc.m_ptAnchor);
                }
                else
                {
                    ptStart = _doc.WordToLeft(_doc.m_ptAnchor);
                    ptEnd = _doc.WordToRight(ptNewCursorPos);
                }
            }
            else
            {
                ptStart = _doc.m_ptAnchor;
                ptEnd = ptNewCursorPos;
            }

            _doc.m_ptCursorPos = ptEnd;
            update_caret();
            _doc.select(text_selection(ptStart, ptEnd));
        }

        if (m_bPreparingToDrag)
        {
            m_bPreparingToDrag = false;
            auto hData = PrepareDragData();

            if (hData != nullptr)
            {
                //undo_group ug(*this);


                /*COleDataSource ds;
                ds.CacheGlobalData(CF_UNICODETEXT, hData);
                m_bDraggingText = true;
                DROPEFFECT de = ds.DoDragDrop(GetDropEffect());
                if (de != DROPEFFECT_NONE)
                if (m_bDraggingText && de == DROPEFFECT_MOVE)
                {
                undo_group ug(_doc.);
                _doc.delete_text(ug, m_ptDraggedText);
                }
                m_bDraggingText = false;

                if (_doc != nullptr)
                _doc.FlushUndoGroup(this);*/
            }
        }
    }

    DROPEFFECT GetDropEffect()
    {
        return DROPEFFECT_COPY | DROPEFFECT_MOVE;
    }

    void OnLButtonUp(const CPoint &point, UINT nFlags)
    {
        if (m_bDragSelection)
        {
            text_location ptNewCursorPos = client_to_text(point);

            if (m_bLineSelection)
            {
                text_location ptStart, ptEnd;

                if (ptNewCursorPos.y < _doc.m_ptAnchor.y ||
                    ptNewCursorPos.y == _doc.m_ptAnchor.y && ptNewCursorPos.x < _doc.m_ptAnchor.x)
                {
                    ptEnd = _doc.m_ptAnchor;
                    if (ptEnd.y == _doc.size() - 1)
                    {
                        ptEnd.x = _doc[ptEnd.y].size();
                    }
                    else
                    {
                        ptEnd.y++;
                        ptEnd.x = 0;
                    }
                    ptNewCursorPos.x = 0;
                    _doc.m_ptCursorPos = ptNewCursorPos;
                }
                else
                {
                    ptEnd = _doc.m_ptAnchor;
                    ptEnd.x = 0;
                    if (ptNewCursorPos.y == _doc.size() - 1)
                    {
                        ptNewCursorPos.x = _doc[ptNewCursorPos.y].size();
                    }
                    else
                    {
                        ptNewCursorPos.y++;
                        ptNewCursorPos.x = 0;
                    }
                    _doc.m_ptCursorPos = ptNewCursorPos;
                }

                ensure_visible(_doc.m_ptCursorPos);
                _doc.select(text_selection(ptNewCursorPos, ptEnd));
            }
            else
            {
                auto selection = m_bWordSelection ? _doc.word_selection() : text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos);
                _doc.m_ptCursorPos = selection._end;
                ensure_visible(_doc.m_ptCursorPos);
                _doc.select(selection);
            }

            ReleaseCapture();
            KillTimer(m_nDragSelTimer);
            m_bDragSelection = false;
        }

        if (m_bPreparingToDrag)
        {
            m_bPreparingToDrag = false;
            _doc.m_ptCursorPos = client_to_text(point);
            ensure_visible(_doc.m_ptCursorPos);
            _doc.select(text_selection(_doc.m_ptCursorPos, _doc.m_ptCursorPos));
        }
    }

    void OnLButtonDblClk(const CPoint &point, UINT nFlags)
    {
        if (!m_bDragSelection)
        {
            _doc.m_ptAnchor = _doc.m_ptCursorPos = client_to_text(point);
            auto selection = _doc.word_selection();

            _doc.m_ptCursorPos = selection._end;
            ensure_visible(_doc.m_ptCursorPos);
            _doc.select(selection);

            SetCapture();
            m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
            assert(m_nDragSelTimer != 0);
            m_bWordSelection = true;
            m_bLineSelection = false;
            m_bDragSelection = true;
        }
    }


    void OnRButtonDown(const CPoint &point, UINT nFlags)
    {
        auto pt = client_to_text(point);

        if (!_doc.is_inside_selection(pt))
        {
            //m_ptAnchor = m_ptCursorPos = pt;
            _doc.select(text_selection(pt, pt));
            ensure_visible(pt);
        }
    }

    void ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar = true)
    {
        if (_char_offset.cx != nNewOffsetChar)
        {
            int nScrollChars = _char_offset.cx - nNewOffsetChar;
            _char_offset.cx = nNewOffsetChar;
            auto rcScroll = client_rect();
            rcScroll.left += _doc.margin_width();
            ScrollWindowEx(0, nScrollChars * _font_extent.cx, rcScroll, rcScroll, nullptr, nullptr, SW_INVALIDATE);

            if (bTrackScrollBar)
                RecalcHorzScrollBar(true);
        }
    }

    void ScrollToLine(int nNewTopLine, bool bTrackScrollBar = true)
    {
        if (_char_offset.cy != nNewTopLine)
        {
            int nScrollLines = _char_offset.cy - nNewTopLine;
            _char_offset.cy = nNewTopLine;
            ScrollWindowEx(0, nScrollLines * _font_extent.cy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
            UpdateWindow();
            if (bTrackScrollBar)
                RecalcVertScrollBar(true);
        }
    }

    void RecalcVertScrollBar(bool bPositionOnly = false)
    {
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        if (bPositionOnly)
        {
            si.fMask = SIF_POS;
            si.nPos = _char_offset.cy;
        }
        else
        {
            if (m_nScreenLines >= _doc.size() && _char_offset.cy > 0)
            {
                _char_offset.cy = 0;
                Invalidate();
                update_caret();
            }
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            si.nMin = 0;
            si.nMax = _doc.size() - 1;
            si.nPage = m_nScreenLines;
            si.nPos = _char_offset.cy;
        }
        SetScrollInfo(SB_VERT, &si);
    }

    void OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
    {

        //	Note we cannot use nPos because of its 16-bit nature
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(SB_VERT, &si);

        int nPageLines = m_nScreenLines;
        int line_count = _doc.size();

        int nNewTopLine;
        switch (nSBCode)
        {
        case SB_TOP:
            nNewTopLine = 0;
            break;
        case SB_BOTTOM:
            nNewTopLine = line_count - nPageLines + 1;
            break;
        case SB_LINEUP:
            nNewTopLine = _char_offset.cy - 1;
            break;
        case SB_LINEDOWN:
            nNewTopLine = _char_offset.cy + 1;
            break;
        case SB_PAGEUP:
            nNewTopLine = _char_offset.cy - si.nPage + 1;
            break;
        case SB_PAGEDOWN:
            nNewTopLine = _char_offset.cy + si.nPage - 1;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            nNewTopLine = si.nTrackPos;
            break;
        default:
            return;
        }

        if (nNewTopLine < 0)
            nNewTopLine = 0;
        if (nNewTopLine >= line_count)
            nNewTopLine = line_count - 1;

        ScrollToLine(nNewTopLine, true);
    }

    void RecalcHorzScrollBar(bool bPositionOnly = false)
    {
        //	Again, we cannot use nPos because it's 16-bit
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        if (bPositionOnly)
        {
            si.fMask = SIF_POS;
            si.nPos = _char_offset.cx;
        }
        else
        {
            if (m_nScreenChars >= _doc.max_line_length() && _char_offset.cx > 0)
            {
                _char_offset.cx = 0;
                Invalidate();
                update_caret();
            }
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            si.nMin = 0;
            si.nMax = _doc.max_line_length() - 1;
            si.nPage = m_nScreenChars;
            si.nPos = _char_offset.cx;
        }
        SetScrollInfo(SB_HORZ, &si);
    }

    void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
    {

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(SB_HORZ, &si);

        int nPageChars = m_nScreenChars;
        int nMaxLineLength = _doc.max_line_length();

        int nNewOffset;
        switch (nSBCode)
        {
        case SB_LEFT:
            nNewOffset = 0;
            break;
        case SB_BOTTOM:
            nNewOffset = nMaxLineLength - nPageChars + 1;
            break;
        case SB_LINEUP:
            nNewOffset = _char_offset.cx - 1;
            break;
        case SB_LINEDOWN:
            nNewOffset = _char_offset.cx + 1;
            break;
        case SB_PAGEUP:
            nNewOffset = _char_offset.cx - si.nPage + 1;
            break;
        case SB_PAGEDOWN:
            nNewOffset = _char_offset.cx + si.nPage - 1;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            nNewOffset = si.nTrackPos;
            break;
        default:
            return;
        }

        if (nNewOffset >= nMaxLineLength)
            nNewOffset = nMaxLineLength - 1;
        if (nNewOffset < 0)
            nNewOffset = 0;

        ScrollToChar(nNewOffset, true);
        update_caret();
    }

    bool OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
    {
        static auto arrow = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW));
        static auto beam = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM));

        if (nHitTest == HTCLIENT)
        {
            CPoint pt;
            GetCursorPos(&pt);
            ScreenToClient(&pt);

            if (pt.x < _doc.margin_width())
            {
                ::SetCursor(arrow);
            }
            else
            {
                if (_doc.is_inside_selection(client_to_text(pt)))
                {
                    ::SetCursor(arrow);
                }
                else
                {
                    ::SetCursor(beam);
                }
            }
            return true;
        }
        return false;
    }

    void update_caret()
    {
        if (_doc.m_bFocused && !m_bCursorHidden &&
            _doc.CalculateActualOffset(_doc.m_ptCursorPos.y, _doc.m_ptCursorPos.x) >= _char_offset.cx)
        {
            CreateSolidCaret(2, _font_extent.cy);

            auto pt = text_to_client(_doc.m_ptCursorPos);
            SetCaretPos(pt.x, pt.y);
            ShowCaret();
        }
        else
        {
            HideCaret();
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID * ppvObj)
    {
        // Always set out parameter to NULL, validating it first.
        if (!ppvObj)
            return E_INVALIDARG;

        *ppvObj = NULL;

        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            // Increment the reference count and return the pointer.
            *ppvObj = (LPVOID)this;
            AddRef();
            return NOERROR;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        InterlockedIncrement(&m_cRef);
        return m_cRef;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        return InterlockedDecrement(&m_cRef);
    }


    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
    {
        CPoint point = pt;

        if (pDataObj->QueryGetData(&plainTextFormat) != S_OK &&
            pDataObj->QueryGetData(&plainTextWFormat) != S_OK)
        {
            HideDropIndicator();

            *pdwEffect = DROPEFFECT_NONE;
        }
        else
        {
            ShowDropIndicator(point);

            *pdwEffect = (GetKeyState(VK_CONTROL) < 0) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave(void)
    {
        HideDropIndicator();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
    {
        ///*
        //	if (! pDataObject->IsDataAvailable(CF_UNICODETEXT))
        //	{
        //		m_pOwner->HideDropIndicator();
        //		return DROPEFFECT_NONE;
        //	}
        //*/
        //	//
        //	
        //	//
        //	bool bDataSupported = false;
        //
        //	if ((!m_pOwner) ||									// If No Owner 
        //			(!( m_pOwner->QueryEditable())) ||			// Or Not Editable
        //			(m_pOwner->GetDisableDragAndDrop()))		// Or Drag And Drop Disabled
        //	{
        //		m_pOwner -> HideDropIndicator();					// Hide Drop Caret
        //		return DROPEFFECT_NONE;							    // Return DE_NONE
        //	}
        ////	if ((pDataObject->IsDataAvailable( CF_UNICODETEXT ) ) ||	    // If Text Available
        ////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
        ////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
        //	if (pDataObject->IsDataAvailable(CF_UNICODETEXT))		  	    // If Text Available
        //	{
        //		bDataSupported = true;								// Set Flag
        //	}
        //	if (!bDataSupported)									// If No Supported Formats Available
        //	{
        //		m_pOwner->HideDropIndicator();					    // Hide Drop Caret
        //		return DROPEFFECT_NONE;						   	    // Return DE_NONE
        //	}
        //	m_pOwner->ShowDropIndicator(point);
        //	if (dwKeyState & MK_CONTROL)
        //		return DROPEFFECT_COPY;
        return DROPEFFECT_MOVE;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
    {
        //	//
        //	 			( m_pOwner -> GetDisableDragAndDrop() ) )		// Or Drag And Drop Disabled
        //	//
        //	bool bDataSupported = false;
        //
        //	m_pOwner->HideDropIndicator();						// Hide Drop Caret
        //	if ((!m_pOwner) ||									// If No Owner 
        //			(!( m_pOwner->QueryEditable())) ||			// Or Not Editable
        //			(m_pOwner->GetDisableDragAndDrop()))		// Or Drag And Drop Disabled
        //	{
        //		return DROPEFFECT_NONE;							// Return DE_NONE
        //	}
        ////	if( ( pDataObject -> IsDataAvailable( CF_UNICODETEXT ) ) ||	// If Text Available
        ////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
        ////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
        //	if (pDataObject->IsDataAvailable(CF_UNICODETEXT))			    // If Text Available
        //	{
        //		bDataSupported = true;								// Set Flag
        //	}
        //	if (!bDataSupported)									// If No Supported Formats Available
        //	{
        //		return DROPEFFECT_NONE;							    // Return DE_NONE
        //	}
        //	return (m_pOwner->DoDropText(pDataObject, point));	    // Return Result Of Drop
        return S_OK;
    }

    DROPEFFECT OnDragScroll(CWindow wnd, DWORD dwKeyState, CPoint point)
    {
        assert(m_hWnd == wnd.m_hWnd);

        auto rcClientRect = client_rect();

        if (point.y < rcClientRect.top + DRAG_BORDER_Y)
        {
            HideDropIndicator();
            ScrollUp();
            ShowDropIndicator(point);
        }
        else if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
        {
            HideDropIndicator();
            ScrollDown();
            ShowDropIndicator(point);
        }
        else if (point.x < rcClientRect.left + _doc.margin_width() + DRAG_BORDER_X)
        {
            HideDropIndicator();
            ScrollLeft();
            ShowDropIndicator(point);
        }
        else if (point.x >= rcClientRect.right - DRAG_BORDER_X)
        {
            HideDropIndicator();
            ScrollRight();
            ShowDropIndicator(point);
        }

        if (dwKeyState & MK_CONTROL)
            return DROPEFFECT_COPY;
        return DROPEFFECT_MOVE;
    }

    void ScrollUp()
    {
        if (_char_offset.cy > 0)
        {
            ScrollToLine(_char_offset.cy - 1);
        }
    }

    void ScrollDown()
    {
        if (_char_offset.cy < _doc.size() - 1)
        {
            ScrollToLine(_char_offset.cy + 1);
        }
    }

    void ScrollLeft()
    {
        if (_char_offset.cx > 0)
        {
            ScrollToChar(_char_offset.cx - 1);
            update_caret();
        }
    }

    void ScrollRight()
    {
        if (_char_offset.cx < _doc.max_line_length() - 1)
        {
            ScrollToChar(_char_offset.cx + 1);
            update_caret();
        }
    }

    bool DoDropText(IDataObject *pDataObject, const CPoint &ptClient)
    {
        STGMEDIUM store;
        std::wstring text;
        bool success = false;


        if (SUCCEEDED(pDataObject->GetData(&plainTextWFormat, &store))) {
            //unicode text
            auto data = (const wchar_t*) GlobalLock(store.hGlobal);
            text = data;
            GlobalUnlock(store.hGlobal);
            ReleaseStgMedium(&store);
            success = true;
        }
        else if (SUCCEEDED(pDataObject->GetData(&plainTextFormat, &store))) {
            //ascii text
            auto data = (const char*) GlobalLock(store.hGlobal);
            text = AsciiToUtf16(data);
            GlobalUnlock(store.hGlobal);
            ReleaseStgMedium(&store);
            success = true;
        }

        /*text_location ptDropPos = client_to_text(ptClient);

        if (IsDraggingText() && is_inside_selection(ptDropPos))
        {
        SetAnchor(ptDropPos);
        select(ptDropPos, ptDropPos);
        SetCursorPos(ptDropPos);
        ensure_visible(ptDropPos);
        return false;
        }

        int x, y;
        _doc.insert_text(this, ptDropPos.y, ptDropPos.x, A2T(pszText), y, x, CE_ACTION_DRAGDROP);
        text_location ptCurPos(x, y);
        SetAnchor(ptDropPos);
        select(ptDropPos, ptCurPos);
        SetCursorPos(ptCurPos);
        ensure_visible(ptCurPos);*/

        //::GlobalUnlock(hData);
        return true;
    }

    void ShowDropIndicator(const CPoint &point)
    {
        if (!m_bDropPosVisible)
        {
            HideCursor();
            m_ptSavedCaretPos = _doc.cursor_pos();
            m_bDropPosVisible = true;
            ::CreateCaret(m_hWnd, (HBITMAP) 1, 2, _font_extent.cy);
        }
        m_ptDropPos = client_to_text(point);
        if (m_ptDropPos.x >= _char_offset.cx)
        {
            auto pt = text_to_client(m_ptDropPos);
            SetCaretPos(pt.x, pt.y);
            ShowCaret();
        }
        else
        {
            HideCaret();
        }
    }

    void HideDropIndicator()
    {
        if (m_bDropPosVisible)
        {
            _doc.SetCursorPos(m_ptSavedCaretPos);
            ShowCursor();
            m_bDropPosVisible = false;
        }
    }

    void ShowCursor()
    {
        m_bCursorHidden = false;
        update_caret();
    }

    void HideCursor()
    {
        m_bCursorHidden = true;
        update_caret();
    }

    std::wstring text_from_clipboard() const
    {
        std::wstring result;
        auto pThis = const_cast<text_view*>(this);

        if (pThis->OpenClipboard())
        {
            auto hData = GetClipboardData(CF_UNICODETEXT);

            if (hData != nullptr)
            {
                auto pszData = (const wchar_t *) GlobalLock(hData);

                if (pszData != nullptr)
                {
                    result = pszData;
                    GlobalUnlock(hData);
                }
            }

            CloseClipboard();
        }

        return result;
    }

    bool text_to_clipboard(const std::wstring &text)
    {
        // TODO CWaitCursor wc;
        auto success = false;

        if (OpenClipboard())
        {
            EmptyClipboard();

            auto len = text.size() + 1;
            auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

            if (hData != nullptr)
            {
                auto pszData = (wchar_t*) ::GlobalLock(hData);
                wcscpy_s(pszData, len, text.c_str());
                GlobalUnlock(hData);
                success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
            }
            CloseClipboard();
        }

        return success;
    }

    void invalidate(LPCRECT r = nullptr)
    {
        InvalidateRect(r, FALSE);
    }

    HGLOBAL PrepareDragData()
    {
        auto sel = _doc.selection();

        if (sel.empty())
            return nullptr;

        auto text = Combine(_doc.text(sel));
        auto len = text.size() + 1;
        auto hData = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

        if (hData == nullptr)
            return nullptr;

        auto pszData = (wchar_t*) ::GlobalLock(hData);
        wcscpy_s(pszData, len, text.c_str());
        ::GlobalUnlock(hData);

        m_ptDraggedText = sel;

        return hData;
    }



    int client_to_line(const CPoint &point) const
    {
        auto line_count = _doc.size();
        auto y = _char_offset.cy;

        while (y < line_count)
        {
            auto const &line = _doc[y];

            if (point.y >= line._y && point.y < (line._y + line._cy))
            {
                return y;
            }

            y += 1;
        }

        return -1;
    }

    text_location client_to_text(const CPoint &point) const
    {
        auto line_count = _doc.size();

        text_location pt;
        pt.y = client_to_line(point);

        if (pt.y >= 0 && pt.y < line_count)
        {
            const auto &line = _doc[pt.y];

            int nPos = _char_offset.cx + (point.x - _doc.margin_width()) / _font_extent.cx;
            if (nPos < 0)
                nPos = 0;

            int i = 0, nCurPos = 0;
            int tabSize = _doc.tab_size();

            while (i < line.size())
            {
                if (line[i] == _T('\t'))
                    nCurPos += (tabSize - nCurPos % tabSize);
                else
                    nCurPos++;

                if (nCurPos > nPos)
                    break;

                i++;
            }

            pt.x = i;
        }

        return pt;
    }

    CPoint text_to_client(const text_location &point) const
    {
        CPoint pt;

        if (point.y >= 0 && point.y < _doc.size())
        {
            pt.y = line_offset(point.y) - top_offset();
            pt.x = 0;

            auto tabSize = _doc.tab_size();
            const auto &line = _doc[point.y];

            for (int i = 0; i < point.x; i++)
            {
                if (line[i] == _T('\t'))
                {
                    pt.x += (tabSize - pt.x % tabSize);
                }
                else
                {
                    pt.x++;
                }
            }

            pt.x = (pt.x - _char_offset.cx) * _font_extent.cx + _doc.margin_width();
        }

        return pt;
    }

    int top_offset() const
    {
        auto result = 0;

        if (!_doc.empty() || _char_offset.cy <= 0)
        {
            result = _doc[_char_offset.cy]._y;
        }

        return result;
    }

    void invalidate_lines(int nLine1, int nLine2, bool bInvalidateMargin = false)
    {
        bInvalidateMargin = true;

        auto rcInvalid = client_rect();

        if (nLine2 == -1)
        {
            if (!bInvalidateMargin)
                rcInvalid.left += _doc.margin_width();

            rcInvalid.top = line_offset(nLine1) - top_offset();
        }
        else
        {
            if (nLine2 < nLine1)
            {
                std::swap(nLine1, nLine2);
            }

            if (!bInvalidateMargin)
                rcInvalid.left += _doc.margin_width();

            rcInvalid.top = line_offset(nLine1) - top_offset();
            rcInvalid.bottom = line_offset(nLine2) + line_height(nLine2) - top_offset();
        }

        invalidate(rcInvalid);
    }

    void invalidate_line(int index)
    {
        auto &line = _doc[index];
        line._expanded_length = -1;
        line._parseCookie = -1;

        invalidate_lines(index, index + 1, true);

        _doc.update_max_line_length(index);

        RecalcHorzScrollBar();
    }

    void invalidate_view()
    {
        m_nScreenChars = -1;

        layout();

        update_caret();
        RecalcVertScrollBar(false);
        RecalcHorzScrollBar(false);
        invalidate();
    }

    void layout()
    {
        auto rect = client_rect();
        auto line_count = _doc.size();
        auto y = 0;
        auto cy = _font_extent.cy;

        for (int i = 0; i < line_count; i++)
        {
            auto &line = _doc[i];

            line._y = y;
            line._cy = cy;

            y += cy;
        }

        m_nScreenLines = rect.Height() / _font_extent.cy;

        RecalcVertScrollBar();
        RecalcHorzScrollBar();
    }

    int line_offset(int lineIndex) const
    {
        auto max = _doc.size();
        auto line = Clamp(lineIndex, 0, max - 1);
        return _doc[line]._y;
    }

    int line_height(int lineIndex) const
    {
        return _font_extent.cy;
    }

    void ensure_visible(const text_location &pt)
    {
        //	Scroll vertically
        int line_count = _doc.size();
        int nNewTopLine = _char_offset.cy;

        if (pt.y >= nNewTopLine + m_nScreenLines)
        {
            nNewTopLine = pt.y - m_nScreenLines + 1;
        }
        else if (pt.y < nNewTopLine)
        {
            nNewTopLine = pt.y;
        }

        if (nNewTopLine < 0)
            nNewTopLine = 0;
        if (nNewTopLine >= line_count)
            nNewTopLine = line_count - 1;

        if (_char_offset.cy != nNewTopLine)
        {
            ScrollToLine(nNewTopLine);
        }

        //	Scroll horizontally
        int nActualPos = _doc.CalculateActualOffset(pt.y, pt.x);
        int nNewOffset = _char_offset.cx;
        if (nActualPos > nNewOffset + m_nScreenChars)
        {
            nNewOffset = nActualPos - m_nScreenChars;
        }
        if (nActualPos < nNewOffset)
        {
            nNewOffset = nActualPos;
        }

        if (nNewOffset >= _doc.max_line_length())
            nNewOffset = _doc.max_line_length() - 1;
        if (nNewOffset < 0)
            nNewOffset = 0;

        if (_char_offset.cx != nNewOffset)
        {
            ScrollToChar(nNewOffset);
        }

        update_caret();
    }

    CRect client_rect() const { return CRect(0, 0, _extent.cx, _extent.cy); }

    void line_color(int lineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace) const
    {
        bDrawWhitespace = true;
        crText = RGB(255, 255, 255);
        crBkgnd = CLR_NONE;
        crText = CLR_NONE;
        bDrawWhitespace = false;
    }

    void draw_line(HDC pdc, text_location &ptOrigin, const CRect &rcClip, const wchar_t * pszChars, int nOffset, int nCount) const
    {
        if (nCount > 0)
        {
            auto line = _doc.ExpandChars(pszChars, nOffset, nCount);
            auto nWidth = rcClip.right - ptOrigin.x;

            if (nWidth > 0)
            {
                auto nCharWidth = _font_extent.cx;
                auto nCount = line.size();
                auto nCountFit = nWidth / nCharWidth + 1;

                if (nCount > nCountFit)
                    nCount = nCountFit;

                /*
                CRect rcBounds = rcClip;
                rcBounds.left = ptOrigin.x;
                rcBounds.right = rcBounds.left + _font_extent.cx * nCount;
                pdc->ExtTextOut(rcBounds.left, rcBounds.top, ETO_OPAQUE, &rcBounds, nullptr, 0, nullptr);
                */
                ::ExtTextOut(pdc, ptOrigin.x, ptOrigin.y, ETO_CLIPPED, &rcClip, line.c_str(), nCount, nullptr);
            }

            ptOrigin.x += _font_extent.cx * line.size();
        }
    }

    void draw_line(HDC pdc, text_location &ptOrigin, const CRect &rcClip, int nColorIndex, const wchar_t * pszChars, int nOffset, int nCount, text_location ptTextPos) const
    {
        if (nCount > 0)
        {
            if (_doc.m_bFocused || _doc.m_bShowInactiveSelection)
            {
                auto sel = _doc.selection();
                int nSelBegin = 0, nSelEnd = 0;

                if (sel._start.y > ptTextPos.y)
                {
                    nSelBegin = nCount;
                }
                else if (sel._start.y == ptTextPos.y)
                {
                    nSelBegin = Clamp(sel._start.x - ptTextPos.x, 0, nCount);
                }
                if (sel._end.y > ptTextPos.y)
                {
                    nSelEnd = nCount;
                }
                else if (sel._end.y == ptTextPos.y)
                {
                    nSelEnd = Clamp(sel._end.x - ptTextPos.x, 0, nCount);
                }

                assert(nSelBegin >= 0 && nSelBegin <= nCount);
                assert(nSelEnd >= 0 && nSelEnd <= nCount);
                assert(nSelBegin <= nSelEnd);

                //	Draw part of the text before selection
                if (nSelBegin > 0)
                {
                    draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset, nSelBegin);
                }
                if (nSelBegin < nSelEnd)
                {
                    COLORREF crOldBk = SetBkColor(pdc, GetColor(IHighlight::COLORINDEX_SELBKGND));
                    COLORREF crOldText = SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_SELTEXT));
                    draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin);
                    SetBkColor(pdc, crOldBk);
                    SetTextColor(pdc, crOldText);
                }
                if (nSelEnd < nCount)
                {
                    draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd);
                }
            }
            else
            {
                draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset, nCount);
            }
        }
    }

    void draw_line(HDC hdc, const CRect &rc, int lineIndex) const
    {
        if (lineIndex == -1)
        {
            //	Draw line beyond the text
            FillSolidRect(hdc, rc, GetColor(IHighlight::COLORINDEX_WHITESPACE));
        }
        else
        {
            //	Acquire the background color for the current line
            auto bDrawWhitespace = false;
            COLORREF crBkgnd, crText;
            line_color(lineIndex, crBkgnd, crText, bDrawWhitespace);

            if (crBkgnd == CLR_NONE)
                crBkgnd = GetColor(IHighlight::COLORINDEX_BKGND);

            const auto &line = _doc[lineIndex];

            if (line.empty())
            {
                //	Draw the empty line
                CRect rect = rc;
                if ((_doc.m_bFocused || _doc.m_bShowInactiveSelection) && _doc.is_inside_selection(text_location(0, lineIndex)))
                {
                    FillSolidRect(hdc, rect.left, rect.top, _font_extent.cx, rect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                    rect.left += _font_extent.cx;
                }
                FillSolidRect(hdc, rect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
                return;
            }

            //	Parse the line
            auto nLength = line.size();
            auto pBuf = (IHighlight::TEXTBLOCK *) _malloca(sizeof(IHighlight::TEXTBLOCK) * nLength * 3);
            auto nBlocks = 0;
            auto cookie = _doc.prse_cookie(lineIndex - 1);

            line._parseCookie = _doc._highlight->ParseLine(cookie, line, pBuf, nBlocks);

            //	Draw the line text
            text_location origin(rc.left - _char_offset.cx * _font_extent.cx, rc.top);
            SetBkColor(hdc, crBkgnd);
            if (crText != CLR_NONE)
                SetTextColor(hdc, crText);

            auto bColorSet = false;
            auto pszChars = line.c_str();

            if (nBlocks > 0)
            {
                assert(pBuf[0].m_nCharPos >= 0 && pBuf[0].m_nCharPos <= nLength);
                if (crText == CLR_NONE)
                    SetTextColor(hdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
                draw_line(hdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0].m_nCharPos, text_location(0, lineIndex));

                for (int i = 0; i < nBlocks - 1; i++)
                {
                    assert(pBuf[i].m_nCharPos >= 0 && pBuf[i].m_nCharPos <= nLength);
                    if (crText == CLR_NONE)
                        SetTextColor(hdc, GetColor(pBuf[i].m_nColorIndex));
                    draw_line(hdc, origin, rc, pBuf[i].m_nColorIndex, pszChars,
                        pBuf[i].m_nCharPos, pBuf[i + 1].m_nCharPos - pBuf[i].m_nCharPos,
                        text_location(pBuf[i].m_nCharPos, lineIndex));
                }
                assert(pBuf[nBlocks - 1].m_nCharPos >= 0 && pBuf[nBlocks - 1].m_nCharPos <= nLength);
                if (crText == CLR_NONE)
                    SetTextColor(hdc, GetColor(pBuf[nBlocks - 1].m_nColorIndex));
                draw_line(hdc, origin, rc, pBuf[nBlocks - 1].m_nColorIndex, pszChars,
                    pBuf[nBlocks - 1].m_nCharPos, nLength - pBuf[nBlocks - 1].m_nCharPos,
                    text_location(pBuf[nBlocks - 1].m_nCharPos, lineIndex));
            }
            else
            {
                if (crText == CLR_NONE)
                    SetTextColor(hdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
                draw_line(hdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, nLength, text_location(0, lineIndex));
            }

            //	Draw whitespaces to the left of the text
            auto frect = rc;

            if (origin.x > frect.left)
                frect.left = origin.x;

            if (frect.right > frect.left)
            {
                if ((_doc.m_bFocused || _doc.m_bShowInactiveSelection) && _doc.is_inside_selection(text_location(nLength, lineIndex)))
                {
                    FillSolidRect(hdc, frect.left, frect.top, _font_extent.cx, frect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                    frect.left += _font_extent.cx;
                }
                if (frect.right > frect.left)
                    FillSolidRect(hdc, frect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
            }

            _freea(pBuf);
        }
    }

    COLORREF GetColor(int nColorIndex) const
    {
        switch (nColorIndex)
        {
        case IHighlight::COLORINDEX_WHITESPACE:
        case IHighlight::COLORINDEX_BKGND:
            return RGB(30, 30, 30);
        case IHighlight::COLORINDEX_NORMALTEXT:
            return RGB(240, 240, 240);
        case IHighlight::COLORINDEX_SELMARGIN:
            return RGB(44, 44, 44);
        case IHighlight::COLORINDEX_PREPROCESSOR:
            return RGB(128, 128, 192);
        case IHighlight::COLORINDEX_COMMENT:
            return RGB(128, 128, 128);
        case IHighlight::COLORINDEX_NUMBER:
            return RGB(255, 128, 128);
        case IHighlight::COLORINDEX_OPERATOR:
            return RGB(128, 255, 128);
        case IHighlight::COLORINDEX_KEYWORD:
            return RGB(128, 128, 255);
        case IHighlight::COLORINDEX_SELBKGND:
            return RGB(88, 88, 88);
        case IHighlight::COLORINDEX_SELTEXT:
            return RGB(255, 255, 255);
        }
        return RGB(255, 0, 0);
    }

    //COLORREF document::GetColor(int nColorIndex)
    //{
    //	switch (nColorIndex)
    //	{
    //	case IHighlight::COLORINDEX_WHITESPACE:
    //	case IHighlight::COLORINDEX_BKGND:
    //		return ::GetSysColor(COLOR_WINDOW);
    //	case IHighlight::COLORINDEX_NORMALTEXT:
    //		return ::GetSysColor(COLOR_WINDOWTEXT);
    //	case IHighlight::COLORINDEX_SELMARGIN:
    //		return ::GetSysColor(COLOR_SCROLLBAR);
    //	case IHighlight::COLORINDEX_PREPROCESSOR:
    //		return RGB(0, 128, 192);
    //	case IHighlight::COLORINDEX_COMMENT:
    //		return RGB(128, 128, 128);
    //		//	[JRT]: Enabled Support For Numbers...
    //	case IHighlight::COLORINDEX_NUMBER:
    //		return RGB(0x80, 0x00, 0x00);
    //	case IHighlight::COLORINDEX_OPERATOR:
    //		return RGB(0x00, 0x00, 0x00);
    //	case IHighlight::COLORINDEX_KEYWORD:
    //		return RGB(0, 0, 255);
    //	case IHighlight::COLORINDEX_SELBKGND:
    //		return RGB(0, 0, 0);
    //	case IHighlight::COLORINDEX_SELTEXT:
    //		return RGB(255, 255, 255);
    //	}
    //	return RGB(255, 0, 0);
    //}


    void draw_margin(HDC hdc, const CRect &rect, int lineIndex) const
    {
        FillSolidRect(hdc, rect, GetColor(_doc.m_bSelMargin ? IHighlight::COLORINDEX_SELMARGIN : IHighlight::COLORINDEX_BKGND));
    }

public:

    void draw(HDC hdc)
    {
        auto oldFont = SelectObject(hdc, _font);
        auto rcClient = client_rect();
        auto line_count = _doc.size();
        auto y = 0;
        auto nCurrentLine = _char_offset.cy;

        while (y < rcClient.bottom)
        {
            auto nLineHeight = line_height(nCurrentLine);
            auto rcLine = rcClient;
            rcLine.bottom = rcLine.top + nLineHeight;

            CRect rcCacheMargin(0, y, _doc.margin_width(), y + nLineHeight);
            CRect rcCacheLine(_doc.margin_width(), y, rcLine.Width(), y + nLineHeight);

            if (nCurrentLine < line_count)
            {
                draw_margin(hdc, rcCacheMargin, nCurrentLine);
                draw_line(hdc, rcCacheLine, nCurrentLine);
            }
            else
            {
                draw_margin(hdc, rcCacheMargin, -1);
                draw_line(hdc, rcCacheLine, -1);
            }

            nCurrentLine++;
            y += nLineHeight;
        }

        SelectObject(hdc, oldFont);
    }

    void move_page(bool down, bool selecting)
    {
        int nNewTopLine = Clamp(down ? _char_offset.cy + m_nScreenLines - 1 : _char_offset.cy - m_nScreenLines + 1, 0, _doc.size() - 1);

        if (_char_offset.cy != nNewTopLine)
        {
            ScrollToLine(nNewTopLine);
        }

        _doc.m_ptCursorPos.y = Clamp(down ? _doc.m_ptCursorPos.y + (m_nScreenLines - 1) : _doc.m_ptCursorPos.y - (m_nScreenLines - 1), 0, _doc.size() - 1);
        
        auto size = _doc[_doc.m_ptCursorPos.y].size();

        if (_doc.m_ptCursorPos.x > size)
            _doc.m_ptCursorPos.x = size;

        _doc.m_nIdealCharPos = _doc.CalculateActualOffset(_doc.m_ptCursorPos.y, _doc.m_ptCursorPos.x);
        ensure_visible(_doc.m_ptCursorPos);	//todo: no vertical scroll

        if (!selecting)
            _doc.m_ptAnchor = _doc.m_ptCursorPos;

        _doc.select(text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos));
    }
};


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
    UINT m_nDragSelTimer = 0; 

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
        _doc.OnCreate();
        //RegisterDragDrop(m_hWnd, this);
        return 0;
    }

    LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        //RevokeDragDrop(m_hWnd);
        _doc.OnDestroy();
        return 0;
    }

    LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        win_dc dc(m_hWnd);
        auto pOldFont = dc.SelectFont(_doc.GetFont());

        CSize font_extent;
        GetTextExtentExPoint(dc, _T("X"), 1, 1, nullptr, nullptr, &font_extent);
        if (font_extent.cy < 1) font_extent.cy = 1;

        /*
        TEXTMETRIC tm;
        if (pdc->GetTextMetrics(&tm))
        m_nCharWidth -= tm.tmOverhang;
        */

        _doc.layout(CSize(LOWORD(lParam), HIWORD(lParam)), font_extent);

        dc.SelectFont(pOldFont);


        return 0;
    }

    LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        PAINTSTRUCT ps = { 0 };
        auto hdc = BeginPaint(&ps);
        _doc.draw(hdc);
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
        _doc.OnChar(wParam, 0xFF & lParam, lParam);
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
            
            auto selection = _doc.select_word(clientLocation);
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
        case ID_EDIT_SCROLL_UP: _doc.ScrollUp(); break;
        case ID_EDIT_SCROLL_DOWN: _doc.ScrollDown(); break;
        case ID_EDIT_PAGE_UP: _doc.MovePgUp(false); break;
        case ID_EDIT_EXT_PAGE_UP: _doc.MovePgDn(true); break;
        case ID_EDIT_PAGE_DOWN: _doc.MovePgDn(false); break;
        case ID_EDIT_EXT_PAGE_DOWN: _doc.MovePgDn(true); break;
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
        if (_doc._selection._start != _doc._selection._end)
            _doc.invalidate_lines(_doc._selection._start.y, _doc._selection._end.y);
        update_caret();
    }

    void OnKillFocus(CWindow newWnd)
    {
        _doc.m_bFocused = false;
        update_caret();
        if (_doc._selection._start != _doc._selection._end)
            _doc.invalidate_lines(_doc._selection._start.y, _doc._selection._end.y);
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

            auto rcClient = _doc.client_rect();
            auto bChanged = false;
            auto nNewTopLine = _doc._char_offset.cy;
            auto line_count = _doc.size();

            if (pt.y < rcClient.top)
            {
                nNewTopLine--;

                if (pt.y < rcClient.top - _doc._font_extent.cy)
                    nNewTopLine -= 2;
            }
            else if (pt.y >= rcClient.bottom)
            {
                nNewTopLine++;

                if (pt.y >= rcClient.bottom + _doc._font_extent.cy)
                    nNewTopLine += 2;
            }

            nNewTopLine = Clamp(nNewTopLine, 0, line_count - 1);

            if (_doc._char_offset.cy != nNewTopLine)
            {
                ScrollToLine(nNewTopLine, true);
                bChanged = true;
            }

            //	Scroll horizontally, if necessary
            auto nNewOffsetChar = _doc._char_offset.cx;
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

            if (_doc._char_offset.cx != nNewOffsetChar)
            {
                ScrollToChar(nNewOffsetChar, true);
                update_caret();
                bChanged = true;
            }

            //	Fix changes
            if (bChanged)
            {
                text_location ptNewCursorPos = _doc.client_to_text(pt);

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
                _doc.m_ptCursorPos = _doc.client_to_text(point);
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
                update_caret();
                _doc.EnsureVisible(_doc.m_ptCursorPos);
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
            auto ptText = _doc.client_to_text(point);
            _doc.PrepareSelBounds();

            if ((_doc.IsInsideSelBlock(ptText)) &&				// If Inside Selection Area
                (!_doc.m_bDisableDragAndDrop))				// And D&D Not Disabled
            {
                m_bPreparingToDrag = true;
            }
            else
            {
                _doc.m_ptCursorPos = _doc.client_to_text(point);
                if (!bShift)
                    _doc.m_ptAnchor = _doc.m_ptCursorPos;

                auto selection = bControl ? _doc.word_selection() : text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos);

                _doc.m_ptCursorPos = selection._end;
                update_caret();
                _doc.EnsureVisible(_doc.m_ptCursorPos);
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
        ScrollToLine(Clamp(_doc._char_offset.cy + zDelta, 0, _doc.size()), true);
    }

    void OnMouseMove(const CPoint &point, UINT nFlags)
    {
        if (m_bDragSelection)
        {
            bool bOnMargin = point.x < _doc.margin_width();
            text_location ptNewCursorPos = _doc.client_to_text(point);
            text_location ptStart, ptEnd;

            if (m_bLineSelection)
            {
                if (bOnMargin)
                {
                    if (ptNewCursorPos.y < _doc.m_ptAnchor.y ||
                        ptNewCursorPos.y == _doc.m_ptAnchor.y && ptNewCursorPos.x < _doc.m_ptAnchor.x)
                    {
                        ptEnd = _doc.m_ptAnchor;
                        if (ptEnd.y == _doc._lines.size() - 1)
                        {
                            ptEnd.x = _doc._lines[ptEnd.y].size();
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
            HGLOBAL hData = _doc.PrepareDragData();
            if (hData != nullptr)
            {
                //undo_group ug(*this);


                /*COleDataSource ds;
                ds.CacheGlobalData(CF_UNICODETEXT, hData);
                m_bDraggingText = true;
                DROPEFFECT de = ds.DoDragDrop(GetDropEffect());
                if (de != DROPEFFECT_NONE)
                OnDropSource(de);
                m_bDraggingText = false;

                if (_lines != nullptr)
                _lines.FlushUndoGroup(this);*/
            }
        }
    }

    void OnLButtonUp(const CPoint &point, UINT nFlags)
    {
        if (m_bDragSelection)
        {
            text_location ptNewCursorPos = _doc.client_to_text(point);

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

                _doc.EnsureVisible(_doc.m_ptCursorPos);
                update_caret();
                _doc.select(text_selection(ptNewCursorPos, ptEnd));
            }
            else
            {
                auto selection = m_bWordSelection ? _doc.word_selection() : text_selection(_doc.m_ptAnchor, _doc.m_ptCursorPos);
                _doc.m_ptCursorPos = selection._end;
                _doc.EnsureVisible(_doc.m_ptCursorPos);
                update_caret();
                _doc.select(selection);
            }

            ReleaseCapture();
            KillTimer(m_nDragSelTimer);
            m_bDragSelection = false;
        }

        if (m_bPreparingToDrag)
        {
            m_bPreparingToDrag = false;
            _doc.m_ptCursorPos = _doc.client_to_text(point);
            _doc.EnsureVisible(_doc.m_ptCursorPos);
            update_caret();
            _doc.select(text_selection(_doc.m_ptCursorPos, _doc.m_ptCursorPos));
        }
    }

    

    void OnLButtonDblClk(const CPoint &point, UINT nFlags)
    {
        if (!m_bDragSelection)
        {
            _doc.m_ptAnchor = _doc.m_ptCursorPos = _doc.client_to_text(point);

            auto selection = _doc.word_selection();

            _doc.m_ptCursorPos = selection._end;
            update_caret();
            _doc.EnsureVisible(_doc.m_ptCursorPos);
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
        auto pt = _doc.client_to_text(point);

        if (!_doc.IsInsideSelBlock(pt))
        {
            //m_ptAnchor = m_ptCursorPos = pt;
            _doc.select(text_selection(pt, pt));
            _doc.EnsureVisible(pt);
            update_caret();
        }
    }

    void ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar)
    {
        if (_doc._char_offset.cx != nNewOffsetChar)
        {
            int nScrollChars = _doc._char_offset.cx - nNewOffsetChar;
            _doc._char_offset.cx = nNewOffsetChar;
            auto rcScroll = _doc.client_rect();
            rcScroll.left += _doc.margin_width();
            ScrollWindowEx(0, nScrollChars * _doc._font_extent.cx, rcScroll, rcScroll, nullptr, nullptr, SW_INVALIDATE);

            if (bTrackScrollBar)
                RecalcHorzScrollBar(true);
        }
    }

    void ScrollToLine(int nNewTopLine, bool bTrackScrollBar)
    {
        if (_doc._char_offset.cy != nNewTopLine)
        {
            int nScrollLines = _doc._char_offset.cy - nNewTopLine;
            _doc._char_offset.cy = nNewTopLine;
            ScrollWindowEx(0, nScrollLines * _doc._font_extent.cy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
            UpdateWindow();
            if (bTrackScrollBar)
                RecalcVertScrollBar(true);
        }
    }

    void RecalcVertScrollBar(bool bPositionOnly /*= false*/)
    {
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        if (bPositionOnly)
        {
            si.fMask = SIF_POS;
            si.nPos = _doc._char_offset.cy;
        }
        else
        {
            if (_doc.screen_lines() >= _doc.size() && _doc._char_offset.cy > 0)
            {
                _doc._char_offset.cy = 0;
                Invalidate();
                update_caret();
            }
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            si.nMin = 0;
            si.nMax = _doc.size() - 1;
            si.nPage = _doc.screen_lines();
            si.nPos = _doc._char_offset.cy;
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

        int nPageLines = _doc.screen_lines();
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
            nNewTopLine = _doc._char_offset.cy - 1;
            break;
        case SB_LINEDOWN:
            nNewTopLine = _doc._char_offset.cy + 1;
            break;
        case SB_PAGEUP:
            nNewTopLine = _doc._char_offset.cy - si.nPage + 1;
            break;
        case SB_PAGEDOWN:
            nNewTopLine = _doc._char_offset.cy + si.nPage - 1;
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

    void RecalcHorzScrollBar(bool bPositionOnly /*= false*/)
    {
        //	Again, we cannot use nPos because it's 16-bit
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        if (bPositionOnly)
        {
            si.fMask = SIF_POS;
            si.nPos = _doc._char_offset.cx;
        }
        else
        {
            if (_doc.screen_chars() >= _doc.max_line_length() && _doc._char_offset.cx > 0)
            {
                _doc._char_offset.cx = 0;
                Invalidate();
                update_caret();
            }
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            si.nMin = 0;
            si.nMax = _doc.max_line_length() - 1;
            si.nPage = _doc.screen_chars();
            si.nPos = _doc._char_offset.cx;
        }
        SetScrollInfo(SB_HORZ, &si);
    }

    void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
    {

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(SB_HORZ, &si);

        int nPageChars = _doc.screen_chars();
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
            nNewOffset = _doc._char_offset.cx - 1;
            break;
        case SB_LINEDOWN:
            nNewOffset = _doc._char_offset.cx + 1;
            break;
        case SB_PAGEUP:
            nNewOffset = _doc._char_offset.cx - si.nPage + 1;
            break;
        case SB_PAGEDOWN:
            nNewOffset = _doc._char_offset.cx + si.nPage - 1;
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
                auto ptText = _doc.client_to_text(pt);
                _doc.PrepareSelBounds();

                if (_doc.IsInsideSelBlock(ptText))
                {
                    if (!_doc.m_bDisableDragAndDrop)
                    {
                        ::SetCursor(arrow);
                    }
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
        if (_doc.m_bFocused && !_doc.m_bCursorHidden &&
            _doc.CalculateActualOffset(_doc.m_ptCursorPos.y, _doc.m_ptCursorPos.x) >= _doc._char_offset.cx)
        {
            CreateSolidCaret(2, _doc._font_extent.cy);

            auto pt = _doc.text_to_client(_doc.m_ptCursorPos);
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
            _doc.HideDropIndicator();

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
        _doc.HideDropIndicator();
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

        _doc.DoDragScroll(point);

        if (dwKeyState & MK_CONTROL)
            return DROPEFFECT_COPY;
        return DROPEFFECT_MOVE;
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

        if (IsDraggingText() && IsInsideSelection(ptDropPos))
        {
            SetAnchor(ptDropPos);
            select(ptDropPos, ptDropPos);
            SetCursorPos(ptDropPos);
            EnsureVisible(ptDropPos);
            return false;
        }

        int x, y;
        _lines.insert_text(this, ptDropPos.y, ptDropPos.x, A2T(pszText), y, x, CE_ACTION_DRAGDROP);
        text_location ptCurPos(x, y);
        SetAnchor(ptDropPos);
        select(ptDropPos, ptCurPos);
        SetCursorPos(ptCurPos);
        EnsureVisible(ptCurPos);*/

        //::GlobalUnlock(hData);
        return true;
    }

    void ShowDropIndicator(const CPoint &point)
    {
        if (!_doc.m_bDropPosVisible)
        {
            _doc.HideCursor();
            _doc.m_ptSavedCaretPos = _doc.cursor_pos();
            _doc.m_bDropPosVisible = true;
            ::CreateCaret(m_hWnd, (HBITMAP) 1, 2, _doc._font_extent.cy);
        }
        m_ptDropPos = _doc.client_to_text(point);
        if (m_ptDropPos.x >= _doc._char_offset.cx)
        {
            auto pt = _doc.text_to_client(m_ptDropPos);
            SetCaretPos(pt.x, pt.y);
            ShowCaret();
        }
        else
        {
            HideCaret();
        }
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

};


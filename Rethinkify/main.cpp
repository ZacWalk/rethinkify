// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "resource.h"
#include "text_view.h"
#include "document.h"
#include "json/json.h"
#include "ui.h"

#include <shellapi.h>

//
// Ideas 
//
// Support editing CSV tables
// open spreadsheets using http://libxls.sourceforge.net/ or http://www.codeproject.com/Articles/42504/ExcelFormat-Library


//#pragma comment(lib, "Comdlg32")
#pragma comment(lib, "Comctl32")

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


const wchar_t *g_szAppName = L"Rethinkify";

extern std::wstring RunTests();

class about_dlg : public CDialogImpl<about_dlg>
{
public:
	enum { IDD = IDD_ABOUTBOX };

	BEGIN_MSG_MAP(about_dlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CenterWindow();
		return 1;
	}

	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		EndDialog(wID);
		return 1;
	}
};

class find_wnd : public CWindowImpl<find_wnd>
{
public:

	const int editId = 101;
	const int tbId = 102;

    document &_doc;
	CWindow _findText;
	CWindow _findNext;

    find_wnd(document &d) : _doc(d)
	{
	}

	BEGIN_MSG_MAP(find_wnd)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		//MESSAGE_HANDLER(WM_PAINT, OnPaint)

        COMMAND_ID_HANDLER(ID_EDIT_FIND_NEXT, OnNext)
        COMMAND_ID_HANDLER(ID_EDIT_FIND_PREVIOUS, OnPrevious)

		COMMAND_HANDLER(editId, EN_CHANGE, OnEditChange)

	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto font = CreateFont(20, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Calibri"));

		_findText.Create(L"EDIT", m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, editId);
		_findText.SetFont(font);

		auto tbStyle = WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NOPARENTALIGN | CCS_NODIVIDER | CCS_ADJUSTABLE;
		
		_findNext.Create(TOOLBARCLASSNAME, m_hWnd, nullptr, nullptr, tbStyle, 0, 1);
		_findNext.SetFont(font);

		//HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, 0,
		//	CCS_ADJUSTABLE | CCS_NODIVIDER | WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
		//	0, 0, 0, 0, m_hwnd, (HMENU) IDR_TOOLBAR1, GetModuleHandle(NULL), 0);

		_findNext.SendMessage(TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

		const int numButtons = 2;

		HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, numButtons, 0);
		ImageList_AddIcon(hImageList, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_LAST)));		
		ImageList_AddIcon(hImageList, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_NEXT)));
		_findNext.SendMessage(TB_SETIMAGELIST, (WPARAM) 0, (LPARAM) hImageList);

		TBBUTTON tbButtons[numButtons] =
		{
            { 0, ID_EDIT_FIND_PREVIOUS, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, 0 },
            { 1, ID_EDIT_FIND_NEXT, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, 0 },
		};
		_findNext.SendMessage(TB_ADDBUTTONS, numButtons, (LPARAM) tbButtons);
		_findNext.SendMessage(TB_AUTOSIZE, 0, 0);

		//auto nextIcon = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_RETHINKIFY), IMAGE_ICON, 32, 32, NULL);
		//_findNext.SendMessage(BM_SETIMAGE, (WPARAM) IMAGE_ICON, (LPARAM) nextIcon);

		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CRect r;
		GetClientRect(r);

		r.left += 4;
		r.right -= 54;
		r.top += 4;
		r.bottom -= 4;

		_findText.MoveWindow(r);
		
		r.left = r.right + 4;
		r.right += 50;

		_findNext.MoveWindow(r);

		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CRect r;
		GetClientRect(r);
		FillSolidRect((HDC)wParam, r, RGB(100, 100, 100));
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CRect r;
		GetClientRect(r);

		PAINTSTRUCT ps = { 0 };
		auto hdc = BeginPaint(&ps);
		FillSolidRect(hdc, r, RGB(100, 100, 100));
		EndPaint(&ps);
		return 0;
	}

	std::wstring text()
	{
		const int bufferSize = 200;
		wchar_t text[bufferSize];
		_findText.GetWindowText(text, bufferSize);
		return text;
	}

    void text(const std::wstring &text)
    {
        _findText.SetWindowText(text.c_str());
    }

	LRESULT OnEditChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
        _doc.find_next(text());
		return 0;
	}

    LRESULT OnPrevious(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
        _doc.find_previous(text());
		return 0;
	}

	LRESULT OnNext(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
        _doc.find_next(text());
		return 0;
	}
};

class mainmf : public CWindowImpl<mainmf>
{
public:

    text_view _view;
	document _doc;	
	find_wnd _find;

    mainmf() : _view(_doc), _find(_doc), _doc(_view)
	{
	}

	BEGIN_MSG_MAP(mainmf)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnInitMenuPopup)

		COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit);
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
		COMMAND_ID_HANDLER(ID_HELP_RUNTESTS, OnRunTests)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnOpen)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
		COMMAND_ID_HANDLER(ID_FILE_SAVE_AS, OnSaveAs)
		COMMAND_ID_HANDLER(ID_FILE_NEW, OnNew)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnEditFind)
        COMMAND_ID_HANDLER(ID_EDIT_REFORMAT, OnEditReformat)        

		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
        _view.Create(m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN);
        _find.Create(_view, nullptr, nullptr, WS_CHILD, WS_EX_COMPOSITED);
        _view.invalidate_view();

		return 0;
	}

    LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        PAINTSTRUCT ps = { 0 };
        auto hdc = BeginPaint(&ps);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        return 1;
    }

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		RECT rc;
		GetClientRect(&rc);
		_view.MoveWindow(&rc);

		rc.right -= 32;
		rc.left = rc.right - 160;
		rc.bottom = rc.top + 32;
		_find.MoveWindow(&rc);

		return 0;
	}

	LRESULT OnFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		_view.SetFocus();
		return 0;
	}

	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		bool destroy = true;

        if (_doc.IsModified())
		{
			auto id = MessageBox(L"Do you want to save?", g_szAppName, MB_YESNOCANCEL | MB_ICONQUESTION);

			if (id == IDYES)
			{
                auto path = _doc.Path();
                bool saved = !path.empty() && Save(path);
                destroy = saved || Save();
			}
			else if (id == IDCANCEL)
			{
				destroy = false;
			}
		}

		if (destroy)
		{
			DestroyWindow();
			PostQuitMessage(0);
		}

		return 0;
	}

	LRESULT OnAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		about_dlg().DoModal(m_hWnd);
		return 0;
	}

	LRESULT OnExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		PostMessage(WM_CLOSE);
		return 0;
	}

	LRESULT OnRunTests(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
        _doc.select(_doc.all());
        
        undo_group ug(_doc);
        auto pos = _doc.delete_text(ug, _doc.selection());
        _doc.insert_text(ug, pos, RunTests());
        _doc.select(text_location());
        _view.invalidate_view();

        // http://www.bbc.com/news/

		_doc.Path(L"tests");
		SetTitle(L"tests");

		return 0;
	}

	LRESULT OnOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		Load();
		return 0;
	}

	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
        Save(_doc.Path());
		return 0;
	}

	LRESULT OnSaveAs(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		Save();
		return 0;
	}

	LRESULT OnNew(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		bool destroy = true;

        if (_doc.IsModified())
		{
			auto id = MessageBox(L"Do you want to save?", g_szAppName, MB_YESNOCANCEL | MB_ICONQUESTION);

			if (id == IDYES)
			{
				destroy = Save();
			}
			else if (id == IDCANCEL)
			{
				destroy = false;
			}
		}

		if (destroy)
		{
			New();
		}

		return 0;
	}

	LRESULT OnEditFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		bool isVisible = _find.IsWindowVisible() != 0;
		_find.ShowWindow(isVisible ? SW_HIDE : SW_SHOW);

        if (!isVisible)
        {
            if (_doc.has_selection())
            {
                _find.text(first_line_text(_doc.text(_doc.selection())));
            }

            _find._findText.SetFocus();
        }
        else
        {
        }

		return 0;
	}

    LRESULT OnEditReformat(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        Json::Value root;   // will contains the root value after parsing.
        Json::Reader reader;
        Json::StyledStreamWriter writer;
                
        if (reader.parse(UTF16ToUtf8(Combine(_doc.text())), root))
        {
            std::stringstream lines;
            writer.write(lines, root);

            _doc.clear();
            std::string line;

            while (std::getline(lines, line))
            {
                _doc.append_line(UTF8ToUtf16(line));
            }

            _view.invalidate_view();
        }

        return 0;
    }

	
	LRESULT OnInitMenuPopup(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
        auto hMenu = (HMENU) wParam;
        auto count = GetMenuItemCount(hMenu);

        for (auto i = 0; i < count; i++)
        {
            auto id = GetMenuItemID(hMenu, i);
            auto enable = true;

            switch (id)
            {
            case ID_EDIT_COPY: enable = _doc.has_selection(); break;
            case ID_EDIT_CUT: enable = _doc.has_selection(); break;
            case ID_EDIT_FIND_PREVIOUS: enable = _doc.can_find_next(); break;
            case ID_EDIT_PASTE: enable = _doc.CanPaste(); break;
            case ID_EDIT_REDO: enable = _doc.can_redo(); break;
            case ID_EDIT_FIND_NEXT: enable = _doc.can_find_next(); break;
            case ID_EDIT_SELECT_ALL: enable = true; break;
            case ID_EDIT_UNDO: enable = _doc.can_undo(); break;
            }

            EnableMenuItem(hMenu, i, MF_BYPOSITION | (enable ? MF_ENABLED : MF_DISABLED));
        }

		return 0;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		_view.OnCommand(LOWORD(wParam));
		return 0;
	}		

	void SetTitle(const wchar_t *name)
	{
		wchar_t title[_MAX_PATH + 10];
		wcscpy_s(title, name);
		wcscat_s(title, L" - ");
		wcscat_s(title, g_szAppName);
		SetWindowText(title);		
	}

	void Load()
	{
		wchar_t filters[] = L"Text Files (*.txt)\0*.txt\0\0";
		wchar_t path[_MAX_PATH] = L"";

		OPENFILENAME ofn = { 0 };
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = filters;
		ofn.lpstrFile = path;
		ofn.lpstrDefExt = _T("txt");
		ofn.nMaxFile = _MAX_PATH;
		ofn.lpstrTitle = _T("Open File");
		ofn.Flags = OFN_READONLY | OFN_ENABLESIZING;

		if (GetOpenFileName(&ofn))
		{
			Load(path);
		}
	}

	void New()
	{
        _view.invalidate_view();
        _doc.Path(L"New");
		SetTitle(L"New");
	}

	void Load(const std::wstring &path)
	{
		if (_doc.LoadFromFile(path))
		{
			SetTitle(PathFindFileName(path.c_str()));
		}
	}

	bool Save(const std::wstring &path)
	{
        if (_doc.SaveToFile(path))
		{
            _doc.Path(path);
			SetTitle(PathFindFileName(path.c_str()));
			return true;
		}

		return false;
	}

	bool Save()
	{
		wchar_t filters[] = L"Text Files (*.txt)\0*.txt\0\0";
		wchar_t path[_MAX_PATH] = L"";

        wcscpy_s(path, _doc.Path().c_str());

		OPENFILENAME ofn = { 0 };
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = filters;
		ofn.lpstrFile = path;
		ofn.lpstrDefExt = _T("txt");
		ofn.nMaxFile = _MAX_PATH;
		ofn.lpstrTitle = _T("Save File");
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

		return GetSaveFileName(&ofn) && Save(path);
	}
};

static bool IsNeededByDialog(const MSG &msg)
{
	static std::set<int> keys;

	if (keys.empty())
	{
		keys.insert(VK_BACK);
		keys.insert(VK_DELETE);
		keys.insert(VK_DOWN);
		keys.insert(VK_END);
		keys.insert(VK_HOME);
		keys.insert(VK_INSERT);
		keys.insert(VK_LEFT);
		keys.insert(VK_NEXT);
		keys.insert(VK_PRIOR);
		keys.insert(VK_RIGHT);
		keys.insert(VK_TAB);
		keys.insert(VK_UP);
	}

	if (msg.message == WM_KEYDOWN)
	{
		return keys.find(msg.wParam) != keys.end();
	}

	return false;
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ wchar_t * lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

    OleInitialize(nullptr);

	mainmf mf;
    mf.Create(nullptr, nullptr, g_szAppName, WS_OVERLAPPEDWINDOW, WS_EX_COMPOSITED);
	mf.SetMenu(LoadMenu(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY)));

	auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RETHINKIFY));
	mf.SetIcon(icon, true);
	mf.SetIcon(icon, false);
	mf.ShowWindow(SW_SHOW);

	int argCount;
	auto args = CommandLineToArgvW(GetCommandLine(), &argCount);

	if (argCount > 1)
	{
		mf.Load(UnQuote(args[1]));
	}

	auto accelerators = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY));
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		auto findFocused = mf._find.IsChild(GetFocus());

		if (findFocused && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			mf._find.ShowWindow(SW_HIDE);
		}
		else
		{
			auto dontTranslate = findFocused && IsNeededByDialog(msg);

			if (dontTranslate || !TranslateAccelerator(mf, accelerators, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	return (int) msg.wParam;
}

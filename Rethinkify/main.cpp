// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "resource.h"
#include "text_view.h"
#include "text_buffer.h"
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
	const int nextId = 103;
	const int lastId = 104;

	text_view &_view;
	CWindow _findText;
	CWindow _findNext;

	find_wnd(text_view &v) : _view(v)
	{
	}

	BEGIN_MSG_MAP(find_wnd)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		//MESSAGE_HANDLER(WM_PAINT, OnPaint)

		COMMAND_ID_HANDLER(nextId, OnNext)
		COMMAND_ID_HANDLER(lastId, OnLast)

		COMMAND_HANDLER(editId, EN_CHANGE, OnEditChange)

	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto font = CreateFont(20, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Calibri"));

		_findText.Create(L"EDIT", m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, editId);
		_findText.SetFont(font);

		auto tbStyle = WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NOPARENTALIGN | CCS_NODIVIDER | CCS_ADJUSTABLE;
		
		_findNext.Create(TOOLBARCLASSNAME, m_hWnd, nullptr, nullptr, tbStyle, 0, nextId);
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
			{ 0, lastId, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, 0 },
			{ 1, nextId, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, 0 },
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

	std::wstring Text()
	{
		const int bufferSize = 200;
		wchar_t text[bufferSize];
		_findText.GetWindowText(text, bufferSize);
		return text;
	}

	LRESULT OnEditChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		_view.Find(Text(), 0);
		return 0;
	}

	LRESULT OnLast(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		_view.Find(Text(), FIND_DIRECTION_UP);
		return 0;
	}

	LRESULT OnNext(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		_view.Find(Text(), 0);
		return 0;
	}
};

class main_frame : public CWindowImpl<main_frame>
{
public:

	text_buffer _text;
	text_view _view;
	find_wnd _find;

	std::wstring _path;

	main_frame() : _view(_text), _find(_view)
	{
	}

	BEGIN_MSG_MAP(main_frame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
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
		_find.Create(_view, nullptr, nullptr, WS_CHILD);
		return 0;
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

		if (_text.IsModified())
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
		_text.clear();

		std::wstringstream lines(RunTests());
		std::wstring line;

		while (std::getline(lines, line))
		{
			_text.AppendLine(line);
		}

		_view.invalidate_view();
		_path = L"tests";
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
		Save(_path);
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

		if (_text.IsModified())
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
		if (!isVisible) _find._findText.SetFocus();
		return 0;
	}

    LRESULT OnEditReformat(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        Json::Value root;   // will contains the root value after parsing.
        Json::Reader reader;
        Json::StyledStreamWriter writer;
                
        if (reader.parse(UTF16ToUtf8(Combine(_text.text())), root))
        {
            std::stringstream lines;
            writer.write(lines, root);

            _text.clear();
            std::string line;

            while (std::getline(lines, line))
            {
                _text.AppendLine(UTF8ToUtf16(line));
            }

            _view.invalidate_view();
        }

        return 0;
    }

	
	LRESULT OnInitMenuPopup(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		_view.EnableMenuItems((HMENU)wParam);
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

		_view.HighlightFromExtension(PathFindExtension(name));
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
		_path = L"New";
		SetTitle(L"New");
	}

	void Load(const std::wstring &path)
	{
		text_buffer text;
		
		if (_text.LoadFromFile(path))
		{
			_view.invalidate_view();
			_path = path;
			SetTitle(PathFindFileName(path.c_str()));
		}
	}

	bool Save(const std::wstring &path)
	{
		if (_text.SaveToFile(path))
		{
			_path = path;
			SetTitle(PathFindFileName(path.c_str()));
			return true;
		}

		return false;
	}

	bool Save()
	{
		wchar_t filters[] = L"Text Files (*.txt)\0*.txt\0\0";
		wchar_t path[_MAX_PATH] = L"";

		wcscpy_s(path, _path.c_str());

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

	main_frame _frame;
	_frame.Create(nullptr, nullptr, g_szAppName, WS_OVERLAPPEDWINDOW);
	_frame.SetMenu(LoadMenu(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY)));

	auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RETHINKIFY));
	_frame.SetIcon(icon, true);
	_frame.SetIcon(icon, false);
	_frame.ShowWindow(SW_SHOW);

	int argCount;
	auto args = CommandLineToArgvW(GetCommandLine(), &argCount);

	if (argCount > 1)
	{
		_frame.Load(UnQuote(args[1]));
	}

	auto hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY));
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		auto findFocused = _frame._find.IsChild(GetFocus());

		if (findFocused && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			_frame._find.ShowWindow(SW_HIDE);
		}
		else
		{
			auto dontTranslate = findFocused && IsNeededByDialog(msg);

			if (dontTranslate || !TranslateAccelerator(_frame, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	return (int) msg.wParam;
}

// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "Rethinkify.h"
#include "TextView.h"
#include "TextBuffer.h"

#include <shellapi.h>

//
// Ideas 
//
// could open spreadsheets using http://libxls.sourceforge.net/ or http://www.codeproject.com/Articles/42504/ExcelFormat-Library


//#pragma comment(lib, "Comdlg32")
//#pragma comment(lib, "Comctl32")

const wchar_t *Title = L"Rethinkify";

extern std::string RunTests();

class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
	enum { IDD = IDD_ABOUTBOX };

	BEGIN_MSG_MAP(CAboutDlg)
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

class CMainFrame : public CWindowImpl<CMainFrame>
{
public:

	TextBuffer _text;
	TextView _view;
	std::wstring _path;

	CMainFrame() : _view(_text)
	{
	}

	BEGIN_MSG_MAP(CMainFrame)
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

		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		_view.Create(m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL);

		LOGFONT lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = ANSI_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = DEFAULT_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
		wcscpy_s(lf.lfFaceName, L"Consolas");

		_view.SetFont(lf);
		_view.Invalidate();		

		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		RECT rc;
		GetClientRect(&rc);
		_view.MoveWindow(&rc);
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
			auto id = MessageBox(L"Do you want to save?", Title, MB_YESNOCANCEL | MB_ICONQUESTION);

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
		CAboutDlg().DoModal(m_hWnd);
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

		std::stringstream lines(RunTests());
		std::string line;

		while (std::getline(lines, line))
		{
			_text.AppendLine(line);
		}

		_view.InvalidateView();
		_path = L"Tests";
		SetTitle(L"Tests");

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
			auto id = MessageBox(L"Do you want to save?", Title, MB_YESNOCANCEL | MB_ICONQUESTION);

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
		wcscat_s(title, Title);
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
		_view.InvalidateView();
		_path = L"New";
		SetTitle(L"New");
	}

	void Load(const std::wstring &path)
	{
		TextBuffer text;
		
		if (_text.LoadFromFile(path))
		{
			_view.InvalidateView();
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

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	CMainFrame _frame;
	_frame.Create(nullptr, nullptr, Title, WS_OVERLAPPEDWINDOW);
	_frame.SetMenu(LoadMenu(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY)));

	auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RETHINKIFY));
	_frame.SetIcon(icon, TRUE);
	_frame.SetIcon(icon, FALSE);
	_frame.ShowWindow(SW_SHOW);

	int argCount;
	auto args = CommandLineToArgvW(GetCommandLine(), &argCount);

	if (argCount > 1)
	{
		_frame.Load(UnQuote(args[1]));
	}

	auto hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_RETHINKIFY));
	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(_frame, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int) msg.wParam;
}

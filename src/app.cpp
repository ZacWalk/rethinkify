// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "text_view.h"
#include "document.h"
#include "ui.h"

#include <shellapi.h>

const wchar_t filters[] = L"All Files (*.*)\0*.*\0Text Files (*.txt)\0*.txt\0\0";

//
// Ideas 
//
// Support editing CSV tables
// open spreadsheets using http://libxls.sourceforge.net/ or http://www.codeproject.com/Articles/42504/ExcelFormat-Library


//#pragma comment(lib, "Comdlg32")
//#pragma comment(lib, "Comctl32")
//#pragma comment(lib, "Shlwapi")

// allow for different calling conventions in Linux and Windows
#ifdef _WIN32
#define STDCALL __stdcall
#else
#define STDCALL
#endif

// functions to call AStyleMain
extern "C" const char* STDCALL AStyleGetVersion(void);
extern "C" char* STDCALL AStyleMain(const char* sourceIn,
	const char* optionsIn,
	void (STDCALL * fpError)(int, const char*),
	char* (STDCALL * fpAlloc)(unsigned long));

// Error handler for the Artistic Style formatter.
void STDCALL ASErrorHandler(int errorNumber, const char* errorMessage)
{
	std::cout << "astyle error " << errorNumber << "\n"
		<< errorMessage << std::endl;
}

// Allocate memory for the Artistic Style formatter.
char* STDCALL ASMemoryAlloc(unsigned long memoryNeeded)
{
	// error condition is checked after return from AStyleMain
	const auto buffer = new(std::nothrow) char[memoryNeeded];
	return buffer;
}

const wchar_t* g_app_name = L"Rethinkify";

extern std::wstring run_all_tests();

class about_dlg : public win_impl
{
public:

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_INITDIALOG) return OnInitDialog(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return OnCommand(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		CenterWindow(m_hWnd);
		return 1;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto id = LOWORD(wParam);
		if (id == IDOK) return EndDialog(m_hWnd, IDOK);
		if (id == IDCANCEL) return EndDialog(m_hWnd, IDCANCEL);
		return DefWindowProc(m_hWnd, WM_COMMAND, wParam, lParam);
	}
};


#ifndef DPI_ENUMS_DECLARED
using PROCESS_DPI_AWARENESS = enum
{
	PROCESS_DPI_UNAWARE = 0,
	PROCESS_SYSTEM_DPI_AWARE = 1,
	PROCESS_PER_MONITOR_DPI_AWARE = 2
};

using MONITOR_DPI_TYPE = enum
{
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
};
#endif /*DPI_ENUMS_DECLARED*/

using funcGetProcessDpiAwareness = HRESULT(WINAPI*)(HANDLE handle, PROCESS_DPI_AWARENESS* awareness);
using funcGetDpiForMonitor = HRESULT(WINAPI*)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);
const float kDefaultDPI = 96.f;

static bool IsProcessPerMonitorDpiAware()
{
	enum class PerMonitorDpiAware
	{
		UNKNOWN = 0,
		PER_MONITOR_DPI_UNAWARE,
		PER_MONITOR_DPI_AWARE,
	};
	static auto per_monitor_dpi_aware = PerMonitorDpiAware::UNKNOWN;
	if (per_monitor_dpi_aware == PerMonitorDpiAware::UNKNOWN)
	{
		per_monitor_dpi_aware = PerMonitorDpiAware::PER_MONITOR_DPI_UNAWARE;

		static auto dll = ::LoadLibrary(L"shcore.dll");

		if (dll)
		{
			const auto get_process_dpi_awareness_func =
				reinterpret_cast<funcGetProcessDpiAwareness>(
					GetProcAddress(dll, "GetProcessDpiAwareness"));
			if (get_process_dpi_awareness_func)
			{
				PROCESS_DPI_AWARENESS awareness;
				if (SUCCEEDED(get_process_dpi_awareness_func(nullptr, &awareness)) &&
					awareness == PROCESS_PER_MONITOR_DPI_AWARE)
					per_monitor_dpi_aware = PerMonitorDpiAware::PER_MONITOR_DPI_AWARE;
			}
		}
	}
	return per_monitor_dpi_aware == PerMonitorDpiAware::PER_MONITOR_DPI_AWARE;
}

float GetScalingFactorFromDPI(int dpi)
{
	return static_cast<float>(dpi) / kDefaultDPI;
}

int GetDefaultSystemDPI()
{
	static int dpi_x = 0;
	static int dpi_y = 0;
	static bool should_initialize = true;

	if (should_initialize)
	{
		should_initialize = false;
		const auto screen_dc = GetDC(nullptr);

		if (screen_dc)
		{
			// This value is safe to cache for the life time of the app since the
			// user must logout to change the DPI setting. This value also applies
			// to all screens.
			dpi_x = GetDeviceCaps(screen_dc, LOGPIXELSX);
			dpi_y = GetDeviceCaps(screen_dc, LOGPIXELSY);
			ReleaseDC(nullptr, screen_dc);
		}
	}
	return dpi_x;
}

// Gets the DPI for a particular monitor.
int GetPerMonitorDPI(HMONITOR monitor)
{
	if (IsProcessPerMonitorDpiAware())
	{
		static const auto dll = ::LoadLibrary(L"shcore.dll");

		if (dll)
		{
			static const auto get_dpi_for_monitor_func = reinterpret_cast<funcGetDpiForMonitor>(GetProcAddress(
				dll, "GetDpiForMonitor"));

			if (get_dpi_for_monitor_func)
			{
				UINT dpi_x, dpi_y;

				if (SUCCEEDED(get_dpi_for_monitor_func(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
				{
					return static_cast<int>(dpi_x);
				}
			}
		}
	}

	return GetDefaultSystemDPI();
}


class main_win : public win_impl
{
public:
	text_view _view;
	document _doc;
	find_wnd _find;

	main_win() : _view(_doc, _find), _doc(_view), _find(_doc)
	{
	}

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return OnCreate(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return OnEraseBackground(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return OnPaint(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return OnSize(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return OnFocus(uMsg, wParam, lParam);
		if (uMsg == WM_CLOSE) return OnClose(uMsg, wParam, lParam);
		if (uMsg == WM_INITMENUPOPUP) return OnInitMenuPopup(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return OnCommand(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return OnCommand(uMsg, wParam, lParam);
		if (uMsg == WM_DPICHANGED) return on_window_dpi_changed(uMsg, wParam, lParam);

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		_view.Create(L"TEXT_FRAME", m_hWnd, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN);
		_find.Create(L"FINDER_FRAME", _view.m_hWnd, WS_CHILD, WS_EX_COMPOSITED);
		const auto monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		_scale_factor = GetScalingFactorFromDPI(GetPerMonitorDPI(monitor));;
		_view.update_font(_scale_factor);
		_find.update_font(_scale_factor);
		_view.invalidate_view();
		update_title();

		return 0;
	}

	double _scale_factor = 1.0;

	LRESULT on_window_dpi_changed(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		_scale_factor = HIWORD(wParam) / static_cast<double>(USER_DEFAULT_SCREEN_DPI);
		_view.update_font(_scale_factor);

		const auto new_bounds = reinterpret_cast<RECT*>(lParam);

		if (new_bounds)
		{
			SetWindowPos(m_hWnd,
				nullptr,
				new_bounds->left,
				new_bounds->top,
				new_bounds->right - new_bounds->left,
				new_bounds->bottom - new_bounds->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}

		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		PAINTSTRUCT ps = { nullptr };
		auto hdc = BeginPaint(m_hWnd, &ps);
		EndPaint(m_hWnd, &ps);
		return 0;
	}



	static LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		return 1;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		auto rc = GetClientRect();
		_view.MoveWindow(rc);

		rc.right -= 32;
		rc.left = rc.right - 160;
		rc.bottom = rc.top + 32;
		_find.MoveWindow(rc);

		return 0;
	}

	LRESULT OnFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		SetFocus(_view.m_hWnd);
		return 0;
	}

	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		bool destroy = true;

		if (_doc.is_modified())
		{
			const auto id = MessageBox(m_hWnd, L"Do you want to save?", g_app_name, MB_YESNOCANCEL | MB_ICONQUESTION);

			if (id == IDYES)
			{
				const auto path = _doc.path();
				const bool saved = !path.empty() && save_doc(path);
				destroy = saved || save_doc();
			}
			else if (id == IDCANCEL)
			{
				destroy = false;
			}
		}

		if (destroy)
		{
			DestroyWindow(m_hWnd);
			PostQuitMessage(0);
		}

		return 0;
	}

	LRESULT OnAbout() const
	{
		about_dlg().DoModal(m_hWnd, IDD_ABOUTBOX);
		return 0;
	}

	LRESULT OnExit()
	{
		PostMessage(m_hWnd, WM_CLOSE, 0, 0);
		return 0;
	}

	LRESULT OnRunTests()
	{
		_doc.select(_doc.all());

		undo_group ug(_doc);
		const auto pos = _doc.delete_text(ug, _doc.selection());
		_doc.insert_text(ug, pos, run_all_tests());
		_doc.select(text_location());
		_view.invalidate_view();

		// http://www.bbc.com/news/

		_doc.path({ L"tests" });

		return 0;
	}

	LRESULT OnOpen()
	{
		load_doc();
		return 0;
	}

	LRESULT OnSave()
	{
		save_doc(_doc.path());
		return 0;
	}

	LRESULT OnSaveAs()
	{
		save_doc();
		return 0;
	}

	LRESULT OnNew()
	{
		bool destroy = true;

		if (_doc.is_modified())
		{
			const auto id = MessageBox(m_hWnd, L"Do you want to save?", g_app_name, MB_YESNOCANCEL | MB_ICONQUESTION);

			if (id == IDYES)
			{
				destroy = save_doc();
			}
			else if (id == IDCANCEL)
			{
				destroy = false;
			}
		}

		if (destroy)
		{
			new_doc();
		}

		return 0;
	}

	LRESULT OnEditFind()
	{
		ShowWindow(_find.m_hWnd, SW_SHOW);

		if (_doc.has_selection())
		{
			const auto sel = _doc.selection();

			if (sel.line_count() == 1 && !sel.empty())
			{
				const auto text = str::combine(_doc.text(sel));
				_find.Text(text);
			}

			SetFocus(_find._find_text.m_hWnd);
		}

		return 0;
	}

	bool is_json() const
	{
		for (const auto& line : _doc.lines())
		{
			for (const auto& c : line._text)
			{
				if (c == '{') 
				{
					return true;
				}
				if (c != ' ' && c != '\n' && c != '\t' && c != '\r') 
				{
					return false;
				}
			}
		}
		return false;
	}

	LRESULT OnEditReformat()
	{
		if (is_json())
		{
			std::wostringstream result;
			int tabs = 0, tokens = -1; //here tokens are  { } , :

			for (const auto& line : _doc.lines())
			{
				for (const auto& ch : line._text)
				{
					if (ch == '{')
					{
						tokens++;

						//open braces tabs
						tabs = tokens;
						if (tokens > 0)
							result << "\n";
						while (tabs)
						{
							result << "\t";
							tabs--;
						}
						result << ch << "\n";

						//json key:value tabs
						tabs = tokens + 1;
						while (tabs)
						{
							result << "\t";
							tabs--;
						}
					}
					else if (ch == ':')
					{
						result << " : ";
					}
					else if (ch == ',')
					{
						result << ",\n";
						tabs = tokens + 1;
						while (tabs)
						{
							result << "\t";
							tabs--;
						}
					}
					else if (ch == '}')
					{
						tabs = tokens;
						result << "\n";
						while (tabs)
						{
							result << "\t";
							tabs--;
						}

						result << ch << "\n";
						tokens--;
						tabs = tokens + 1;

						while (tabs)
						{
							result << "\t";
							tabs--;
						}
					}
					else
					{
						if (ch == '\n' || ch == '\t')
							continue;
						else
							result << ch;
					}
				}
			}

			undo_group ug(_doc);
			_doc.select(_doc.replace_text(ug, _doc.all(), result.str()));
		}
		else
		{
			const auto text = UTF16ToUtf8(_doc.str());
			auto options = "-A1tOP";

			// call the Artistic Style formatting function
			auto textOut = AStyleMain(text.c_str(),
				options,
				ASErrorHandler,
				ASMemoryAlloc);

			undo_group ug(_doc);
			_doc.select(_doc.replace_text(ug, _doc.all(), UTF8ToUtf16(textOut)));

			delete[] textOut;
		}

		return 0;
	}

	LRESULT OnEditRemoveDuplicates()
	{
		auto lines = _doc.lines();

		std::sort(lines.begin(), lines.end());
		lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

		undo_group ug(_doc);
		_doc.select(_doc.replace_text(ug, _doc.all(), document::combine_line_text(lines)));

		return 0;
	}


	LRESULT OnInitMenuPopup(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		const auto hMenu = reinterpret_cast<HMENU>(wParam);
		const auto count = GetMenuItemCount(hMenu);

		for (auto i = 0; i < count; i++)
		{
			const auto id = GetMenuItemID(hMenu, i);
			auto enable = true;

			switch (id)
			{
			case ID_EDIT_COPY: enable = _doc.has_selection();
				break;
			case ID_EDIT_CUT: enable = _doc.has_selection();
				break;
			case ID_EDIT_FIND_PREVIOUS: enable = _doc.can_find_next();
				break;
			case ID_EDIT_PASTE: enable = _doc.can_paste();
				break;
			case ID_EDIT_REDO: enable = _doc.can_redo();
				break;
			case ID_EDIT_REPEAT: enable = _doc.can_find_next();
				break;
			case ID_EDIT_SELECT_ALL: enable = true;
				break;
			case ID_EDIT_UNDO: enable = _doc.can_undo();
				break;
			}

			EnableMenuItem(hMenu, i, MF_BYPOSITION | (enable ? MF_ENABLED : MF_DISABLED));
		}

		return 0;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto id = LOWORD(wParam);

		if (id == ID_APP_EXIT) return OnExit();
		if (id == ID_APP_ABOUT) return OnAbout();
		if (id == ID_HELP_RUNTESTS) return OnRunTests();
		if (id == ID_FILE_OPEN) return OnOpen();
		if (id == ID_FILE_SAVE) return OnSave();
		if (id == ID_FILE_SAVE_AS) return OnSaveAs();
		if (id == ID_FILE_NEW) return OnNew();
		if (id == ID_EDIT_FIND) return OnEditFind();
		if (id == ID_EDIT_REFORMAT) return OnEditReformat();
		if (id == ID_EDIT_SORTANDREMOVEDUPLICATES) return OnEditRemoveDuplicates();

		_view.OnCommand(id);
		return 0;
	}

	void update_title()
	{
		auto name = _doc.path().name();
		if (name.empty()) name = _doc.path().view();
		const auto title = name.empty() ? g_app_name : std::format(L"{} - {}", name, g_app_name);
		::SetWindowText(m_hWnd, title.c_str());
	}

	void load_doc()
	{
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
			load_doc({ path });
		}
	}

	void new_doc()
	{
		_doc.clear();
		_view.invalidate_view();
		_doc.path({ L"New" });
	}

	void load_doc(file_path path)
	{
		if (_doc.load_from_file(path))
		{
			// anything?
		}
	}

	bool save_doc(file_path path)
	{
		if (_doc.save_to_file(path))
		{
			_doc.path(path);
			return true;
		}

		return false;
	}

	bool save_doc()
	{
		wchar_t path[_MAX_PATH] = L"";
		wcscpy_s(path, _doc.path().c_str());

		OPENFILENAME ofn = { 0 };
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = filters;
		ofn.lpstrFile = path;
		ofn.lpstrDefExt = _T("txt");
		ofn.nMaxFile = _MAX_PATH;
		ofn.lpstrTitle = _T("save_doc File");
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

		return GetSaveFileName(&ofn) && save_doc({ path });
	}

	void on_idle()
	{
		const auto invalids = _doc.validate();

		if (invalids & invalid::title)
		{
			update_title();
		}

		if (invalids & invalid::view)
		{
			_view.invalidate_view();
		}

		if (invalids & invalid::layout)
		{
			_view.layout();
		}

		if (invalids & invalid::caret)
		{
			_view.update_caret();
		}

		if (invalids & invalid::horz_scrollbar)
		{
			_view.recalc_horz_scrollbar();
		}

		if (invalids & invalid::vert_scrollbar)
		{
			_view.recalc_vert_scrollbar();
		}
	}
};

static bool is_needed_by_dialog(const MSG& msg)
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
		return keys.contains(msg.wParam);
	}

	return false;
}

HINSTANCE resource_instance = nullptr;

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ wchar_t* lpCmdLine,
	_In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	resource_instance = hInstance;
	OleInitialize(nullptr);
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	main_win win;
	win.Create(L"APP_FRAME", nullptr, WS_OVERLAPPEDWINDOW, WS_EX_COMPOSITED);
	SetMenu(win.m_hWnd, LoadMenu(hInstance, MAKEINTRESOURCE(IDC_APP)));

	const auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
	SetIcon(win.m_hWnd, icon);
	ShowWindow(win.m_hWnd, SW_SHOW);

	int argCount;
	const auto args = CommandLineToArgvW(GetCommandLine(), &argCount);

	if (argCount > 1)
	{
		win.load_doc(std::wstring{ str::unquote(args[1]) });
	}

	const auto accelerators = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_APP));
	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		const auto find_focused = IsChild(win._find.m_hWnd, GetFocus());

		if (find_focused && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			ShowWindow(win._find.m_hWnd, SW_HIDE);
		}
		else
		{
			const auto dont_translate = find_focused && is_needed_by_dialog(msg);

			if (dont_translate || !TranslateAccelerator(win.m_hWnd, accelerators, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		if (!::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			win.on_idle();
		}
	}

	return static_cast<int>(msg.wParam);
}

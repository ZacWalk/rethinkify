// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "text_view.h"
#include "document.h"
#include "ui.h"

const wchar_t filters[] = L"All Files (*.*)\0*.*\0Text Files (*.txt)\0*.txt\0\0";

constexpr auto SPLITTER_BAR_WIDTH = 5;

constexpr auto handle_color = 0x00444444;
constexpr auto main_wnd_clr = 0x00222222;
constexpr auto tool_wnd_clr = 0x00333333;
constexpr auto handle_hover_color = 0x00666666;
const unsigned handle_tracking_color = 0x00CC6611;
const unsigned text_color = 0x00FFFFFF;
const unsigned darker_text_color = 0x00CCCCCC;

const unsigned WM_ITEM = WM_USER + 99;

COLORREF style_to_color(style style_index)
{
	switch (style_index)
	{
	case style::white_space:
		return main_wnd_clr;
	case style::normal_bkgnd:
		return RGB(30, 30, 30);
	case style::normal_text:
		return RGB(222, 222, 222);
	case style::sel_margin:
		return RGB(44, 44, 44);
	case style::code_preprocessor:
		return RGB(133, 133, 211);
	case style::code_comment:
		return RGB(128, 222, 128);
	case style::code_number:
		return RGB(244, 244, 144);
	case style::code_string:
		return RGB(244, 244, 144);
	case style::code_operator:
		return RGB(128, 255, 128);
	case style::code_keyword:
		return RGB(128, 128, 255);
	case style::sel_bkgnd:
		return RGB(88, 88, 88);
	case style::sel_text:
		return RGB(255, 255, 255);
	}
	return RGB(222, 222, 222);
}


//
// Ideas 
//
// Support editing CSV tables
// open spreadsheets using http://libxls.sourceforge.net/ or http://www.codeproject.com/Articles/42504/ExcelFormat-Library


//#pragma comment(lib, "Comdlg32")
//#pragma comment(lib, "Comctl32")
//#pragma comment(lib, "Shlwapi")
//#pragma comment(lib, "User32")

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

class about_dlg : public ui::win_impl
{
public:
	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_INITDIALOG) return OnInitDialog(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return OnCommand(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		ui::center_window(m_hWnd);
		return 1;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
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

BOOL SetProcessDpiAwarenessContextIndirect(_In_ DPI_AWARENESS_CONTEXT dpiContext)
{
	static const auto dll = ::LoadLibrary(L"user32.dll");

	if (dll != nullptr)
	{
		typedef int(WINAPI* PfnSetProcessDpiAwarenessContexts)(DPI_AWARENESS_CONTEXT dpiContext);

		static PfnSetProcessDpiAwarenessContexts pfn = (PfnSetProcessDpiAwarenessContexts)GetProcAddress(dll, "SetProcessDpiAwarenessContext");

		if (pfn != nullptr)
		{
			return pfn(dpiContext);
		}

	}

	return FALSE;
}

static bool is_folder(DWORD attributes)
{
	return (attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

struct file_attributes_t
{
	bool is_readonly = false;
	bool is_offline = false;
	bool is_hidden = false;
	uint64_t modified = 0;
	uint64_t created = 0;
	uint64_t size = 0;
};

struct file_info
{
	file_path path;
	file_attributes_t attributes;
};

struct folder_info
{
	file_path path;
	file_attributes_t attributes;
};

struct folder_contents
{
	std::vector<folder_info> folders;
	std::vector<file_info> files;
};

uint64_t ft_to_ts(const FILETIME& ft)
{
	return static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

static uint64_t fs_to_i64(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
	return static_cast<__int64>(nFileSizeHigh) << 32 | nFileSizeLow;
}

static bool is_offline_attribute(DWORD attributes)
{
	// Onedrive and GVFS use file attributes to denote files or directories that
	// may not be locally present and are only available "online". These files are applied one of
	// the two file attributes: FILE_ATTRIBUTE_RECALL_ON_OPEN or FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS.
	// When the attribute FILE_ATTRIBUTE_RECALL_ON_OPEN is set, skip the file during enumeration because the file
	// is not locally present at all. A file with FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS may be partially present locally.
	//
	// https://stackoverflow.com/questions/49301958/how-to-detect-onedrive-online-only-files
	//
	const auto offline_mask = FILE_ATTRIBUTE_OFFLINE |
		FILE_ATTRIBUTE_RECALL_ON_OPEN |
		FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
		FILE_ATTRIBUTE_VIRTUAL;

	return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & offline_mask) != 0;
}

static __forceinline void populate_file_attributes(file_attributes_t& fi, const WIN32_FIND_DATA& fad)
{
	fi.created = ft_to_ts(fad.ftCreationTime);
	fi.modified = ft_to_ts(fad.ftLastWriteTime);
	fi.size = fs_to_i64(fad.nFileSizeHigh, fad.nFileSizeLow);
	fi.is_readonly = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
	fi.is_hidden = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	fi.is_offline = 0 != is_offline_attribute(fad.dwFileAttributes);
}

static bool is_dots(const wchar_t* name)
{
	const auto* p = name;
	while (*p)
	{
		if (*p != '.') return false;
		p += 1;
	}

	return !str::is_empty(name);
}

static bool can_show_file(const wchar_t* name, DWORD attributes, bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return !is_folder(attributes) && !is_dots(name);
}

static bool can_show_folder(const wchar_t* name, DWORD attributes, bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return is_folder(attributes) && !is_dots(name);
}

static bool can_show_file_or_folder(const wchar_t* name, DWORD attributes, bool show_hidden)
{
	if (is_folder(attributes))
	{
		return can_show_folder(name, attributes, show_hidden);
	}
	return can_show_file(name, attributes, show_hidden);
}

folder_contents iterate_file_items(const file_path folder, bool show_hidden)
{
	folder_contents results;
	WIN32_FIND_DATA fd;

	const auto file_search_path = std::format(L"{}\\*.*", folder.c_str());
	auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
		FIND_FIRST_EX_LARGE_FETCH);

	results.files.reserve(256);
	results.folders.reserve(64);

	if (files != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (is_folder(fd.dwFileAttributes))
			{
				if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					folder_info i;
					i.path = folder.Combine(fd.cFileName);
					populate_file_attributes(i.attributes, fd);
					results.folders.emplace_back(i);
				}
			}
			else
			{
				if (can_show_file(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					file_info i;
					i.path = folder.Combine(fd.cFileName);
					populate_file_attributes(i.attributes, fd);
					results.files.emplace_back(i);
				}
			}
		} while (FindNextFile(files, &fd) != 0);

		FindClose(files);
	}

	return results;
}

struct Item
{
	std::wstring name;
	std::wstring Source;
	CRect bounds;
	file_path path;
};

class ListWnd : public ui::win_impl
{
public:

	IEvents& _events;

	std::vector<std::shared_ptr<Item>> _results;
	std::shared_ptr<Item> _selectedItem;
	std::shared_ptr<Item> _hoverItem;
	HFONT _font = nullptr;

	CSize _extent;
	CPoint _offset;
	int _yMax;
	int _yTrackingStart;
	int _yTrackingOffsetStart;
	bool _highlightScroll;
	bool _hover;
	bool _tracking;
	bool _trackingScrollbar;
	

	ListWnd(IEvents& events) :
		_selectedItem(nullptr),
		_hoverItem(nullptr),
		_offset(0, 0),
		_yMax(0),
		_yTrackingStart(0),
		_yTrackingOffsetStart(0),
		_highlightScroll(false),
		_tracking(false),
		_trackingScrollbar(false), _events(events)
	{
	}

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return OnCreate(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return OnSize(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return OnEraseBackground(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return OnPaint(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEACTIVATE) return OnMouseActivate(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return OnLButtonDown(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return OnLButtonUp(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDBLCLK) return OnMouseDblClk(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return OnMouseMove(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSELEAVE) return OnMouseLeave(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return OnMouseWheel(uMsg, wParam, lParam);
		if (uMsg == WM_ITEM) return OnItem(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{		
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		_extent.cx = LOWORD(lParam);
		_extent.cy = HIWORD(lParam);
		Layout();
		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		assert(::IsWindow(m_hWnd));

		if (wParam != 0)
		{
			OnPaint((HDC)wParam);
		}
		else
		{
			const auto r = GetClientRect();

			PAINTSTRUCT ps;
			auto hPaintDc = BeginPaint(m_hWnd, &ps);
			auto hdc = CreateCompatibleDC(hPaintDc);
			auto hBitmap = CreateCompatibleBitmap(hPaintDc, r.Width(), r.Height());
			auto hOldBitmap = SelectObject(hdc, hBitmap);

			OnPaint(hdc);
			BitBlt(hPaintDc, 0, 0, r.Width(), r.Height(), hdc, 0, 0, SRCCOPY);

			SelectObject(hdc, hOldBitmap);
			DeleteObject(hBitmap);
			DeleteDC(hdc);
			EndPaint(m_hWnd, &ps);
		}

		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		return 1;
	}

	LRESULT OnMouseActivate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		return MA_NOACTIVATE;
	}

	void Layout()
	{
		auto hdc = GetWindowDC(m_hWnd);
		auto old_font = SelectObject(hdc, _font);

		std::sort(_results.begin(), _results.end(), [](const std::shared_ptr<Item> &l, const std::shared_ptr<Item> &r) { return str::icmp(l->name, r->name) < 0; });

		int y = 16;
		int n = 0;

		for (auto i : _results)
		{
			if (i->bounds.Width() == _extent.cx)
			{
				CRect bounds(0, y, _extent.cx, y + i->bounds.Height());
				i->bounds = bounds;
				y = bounds.bottom;
			}
			else
			{
				CRect bounds(0, 0, _extent.cx - 16, 100);
				DrawText(hdc, i->name.c_str(), i->name.length(), bounds, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_CALCRECT);

				bounds.left = 0;
				bounds.top = y;
				bounds.right = _extent.cx;
				bounds.bottom += y + 8;

				i->bounds = bounds;
				y = bounds.bottom;
			}
		}

		_yMax = y + 64;
		SelectObject(hdc, old_font);
		ReleaseDC(m_hWnd, hdc);
	}

	void OnPaint(HDC hdc)
	{
		auto now = time(nullptr);
		wchar_t sz[64];

		const auto r = GetClientRect();
		ui::fill_solid_rect(hdc, r, tool_wnd_clr);

		const auto old_font = SelectObject(hdc, _font);
		SetBkMode(hdc, TRANSPARENT);

		for (auto i : _results)
		{
			auto bounds = i->bounds.Offset(-_offset);

			if (bounds.Intersects(r))
			{
				if (i == _hoverItem)
				{
					ui::fill_solid_rect(hdc, bounds, handle_hover_color);
				}

				if (i == _selectedItem)
				{
					ui::fill_solid_rect(hdc, bounds, handle_tracking_color);
				}

				SetTextColor(hdc, text_color);

				bounds = bounds.Inflate(-8, -4);
				DrawText(hdc, i->name.c_str(), i->name.length(), bounds, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);


				SetTextColor(hdc, darker_text_color);
				//FormatDateDiff(sz, now - i->Date);
				//TextOut(hdc, bounds.left, bounds.bottom - 10, sz, wcslen(sz));
				TextOut(hdc, bounds.left, bounds.bottom - 20, i->Source.c_str(), i->Source.length());
			}
		}

		DrawScroll(hdc, _highlightScroll, _trackingScrollbar);

		SelectObject(hdc, old_font);
	}

	bool CanScroll() const
	{
		return _yMax > _extent.cy;
	}

	void DrawScroll(HDC hdc, bool highlight, bool tracking)
	{
		if (CanScroll())
		{
			const auto y = MulDiv(_offset.y, _extent.cy, _yMax);
			const auto cy = MulDiv(_extent.cy, _extent.cy, _yMax);
			auto xPadding = 0;
			const auto right = _extent.cx;

			if (highlight || tracking)
			{
				ui::fill_solid_rect(hdc, CRect(right - 26, 0, right, _extent.cy), handle_color);
				xPadding = 10;
			}

			const auto c = tracking ? handle_tracking_color : handle_hover_color;
			ui::fill_solid_rect(hdc, CRect(right - 12 - xPadding, y, right - 4, y + cy), c);
		}
	}

	std::shared_ptr<Item> SelectionFromPoint(const CPoint& pt)
	{
		for (auto i : _results)
		{
			if (i->bounds.Contains(pt))
				return i;
		}

		return nullptr;
	}

	void SetHover(std::shared_ptr<Item> h)
	{
		if (_hoverItem != h)
		{
			_hoverItem = h;
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void select_item(const std::shared_ptr<Item>& i)
	{
		if (_selectedItem != i)
		{
			_selectedItem = i;

			if (_selectedItem)
			{
				_events.path_selected(i->path);				
			}

			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		SetFocus(m_hWnd);

		if (IsOverScrollbar(point))
		{
			if (!_tracking)
			{
				_yTrackingStart = point.y;
				_yTrackingOffsetStart = _offset.y;

				_trackingScrollbar = IsOverScrollbar(point);
				_tracking = true;
				SetCapture(m_hWnd);
			}
		}
		else
		{
			const auto i = SelectionFromPoint(point + _offset);
			SetHover(i);
			select_item(i);
		}

		InvalidateRect(m_hWnd, nullptr, FALSE);

		return 0;
	}

	LRESULT OnMouseDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		SetHover(SelectionFromPoint(point + _offset));

		if (_hoverItem != nullptr)
		{
			//ShellExecute(m_hWnd, L"open", _hoverItem->Link.c_str(), L"", L"", SW_SHOWNORMAL);
		}
		return 0;
	}

	LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (!_hover)
		{
			TRACKMOUSEEVENT tme = { 0 };
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = m_hWnd;
			TrackMouseEvent(&tme);
			_hover = true;
		}

		if (_trackingScrollbar)
		{
			auto offset = MulDiv(point.y - _yTrackingStart, _yMax, _extent.cy);
			ScrollTo(_yTrackingOffsetStart + offset);
		}
		else
		{
			UpdateMousePos(point);
		}

		SetHover(SelectionFromPoint(point + _offset));
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		if (_tracking)
		{
			_tracking = false;
			_trackingScrollbar = false;
			_yTrackingStart = 0;

			ReleaseCapture();
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}

		return 0;
	}

	LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		_hoverItem = nullptr;
		_hover = false;
		_highlightScroll = false;
		InvalidateRect(m_hWnd, nullptr, FALSE);
		return 0;
	}

	LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		int zDelta = ((short)HIWORD(wParam)) / 2;
		ScrollTo(_offset.y - zDelta);
		return 0;
	}

	bool IsOverScrollbar(const CPoint& point) const
	{
		return (_extent.cx - 32) < point.x;
	}

	void UpdateMousePos(const CPoint& point)
	{
		auto h = IsOverScrollbar(point);

		if (_highlightScroll != h)
		{
			_highlightScroll = h;
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void ScrollTo(int offset)
	{
		offset = clamp(offset, 0, _yMax - _extent.cy);

		if (_offset.y != offset)
		{
			_offset.y = offset;
			Layout();
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	/*void StepSelection(int i)
	{
	auto size = static_cast<int>(_results.size());

	if (size > 0)
	{
	_selected = (_selected + i) % size;
	if (_selected < 0) _selected += size;
	Invalidate(FALSE);
	}
	else
	{
	_selected = -1;
	}
	}*/

	LRESULT OnItem(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		/*auto i = reinterpret_cast<Item*>(wParam);
		_results.push_back(i);
		Layout();
		InvalidateRect(m_hWnd, nullptr, FALSE);*/
		return 0;
	}

	void update_font(const double scale_factor)
	{
		LOGFONT lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfHeight = 20 * scale_factor;
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = ANSI_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
		wcscpy_s(lf.lfFaceName, L"Calibri");

		if (_font) DeleteObject(_font);
		_font = ::CreateFontIndirect(&lf);
	}

	void populate(const folder_contents& folder_contents)
	{
		std::vector<std::shared_ptr<Item>> new_results;
		std::unordered_map<file_path, std::shared_ptr<Item>, ihash, ieq> existing;

		for (const auto& i : _results)
		{
			existing[i->path] = i;
		}

		for (const auto& f : folder_contents.files)
		{
			const auto found = existing.find(f.path);

			if (found != existing.end())
			{
				new_results.push_back(found->second);
			}
			else
			{
				auto i = std::make_shared<Item>();
				i->name = f.path.name();
				i->path = f.path;
				new_results.push_back(i);
			}
		}

		std::swap(_results, new_results);
		Layout();
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
};


class main_win : public ui::win_impl, public IEvents
{
public:
	text_view _view;
	ListWnd _list;
	document _doc;
	find_wnd _find;

	HCURSOR _cursor_ew = LoadCursor(nullptr, IDC_SIZEWE);
	double _split_ratio = 0.2;
	bool _is_tracking_splitter = false;
	bool _is_hover_splitter = false;

	main_win() : _view(_doc, _find, *this), _doc(_view), _find(_doc), _list(*this)
	{
	}

	void path_selected(const file_path& path) override
	{
		load_doc(path);
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
		if (uMsg == WM_DPICHANGED) return on_window_dpi_changed(uMsg, wParam, lParam);

		switch (uMsg)
		{
		case WM_LBUTTONDOWN:
		{
			const auto rect = GetClientRect();
			const auto xPos = GET_X_LPARAM(lParam);
			const auto yPos = GET_Y_LPARAM(lParam);
			const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);

			_is_tracking_splitter = (xPos > split_pos - SPLITTER_BAR_WIDTH &&
				xPos < split_pos + SPLITTER_BAR_WIDTH);

			if (_is_tracking_splitter)
			{
				SetCapture(hWnd);
				SetCursor(_cursor_ew);
			}
		}
		break;

		case WM_MOUSELEAVE:
			if (_is_hover_splitter)
			{
				_is_hover_splitter = false;
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			break;

		case WM_MOUSEMOVE:
		{
			const auto xPos = GET_X_LPARAM(lParam);
			const auto yPos = GET_Y_LPARAM(lParam);
			const auto rect = GetClientRect();

			if (wParam == MK_LBUTTON)
			{
				if (_is_tracking_splitter)
				{
					_split_ratio = (xPos - rect.left) / static_cast<double>(rect.right - rect.left);
					if (_split_ratio < 0.05) _split_ratio = 0.05;
					if (_split_ratio > 0.95) _split_ratio = 0.95;
					InvalidateRect(hWnd, nullptr, FALSE);
					layout_views();
				}
			}

			const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);
			const auto new_hover_splitter = (xPos > (split_pos - SPLITTER_BAR_WIDTH) &&
				(xPos < (split_pos + SPLITTER_BAR_WIDTH)));

			if (new_hover_splitter != _is_hover_splitter)
			{
				_is_hover_splitter = new_hover_splitter;
				InvalidateRect(hWnd, nullptr, FALSE);

				if (_is_hover_splitter)
				{
					TRACKMOUSEEVENT tme = { 0 };
					tme.cbSize = sizeof(tme);
					tme.dwFlags = TME_LEAVE;
					tme.hwndTrack = m_hWnd;
					TrackMouseEvent(&tme);
				}
			}

			if (_is_hover_splitter)
			{
				SetCursor(_cursor_ew);
			}
		}
		break;


		case WM_LBUTTONUP:
			if (_is_tracking_splitter)
			{
				ReleaseCapture();

				if (_is_tracking_splitter)
				{
					InvalidateRect(hWnd, nullptr, FALSE);
					_is_tracking_splitter = false;
				}
			}
			break;
		}

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		_view.Create(L"TEXT_FRAME", m_hWnd, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN);
		_list.Create(L"LIST_FRAME", m_hWnd, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
		_find.Create(L"FINDER_FRAME", _view.m_hWnd, WS_CHILD, WS_EX_COMPOSITED);
		const auto monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		_scale_factor = GetScalingFactorFromDPI(GetPerMonitorDPI(monitor));
		_view.update_font(_scale_factor);
		_find.update_font(_scale_factor);
		_list.update_font(_scale_factor);
		_view.invalidate_view();
		update_title();

		return 0;
	}

	double _scale_factor = 1.0;

	LRESULT on_window_dpi_changed(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		_scale_factor = HIWORD(wParam) / static_cast<double>(USER_DEFAULT_SCREEN_DPI);
		_view.update_font(_scale_factor);
		_list.update_font(_scale_factor);

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

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		PAINTSTRUCT ps = { nullptr };
		auto hdc = BeginPaint(m_hWnd, &ps);

		const auto bg_brush = CreateSolidBrush(main_wnd_clr);
		const auto bounds = GetClientRect();

		auto c = handle_color;
		if (_is_hover_splitter) c = handle_hover_color;
		if (_is_tracking_splitter) c = handle_tracking_color;
		const auto splitter_brush = CreateSolidBrush(c);

		const auto split_pos = static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio);

		const RECT splitter_rect = {
			split_pos - SPLITTER_BAR_WIDTH, bounds.top,
			split_pos + SPLITTER_BAR_WIDTH, bounds.bottom
		};

		const RECT client_area1 = {
			bounds.left, bounds.top,
			split_pos - SPLITTER_BAR_WIDTH, bounds.bottom
		};

		const RECT client_area2 = {
			split_pos + SPLITTER_BAR_WIDTH, bounds.top,
			bounds.right, bounds.bottom
		};

		//FillRect(hdc, &client_area1, bg_brush);
		FillRect(hdc, &splitter_rect, splitter_brush);
		//FillRect(hdc, &client_area2, bg_brush);

		DeleteObject(bg_brush);
		DeleteObject(splitter_brush);

		EndPaint(m_hWnd, &ps);
		return 0;
	}

	static LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		return 1;
	}

	void layout_views() const
	{
		const auto bounds = GetClientRect();
		const auto split_pos = static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio);

		auto text_bounds = bounds;
		text_bounds.left = split_pos + SPLITTER_BAR_WIDTH;
		_view.MoveWindow(text_bounds);

		auto list_bounds = bounds;
		list_bounds.right = split_pos - SPLITTER_BAR_WIDTH;
		_list.MoveWindow(list_bounds);

		auto find_bounds = text_bounds;
		find_bounds.right -= 32;
		find_bounds.left = find_bounds.right - std::min(static_cast<int>(300 * _scale_factor), find_bounds.Width() / 2);
		find_bounds.bottom = find_bounds.top + 40;
		_find.MoveWindow(find_bounds);
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		layout_views();
		return 0;
	}

	LRESULT OnFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
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

	LRESULT OnExit() const
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

		_doc.path({ L"test-results.md" });

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

	LRESULT OnEditFind() const
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
		}

		SetFocus(_find._find_edit.m_hWnd);
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
						result << ch;
					}
				}
			}

			undo_group ug(_doc);
			_doc.select(_doc.replace_text(ug, _doc.all(), result.str()));
		}
		else
		{
			const auto text = str::UTF16ToUtf8(_doc.str());
			const auto options = "-A1tOP";

			// call the Artistic Style formatting function
			const auto textOut = AStyleMain(text.c_str(),
				options,
				ASErrorHandler,
				ASMemoryAlloc);

			undo_group ug(_doc);
			_doc.select(_doc.replace_text(ug, _doc.all(), str::UTF8ToUtf16(textOut)));

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

	void update_title() const
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
			// populate the folder list
			_list.populate(iterate_file_items(path.folder(), false));
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
	SetProcessDpiAwarenessContextIndirect(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	main_win win;
	win.Create(L"APP_FRAME", nullptr, WS_OVERLAPPEDWINDOW, WS_EX_COMPOSITED);
	SetMenu(win.m_hWnd, LoadMenu(hInstance, MAKEINTRESOURCE(IDC_APP)));

	const auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
	ui::set_icon(win.m_hWnd, icon);
	ShowWindow(win.m_hWnd, SW_SHOW);

	/*int argCount;
	const auto args = CommandLineToArgvW(GetCommandLine(), &argCount);

	if (argCount > 1)
	{
		win.load_doc(std::wstring{ str::unquote(args[1]) });
	}*/

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

		if (!::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
		{
			win.on_idle();
		}
	}

	return static_cast<int>(msg.wParam);
}

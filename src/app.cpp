// Rethinkify.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "text_view.h"
#include "document.h"
#include "ui.h"

constexpr wchar_t filters[] = L"All Files (*.*)\0*.*\0Text Files (*.txt)\0*.txt\0\0";

constexpr auto splitter_bar_width = 5;



COLORREF style_to_color(style style_index)
{
	switch (style_index)
	{
	case style::white_space:
		return ui::main_wnd_clr;
	case style::main_wnd_clr:
		return ui::main_wnd_clr;
	case style::tool_wnd_clr:
		return ui::tool_wnd_clr;
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
		if (uMsg == WM_INITDIALOG) return on_init_dialog(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return on_command(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_init_dialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		ui::center_window(m_hWnd);
		return 1;
	}

	LRESULT on_command(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
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


struct list_view_item
{
	static constexpr uint32_t style_top = 1 << 0;
	static constexpr uint32_t style_bottom = 1 << 1;
	static constexpr uint32_t style_folder = 1 << 2;

	std::wstring name;
	irect bounds;
	file_path path;
	uint32_t style;
};

class list_view : public ui::win_impl
{
public:

	IEvents& _events;

	std::vector<std::shared_ptr<list_view_item>> _results;
	std::shared_ptr<list_view_item> _selected_item;
	std::shared_ptr<list_view_item> _hover_item;
	HFONT _font = nullptr;
	HPEN _pen = nullptr;

	isize _extent;
	ipoint _offset;
	int _y_max = 0;
	int _y_tracking_start = 0;
	int _y_tracking_offset_start = 0;
	bool _highlight_scroll = false;
	bool _hover = false;
	bool _tracking = false;
	bool _trackingScrollbar = false;

	int _item_bullet_x = 10;
	int _item_padding_x = 4;
	int _item_padding_y = 4;
	

	list_view(IEvents& events) : _events(events)
	{
	}

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_create(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return on_size(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return on_paint(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEACTIVATE) return on_mouse_activate(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return on_left_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return on_left_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDBLCLK) return on_mouse_dbl_clk(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return on_mouse_move(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSELEAVE) return on_mouse_leave(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return on_mouse_wheel(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_create(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		_pen = CreatePen(PS_SOLID, 3, ui::line_color);
		return 0;
	}

	LRESULT on_size(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		_extent.cx = LOWORD(lParam);
		_extent.cy = HIWORD(lParam);
		layout_list();
		return 0;
	}

	LRESULT on_paint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		assert(::IsWindow(m_hWnd));

		if (wParam != 0)
		{
			on_paint((HDC)wParam);
		}
		else
		{
			const auto r = get_client_rect();

			PAINTSTRUCT ps;
			auto hPaintDc = BeginPaint(m_hWnd, &ps);
			auto hdc = CreateCompatibleDC(hPaintDc);
			auto hBitmap = CreateCompatibleBitmap(hPaintDc, r.Width(), r.Height());
			auto hOldBitmap = SelectObject(hdc, hBitmap);

			on_paint(hdc);
			BitBlt(hPaintDc, 0, 0, r.Width(), r.Height(), hdc, 0, 0, SRCCOPY);

			SelectObject(hdc, hOldBitmap);
			DeleteObject(hBitmap);
			DeleteDC(hdc);
			EndPaint(m_hWnd, &ps);
		}

		return 0;
	}

	LRESULT on_erase_background(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		return 1;
	}

	LRESULT on_mouse_activate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		return MA_NOACTIVATE;
	}

	void layout_list()
	{
		auto hdc = GetWindowDC(m_hWnd);
		auto old_font = SelectObject(hdc, _font);

		std::sort(_results.begin(), _results.end(), [](const std::shared_ptr<list_view_item> &l, const std::shared_ptr<list_view_item> &r) { return str::icmp(l->name, r->name) < 0; });

		int y = 16;
		int n = 0;

		for (auto i : _results)
		{
			if (i->bounds.Width() == _extent.cx)
			{
				irect bounds(0, y, _extent.cx, y + i->bounds.Height());
				i->bounds = bounds;
				y = bounds.bottom;
			}
			else
			{
				irect bounds(0, 0, _extent.cx - (_item_padding_x * 3) + _item_bullet_x, 100);
				DrawText(hdc, i->name.c_str(), i->name.length(), bounds, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_CALCRECT);

				bounds.left = 0;
				bounds.top = y;
				bounds.right = _extent.cx;
				bounds.bottom += y + (_item_padding_y * 2);

				i->bounds = bounds;
				y = bounds.bottom;
			}

			uint32_t style = i->style & ~(list_view_item::style_top | list_view_item::style_bottom);
			if (i == _results.front()) style |= list_view_item::style_top;
			if (i == _results.back()) style |= list_view_item::style_bottom;
			i->style = style;
		}

		_y_max = y + 64;
		SelectObject(hdc, old_font);
		ReleaseDC(m_hWnd, hdc);
	}

	void on_paint(HDC hdc)
	{
		const auto r = get_client_rect();
		ui::fill_solid_rect(hdc, r, ui::tool_wnd_clr);

		const auto old_font = SelectObject(hdc, _font);
		const auto old_pen =  SelectObject(hdc, _pen);
		SetBkMode(hdc, TRANSPARENT);

		for (auto i : _results)
		{
			auto bounds = i->bounds.Offset(-_offset);

			if (bounds.Intersects(r))
			{
				if (i == _hover_item)
				{
					ui::fill_solid_rect(hdc, bounds, ui::handle_hover_color);
				}

				if (i == _selected_item)
				{
					ui::fill_solid_rect(hdc, bounds, ui::handle_tracking_color);
				}

				const auto xx = bounds.left + _item_padding_x + (_item_bullet_x / 3);
				const auto xx2 = bounds.left + _item_padding_x + _item_bullet_x;
				const auto yy = (bounds.top + bounds.bottom) / 2;
				const auto y_top = (i->style & list_view_item::style_top) ? yy : bounds.top;
				const auto y_bottom = (i->style & list_view_item::style_bottom) ? yy : bounds.bottom;

				MoveToEx(hdc, xx, y_top, nullptr);
				LineTo(hdc, xx, y_bottom);
				MoveToEx(hdc, xx, yy, nullptr);
				LineTo(hdc, xx2, yy);

				bounds.left += _item_padding_x + _item_bullet_x;
				bounds = bounds.Inflate(-_item_padding_x, -_item_padding_y);

				SetTextColor(hdc, (i->style & list_view_item::style_folder) ? ui::folder_text_color : ui::text_color);
				DrawText(hdc, i->name.c_str(), i->name.length(), bounds, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
			}
		}

		DrawScroll(hdc, _highlight_scroll, _trackingScrollbar);

		SelectObject(hdc, old_font);
		SelectObject(hdc, old_pen);
	}

	bool CanScroll() const
	{
		return _y_max > _extent.cy;
	}

	void DrawScroll(HDC hdc, bool highlight, bool tracking)
	{
		if (CanScroll())
		{
			const auto y = MulDiv(_offset.y, _extent.cy, _y_max);
			const auto cy = MulDiv(_extent.cy, _extent.cy, _y_max);
			auto xPadding = 0;
			const auto right = _extent.cx;

			if (highlight || tracking)
			{
				ui::fill_solid_rect(hdc, irect(right - 26, 0, right, _extent.cy), ui::handle_color);
				xPadding = 10;
			}

			const auto c = tracking ? ui::handle_tracking_color : ui::handle_hover_color;
			ui::fill_solid_rect(hdc, irect(right - 12 - xPadding, y, right - 4, y + cy), c);
		}
	}

	std::shared_ptr<list_view_item> SelectionFromPoint(const ipoint& pt) const
	{
		for (auto i : _results)
		{
			if (i->bounds.Contains(pt))
				return i;
		}

		return nullptr;
	}

	void SetHover(std::shared_ptr<list_view_item> h)
	{
		if (_hover_item != h)
		{
			_hover_item = h;
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void select_item(const std::shared_ptr<list_view_item>& i)
	{
		if (_selected_item != i)
		{
			_selected_item = i;

			if (_selected_item)
			{
				_events.path_selected(i->path);				
			}

			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	LRESULT on_left_button_down(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		ipoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		SetFocus(m_hWnd);

		if (IsOverScrollbar(point))
		{
			if (!_tracking)
			{
				_y_tracking_start = point.y;
				_y_tracking_offset_start = _offset.y;

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

	LRESULT on_mouse_dbl_clk(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		ipoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		SetHover(SelectionFromPoint(point + _offset));

		if (_hover_item != nullptr)
		{
			//ShellExecute(m_hWnd, L"open", _hoverItem->Link.c_str(), L"", L"", SW_SHOWNORMAL);
		}
		return 0;
	}

	LRESULT on_mouse_move(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		ipoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

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
			auto offset = MulDiv(point.y - _y_tracking_start, _y_max, _extent.cy);
			ScrollTo(_y_tracking_offset_start + offset);
		}
		else
		{
			UpdateMousePos(point);
		}

		SetHover(SelectionFromPoint(point + _offset));
		return 0;
	}

	LRESULT on_left_button_up(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		if (_tracking)
		{
			_tracking = false;
			_trackingScrollbar = false;
			_y_tracking_start = 0;

			ReleaseCapture();
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}

		return 0;
	}

	LRESULT on_mouse_leave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		_hover_item = nullptr;
		_hover = false;
		_highlight_scroll = false;
		InvalidateRect(m_hWnd, nullptr, FALSE);
		return 0;
	}

	LRESULT on_mouse_wheel(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		int zDelta = ((short)HIWORD(wParam)) / 2;
		ScrollTo(_offset.y - zDelta);
		return 0;
	}

	bool IsOverScrollbar(const ipoint& point) const
	{
		return (_extent.cx - 32) < point.x;
	}

	void UpdateMousePos(const ipoint& point)
	{
		auto h = IsOverScrollbar(point);

		if (_highlight_scroll != h)
		{
			_highlight_scroll = h;
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void ScrollTo(int offset)
	{
		offset = clamp(offset, 0, _y_max - _extent.cy);

		if (_offset.y != offset)
		{
			_offset.y = offset;
			layout_list();
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

		_item_bullet_x = 10 * scale_factor;
		_item_padding_x = 5 * scale_factor;
		_item_padding_y = 5 * scale_factor;
	}

	void populate(const folder_contents& folder_contents, const file_path &selected_path)
	{
		std::vector<std::shared_ptr<list_view_item>> new_results;
		std::unordered_map<file_path, std::shared_ptr<list_view_item>, ihash, ieq> existing;
		std::shared_ptr<list_view_item> new_selected_item;

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
				auto i = std::make_shared<list_view_item>();
				i->name = f.path.name();
				i->path = f.path;
				new_results.push_back(i);
			}

			if (selected_path == new_results.back()->path)
			{
				new_selected_item = new_results.back();
			}
		}

		for (const auto& f : folder_contents.folders)
		{
			const auto found = existing.find(f.path);

			if (found != existing.end())
			{
				new_results.push_back(found->second);
			}
			else
			{
				auto i = std::make_shared<list_view_item>();
				i->name = std::format(L"{}\\", f.path.name());
				i->path = f.path;
				new_results.push_back(i);
			}

			new_results.back()->style |= list_view_item::style_folder;

			if (selected_path == new_results.back()->path)
			{
				new_selected_item = new_results.back();
			}
		}

		std::swap(_results, new_results);
		_selected_item = new_selected_item;

		layout_list();
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
};


class main_win : public ui::win_impl, public IEvents
{
public:
	text_view _view;
	list_view _list;
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
		if (uMsg == WM_CREATE) return on_create(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return on_paint(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return on_size(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return on_focus(uMsg, wParam, lParam);
		if (uMsg == WM_CLOSE) return on_close(uMsg, wParam, lParam);
		if (uMsg == WM_INITMENUPOPUP) return on_init_menu_popup(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return on_command(uMsg, wParam, lParam);
		if (uMsg == WM_DPICHANGED) return on_window_dpi_changed(uMsg, wParam, lParam);

		switch (uMsg)
		{
		case WM_LBUTTONDOWN:
		{
			const auto rect = get_client_rect();
			const auto x_pos = GET_X_LPARAM(lParam);
			const auto y_pos = GET_Y_LPARAM(lParam);
			const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);

			_is_tracking_splitter = (x_pos > split_pos - splitter_bar_width &&
				x_pos < split_pos + splitter_bar_width);

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
			const auto x_pos = GET_X_LPARAM(lParam);
			const auto y_pos = GET_Y_LPARAM(lParam);
			const auto rect = get_client_rect();

			if (wParam == MK_LBUTTON)
			{
				if (_is_tracking_splitter)
				{
					_split_ratio = (x_pos - rect.left) / static_cast<double>(rect.right - rect.left);
					if (_split_ratio < 0.05) _split_ratio = 0.05;
					if (_split_ratio > 0.95) _split_ratio = 0.95;
					InvalidateRect(hWnd, nullptr, FALSE);
					layout_views();
				}
			}

			const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);
			const auto new_hover_splitter = (x_pos > (split_pos - splitter_bar_width) &&
				(x_pos < (split_pos + splitter_bar_width)));

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

	LRESULT on_create(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		_view.create(L"TEXT_FRAME", m_hWnd, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN);
		_list.create(L"LIST_FRAME", m_hWnd, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
		_find.create(L"FINDER_FRAME", m_hWnd, WS_CHILD, WS_EX_COMPOSITED);
		const auto monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		_scale_factor = GetScalingFactorFromDPI(GetPerMonitorDPI(monitor));
		_view.update_font(_scale_factor);
		_find.update_font(_scale_factor);
		_list.update_font(_scale_factor);
		_doc.invalidate(invalid::view);
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

	LRESULT on_paint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		PAINTSTRUCT ps = { nullptr };
		auto hdc = BeginPaint(m_hWnd, &ps);

		const auto bg_brush = CreateSolidBrush(ui::main_wnd_clr);
		const auto bounds = get_client_rect();

		auto c = ui::handle_color;
		if (_is_hover_splitter) c = ui::handle_hover_color;
		if (_is_tracking_splitter) c = ui::handle_tracking_color;
		const auto splitter_brush = CreateSolidBrush(c);

		const auto split_pos = static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio);

		const RECT splitter_rect = {
			split_pos - splitter_bar_width, bounds.top,
			split_pos + splitter_bar_width, bounds.bottom
		};

		const RECT client_area1 = {
			bounds.left, bounds.top,
			split_pos - splitter_bar_width, bounds.bottom
		};

		const RECT client_area2 = {
			split_pos + splitter_bar_width, bounds.top,
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

	static LRESULT on_erase_background(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		return 1;
	}

	void layout_views() const
	{
		const auto is_find_visible = IsWindowVisible(_find.m_hWnd) != 0;
		const auto find_height = is_find_visible ? 40 : 0;
		const auto bounds = get_client_rect();
		const auto split_pos = static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio);

		auto text_bounds = bounds;
		text_bounds.left = split_pos + splitter_bar_width;
		text_bounds.bottom -= find_height;
		_view.move_window(text_bounds);

		auto list_bounds = bounds;
		list_bounds.right = split_pos - splitter_bar_width;
		_list.move_window(list_bounds);

		auto find_bounds = bounds;
		find_bounds.left = split_pos + splitter_bar_width;
		find_bounds.top = find_bounds.bottom - find_height;
		_find.move_window(find_bounds);
	}

	LRESULT on_size(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		layout_views();
		return 0;
	}

	LRESULT on_focus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		SetFocus(_view.m_hWnd);
		return 0;
	}

	LRESULT on_close(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
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

	LRESULT on_about() const
	{
		about_dlg().do_modal(m_hWnd, IDD_ABOUTBOX);
		return 0;
	}

	LRESULT on_exit() const
	{
		PostMessage(m_hWnd, WM_CLOSE, 0, 0);
		return 0;
	}

	LRESULT on_run_tests()
	{
		_doc.select(_doc.all());

		undo_group ug(_doc);
		const auto pos = _doc.delete_text(ug, _doc.selection());
		_doc.insert_text(ug, pos, run_all_tests());
		_doc.select(text_location());
		_doc.path({ L"test-results.md" });
		_doc.invalidate(invalid::view);
		return 0;
	}

	LRESULT on_open()
	{
		load_doc();
		return 0;
	}

	LRESULT on_save()
	{
		save_doc(_doc.path());
		return 0;
	}

	LRESULT on_save_as()
	{
		save_doc();
		return 0;
	}

	LRESULT on_new()
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

	LRESULT on_edit_find() const
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
		layout_views();
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

	LRESULT on_edit_reformat()
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
			const auto text = str::utf16_to_utf8(_doc.str());
			const auto options = "-A1tOP";

			// call the Artistic Style formatting function
			const auto textOut = AStyleMain(text.c_str(),
				options,
				ASErrorHandler,
				ASMemoryAlloc);

			undo_group ug(_doc);
			_doc.select(_doc.replace_text(ug, _doc.all(), str::utf8_to_utf16(textOut)));

			delete[] textOut;
		}

		return 0;
	}

	LRESULT on_edit_remove_duplicates()
	{
		auto lines = _doc.lines();

		std::sort(lines.begin(), lines.end());
		lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

		undo_group ug(_doc);
		_doc.select(_doc.replace_text(ug, _doc.all(), document::combine_line_text(lines)));

		return 0;
	}


	LRESULT on_init_menu_popup(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
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

	LRESULT on_command(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto id = LOWORD(wParam);

		if (id == ID_APP_EXIT) return on_exit();
		if (id == ID_APP_ABOUT) return on_about();
		if (id == ID_HELP_RUNTESTS) return on_run_tests();
		if (id == ID_FILE_OPEN) return on_open();
		if (id == ID_FILE_SAVE) return on_save();
		if (id == ID_FILE_SAVE_AS) return on_save_as();
		if (id == ID_FILE_NEW) return on_new();
		if (id == ID_EDIT_FIND) return on_edit_find();
		if (id == ID_EDIT_REFORMAT) return on_edit_reformat();
		if (id == ID_EDIT_SORTANDREMOVEDUPLICATES) return on_edit_remove_duplicates();

		_view.on_command(id);
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
		_doc.path({ L"New" });
		_doc.invalidate(invalid::view);
	}

	void load_doc(const file_path &path)
	{
		if (_doc.load_from_file(path))
		{
			// populate the folder list
			_list.populate(iterate_file_items({ path.folder() }, false), path);
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

		if (invalids & invalid::invalidate)
		{
			_view.invalidate();
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
	win.create(L"APP_FRAME", nullptr, WS_OVERLAPPEDWINDOW, WS_EX_COMPOSITED);
	SetMenu(win.m_hWnd, LoadMenu(hInstance, MAKEINTRESOURCE(IDC_APP)));

	const auto icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
	ui::set_icon(win.m_hWnd, icon);
	ShowWindow(win.m_hWnd, SW_SHOW);

	if (!str::is_empty(lpCmdLine))
	{
		const auto path = file_path{ str::unquote(lpCmdLine) };

		if (path.exists())
		{
			win.load_doc(path);
		}
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

		if (!::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
		{
			win.on_idle();
		}
	}

	return static_cast<int>(msg.wParam);
}

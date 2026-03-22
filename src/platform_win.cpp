// platform_win.cpp — Win32 platform layer: entry point, windowing, timers,
// resources, menus, device context, file dialogs, and spell checking

#include "pch.h"

// Windows Header Files:
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ole2.h>
#include <spellcheck.h>

#include <functional>
#include <map>
#include <vector>
#include <string>
#include <optional>

#include "platform.h"

#pragma comment(lib, "shlwapi.lib")

using namespace std::string_view_literals;

// irect and RECT have identical binary layout (4 x int32_t)
inline RECT& as_rect(irect& r) { return reinterpret_cast<RECT&>(r); }
inline const RECT& as_rect(const irect& r) { return reinterpret_cast<const RECT&>(r); }

constexpr uint32_t xrgb(const uint32_t r, const uint32_t g, const uint32_t b)
{
	return (r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16) | 0xff000000;
}


bool file_path::exists() const
{
	const auto attribs = ::GetFileAttributes(_path.c_str());
	return attribs != INVALID_FILE_ATTRIBUTES &&
		(attribs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

file_path file_path::module_folder()
{
	wchar_t raw_path[MAX_PATH];
	::GetModuleFileName(nullptr, raw_path, MAX_PATH);
	return file_path{file_path(raw_path).folder()};
}

file_path file_path::app_data_folder()
{
	wchar_t raw_path[MAX_PATH];
	::SHGetSpecialFolderPath(GetActiveWindow(), raw_path, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, TRUE);

	auto result = file_path(raw_path).combine(g_app_name);
	const auto attribs = ::GetFileAttributes(result.c_str());
	if (attribs == INVALID_FILE_ATTRIBUTES)
		::CreateDirectory(result.c_str(), nullptr);
	return {result};
}

// ── Globals ────────────────────────────────────────────────────────────────────

HINSTANCE resource_instance = nullptr;

static HWND g_hWnd = nullptr;
static LARGE_INTEGER g_perfFreq;
static LARGE_INTEGER g_perfStart;
static HMENU g_hMenu = nullptr;
static HACCEL g_hAccel = nullptr;
static std::vector<pf::menu_command> g_menuDef;
static int g_nCmdShow = SW_SHOW;

// Look up a pf::menu_command by ID and invoke its action
static bool dispatch_menu_command(const std::vector<pf::menu_command>& items, const int cmd_id)
{
	for (const auto& item : items)
	{
		if (item.id == cmd_id && item.action)
		{
			if (item.is_enabled && !item.is_enabled())
				return true; // consumed but not executed
			item.action();
			return true;
		}
		if (!item.children.empty() && dispatch_menu_command(item.children, cmd_id))
			return true;
	}
	return false;
}

// Find pf::menu_command by ID in the global menu definition tree
static const pf::menu_command* find_menu_command(const std::vector<pf::menu_command>& items,
                                                 const int cmd_id)
{
	for (const auto& item : items)
	{
		if (item.id == cmd_id)
			return &item;
		if (!item.children.empty())
		{
			if (const auto* found = find_menu_command(item.children, cmd_id))
				return found;
		}
	}
	return nullptr;
}

// Apply is_enabled/is_checked from pf::menu_command callbacks to HMENU items
static void apply_menu_state(const HMENU hMenu)
{
	const auto count = GetMenuItemCount(hMenu);
	for (int i = 0; i < count; i++)
	{
		const auto id = GetMenuItemID(hMenu, i);
		if (id == static_cast<UINT>(-1))
			continue; // separator or submenu

		if (const auto* cmd = find_menu_command(g_menuDef, static_cast<int>(id)))
		{
			if (cmd->is_enabled)
				EnableMenuItem(hMenu, i, MF_BYPOSITION | (cmd->is_enabled() ? MF_ENABLED : MF_DISABLED));
			if (cmd->is_checked)
				CheckMenuItem(hMenu, i, MF_BYPOSITION | (cmd->is_checked() ? MF_CHECKED : MF_UNCHECKED));
		}
	}
}

// Build a Win32 accelerator table at runtime from menu key bindings
static void build_runtime_accelerators()
{
	std::vector<ACCEL> accels;

	std::function<void(const std::vector<pf::menu_command>&)> collect;
	collect = [&](const std::vector<pf::menu_command>& items)
	{
		for (const auto& item : items)
		{
			if (!item.accel.empty() && item.id != 0)
			{
				ACCEL a = {};
				a.cmd = static_cast<WORD>(item.id);
				a.fVirt = FVIRTKEY | FNOINVERT;
				if (item.accel.modifiers & pf::key_mod::ctrl) a.fVirt |= FCONTROL;
				if (item.accel.modifiers & pf::key_mod::shift) a.fVirt |= FSHIFT;
				if (item.accel.modifiers & pf::key_mod::alt) a.fVirt |= FALT;
				a.key = static_cast<WORD>(item.accel.key);
				accels.push_back(a);
			}
			if (!item.children.empty())
				collect(item.children);
		}
	};

	collect(g_menuDef);

	if (g_hAccel)
	{
		DestroyAcceleratorTable(g_hAccel);
		g_hAccel = nullptr;
	}

	if (!accels.empty())
		g_hAccel = CreateAcceleratorTable(accels.data(), static_cast<int>(accels.size()));
}


inline SIZE measure_toolbar(const HWND tb)
{
	SIZE result{0, 0};
	const auto count = static_cast<int>(::SendMessage(tb, TB_BUTTONCOUNT, 0, 0L));

	for (int i = 0; i < count; ++i)
	{
		RECT r;
		if (static_cast<BOOL>(::SendMessage(tb, TB_GETITEMRECT, i, (LPARAM)&r)))
		{
			result.cx = std::max(r.right, result.cx);
			result.cy = std::max(r.bottom, result.cy);
		}
	}

	return result;
}


class win_dc
{
public:
	HDC _hdc;
	HWND _hwnd;

	win_dc(const HWND hwnd) : _hdc(GetDC(hwnd)), _hwnd(hwnd)
	{
	}

	~win_dc()
	{
		ReleaseDC(_hwnd, _hdc);
	}

	operator HDC() const
	{
		return _hdc;
	}

	HFONT SelectFont(const HFONT hFont) const
	{
		return static_cast<HFONT>(SelectObject(_hdc, hFont));
	}
};

class win
{
public:
	HWND m_hWnd = nullptr;

	irect get_client_rect() const
	{
		irect result;
		GetClientRect(m_hWnd, &as_rect(result));
		return result;
	}

	void move_window(const irect& bounds) const
	{
		MoveWindow(m_hWnd, bounds.left, bounds.top, bounds.Width(), bounds.Height(), TRUE);
	}


	void create_control(const LPCWSTR class_name, const HWND parent, const uint32_t style,
	                    const uint32_t exstyle = 0,
	                    const uintptr_t id = 0)
	{
		m_hWnd = CreateWindowEx(
			exstyle,
			class_name,
			nullptr,
			style,
			0, 0, 0, 0,
			parent,
			std::bit_cast<HMENU>(id),
			resource_instance,
			this);
	}
};

// Map Win32 WM_* to pf::message_type
static std::optional<pf::message_type> map_message(const UINT uMsg)
{
	switch (uMsg)
	{
	case WM_CREATE: return pf::message_type::create;
	case WM_DESTROY: return pf::message_type::destroy;
	case WM_SETFOCUS: return pf::message_type::set_focus;
	case WM_KILLFOCUS: return pf::message_type::kill_focus;
	case WM_ERASEBKGND: return pf::message_type::erase_background;
	case WM_TIMER: return pf::message_type::timer;
	case WM_SYSCOLORCHANGE: return pf::message_type::sys_color_change;
	case WM_LBUTTONDBLCLK: return pf::message_type::left_button_dbl_clk;
	case WM_LBUTTONDOWN: return pf::message_type::left_button_down;
	case WM_RBUTTONDOWN: return pf::message_type::right_button_down;
	case WM_LBUTTONUP: return pf::message_type::left_button_up;
	case WM_MOUSEMOVE: return pf::message_type::mouse_move;
	case WM_MOUSEWHEEL: return pf::message_type::mouse_wheel;
	case WM_MOUSELEAVE: return pf::message_type::mouse_leave;
	case WM_CHAR: return pf::message_type::char_input;
	case WM_KEYDOWN: return pf::message_type::key_down;
	case WM_CONTEXTMENU: return pf::message_type::context_menu;
	case WM_COMMAND: return pf::message_type::command;
	case WM_CLOSE: return pf::message_type::close;
	case WM_DPICHANGED: return pf::message_type::dpi_changed;
	case WM_INITDIALOG: return pf::message_type::init_dialog;
	case WM_SETCURSOR: return pf::message_type::set_cursor_msg;
	case WM_MOUSEACTIVATE: return pf::message_type::mouse_activate;
	default: return std::nullopt;
	}
}

// Map platform window styles to Win32 WS_* styles
static DWORD map_window_style(const uint32_t style)
{
	DWORD ws = 0;
	if (style & pf::window_style::child) ws |= WS_CHILD;
	if (style & pf::window_style::visible) ws |= WS_VISIBLE;
	if (style & pf::window_style::clip_children) ws |= WS_CLIPCHILDREN;
	return ws;
}

static DWORD map_window_ex_style(const uint32_t style)
{
	DWORD ws = 0;
	if (style & pf::window_style::composited) ws |= WS_EX_COMPOSITED;
	return ws;
}

// Map font_name enum to Win32 font family name
static const wchar_t* map_font_name(const pf::font_name name)
{
	switch (name)
	{
	case pf::font_name::consolas: return L"Consolas";
	case pf::font_name::arial: return L"Arial";
	case pf::font_name::calibri: return L"Calibri";
	}
	return L"Consolas";
}

static HFONT create_platform_font(const pf::font& f)
{
	return ::CreateFont(
		-f.size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
		map_font_name(f.name));
}

// Font cache — keyed on (size, font_name), HFONTs live until process exit
static std::map<std::pair<int, pf::font_name>, HFONT> s_font_cache;

static HFONT get_cached_font(const pf::font& f)
{
	const auto key = std::make_pair(f.size, f.name);
	const auto it = s_font_cache.find(key);
	if (it != s_font_cache.end())
		return it->second;
	const auto hfont = create_platform_font(f);
	s_font_cache[key] = hfont;
	return hfont;
}

// Win32 draw_context implementation
// Caches fonts and DC state — only issues GDI calls when values actually change.
// Original DC state is restored in the destructor.
class win_draw_context final : public pf::draw_context
{
	HDC _hdc;
	irect _clip;

	// Current DC state
	COLORREF _text_color;
	COLORREF _bk_color;
	mutable HFONT _font = nullptr;
	HBRUSH _fill_brush = nullptr;
	uint32_t _fill_color = 0;

	// Original DC state (restored in destructor)
	COLORREF _orig_text_color;
	COLORREF _orig_bk_color;
	HFONT _orig_font;

	void select_font(const pf::font& f) const
	{
		const auto hfont = get_cached_font(f);
		if (hfont != _font)
		{
			SelectObject(_hdc, hfont);
			_font = hfont;
		}
	}

	void select_text_color(const uint32_t color)
	{
		if (color != _text_color)
		{
			SetTextColor(_hdc, color);
			_text_color = color;
		}
	}

	void select_bk_color(const uint32_t color)
	{
		if (color != _bk_color)
		{
			SetBkColor(_hdc, color);
			_bk_color = color;
		}
	}

public:
	explicit win_draw_context(const HDC hdc, const irect& clip) : _hdc(hdc), _clip(clip)
	{
		_orig_text_color = _text_color = GetTextColor(_hdc);
		_orig_bk_color = _bk_color = GetBkColor(_hdc);
		_orig_font = _font = static_cast<HFONT>(GetCurrentObject(_hdc, OBJ_FONT));
	}

	~win_draw_context() override
	{
		if (_fill_brush) DeleteObject(_fill_brush);
		if (_text_color != _orig_text_color) SetTextColor(_hdc, _orig_text_color);
		if (_bk_color != _orig_bk_color) SetBkColor(_hdc, _orig_bk_color);
		if (_font != _orig_font) SelectObject(_hdc, _orig_font);
	}

	irect clip_rect() const override { return _clip; }

	void fill_solid_rect(const irect& rc, const color_t color) override
	{
		const auto c = color.rgb();
		if (!_fill_brush || c != _fill_color)
		{
			if (_fill_brush) DeleteObject(_fill_brush);
			_fill_brush = CreateSolidBrush(c);
			_fill_color = c;
		}
		FillRect(_hdc, &as_rect(rc), _fill_brush);
	}

	void fill_solid_rect(const int x, const int y, const int cx, const int cy, const color_t color) override
	{
		fill_solid_rect(irect(x, y, x + cx, y + cy), color);
	}

	void draw_text(const int x, const int y, const irect& clip, const std::wstring_view text,
	               const pf::font& f, const color_t text_color, const color_t bg_color) override
	{
		select_font(f);
		select_text_color(text_color.rgb());
		select_bk_color(bg_color.rgb());
		const RECT rc = {clip.left, clip.top, clip.right, clip.bottom};
		::ExtTextOut(_hdc, x, y, ETO_CLIPPED | ETO_OPAQUE, &rc, text.data(), static_cast<UINT>(text.size()), nullptr);
	}

	isize measure_text(const std::wstring_view text, const pf::font& f) const override
	{
		select_font(f);
		SIZE sz;
		::GetTextExtentPoint32(_hdc, text.data(), static_cast<int>(text.size()), &sz);
		return {sz.cx, sz.cy};
	}

	void draw_lines(const std::span<const ipoint> points, const color_t color) override
	{
		if (points.size() < 2) return;
		const auto pen = CreatePen(PS_SOLID, 1, color.rgb());
		const auto old_pen = SelectObject(_hdc, pen);
		MoveToEx(_hdc, points[0].x, points[0].y, nullptr);
		for (size_t i = 1; i < points.size(); ++i)
		{
			LineTo(_hdc, points[i].x, points[i].y);
		}
		SelectObject(_hdc, old_pen);
		DeleteObject(pen);
	}
};

// Win32 measure_context implementation
class win_measure_context : public pf::measure_context
{
	HDC _hdc;
	mutable HFONT _font = nullptr;
	HFONT _orig_font;

	void select_font(const pf::font& f) const
	{
		const auto hfont = get_cached_font(f);
		if (hfont != _font)
		{
			SelectObject(_hdc, hfont);
			_font = hfont;
		}
	}

public:
	explicit win_measure_context(const HDC hdc) : _hdc(hdc)
	{
		_orig_font = _font = static_cast<HFONT>(GetCurrentObject(_hdc, OBJ_FONT));
	}

	~win_measure_context() override
	{
		if (_font != _orig_font) SelectObject(_hdc, _orig_font);
	}

	isize measure_text(const std::wstring_view text, const pf::font& f) const override
	{
		select_font(f);
		SIZE sz;
		::GetTextExtentPoint32(_hdc, text.data(), static_cast<int>(text.size()), &sz);
		return {sz.cx, sz.cy};
	}

	isize measure_char(const pf::font& f) const override
	{
		select_font(f);
		TEXTMETRIC tm;
		::GetTextMetrics(_hdc, &tm);
		return {tm.tmAveCharWidth, tm.tmHeight + tm.tmExternalLeading};
	}
};

// Win32 measure_context that owns its HDC (for use outside paint)
class win_measure_context_owned final : public win_measure_context
{
	HWND _hwnd;
	HDC _owned_hdc;

public:
	win_measure_context_owned(const HWND hwnd, const HDC hdc) : win_measure_context(hdc), _hwnd(hwnd), _owned_hdc(hdc)
	{
	}

	~win_measure_context_owned() override { ReleaseDC(_hwnd, _owned_hdc); }
};


#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lParam)	((int)(short)LOWORD(lParam))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lParam)	((int)(short)HIWORD(lParam))
#endif

class win_impl final : public win, public pf::window_frame
{
	pf::frame_reactor_ptr _reactor;
	pf::window_frame_ptr _self_ref; // cleared on WM_DESTROY

	// Cached back buffer for flicker-free painting
	HDC _hdc_back = nullptr;
	HBITMAP _hbm_back = nullptr;
	HGDIOBJ _hbm_back_old = nullptr;
	int _back_cx = 0;
	int _back_cy = 0;

	void ensure_back_buffer(const HDC hdc, const int cx, const int cy)
	{
		if (_hdc_back && _back_cx == cx && _back_cy == cy)
			return;

		discard_back_buffer();

		_hdc_back = CreateCompatibleDC(hdc);
		_hbm_back = CreateCompatibleBitmap(hdc, cx, cy);
		_hbm_back_old = SelectObject(_hdc_back, _hbm_back);
		_back_cx = cx;
		_back_cy = cy;
	}

	void discard_back_buffer()
	{
		if (_hdc_back)
		{
			SelectObject(_hdc_back, _hbm_back_old);
			DeleteObject(_hbm_back);
			DeleteDC(_hdc_back);
			_hdc_back = nullptr;
			_hbm_back = nullptr;
			_hbm_back_old = nullptr;
			_back_cx = _back_cy = 0;
		}
	}

public:
	~win_impl() override
	{
		discard_back_buffer();

		if (IsWindow(m_hWnd))
		{
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
		}
	}

	void set_self_ref(const pf::window_frame_ptr& ref) { _self_ref = ref; }
	pf::window_frame_ptr self() { return _self_ref; }

	// ── window_frame implementation ──

	void set_reactor(pf::frame_reactor_ptr reactor) override
	{
		_reactor = std::move(reactor);
	}

	void notify_size() override
	{
		if (!_reactor || !IsWindow(m_hWnd)) return;
		RECT rc;
		GetClientRect(m_hWnd, &rc);
		const isize extent(rc.right - rc.left, rc.bottom - rc.top);
		const win_dc hdc(m_hWnd);
		win_measure_context measure_ctx(hdc);
		auto self = _self_ref;
		_reactor->on_size(self, extent, measure_ctx);
	}

	irect get_client_rect() const override
	{
		irect result;
		GetClientRect(m_hWnd, &as_rect(result));
		return result;
	}

	void invalidate() override
	{
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	void invalidate_rect(const irect& rect) override
	{
		InvalidateRect(m_hWnd, &as_rect(const_cast<irect&>(rect)), FALSE);
	}

	void set_focus() override
	{
		SetFocus(m_hWnd);
	}

	bool has_focus() const override
	{
		return GetFocus() == m_hWnd;
	}

	void set_capture() override
	{
		SetCapture(m_hWnd);
	}

	void release_capture() override
	{
		ReleaseCapture();
	}

	uint32_t set_timer(const uint32_t id, const uint32_t ms) override
	{
		return static_cast<uint32_t>(SetTimer(m_hWnd, id, ms, nullptr));
	}

	void kill_timer(const uint32_t id) override
	{
		KillTimer(m_hWnd, id);
	}

	ipoint screen_to_client(const ipoint pt) const override
	{
		POINT p = {pt.x, pt.y};
		ScreenToClient(m_hWnd, &p);
		return {p.x, p.y};
	}

	void set_cursor_shape(const pf::cursor_shape shape) override
	{
		LPCWSTR id = IDC_ARROW;
		switch (shape)
		{
		case pf::cursor_shape::arrow: id = IDC_ARROW;
			break;
		case pf::cursor_shape::ibeam: id = IDC_IBEAM;
			break;
		case pf::cursor_shape::size_we: id = IDC_SIZEWE;
			break;
		case pf::cursor_shape::size_ns: id = IDC_SIZENS;
			break;
		}
		SetCursor(::LoadCursor(nullptr, id));
	}

	void move_window(const irect& bounds) override
	{
		MoveWindow(m_hWnd, bounds.left, bounds.top, bounds.Width(), bounds.Height(), TRUE);
	}

	void show(const bool visible) override
	{
		ShowWindow(m_hWnd, visible ? SW_SHOW : SW_HIDE);
	}

	bool is_visible() const override
	{
		return IsWindowVisible(m_hWnd) != 0;
	}

	void set_text(const std::wstring_view text) override
	{
		::SetWindowText(m_hWnd, text.data());
	}

	std::wstring text_from_clipboard() override
	{
		std::wstring result;
		if (OpenClipboard(m_hWnd))
		{
			const auto hData = GetClipboardData(CF_UNICODETEXT);
			if (hData)
			{
				const auto pszData = static_cast<const wchar_t*>(GlobalLock(hData));
				if (pszData)
				{
					result = pszData;
					GlobalUnlock(hData);
				}
			}
			CloseClipboard();
		}
		return result;
	}

	bool text_to_clipboard(const std::wstring_view text) override
	{
		bool success = false;
		if (OpenClipboard(m_hWnd))
		{
			EmptyClipboard();
			const auto len = text.size() + 1;
			const auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));
			if (hData)
			{
				const auto pszData = static_cast<wchar_t*>(GlobalLock(hData));
				wcsncpy_s(pszData, len, text.data(), text.size());
				GlobalUnlock(hData);
				success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
			}
			CloseClipboard();
		}
		return success;
	}

	placement get_placement() const override
	{
		WINDOWPLACEMENT wp = {};
		wp.length = sizeof(wp);
		GetWindowPlacement(m_hWnd, &wp);
		placement p;
		std::memcpy(&p.normal_bounds, &wp.rcNormalPosition, sizeof(RECT));
		p.maximized = wp.showCmd == SW_SHOWMAXIMIZED || IsZoomed(m_hWnd);
		return p;
	}

	void set_placement(const placement& p) override
	{
		WINDOWPLACEMENT wp = {};
		wp.length = sizeof(wp);
		wp.showCmd = p.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
		std::memcpy(&wp.rcNormalPosition, &p.normal_bounds, sizeof(RECT));
		SetWindowPlacement(m_hWnd, &wp);
	}

	void track_mouse_leave() override
	{
		TRACKMOUSEEVENT tme = {};
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		TrackMouseEvent(&tme);
	}

	bool is_key_down(const unsigned int vk) const override
	{
		return (GetKeyState(vk) & 0x8000) != 0;
	}

	bool is_key_down_async(const unsigned int vk) const override
	{
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	pf::window_frame_ptr create_child(const std::wstring_view class_name,
	                                  const uint32_t style, const color_t background) const & override
	{
		auto child = std::make_shared<win_impl>();
		child->create_window(std::wstring(class_name).c_str(), m_hWnd, background,
		                     map_window_style(style), map_window_ex_style(style));
		child->set_self_ref(child);
		return child;
	}

	void close() override
	{
		DestroyWindow(m_hWnd);
	}

	int message_box(const std::wstring_view text, const std::wstring_view title, const uint32_t style) override
	{
		return ::MessageBox(m_hWnd, text.data(), title.data(), style);
	}

	void set_menu(std::vector<pf::menu_command> menu_def) override
	{
		pf::platform_set_menu(std::move(menu_def));
	}


	std::unique_ptr<pf::measure_context> create_measure_context() const override
	{
		HDC hdc = GetDC(m_hWnd);
		auto ctx = std::make_unique<win_measure_context_owned>(m_hWnd, hdc);
		return ctx;
	}

	void show_popup_menu(const std::vector<pf::menu_command>& items, const ipoint& screen_pt) override
	{
		if (items.empty()) return;

		// Assign temporary IDs and build a lookup table
		std::unordered_map<int, const pf::menu_command*> id_map;
		int next_id = 30000;

		std::function<HMENU(const std::vector<pf::menu_command>&)> build;
		build = [&](const std::vector<pf::menu_command>& cmds) -> HMENU
		{
			const HMENU hMenu = CreatePopupMenu();
			for (const auto& cmd : cmds)
			{
				if (cmd.text.empty() && cmd.children.empty())
				{
					AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
				}
				else if (!cmd.children.empty())
				{
					AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(build(cmd.children)),
					           cmd.text.c_str());
				}
				else
				{
					const int id = next_id++;
					id_map[id] = &cmd;

					UINT flags = MF_STRING;
					if (cmd.is_enabled && !cmd.is_enabled()) flags |= MF_GRAYED;
					if (cmd.is_checked && cmd.is_checked()) flags |= MF_CHECKED;

					AppendMenu(hMenu, flags, id, cmd.text.c_str());
				}
			}
			return hMenu;
		};

		const HMENU hMenu = build(items);
		const int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
		                               screen_pt.x, screen_pt.y, 0, m_hWnd, nullptr);
		if (sel != 0)
		{
			const auto it = id_map.find(sel);
			if (it != id_map.end() && it->second->action)
				it->second->action();
		}
		DestroyMenu(hMenu);
	}

	// ── Win32 message handling ──

	virtual LRESULT handle_message(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		if (!_reactor)
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		// Special-case WM_NCHITTEST: expand resize border for easier grabbing
		if (uMsg == WM_NCHITTEST && GetWindowLong(hWnd, GWL_STYLE) & WS_THICKFRAME)
		{
			const auto result = DefWindowProc(hWnd, uMsg, wParam, lParam);
			if (result == HTCLIENT)
			{
				RECT rc;
				GetWindowRect(hWnd, &rc);
				const int x = GET_X_LPARAM(lParam);
				const int y = GET_Y_LPARAM(lParam);
				constexpr int border = 6;

				const bool near_left = x < rc.left + border;
				const bool near_right = x >= rc.right - border;
				const bool near_top = y < rc.top + border;
				const bool near_bottom = y >= rc.bottom - border;

				if (near_top && near_left) return HTTOPLEFT;
				if (near_top && near_right) return HTTOPRIGHT;
				if (near_bottom && near_left) return HTBOTTOMLEFT;
				if (near_bottom && near_right) return HTBOTTOMRIGHT;
				if (near_left) return HTLEFT;
				if (near_right) return HTRIGHT;
				if (near_top) return HTTOP;
				if (near_bottom) return HTBOTTOM;
			}
			return result;
		}

		// Special-case WM_SETCURSOR: let DefWindowProc handle non-client cursors (resize arrows)
		if (uMsg == WM_SETCURSOR)
		{
			const auto hitTest = LOWORD(lParam);
			if (hitTest != HTCLIENT)
				return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		// Special-case WM_PAINT: double-buffered paint via cached offscreen bitmap
		if (uMsg == WM_PAINT)
		{
			PAINTSTRUCT ps;
			const HDC hdc = BeginPaint(m_hWnd, &ps);

			RECT rc;
			GetClientRect(m_hWnd, &rc);
			const int cx = rc.right - rc.left;
			const int cy = rc.bottom - rc.top;

			const irect clip(ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom);

			ensure_back_buffer(hdc, cx, cy);

			{
				win_draw_context draw_ctx(_hdc_back, clip);
				auto self = _self_ref;
				_reactor->on_paint(self, draw_ctx);
			}

			BitBlt(hdc, 0, 0, cx, cy, _hdc_back, 0, 0, SRCCOPY);

			EndPaint(m_hWnd, &ps);
			return 0;
		}

		// Special-case WM_SIZE: extract dimensions and call on_size
		if (uMsg == WM_SIZE)
		{
			const isize extent(LOWORD(lParam), HIWORD(lParam));
			const win_dc hdc(m_hWnd);
			win_measure_context measure_ctx(hdc);
			auto self = _self_ref;
			_reactor->on_size(self, extent, measure_ctx);
			return 0;
		}

		// Apply enable/check state when a menu is about to be shown
		if (uMsg == WM_INITMENUPOPUP)
		{
			apply_menu_state(reinterpret_cast<HMENU>(wParam));
			return 0;
		}

		// Dispatch menu/accelerator WM_COMMAND via pf::menu_command actions
		if (uMsg == WM_COMMAND && lParam == 0)
		{
			const int cmdId = LOWORD(wParam);
			if (dispatch_menu_command(g_menuDef, cmdId))
				return 0;
		}

		// Map to platform message_type and delegate to reactor
		const auto mt = map_message(uMsg);
		if (mt)
		{
			const auto self = _self_ref;
			if (self)
			{
				const auto result = _reactor->handle_message(self, *mt, wParam, lParam);

				if (uMsg == WM_DESTROY)
					PostQuitMessage(0);

				return result;
			}
		}

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	// ── Win32 window procedure ──

	static LRESULT CALLBACK win_proc(const HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		if (uMsg == WM_NCCREATE)
		{
			const auto pt = std::bit_cast<win_impl*>(std::bit_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
			const auto ptr = std::bit_cast<LONG_PTR>(std::bit_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, ptr);

			if (pt)
			{
				pt->m_hWnd = hwnd;
			}
		}

		if (uMsg == WM_INITDIALOG)
		{
			const auto pt = std::bit_cast<win_impl*>(lParam);
			const auto ptr = std::bit_cast<LONG_PTR>(lParam);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, ptr);

			if (pt)
			{
				pt->m_hWnd = hwnd;
			}
		}

		// get the pointer to the window
		const auto ptr = std::bit_cast<win_impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		if (ptr)
		{
			return ptr->handle_message(hwnd, uMsg, wParam, lParam);
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	// ── Window class registration & creation ──

	static bool register_class(const UINT style, const HICON hIcon, const HCURSOR hCursor,
	                           const uint32_t clr_background,
	                           const LPCWSTR lpszMenuName, const LPCWSTR lpszClassName, const HICON hIconSm)
	{
		WNDCLASSEX wcx;
		wcx.cbSize = sizeof(WNDCLASSEX);
		wcx.style = style;
		wcx.lpfnWndProc = win_proc;
		wcx.cbClsExtra = 0;
		wcx.cbWndExtra = 0;
		wcx.hInstance = resource_instance;
		wcx.hIcon = hIcon;
		wcx.hCursor = hCursor;
		wcx.hbrBackground = CreateSolidBrush(clr_background);
		wcx.lpszMenuName = lpszMenuName;
		wcx.lpszClassName = lpszClassName;
		wcx.hIconSm = hIconSm;

		if (RegisterClassEx(&wcx) == 0)
		{
			if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			{
				DeleteObject(wcx.hbrBackground);
				return false;
			}
			DeleteObject(wcx.hbrBackground);
		}

		return true;
	}

	void create_window(const LPCWSTR class_name, const HWND parent, const color_t wnd_clr, const uint32_t style,
	                   const uint32_t exstyle = 0,
	                   const uintptr_t id = 0)
	{
		const auto default_cursor = LoadCursor(nullptr, IDC_ARROW);
		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
		                   nullptr, default_cursor,
		                   wnd_clr.rgb(),
		                   nullptr, class_name, nullptr))
		{
			const bool is_child = (style & WS_CHILD) != 0;
			const int x = is_child ? 0 : CW_USEDEFAULT;
			const int y = is_child ? 0 : CW_USEDEFAULT;
			const int w = is_child ? 0 : CW_USEDEFAULT;
			const int h = is_child ? 0 : CW_USEDEFAULT;

			m_hWnd = CreateWindowEx(
				exstyle,
				class_name,
				nullptr,
				style,
				x, y, w, h,
				parent, std::bit_cast<HMENU>(id),
				resource_instance,
				this);
		}
	}
};

inline void set_font(const HWND h, HFONT f)
{
	SendMessage(h, WM_SETFONT, (WPARAM)f, 1);
}

inline void set_icon(const HWND h, HICON i)
{
	SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM)i);
	SendMessage(h, WM_SETICON, ICON_SMALL, (LPARAM)i);
}

inline DWORD get_style(const HWND m_hWnd)
{
	return static_cast<DWORD>(::GetWindowLong(m_hWnd, GWL_STYLE));
}

inline BOOL center_window(const HWND m_hWnd, HWND hWndCenter = nullptr) noexcept
{
	// determine owner window to center against
	const DWORD dwStyle = get_style(m_hWnd);
	if (hWndCenter == nullptr)
	{
		if (dwStyle & WS_CHILD)
			hWndCenter = GetParent(m_hWnd);
		else
			hWndCenter = GetWindow(m_hWnd, GW_OWNER);
	}

	// get coordinates of the window relative to its parent
	RECT rcDlg;
	GetWindowRect(m_hWnd, &rcDlg);
	RECT rcArea;
	RECT rcCenter;
	const HWND hWndParent = hWndCenter;
	if (!(dwStyle & WS_CHILD))
	{
		// don't center against invisible or minimized windows
		if (hWndCenter != nullptr)
		{
			const DWORD dwStyleCenter = ::GetWindowLong(hWndCenter, GWL_STYLE);
			if (!(dwStyleCenter & WS_VISIBLE) || dwStyleCenter & WS_MINIMIZE)
				hWndCenter = nullptr;
		}

		// center within screen coordinates
		HMONITOR hMonitor = nullptr;
		if (hWndCenter != nullptr)
		{
			hMonitor = MonitorFromWindow(hWndCenter, MONITOR_DEFAULTTONEAREST);
		}
		else
		{
			hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		}

		MONITORINFO minfo;
		minfo.cbSize = sizeof(MONITORINFO);
		BOOL bResult = ::GetMonitorInfo(hMonitor, &minfo);

		rcArea = minfo.rcWork;

		if (hWndCenter == nullptr)
			rcCenter = rcArea;
		else
			GetWindowRect(hWndCenter, &rcCenter);
	}
	else
	{
		// center within parent client coordinates
		GetClientRect(hWndParent, &rcArea);
		GetClientRect(hWndCenter, &rcCenter);
		MapWindowPoints(hWndCenter, hWndParent, (POINT*)&rcCenter, 2);
	}

	const int DlgWidth = rcDlg.right - rcDlg.left;
	const int DlgHeight = rcDlg.bottom - rcDlg.top;

	// find dialog's upper left based on rcCenter
	int xLeft = (rcCenter.left + rcCenter.right) / 2 - DlgWidth / 2;
	int yTop = (rcCenter.top + rcCenter.bottom) / 2 - DlgHeight / 2;

	// if the dialog is outside the screen, move it inside
	if (xLeft + DlgWidth > rcArea.right)
		xLeft = rcArea.right - DlgWidth;
	if (xLeft < rcArea.left)
		xLeft = rcArea.left;

	if (yTop + DlgHeight > rcArea.bottom)
		yTop = rcArea.bottom - DlgHeight;
	if (yTop < rcArea.top)
		yTop = rcArea.top;

	// map screen coordinates to child coordinates
	return SetWindowPos(m_hWnd, nullptr, xLeft, yTop, -1, -1,
	                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static std::wstring window_text(const HWND h)
{
	const auto len = ::GetWindowTextLength(h);
	if (len == 0) return {};
	std::wstring result(len + 1, 0);
	GetWindowText(h, result.data(), len + 1);
	result.resize(len, 0);
	return result;
}

// ── Platform API implementations ───────────────────────────────────────────

// ── Key binding formatting ─────────────────────────────────────────────────────

std::wstring pf::format_key_binding(const key_binding& kb)
{
	if (kb.empty()) return {};

	std::wstring result;

	if (kb.modifiers & key_mod::ctrl)
		result += L"Ctrl+";
	if (kb.modifiers & key_mod::alt)
		result += L"Alt+";
	if (kb.modifiers & key_mod::shift)
		result += L"Shift+";

	switch (kb.key)
	{
	case platform_key::Back: result += L"Backspace"; break;
	case platform_key::Tab: result += L"Tab"; break;
	case platform_key::Return: result += L"Enter"; break;
	case platform_key::Escape: result += L"Esc"; break;
	case platform_key::Space: result += L"Space"; break;
	case platform_key::Prior: result += L"Page Up"; break;
	case platform_key::Next: result += L"Page Down"; break;
	case platform_key::End: result += L"End"; break;
	case platform_key::Home: result += L"Home"; break;
	case platform_key::Left: result += L"Left"; break;
	case platform_key::Up: result += L"Up"; break;
	case platform_key::Right: result += L"Right"; break;
	case platform_key::Down: result += L"Down"; break;
	case platform_key::Insert: result += L"Ins"; break;
	case platform_key::Delete: result += L"Del"; break;
	case platform_key::F1: result += L"F1"; break;
	case platform_key::F3: result += L"F3"; break;
	case platform_key::F5: result += L"F5"; break;
	case platform_key::F6: result += L"F6"; break;
	case platform_key::F7: result += L"F7"; break;
	case platform_key::F8: result += L"F8"; break;
	case platform_key::F9: result += L"F9"; break;
	case platform_key::F10: result += L"F10"; break;
	default:
		if (kb.key >= 'A' && kb.key <= 'Z')
			result += static_cast<wchar_t>(kb.key);
		else if (kb.key >= '0' && kb.key <= '9')
			result += static_cast<wchar_t>(kb.key);
		else
			result += std::format(L"0x{:02X}", kb.key);
		break;
	}

	return result;
}

// ── Cursor position (global) ───────────────────────────────────────────────────

ipoint pf::platform_cursor_pos()
{
	POINT pt;
	GetCursorPos(&pt);
	return {pt.x, pt.y};
}

// ── Timer ──────────────────────────────────────────────────────────────────────


double pf::platform_get_time()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart - g_perfStart.QuadPart)
		/ static_cast<double>(g_perfFreq.QuadPart);
}

void pf::platform_sleep(const int milliseconds)
{
	Sleep(static_cast<DWORD>(milliseconds));
}

// ── Resource Loading ───────────────────────────────────────────────────────────

void* pf::platform_load_resource(const std::wstring_view name, const std::wstring_view type)
{
	LPCWSTR resType = type.data();
	if (type == L"BITMAP"sv)
		resType = RT_BITMAP;

	const HRSRC hResInfo = FindResource(nullptr, name.data(), resType);
	if (!hResInfo) return nullptr;

	const HGLOBAL hResData = LoadResource(nullptr, hResInfo);
	if (!hResData) return nullptr;

	return LockResource(hResData);
}

std::optional<pf::bitmap_data> pf::platform_load_bitmap_resource(const std::wstring_view resName)
{
	const auto pData = static_cast<const uint8_t*>(platform_load_resource(resName, L"BITMAP"));
	if (!pData) return std::nullopt;

	const auto bih = reinterpret_cast<const BITMAPINFOHEADER*>(pData);
	const int w = bih->biWidth;
	const int h = abs(bih->biHeight);
	const bool topDown = bih->biHeight < 0;
	const int bpp = bih->biBitCount;

	int paletteSize = 0;
	if (bpp <= 8)
		paletteSize = (bih->biClrUsed ? bih->biClrUsed : 1 << bpp) * static_cast<int>(sizeof(RGBQUAD));

	const uint8_t* pixelData = pData + bih->biSize + paletteSize;
	const RGBQUAD* palette = bpp <= 8 ? reinterpret_cast<const RGBQUAD*>(pData + bih->biSize) : nullptr;

	std::vector<uint32_t> pixels(w * h, 0);
	const int srcStride = (w * bpp + 31) / 32 * 4;

	for (int y = 0; y < h; y++)
	{
		const int srcY = topDown ? y : h - 1 - y;
		const uint8_t* srcRow = pixelData + srcY * srcStride;

		for (int x = 0; x < w; x++)
		{
			uint32_t c = 0;
			if (bpp == 24)
			{
				c = xrgb(srcRow[x * 3 + 2], srcRow[x * 3 + 1], srcRow[x * 3 + 0]);
			}
			else if (bpp == 32)
			{
				c = *reinterpret_cast<const uint32_t*>(srcRow + x * 4) | 0xFF000000;
			}
			else if (bpp == 8 && palette)
			{
				const auto& p = palette[srcRow[x]];
				c = xrgb(p.rgbRed, p.rgbGreen, p.rgbBlue);
			}
			else if (bpp == 4 && palette)
			{
				const uint8_t idx = x & 1 ? srcRow[x / 2] & 0x0F : srcRow[x / 2] >> 4;
				c = xrgb(palette[idx].rgbRed, palette[idx].rgbGreen, palette[idx].rgbBlue);
			}
			pixels[y * w + x] = c;
		}
	}

	return bitmap_data{w, h, pixels};
}

// ── Utility ────────────────────────────────────────────────────────────────────

void pf::platform_show_error(const std::wstring_view message, const std::wstring_view title)
{
	MessageBox(nullptr, message.data(), title.data(), MB_OK);
}

void pf::debug_trace(const std::wstring& msg)
{
#ifdef _DEBUG
	::OutputDebugString(msg.c_str());
	struct log_file
	{
		FILE* f = nullptr;
		log_file() { _wfopen_s(&f, L"debug_trace.log", L"w"); }
		~log_file() { if (f) fclose(f); }
	};
	static log_file log;
	if (log.f)
	{
		fwprintf(log.f, L"%s", msg.c_str());
		fflush(log.f);
	}
#endif
}

// ── Sound — WAV resource helpers ───────────────────────────────────────────────

// ── Menu & Accelerators ────────────────────────────────────────────────────────

static HMENU BuildPopupMenu(const std::vector<pf::menu_command>& items)
{
	const HMENU hMenu = CreatePopupMenu();
	for (auto& item : items)
	{
		if (item.text.empty() && item.children.empty())
			AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
		else if (!item.children.empty())
			AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPopupMenu(item.children)),
			           item.text.c_str());
		else
			AppendMenu(hMenu, MF_STRING, item.id, item.text.c_str());
	}
	return hMenu;
}

void pf::platform_set_menu(std::vector<menu_command> menuDef)
{
	g_menuDef = std::move(menuDef);
	g_hMenu = CreateMenu();
	for (auto& top : g_menuDef)
	{
		if (!top.children.empty())
			AppendMenu(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPopupMenu(top.children)),
			           top.text.c_str());
		else
			AppendMenu(g_hMenu, MF_STRING, top.id, top.text.c_str());
	}
	if (g_hWnd)
		SetMenu(g_hWnd, g_hMenu);

	build_runtime_accelerators();
}


bool pf::platform_events()
{
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return false;
		if (!TranslateAccelerator(g_hWnd, g_hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return true;
}

// ── Message Loop ───────────────────────────────────────────────────────────────

static CRITICAL_SECTION cs_async;
static CRITICAL_SECTION cs_ui;
static std::vector<std::function<void()>> async_tasks;
static std::vector<std::function<void()>> ui_tasks;
static HANDLE async_h = nullptr;
static HANDLE exit_h = nullptr;
static HANDLE ui_event_h = nullptr;

void pf::run_async(std::function<void()> task)
{
	EnterCriticalSection(&cs_async);
	async_tasks.push_back(std::move(task));
	LeaveCriticalSection(&cs_async);
	SetEvent(async_h);
}

void pf::run_ui(std::function<void()> task)
{
	EnterCriticalSection(&cs_ui);
	ui_tasks.push_back(std::move(task));
	LeaveCriticalSection(&cs_ui);
	SetEvent(ui_event_h);
}

static void run_ui_tasks()
{
	std::vector<std::function<void()>> tasks;
	EnterCriticalSection(&cs_ui);
	tasks.swap(ui_tasks);
	LeaveCriticalSection(&cs_ui);
	for (auto& t : tasks)
	{
		try { t(); }
		catch (const std::exception& e)
		{
			pf::debug_trace(L"UI task exception: " + str::utf8_to_utf16(e.what()) + L"\n");
		}
		catch (...)
		{
			pf::debug_trace(L"UI task: unknown exception\n");
		}
	}
}

static DWORD WINAPI async_thread_proc(LPVOID /*param*/)
{
	for (;;)
	{
		const HANDLE h[] = {async_h, exit_h};

		switch (WaitForMultipleObjects(2, h, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			{
				std::vector<std::function<void()>> tasks;
				EnterCriticalSection(&cs_async);
				tasks.swap(async_tasks);
				LeaveCriticalSection(&cs_async);
				for (auto& t : tasks)
				{
					try { t(); }
					catch (const std::exception& e)
					{
						pf::debug_trace(L"Async task exception: " + str::utf8_to_utf16(e.what()) + L"\n");
					}
					catch (...)
					{
						pf::debug_trace(L"Async task: unknown exception\n");
					}
				}
			}
			break;
		case WAIT_OBJECT_0 + 1:
			return 0;
		default:
			return 1;
		}
	}
}

static void init_handles()
{
	InitializeCriticalSection(&cs_async);
	InitializeCriticalSection(&cs_ui);
	async_h = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	exit_h = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	ui_event_h = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	QueryPerformanceFrequency(&g_perfFreq);
	QueryPerformanceCounter(&g_perfStart);
}

int pf::platform_run()
{
	MSG msg = {};
	HANDLE hAsyncThread = CreateThread(nullptr, 0, async_thread_proc, nullptr, 0, nullptr);
	int result = 0;

	for (;;)
	{
		const HANDLE h[] = {ui_event_h, exit_h};
		constexpr auto n = std::size(h);

		const auto wait = MsgWaitForMultipleObjects(n, h, FALSE, INFINITE, QS_ALLINPUT);

		if (wait == WAIT_OBJECT_0)
		{
			run_ui_tasks();
		}
		else if (wait == WAIT_OBJECT_0 + 1)
		{
			break;
		}
		else if (wait == WAIT_OBJECT_0 + 2)
		{
			// Windows message pending
		}
		else
		{
			result = 1;
			break;
		}

		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				result = static_cast<int>(msg.wParam);
				SetEvent(exit_h);
				goto cleanup;
			}

			if (!TranslateAccelerator(g_hWnd, g_hAccel, &msg))
			{
				TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		app_idle();
	}

cleanup:
	if (hAsyncThread)
	{
		SetEvent(exit_h);
		WaitForSingleObject(hAsyncThread, 5000);
		CloseHandle(hAsyncThread);
		hAsyncThread = nullptr;
	}

	CloseHandle(async_h);
	CloseHandle(exit_h);
	CloseHandle(ui_event_h);
	async_h = nullptr;
	exit_h = nullptr;
	ui_event_h = nullptr;

	DeleteCriticalSection(&cs_async);
	DeleteCriticalSection(&cs_ui);

	return result;
}

// ── File I/O ───────────────────────────────────────────────────────────────────


bool pf::platform_move_file_replace(const wchar_t* source, const wchar_t* dest)
{
	return MoveFileExW(source, dest,
	                   MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::wstring pf::platform_temp_file_path(const wchar_t* prefix)
{
	wchar_t dir[MAX_PATH + 1] = {0};
	GetTempPathW(MAX_PATH, dir);
	wchar_t result[MAX_PATH + 1] = {0};
	GetTempFileNameW(dir, prefix, 0, result);
	return result;
}

std::wstring pf::platform_last_error_message()
{
	std::wstring result;
	const auto error = GetLastError();
	if (error)
	{
		LPVOID lpMsgBuf = nullptr;
		const auto bufLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&lpMsgBuf),
			0, nullptr);
		if (bufLen)
		{
			result = static_cast<const wchar_t*>(lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
	}
	return result;
}

bool pf::platform_clipboard_has_text()
{
	return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

std::wstring pf::platform_text_from_clipboard()
{
	std::wstring result;
	if (OpenClipboard(nullptr))
	{
		const auto hData = GetClipboardData(CF_UNICODETEXT);
		if (hData)
		{
			const auto pszData = static_cast<const wchar_t*>(GlobalLock(hData));
			if (pszData)
			{
				result = pszData;
				GlobalUnlock(hData);
			}
		}
		CloseClipboard();
	}
	return result;
}

bool pf::platform_text_to_clipboard(const std::wstring_view text)
{
	bool success = false;
	if (OpenClipboard(nullptr))
	{
		EmptyClipboard();
		const auto len = text.size() + 1;
		const auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));
		if (hData)
		{
			const auto pszData = static_cast<wchar_t*>(GlobalLock(hData));
			wcsncpy_s(pszData, len, text.data(), text.size());
			GlobalUnlock(hData);
			success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
		}
		CloseClipboard();
	}
	return success;
}

// ── File system utilities ──────────────────────────────────────────────────────

// ── Configuration (INI file) ───────────────────────────────────────────────────

static std::wstring get_config_path()
{
	// Try next to the exe first
	wchar_t exe_path[MAX_PATH];
	::GetModuleFileName(nullptr, exe_path, MAX_PATH);
	auto ini_path = file_path(exe_path).without_extension() + L".ini";

	// Test if we can write to the exe directory
	const auto h = CreateFileW(ini_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
	                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h != INVALID_HANDLE_VALUE)
	{
		CloseHandle(h);
		return ini_path;
	}

	// Fall back to AppData\Local
	return file_path::app_data_folder().combine(file_path(exe_path).name()).without_extension() + L".ini";
}

std::wstring pf::config_read(const std::wstring_view section, const std::wstring_view key,
                             const std::wstring_view default_value)
{
	static const auto ini_path = get_config_path();
	wchar_t buf[4096];
	const auto len = GetPrivateProfileStringW(
		std::wstring(section).c_str(),
		std::wstring(key).c_str(),
		std::wstring(default_value).c_str(),
		buf, _countof(buf),
		ini_path.c_str());
	return {buf, len};
}

void pf::config_write(const std::wstring_view section, const std::wstring_view key, const std::wstring_view value)
{
	static const auto ini_path = get_config_path();
	WritePrivateProfileStringW(
		std::wstring(section).c_str(),
		std::wstring(key).c_str(),
		std::wstring(value).c_str(),
		ini_path.c_str());
}

bool pf::is_directory(const file_path& path)
{
	const auto attribs = ::GetFileAttributes(path.c_str());
	return attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring pf::current_directory()
{
	wchar_t buf[MAX_PATH];
	if (::GetCurrentDirectory(MAX_PATH, buf))
		return buf;
	return {};
}

static constexpr wchar_t default_filter[] = L"All Files (*.*)\0*.*\0Text Files (*.txt)\0*.txt\0\0";

file_path pf::open_file_path(const std::wstring_view title, const std::wstring_view filters)
{
	wchar_t szFile[MAX_PATH] = {};
	OPENFILENAME ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_hWnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title.data();
	ofn.lpstrFilter = filters.empty() ? default_filter : filters.data();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (::GetOpenFileName(&ofn))
		return file_path{szFile};
	return {};
}

file_path pf::save_file_path(const std::wstring_view title, const file_path& default_path,
                             const std::wstring_view filters)
{
	wchar_t szFile[MAX_PATH] = {};
	if (!default_path.empty())
		wcsncpy_s(szFile, default_path.c_str(), MAX_PATH - 1);

	OPENFILENAME ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_hWnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title.data();
	ofn.lpstrFilter = filters.empty() ? default_filter : filters.data();
	ofn.Flags = OFN_OVERWRITEPROMPT;

	if (::GetSaveFileName(&ofn))
		return file_path{szFile};
	return {};
}

// ── Platform locale ────────────────────────────────────────────────────────────

std::wstring pf::platform_language()
{
	wchar_t buf[LOCALE_NAME_MAX_LENGTH];
	if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH))
		return buf;
	return L"en-US";
}

// ── Platform Spell Checking (ISpellChecker) ────────────────────────────────────

class win_spell_checker final : public pf::spell_checker
{
	ISpellChecker* _checker = nullptr;
	std::wstring _custom_dic_path;

public:
	win_spell_checker()
	{
		ISpellCheckerFactory* factory = nullptr;
		const HRESULT hr = CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
		                                    IID_PPV_ARGS(&factory));
		if (SUCCEEDED(hr) && factory)
		{
			const auto lang = pf::platform_language();
			BOOL supported = FALSE;
			factory->IsSupported(lang.c_str(), &supported);

			if (supported)
			{
				factory->CreateSpellChecker(lang.c_str(), &_checker);
			}
			else
			{
				// Fall back to en-US
				factory->IsSupported(L"en-US", &supported);
				if (supported)
					factory->CreateSpellChecker(L"en-US", &_checker);
			}
			factory->Release();
		}

		_custom_dic_path = file_path::app_data_folder().combine(L"custom.dic").view();

		// Load custom dictionary words
		std::ifstream f(_custom_dic_path);
		if (f.is_open())
		{
			std::string line;
			while (std::getline(f, line))
			{
				if (!line.empty() && _checker)
				{
					auto word = str::utf8_to_utf16(line);
					_checker->Add(word.c_str());
				}
			}
		}
	}

	~win_spell_checker() override
	{
		if (_checker) _checker->Release();
	}

	bool is_word_valid(const std::wstring_view word) override
	{
		if (!_checker) return true;

		const std::wstring w(word);
		IEnumSpellingError* errors = nullptr;
		const HRESULT hr = _checker->Check(w.c_str(), &errors);
		if (FAILED(hr) || !errors) return true;

		ISpellingError* error = nullptr;
		bool valid = true;
		if (errors->Next(&error) == S_OK && error)
		{
			valid = false;
			error->Release();
		}
		errors->Release();
		return valid;
	}

	std::vector<std::wstring> suggest(const std::wstring_view word) override
	{
		std::vector<std::wstring> results;
		if (!_checker) return results;

		const std::wstring w(word);
		IEnumString* suggestions = nullptr;
		const HRESULT hr = _checker->Suggest(w.c_str(), &suggestions);
		if (FAILED(hr) || !suggestions) return results;

		LPOLESTR suggestion = nullptr;
		while (suggestions->Next(1, &suggestion, nullptr) == S_OK)
		{
			results.emplace_back(suggestion);
			CoTaskMemFree(suggestion);
		}
		suggestions->Release();
		return results;
	}

	void add_word(const std::wstring_view word) override
	{
		if (!_checker) return;

		const std::wstring w(word);
		_checker->Add(w.c_str());

		// Persist to custom dictionary file
		const auto word_utf8 = str::utf16_to_utf8(word);
		std::ofstream f(_custom_dic_path, std::ios::out | std::ios::app);
		f << word_utf8 << std::endl;
	}
};

std::unique_ptr<pf::spell_checker> pf::create_spell_checker()
{
	return std::make_unique<win_spell_checker>();
}

// ── Platform File I/O ──────────────────────────────────────────────────────────

struct win_file_handle final : pf::file_handle
{
	HANDLE _h = INVALID_HANDLE_VALUE;
	uint32_t _size = 0;

	win_file_handle(const HANDLE h, const uint32_t sz) : _h(h), _size(sz)
	{
	}

	~win_file_handle() override { if (_h != INVALID_HANDLE_VALUE) CloseHandle(_h); }

	bool read(uint8_t* buffer, const uint32_t bytesToRead, uint32_t* bytesRead) override
	{
		DWORD dwRead = 0;
		const auto ok = ReadFile(_h, buffer, bytesToRead, &dwRead, nullptr);
		if (bytesRead) *bytesRead = dwRead;
		return ok != FALSE;
	}

	uint32_t size() const override { return _size; }
};

pf::file_handle_ptr pf::open_for_read(const file_path& path)
{
	const auto h = ::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return nullptr;
	DWORD high = 0;
	const auto sz = GetFileSize(h, &high);
	if (high != 0)
	{
		CloseHandle(h);
		return nullptr; // file > 4GB not supported
	}
	return std::make_shared<win_file_handle>(h, sz);
}

// ── DPI Awareness Helper ───────────────────────────────────────────────────────

static BOOL SetProcessDpiAwarenessContextIndirect(const DPI_AWARENESS_CONTEXT dpiContext)
{
	static const auto dll = ::LoadLibrary(L"user32.dll");

	if (dll != nullptr)
	{
		using PfnSetProcessDpiAwarenessContexts = int(WINAPI*)(DPI_AWARENESS_CONTEXT dpiContext);
		static auto pfn = (PfnSetProcessDpiAwarenessContexts)GetProcAddress(dll, "SetProcessDpiAwarenessContext");
		if (pfn != nullptr)
			return pfn(dpiContext);
	}

	return FALSE;
}

// ── Entry Point ────────────────────────────────────────────────────────────────

INT WINAPI WinMain(const HINSTANCE hInstance, HINSTANCE, LPSTR, const int nCmdShow)
{
	resource_instance = hInstance;
	g_nCmdShow = nCmdShow;

	OleInitialize(nullptr);
	SetProcessDpiAwarenessContextIndirect(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	init_handles();

	const auto app_framedow = std::make_shared<win_impl>();
	app_framedow->set_self_ref(app_framedow);

	int argc = 0;
	const auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);

	// Attach parent console so /test output is visible in terminal
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* dummy = nullptr;
		_wfreopen_s(&dummy, L"CONOUT$", L"w", stdout);
	}


	// Convert argc/argv to span of wstring_view (skip argv[0] which is the exe path)
	std::vector<std::wstring_view> params;
	for (int i = 1; i < argc; ++i)
		params.emplace_back(argv[i]);

	pf::debug_trace(L"WinMain: before app_init\n");

	// Bind the reactor and build menu definition before creating the HWND,
	// so WM_CREATE is delivered to the reactor's on_create handler.
	if (!app_init(app_framedow, params))
	{
		LocalFree(argv);
		OleUninitialize();
		return 0;
	}
	LocalFree(argv);

	pf::debug_trace(L"WinMain: before create\n");

	app_framedow->create_window(L"RethinkifyWnd", nullptr, {},
	                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);

	pf::debug_trace(L"WinMain: after create\n");

	g_hWnd = app_framedow->m_hWnd;

	// Attach menu now that g_hWnd is valid
	if (g_hMenu)
		SetMenu(g_hWnd, g_hMenu);

	ShowWindow(g_hWnd, g_nCmdShow);
	UpdateWindow(g_hWnd);

	const int result = pf::platform_run();

	OleUninitialize();
	return result;
}


// allow for different calling conventions in Linux and Windows
#ifdef _WIN32
#define STDCALL __stdcall
#else
#define STDCALL
#endif


// Error handler for the Artistic Style formatter.
void STDCALL ASErrorHandler(const int errorNumber, const char* errorMessage)
{
	std::cout << "astyle error " << errorNumber << "\n"
		<< errorMessage << std::endl;
}

// Allocate memory for the Artistic Style formatter.
char* STDCALL ASMemoryAlloc(const unsigned long memoryNeeded)
{
	// error condition is checked after return from AStyleMain
	const auto buffer = new(std::nothrow) char[memoryNeeded];
	return buffer;
}


static bool is_folder(const DWORD attributes)
{
	return attributes != INVALID_FILE_ATTRIBUTES &&
		attributes & FILE_ATTRIBUTE_DIRECTORY;
}

uint64_t ft_to_ts(const FILETIME& ft)
{
	return static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

static uint64_t fs_to_i64(const DWORD nFileSizeHigh, const DWORD nFileSizeLow)
{
	return static_cast<__int64>(nFileSizeHigh) << 32 | nFileSizeLow;
}

static bool is_offline_attribute(const DWORD attributes)
{
	constexpr auto offline_mask = FILE_ATTRIBUTE_OFFLINE |
		FILE_ATTRIBUTE_RECALL_ON_OPEN |
		FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
		FILE_ATTRIBUTE_VIRTUAL;

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & offline_mask) != 0;
}

static void populate_file_attributes(pf::file_attributes_t& fi, const WIN32_FIND_DATA& fad)
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

static bool can_show_file(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return !is_folder(attributes) && !is_dots(name);
}

static bool can_show_folder(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return is_folder(attributes) && !is_dots(name);
}

pf::folder_contents pf::iterate_file_items(const file_path& folder, const bool show_hidden)
{
	folder_contents results;
	WIN32_FIND_DATA fd;

	const auto file_search_path = std::format(L"{}\\*.*", folder.view());
	auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
	                                    nullptr,
	                                    FIND_FIRST_EX_LARGE_FETCH);

	results.files.reserve(64);
	results.folders.reserve(16);

	if (files != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (is_folder(fd.dwFileAttributes))
			{
				if (can_show_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					folder_info i;
					i.path = folder.combine(fd.cFileName);
					populate_file_attributes(i.attributes, fd);
					results.folders.emplace_back(i);
				}
			}
			else
			{
				if (can_show_file(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					file_info i;
					i.path = folder.combine(fd.cFileName);
					populate_file_attributes(i.attributes, fd);
					results.files.emplace_back(i);
				}
			}
		}
		while (FindNextFile(files, &fd) != 0);

		FindClose(files);
	}

	return results;
}

uint64_t pf::file_modified_time(const file_path& path)
{
	WIN32_FILE_ATTRIBUTE_DATA attr{};
	if (::GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attr))
		return ft_to_ts(attr.ftLastWriteTime);
	return 0;
}

#pragma once

extern HINSTANCE resource_instance;

namespace ui
{
	constexpr uint32_t handle_color = 0x00444444;
	constexpr uint32_t main_wnd_clr = 0x00222222;
	constexpr uint32_t tool_wnd_clr = 0x00333333;
	constexpr uint32_t handle_hover_color = 0x00666666;
	constexpr uint32_t handle_tracking_color = 0x00CC6611;
	constexpr uint32_t text_color = 0x00FFFFFF;
	constexpr uint32_t folder_text_color = 0x0022CCEE;
	constexpr uint32_t darker_text_color = 0x00CCCCCC;
	constexpr uint32_t line_color = 0x00AA5522;

	inline COLORREF rgba(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a = 255)
	{
		return r | (g << 8) | (b << 16) | (a << 24);
	};

	inline uint32_t byte_clamp(const int n)
	{
		return (n > 255) ? 255u : (n < 0) ? 0u : static_cast<uint32_t>(n);
	}

	inline COLORREF saturate_rgba(const int r, const int g, const int b, const int a)
	{
		return byte_clamp(r) | (byte_clamp(g) << 8) | (byte_clamp(b) << 16) | (byte_clamp(a) << 24);
	}

	inline uint32_t get_a(const COLORREF c)
	{
		return 0xffu & (c >> 24);
	}

	inline uint32_t get_r(const COLORREF c)
	{
		return 0xffu & c;
	}

	inline uint32_t get_g(const COLORREF c)
	{
		return 0xffu & (c >> 8);
	}

	inline uint32_t get_b(const COLORREF c)
	{
		return 0xffu & (c >> 16);
	}

	inline COLORREF lighten(const COLORREF c, const int n = 32)
	{
		return saturate_rgba(get_r(c) + n, get_g(c) + n, get_b(c) + n, get_a(c));
	}

	inline COLORREF darken(const COLORREF c, const int n = 32)
	{
		return lighten(c, -n);
	}

	inline COLORREF emphasize(const COLORREF c, const int n = 48)
	{
		const bool is_light = get_b(c) > 0x80 || get_g(c) > 0x80 || get_r(c) > 0x80;
		return lighten(c, is_light ? -n : n);
	}

	inline SIZE measure_toolbar(HWND tb)
	{
		SIZE result{ 0, 0 };
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

		win_dc(HWND hwnd) : _hdc(GetDC(hwnd)), _hwnd(hwnd)
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

		HFONT SelectFont(HFONT hFont) const
		{
			return static_cast<HFONT>(SelectObject(_hdc, hFont));
		}
	};

	inline void fill_solid_rect(HDC hDC, int x, int y, int cx, int cy, COLORREF clr)
	{
		const RECT rect = { x, y, x + cx, y + cy };
		const COLORREF crOldBkColor = SetBkColor(hDC, clr);
		::ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
		SetBkColor(hDC, crOldBkColor);
	}


	inline void fill_solid_rect(HDC hDC, const RECT* pRC, COLORREF crColor)
	{
		fill_solid_rect(hDC, pRC->left, pRC->top, pRC->right - pRC->left, pRC->bottom - pRC->top, crColor);
	}

	class win
	{
	public:
		HWND m_hWnd = nullptr;

		irect get_client_rect() const
		{
			irect result;
			::GetClientRect(m_hWnd, &result);
			return result;
		}

		void move_window(const irect& bounds) const
		{
			::MoveWindow(m_hWnd, bounds.left, bounds.top, bounds.Width(), bounds.Height(), TRUE);
		}


		void create_control(LPCWSTR class_name, const HWND parent, const uint32_t style, const uint32_t exstyle = 0,
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

	class win_impl : public win
	{
	public:
		virtual ~win_impl()
		{
			if (IsWindow(m_hWnd))
			{
				SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
			}
		}

		virtual LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		static LRESULT CALLBACK win_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

		static bool register_class(const UINT style, const HICON hIcon, const HCURSOR hCursor, const uint32_t clr_background,
			LPCWSTR lpszMenuName, LPCWSTR lpszClassName, const HICON hIconSm)
		{
			WNDCLASSEX wcx;
			wcx.cbSize = sizeof(WNDCLASSEX); // size of structure
			wcx.style = style; // redraw if size changes
			wcx.lpfnWndProc = win_proc; // points to window procedure
			wcx.cbClsExtra = 0; // no extra class memory
			wcx.cbWndExtra = 0; // no extra window memory
			wcx.hInstance = resource_instance; // handle to instance
			wcx.hIcon = hIcon; // predefined app. icon
			wcx.hCursor = hCursor; // predefined arrow
			wcx.hbrBackground = CreateSolidBrush(clr_background); // white background brush
			wcx.lpszMenuName = lpszMenuName; // name of menu resource
			wcx.lpszClassName = lpszClassName; // name of window class
			wcx.hIconSm = hIconSm;

			if (RegisterClassEx(&wcx) == 0)
			{
				if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
				{
					return false;
				}
			}

			return true;
		}

		INT_PTR do_modal(const HWND parent, const uintptr_t id)
		{
			return ::DialogBoxParam(resource_instance, MAKEINTRESOURCE(id),
				parent, win_proc, (LPARAM)this);
		}

		void create(LPCWSTR class_name, const HWND parent, const uint32_t style, const uint32_t exstyle = 0,
			const uintptr_t id = 0)
		{
			const auto default_cursor = LoadCursor(nullptr, IDC_ARROW);
			if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
				nullptr, default_cursor,
				main_wnd_clr,
				nullptr, class_name, nullptr))
			{
				m_hWnd = CreateWindowEx(
					exstyle,
					class_name,
					nullptr,
					style,
					CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
					parent, std::bit_cast<HMENU>(id),
					resource_instance,
					this);
			}
		}
	};

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lParam)	((int)(short)LOWORD(lParam))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lParam)	((int)(short)HIWORD(lParam))
#endif

	inline void set_font(HWND h, HFONT f)
	{
		SendMessage(h, WM_SETFONT, (WPARAM)f, 1);
	}

	inline void set_icon(HWND h, HICON i)
	{
		SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM)i);
		SendMessage(h, WM_SETICON, ICON_SMALL, (LPARAM)i);
	}

	inline DWORD get_style(HWND m_hWnd)
	{
		return static_cast<DWORD>(::GetWindowLong(m_hWnd, GWL_STYLE));
	}

	BOOL center_window(HWND m_hWnd, HWND hWndCenter = nullptr) noexcept
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
				if (!(dwStyleCenter & WS_VISIBLE) || (dwStyleCenter & WS_MINIMIZE))
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

	static std::wstring window_text(HWND h)
	{
		const auto len = ::GetWindowTextLength(h);
		if (len == 0) return {};
		std::wstring result(len + 1, 0);
		GetWindowText(h, result.data(), len + 1);
		result.resize(len, 0);
		return result;
	}
}

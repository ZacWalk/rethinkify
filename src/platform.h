#pragma once

// platform.h — Platform-independent types, constants, and API declarations.
// Must NOT include OS-specific headers. See platform_win.cpp for Win32 implementation.

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "util.h"

namespace pf
{
	// Key codes (match Windows VK_ values)
	//
	namespace platform_key
	{
		constexpr unsigned int LButton = 0x01;
		constexpr unsigned int RButton = 0x02;
		constexpr unsigned int Back = 0x08;
		constexpr unsigned int Tab = 0x09;
		constexpr unsigned int Return = 0x0D;
		constexpr unsigned int Shift = 0x10;
		constexpr unsigned int Control = 0x11;
		constexpr unsigned int Escape = 0x1B;
		constexpr unsigned int Alt = 0x12;
		constexpr unsigned int Space = 0x20;
		constexpr unsigned int Prior = 0x21;
		constexpr unsigned int Next = 0x22;
		constexpr unsigned int End = 0x23;
		constexpr unsigned int Home = 0x24;
		constexpr unsigned int Left = 0x25;
		constexpr unsigned int Up = 0x26;
		constexpr unsigned int Right = 0x27;
		constexpr unsigned int Down = 0x28;
		constexpr unsigned int Insert = 0x2D;
		constexpr unsigned int Delete = 0x2E;
		constexpr unsigned int F1 = 0x70;
		constexpr unsigned int F3 = 0x72;
		constexpr unsigned int F5 = 0x74;
		constexpr unsigned int F6 = 0x75;
		constexpr unsigned int F7 = 0x76;
		constexpr unsigned int F8 = 0x77;
		constexpr unsigned int F9 = 0x78;
		constexpr unsigned int F10 = 0x79;
	}

	// Key modifier flags for accelerator bindings
	//
	namespace key_mod
	{
		constexpr uint8_t none = 0;
		constexpr uint8_t ctrl = 1;
		constexpr uint8_t shift = 2;
		constexpr uint8_t alt = 4;
	}

	// Keyboard accelerator binding (key + modifiers)
	//
	struct key_binding
	{
		unsigned int key = 0;
		uint8_t modifiers = key_mod::none;

		[[nodiscard]] bool empty() const { return key == 0; }
	};

	// Menu definitions
	//
	struct menu_command
	{
		std::wstring text;
		int id = 0;
		std::function<void()> action;
		std::function<bool()> is_enabled;
		std::function<bool()> is_checked;
		std::vector<menu_command> children;
		key_binding accel;

		menu_command() = default;

		// Leaf item with action + optional enabled/checked + optional key binding
		menu_command(std::wstring t, const int cmd_id,
		             std::function<void()> act,
		             std::function<bool()> en = nullptr,
		             std::function<bool()> chk = nullptr,
		             const key_binding kb = {})
			: text(std::move(t)), id(cmd_id), action(std::move(act)),
			  is_enabled(std::move(en)), is_checked(std::move(chk)),
			  accel(kb)
		{
		}

		// Submenu item
		menu_command(std::wstring t, const int cmd_id,
		             std::function<void()> act,
		             std::function<bool()> en,
		             std::function<bool()> chk,
		             std::vector<menu_command> ch)
			: text(std::move(t)), id(cmd_id), action(std::move(act)),
			  is_enabled(std::move(en)), is_checked(std::move(chk)),
			  children(std::move(ch))
		{
		}
	};

	// Runtime accelerator table — maps key bindings to actions
	//
	class accelerator_table
	{
		static uint32_t pack_key(const unsigned int key, const uint8_t modifiers)
		{
			return (static_cast<uint32_t>(modifiers) << 16) | (key & 0xFFFF);
		}

		std::unordered_map<uint32_t, std::function<void()>> _entries;

	public:
		void add(const key_binding binding, std::function<void()> action)
		{
			_entries[pack_key(binding.key, binding.modifiers)] = std::move(action);
		}

		[[nodiscard]] bool dispatch(const unsigned int key, const uint8_t modifiers) const
		{
			const auto it = _entries.find(pack_key(key, modifiers));
			if (it != _entries.end())
			{
				it->second();
				return true;
			}
			return false;
		}

		void add_from_menu(const std::vector<menu_command>& items)
		{
			for (const auto& item : items)
			{
				if (!item.accel.empty() && item.action)
					add(item.accel, item.action);
				if (!item.children.empty())
					add_from_menu(item.children);
			}
		}
	};

	// Cursor shapes
	//
	enum class cursor_shape { arrow, ibeam, size_we, size_ns };

	// Window style flags
	//
	namespace window_style
	{
		constexpr uint32_t child = 1 << 0;
		constexpr uint32_t visible = 1 << 1;
		constexpr uint32_t clip_children = 1 << 4;

		constexpr uint32_t composited = 1 << 16;
	}


	// Message types for frame_reactor
	//
	enum class message_type : unsigned int
	{
		create,
		destroy,
		set_focus,
		kill_focus,
		erase_background,
		timer,
		sys_color_change,
		left_button_dbl_clk,
		left_button_down,
		right_button_down,
		left_button_up,
		mouse_move,
		mouse_wheel,
		mouse_leave,
		mouse_activate,
		char_input,
		key_down,
		context_menu,
		command,
		close,
		dpi_changed,
		init_dialog,
		set_cursor_msg,
	};

	// Extract signed mouse coordinates from packed lParam (handles negative values on multi-monitor)
	inline ipoint point_from_lparam(const intptr_t lParam)
	{
		return ipoint(static_cast<int16_t>(lParam & 0xFFFF),
		              static_cast<int16_t>(lParam >> 16 & 0xFFFF));
	}

	// Font types
	//
	enum class font_name
	{
		consolas,
		arial,
		calibri,
	};

	struct font
	{
		int size = 12; // in points
		font_name name;
	};


	// Measure / Draw contexts
	//
	struct measure_context
	{
		virtual ~measure_context() = default;
		virtual isize measure_text(std::wstring_view text, const font& f) const = 0;
		virtual isize measure_char(const font& f) const = 0;
	};

	struct draw_context
	{
		virtual ~draw_context() = default;

		// Clip region — the dirty rectangle that needs repainting
		virtual irect clip_rect() const = 0;

		// Fill operations
		virtual void fill_solid_rect(const irect& rc, color_t color) = 0;
		virtual void fill_solid_rect(int x, int y, int cx, int cy, color_t color) = 0;

		// Text output
		virtual void draw_text(int x, int y, const irect& clip, std::wstring_view text,
		                       const font& f, color_t text_color, color_t bg_color) = 0;
		virtual isize measure_text(std::wstring_view text, const font& f) const = 0;

		// Line drawing
		virtual void draw_lines(std::span<const ipoint> points, color_t color) = 0;
	};

	struct frame_reactor;
	struct window_frame;

	using window_frame_ptr = std::shared_ptr<window_frame>;
	using frame_reactor_ptr = std::shared_ptr<frame_reactor>;

	// window_frame — Platform-independent window abstraction
	struct window_frame
	{
		virtual ~window_frame() = default;

		// Reactor binding
		virtual void set_reactor(frame_reactor_ptr reactor) = 0;
		virtual void notify_size() = 0;

		virtual irect get_client_rect() const = 0;

		virtual void invalidate() = 0;
		virtual void invalidate_rect(const irect& rect) = 0;

		// Focus & capture
		virtual void set_focus() = 0;
		virtual bool has_focus() const = 0;
		virtual void set_capture() = 0;
		virtual void release_capture() = 0;
		// Timers
		virtual uint32_t set_timer(uint32_t id, uint32_t ms) = 0;
		virtual void kill_timer(uint32_t id) = 0;
		// Coordinate mapping
		virtual ipoint screen_to_client(ipoint pt) const = 0;
		// Cursor
		virtual void set_cursor_shape(cursor_shape shape) = 0;
		// Window management
		virtual void move_window(const irect& bounds) = 0;
		virtual void show(bool visible) = 0;
		virtual bool is_visible() const = 0;
		virtual void set_text(std::wstring_view text) = 0;
		// Clipboard
		virtual std::wstring text_from_clipboard() = 0;
		virtual bool text_to_clipboard(std::wstring_view text) = 0;

		// Window placement
		struct placement
		{
			irect normal_bounds;
			bool maximized = false;
		};

		virtual placement get_placement() const = 0;
		virtual void set_placement(const placement& p) = 0;
		// Mouse tracking
		virtual void track_mouse_leave() = 0;
		// Key state
		virtual bool is_key_down(unsigned int vk) const = 0;
		virtual bool is_key_down_async(unsigned int vk) const = 0;
		// Child windows
		virtual window_frame_ptr create_child(std::wstring_view class_name, uint32_t style,
		                                      color_t background) const & = 0;
		virtual void close() = 0;
		virtual int message_box(std::wstring_view text, std::wstring_view title, uint32_t style) = 0;
		// Menu
		virtual void set_menu(std::vector<menu_command> menu_def) = 0;
		// Measure context
		virtual std::unique_ptr<measure_context> create_measure_context() const = 0;
		// Popup menu
		virtual void show_popup_menu(const std::vector<menu_command>& items, const ipoint& screen_pt) = 0;
	};

	// frame_reactor — Event handler for window_frame
	struct frame_reactor
	{
		virtual ~frame_reactor() = default;
		virtual uint32_t handle_message(window_frame_ptr window, message_type message, uintptr_t wParam,
		                                intptr_t lParam) = 0;
		virtual void on_paint(window_frame_ptr& window, draw_context& draw) = 0;
		virtual void on_size(window_frame_ptr& window, isize extent, measure_context& measure) = 0;
	};

	// Cursor position (global, not window-specific)
	ipoint platform_cursor_pos();

	// Dialog / Message box constants
	namespace dialog_id
	{
		constexpr int ok = 1;
		constexpr int cancel = 2;
	}

	namespace msg_box_style
	{
		constexpr uint32_t ok = 0x0000;
		constexpr uint32_t yes_no = 0x0004;
		constexpr uint32_t yes_no_cancel = 0x0003;
		constexpr uint32_t icon_warning = 0x0030;
		constexpr uint32_t icon_question = 0x0020;
	}

	namespace msg_box_result
	{
		constexpr int yes = 6;
		constexpr int no = 7;
		constexpr int cancel = 2;
	}

	inline int mul_div(const int a, const int b, const int c)
	{
		return static_cast<int>(static_cast<int64_t>(a) * b / c);
	}

	// File system
	//
	bool is_directory(const file_path& path);
	std::wstring current_directory();

	// File dialog
	file_path open_file_path(std::wstring_view title, std::wstring_view filters);
	file_path save_file_path(std::wstring_view title, const file_path& default_path, std::wstring_view filters);

	// File iteration
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

	folder_contents iterate_file_items(const file_path& folder, bool show_hidden);

	uint64_t file_modified_time(const file_path& path);

	bool platform_events();
	void platform_set_menu(std::vector<menu_command> menuDef);

	// Platform message loop (returns process exit code)
	int platform_run();

	// Timer
	double platform_get_time();
	void platform_sleep(int milliseconds);

	// Resource loading
	void* platform_load_resource(std::wstring_view name, std::wstring_view type);

	void platform_show_error(std::wstring_view message, std::wstring_view title);

	// Platform locale
	std::wstring platform_language();

	// Spell checking
	struct spell_checker
	{
		virtual ~spell_checker() = default;
		virtual bool is_word_valid(std::wstring_view word) = 0;
		virtual std::vector<std::wstring> suggest(std::wstring_view word) = 0;
		virtual void add_word(std::wstring_view word) = 0;
	};

	std::unique_ptr<spell_checker> create_spell_checker();

	// File I/O
	struct file_handle
	{
		virtual ~file_handle() = default;
		virtual bool read(uint8_t* buffer, uint32_t bytesToRead, uint32_t* bytesRead) = 0;
		virtual uint32_t size() const = 0;
	};

	using file_handle_ptr = std::shared_ptr<file_handle>;

	file_handle_ptr open_for_read(const file_path& path);

	// File operations
	bool platform_move_file_replace(const wchar_t* source, const wchar_t* dest);
	std::wstring platform_temp_file_path(const wchar_t* prefix);
	std::wstring platform_last_error_message();

	// Clipboard
	bool platform_clipboard_has_text();
	std::wstring platform_text_from_clipboard();
	bool platform_text_to_clipboard(std::wstring_view text);

	// Bitmap resource loading
	struct bitmap_data
	{
		int width;
		int height;
		std::vector<uint32_t> pixels;
	};

	std::optional<bitmap_data> platform_load_bitmap_resource(std::wstring_view resName);


	void debug_trace(const std::wstring& msg);

	// Configuration (INI file)
	std::wstring config_read(std::wstring_view section, std::wstring_view key, std::wstring_view default_value = {});
	void config_write(std::wstring_view section, std::wstring_view key, std::wstring_view value);

	// background tasks
	void run_async(std::function<void()> task);
	void run_ui(std::function<void()> task);
}


// App callbacks implemented by the application layer
bool app_init(const pf::window_frame_ptr& main_frame, std::span<const std::wstring_view> params);
void app_idle();
void app_destroy();

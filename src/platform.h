#include "util.h"

namespace platform
{
	extern size_t StaticMemoryUsage;

	class crit_sec
	{
		mutable CRITICAL_SECTION _cs;

		crit_sec(const crit_sec& other) = delete;
		const crit_sec& operator=(const crit_sec& other) = delete;

	public:
		crit_sec()
		{
			ZeroMemory(&_cs, sizeof(_cs));
			InitializeCriticalSection(&_cs);
		}

		~crit_sec()
		{
			DeleteCriticalSection(&_cs);
		}

		operator LPCRITICAL_SECTION() const
		{
			return &_cs;
		}

		friend class scope_lock;
	};

	class scope_lock
	{
		const crit_sec& _cs;

		scope_lock(const scope_lock& other) = delete;
		const scope_lock& operator=(const scope_lock& other) = delete;

	public:
		scope_lock(const crit_sec& cs) : _cs(cs)
		{
			EnterCriticalSection(_cs);
		}

		~scope_lock()
		{
			LeaveCriticalSection(_cs);
		}
	};
}

class file_path
{
	std::wstring _path;

public:
	file_path(std::wstring path = L"") : _path(std::move(path))
	{
	}

	file_path(const file_path& other) = default;
	file_path(file_path&& other) = default;
	file_path& operator=(const file_path& other) = default;
	file_path& operator=(file_path&& other) = default;

	const wchar_t* c_str() const
	{
		return _path.c_str();
	}

	std::wstring_view view() const
	{
		return _path;
	}


	static constexpr std::wstring_view::size_type find_ext(const std::wstring_view path)
	{
		const auto last = path.find_last_of(L"./\\");
		if (last == std::u8string_view::npos || path[last] != '.') return path.size();
		return last;
	}

	static constexpr std::wstring_view::size_type find_last_slash(const std::wstring_view path)
	{
		const auto last = path.find_last_of(L"/\\");
		if (last == std::u8string_view::npos) return path.size();
		return last + 1;
	}

	std::wstring without_extension() const
	{
		return _path.substr(0, find_ext(_path));
	}

	std::wstring extension() const
	{
		return _path.substr(find_ext(_path));
	}

	file_path Combine(std::wstring name, std::wstring extension) const
	{
		const auto with_name = Combine(name);
		auto result = std::wstring{with_name.without_extension()};

		if (!extension.empty())
		{
			if (extension[0] != L'.') result += L'.';
			result += extension;
		}
		return {result};
	}

	static constexpr bool is_path_sep(const wchar_t c)
	{
		return c == L'\\' || c == L'/';
	}

	file_path Combine(const std::wstring_view part) const
	{
		auto result = _path;

		if (!part.empty())
		{
			if (!is_path_sep(str::last_char(result)) && !is_path_sep(part[0])) result += '\\';
			result += part;
		}
		return {result};
	}

	bool exists() const
	{
		const auto attribs = ::GetFileAttributes(_path.c_str());

		return (attribs != INVALID_FILE_ATTRIBUTES &&
			(attribs & FILE_ATTRIBUTE_DIRECTORY) == 0);
	}

	bool empty() const
	{
		return _path.empty();
	}

	std::wstring name() const
	{
		return _path.substr(find_last_slash(_path));
	}

	std::wstring folder() const
	{
		return _path.substr(0, find_last_slash(_path));
	}

	static file_path module_folder()
	{
		wchar_t raw_path[MAX_PATH];
		::GetModuleFileName(nullptr, raw_path, MAX_PATH);
		return file_path(raw_path).folder();
	}

	static file_path app_data_folder()
	{
		wchar_t raw_path[MAX_PATH];
		::SHGetSpecialFolderPath(GetActiveWindow(), raw_path, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, TRUE);

		auto result = file_path(raw_path).Combine(g_app_name);
		if (!result.exists()) ::CreateDirectory(raw_path, nullptr);
		return {result};
	}
};


struct ihash
{
	size_t operator()(const file_path path) const
	{
		return fnv1a_i(path.view());
	}

	size_t operator()(const std::wstring_view s) const
	{
		return fnv1a_i(s);
	}
};

struct iless
{
	bool operator()(const file_path l, const file_path r) const
	{
		return str::icmp(l.view(), r.view()) < 0;
	}

	bool operator()(const std::wstring_view l, const std::wstring_view r) const
	{
		return str::icmp(l, r) < 0;
	}
};

struct ieq
{
	bool operator()(const file_path l, const file_path r) const
	{
		return str::icmp(l.view(), r.view()) == 0;
	}

	bool operator()(const std::wstring_view l, const std::wstring_view r) const
	{
		return str::icmp(l, r) == 0;
	}
};
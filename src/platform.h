namespace platform
{
	extern size_t StaticMemoryUsage;

	class crit_sec
	{
		mutable CRITICAL_SECTION _cs;

		crit_sec(const crit_sec& other) = delete;
		const crit_sec& operator=(const crit_sec& other) = delete;

	public:

		inline crit_sec()
		{
			ZeroMemory(&_cs, sizeof(_cs));
			InitializeCriticalSection(&_cs);
		}

		inline ~crit_sec()
		{
			DeleteCriticalSection(&_cs);
		}

		inline operator LPCRITICAL_SECTION() const
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

		inline scope_lock(const crit_sec& cs) : _cs(cs)
		{
			EnterCriticalSection(_cs);
		}

		inline ~scope_lock()
		{
			LeaveCriticalSection(_cs);
		}
	};
}

class Path
{
	std::wstring _path;

public:

	Path(const wchar_t* path = L"") : _path(path) { };

	Path(const Path& other) : _path(other._path) { };

	Path(Path&& other) : _path(std::move(other._path)) { }

	const Path& operator=(const Path& other)
	{
		_path = other._path;
		return *this;
	};

	const wchar_t* c_str() const
	{
		return _path.c_str();
	}

	std::wstring str() const
	{
		return _path;
	}

	inline Path Combine(LPCWSTR name, LPCWSTR extension) const
	{
		wchar_t path[MAX_PATH];
		::PathCombine(path, _path.c_str(), name);
		::PathRenameExtension(path, extension);
		return path;
	}

	inline Path Combine(const std::wstring& name, LPCWSTR extension) const
	{
		return Combine(name.c_str(), extension);
	}

	inline Path Combine(LPCWSTR name) const
	{
		wchar_t path[MAX_PATH];
		return ::PathCombine(path, _path.c_str(), name);
	}

	bool Exists() const
	{
		return ::PathFileExists(_path.c_str()) != 0;
	}

	static inline Path ModuleFolder()
	{
		wchar_t path[MAX_PATH];
		::GetModuleFileName(nullptr, path, MAX_PATH);
		::PathRemoveFileSpec(path);
		return path;
	}

	static inline Path AppData()
	{
		wchar_t path[MAX_PATH];
		::SHGetSpecialFolderPath(GetActiveWindow(), path, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, TRUE);
		::PathCombine(path, path, g_szAppName);
		::CreateDirectory(path, nullptr);
		return path;
	}
};

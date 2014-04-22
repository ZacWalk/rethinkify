#pragma once


inline static std::string ToUtf8(const std::wstring &wstr)
{
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), NULL, 0, NULL, NULL);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &result[0], size_needed, NULL, NULL);
	return result;
}

inline static std::wstring ToUtf16(const std::string &str)
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), NULL, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), &result[0], size_needed);
	return result;
}

inline int Clamp(int v, int l, int r)
{
	if (v < l) return l;
	if (v > r) return r;
	return v;
}


class CPoint : public POINT
{
public:
	CPoint(int xx = 0, int yy = 0) { x = xx; y = yy; }

	bool operator==(const CPoint &other) const { return x == other.x && y == other.y; }
	bool operator!=(const CPoint &other) const { return x != other.x && y != other.y; }
};

class CSize : public SIZE
{
public:
	CSize(int xx = 0, int yy = 0) { cx = xx; cy = yy; }

	bool operator==(const CSize &other) const { return cx == other.cx && cy == other.cy; }
	bool operator!=(const CSize &other) const { return cx != other.cx && cy != other.cy; }
};


class CRect : public RECT
{
public:
	CRect(int l = 0, int t = 0, int r = 0, int b = 0) { left = l; top = t; right = r; bottom = b; }

	int Width() const { return right - left; };
	int Height() const { return bottom - top; };

	operator LPRECT() { return this; }
	operator LPCRECT() const { return this; }

	void OffsetRect(int x, int y)
	{
		left += x;
		top += y;
		right += x;
		bottom += y;
	}
};
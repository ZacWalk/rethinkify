#include "pch.h"
#include "spell_check.h"
#include "Util.h"

#ifdef near
#undef near
#endif

#define HUNSPELL_STATIC
#include "hunspell\hunspell.hxx"

spell_check::spell_check(void) {}

spell_check::~spell_check(void)
{
	// Make sure that we're not in the middle of loading
	// or using the spell checker at this point
	platform::scope_lock l(_cs);
	_psc = nullptr;
}

static std::wstring Language()
{
	wchar_t sz[17];
	auto ccBuf = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, sz, 8) - 1;
	sz[ccBuf++] = '_';
	ccBuf += GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, sz + ccBuf, 8);
	return sz;
}

void spell_check::Init()
{
	platform::scope_lock l(_cs);

	if (!_psc)
	{
		auto language = Language();
		auto folder = Path::ModuleFolder();
		auto affPath = folder.Combine(language, L".aff");
		auto dicPath = folder.Combine(language, L".dic");
		auto extPath = Path::AppData().Combine(L"custom.dic");

		if (!affPath.Exists())
		{
			auto defaulLang = L"en_US";
			affPath = folder.Combine(defaulLang, L".aff");
			dicPath = folder.Combine(defaulLang, L".dic");
		}

		_psc = std::make_shared<Hunspell>(UTF16ToAscii(affPath.str()).c_str(), UTF16ToAscii(dicPath.str()).c_str());

		std::ifstream f(extPath.c_str());

		if (f.is_open())
		{
			std::string line;

			while (f.good())
			{
				std::getline(f, line);
				_psc->add(line.c_str());
			}
			f.close();
		}
	}
}

bool spell_check::is_word_valid(const wchar_t* wword, int wlen)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();
	auto size_needed = WideCharToMultiByte(CP_ACP, 0, wword, wlen, nullptr, 0, nullptr, nullptr);
	auto aword = static_cast<char*>(_alloca(size_needed + 1));
	WideCharToMultiByte(CP_ACP, 0, wword, wlen, aword, size_needed, nullptr, nullptr);
	aword[size_needed] = 0;
	return _psc && _psc->spell(aword) != 0;
}

std::vector<std::wstring> spell_check::suggest(const std::wstring& wword)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();

	auto size_needed = WideCharToMultiByte(CP_ACP, 0, &wword[0], static_cast<int>(wword.size()), nullptr, 0, nullptr, nullptr);
	auto aword = static_cast<char*>(_alloca(size_needed + 1));
	WideCharToMultiByte(CP_ACP, 0, &wword[0], static_cast<int>(wword.size()), aword, size_needed, nullptr, nullptr);
	aword[size_needed] = 0;

	std::vector<std::wstring> results;

	if (_psc)
	{
		char** wordList;
		auto wordCount = _psc->suggest(&wordList, aword);

		for (auto i = 0; i < wordCount; i++)
		{
			results.emplace_back(AsciiToUtf16(wordList[i]));
		}

		_psc->free_list(&wordList, wordCount);
	}

	return results;
}

void spell_check::add_word(const std::wstring& wword)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();

	if (_psc)
	{
		auto customPath = Path::AppData().Combine(L"custom.dic");

		auto size_needed = WideCharToMultiByte(CP_ACP, 0, &wword[0], static_cast<int>(wword.size()), nullptr, 0, nullptr, nullptr);
		auto aword = static_cast<char*>(_alloca(size_needed + 1));
		WideCharToMultiByte(CP_ACP, 0, &wword[0], static_cast<int>(wword.size()), aword, size_needed, nullptr, nullptr);
		aword[size_needed] = 0;

		_psc->add(aword);

		std::ofstream f(customPath.c_str(), std::ios::out | std::ios::app);
		f << aword << std::endl;
	}
}

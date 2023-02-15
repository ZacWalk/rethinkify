#include "pch.h"
#include "spell_check.h"
#include "Util.h"

#ifdef near
#undef near
#endif

#define HUNSPELL_STATIC
#include "hunspell\hunspell.hxx"

spell_check::spell_check(void) = default;

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
		const auto language = Language();
		const auto folder = file_path::module_folder();
		auto affPath = folder.Combine(language, L".aff");
		auto dicPath = folder.Combine(language, L".dic");
		const auto extPath = file_path::app_data_folder().Combine(L"custom.dic");

		if (!affPath.exists())
		{
			const auto defaulLang = L"en_US";
			affPath = folder.Combine(defaulLang, L".aff");
			dicPath = folder.Combine(defaulLang, L".dic");
		}

		_psc = std::make_shared<Hunspell>(UTF16ToAscii(affPath.view()).c_str(), UTF16ToAscii(dicPath.view()).c_str());

		std::ifstream f(extPath.c_str());

		if (f.is_open())
		{
			std::string line;

			while (f.good())
			{
				std::getline(f, line);
				_psc->add(line);
			}
			f.close();
		}
	}
}

bool spell_check::is_word_valid(std::wstring_view word)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();
	return _psc && _psc->spell(UTF16ToAscii(word)) != 0;
}

std::vector<std::wstring> spell_check::suggest(std::wstring_view word)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();

	const auto word_utf8 = UTF16ToUtf8(word);

	std::vector<std::wstring> results;

	if (_psc)
	{
		for (const auto& r : _psc->suggest(word_utf8))
		{
			results.emplace_back(AsciiToUtf16(r));
		}
	}

	return results;
}

void spell_check::add_word(std::wstring_view word)
{
	platform::scope_lock l(_cs);

	if (!_psc) Init();

	if (_psc)
	{
		const auto customPath = file_path::app_data_folder().Combine(L"custom.dic");
		const auto word_utf8 = UTF16ToUtf8(word);

		_psc->add(word_utf8);

		std::ofstream f(customPath.c_str(), std::ios::out | std::ios::app);
		f << word_utf8 << std::endl;
	}
}

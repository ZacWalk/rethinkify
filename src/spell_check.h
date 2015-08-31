#pragma once

#include "platform.h"

class Hunspell;

class spell_check
{
private:
	std::shared_ptr<Hunspell> _psc;
	platform::crit_sec _cs;

	void Init();

public:

	spell_check(void);
	~spell_check(void);

	bool is_word_valid(const wchar_t* wword, int wlen);
	std::vector<std::wstring> suggest(const std::wstring& szWord);
	void add_word(const std::wstring& szWord);
};

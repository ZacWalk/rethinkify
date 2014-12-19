#pragma once

#include "platform.h"

class Hunspell;

class spell_check
{
private:
	std::shared_ptr<Hunspell> _psc;
	platform::CritSec _cs;

	void Init();

public:

	spell_check(void);
	~spell_check(void);

	bool WordValid(const wchar_t *wword, int wlen);
	std::vector<std::wstring> Suggest(const std::wstring &szWord);
	void AddWord(const std::wstring &szWord);
};


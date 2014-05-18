#pragma once

#include "platform.h"

class Hunspell;

class SpellCheck
{
private:
	std::shared_ptr<Hunspell> _psc;
	Platform::CritSec _cs;

	void Init();

public:

	SpellCheck(void);
	~SpellCheck(void);

	bool WordValid(const wchar_t *wword, int wlen);
	std::vector<std::wstring> Suggest(const std::wstring &szWord);
	void AddWord(const std::wstring &szWord);
};


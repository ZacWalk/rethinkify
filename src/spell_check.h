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

	bool is_word_valid(std::wstring_view word);
	std::vector<std::wstring> suggest(std::wstring_view word);
	void add_word(std::wstring_view word);
};

#pragma once

#include "util.h"

class should
{
public:
	static void is_equal(std::wstring_view expected, std::wstring_view actual, std::wstring_view message = L"Test")
	{
		if (str::icmp(actual, expected) != 0)
		{
			throw std::format(L"{}: expected '{}', got '{}'", message, expected, actual);
		}
	}

	static void is_equal(int expected, int actual, std::wstring_view message = L"Test")
	{
		static const int size = 64;
		wchar_t expected_text[size], actual_text[size];
		_itow_s(expected, expected_text, size, 10);
		_itow_s(actual, actual_text, size, 10);
		is_equal(expected_text, actual_text, message);
	}

	static void is_equal(bool expected, bool actual, std::wstring_view message = L"Test")
	{
		is_equal(str::From(expected), str::From(actual), message);
	}

	static void is_equal_true(bool actual, std::wstring_view message = L"Test")
	{
		is_equal(true, actual, message);
	}
};

class tests
{
private:
	static std::chrono::high_resolution_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	};

	static long long duration_in_microseconds(const std::chrono::high_resolution_clock::time_point& started)
	{
		const auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	};

	std::map<std::wstring_view, std::function<void()>> _tests;

public:
	void register_test(std::wstring_view name, const std::function<void()>& f)
	{
		_tests[name] = f;
	}

	void run_all(std::wstringstream& output) const
	{
		const auto started = now();
		auto count = 0;

		for (auto& test : _tests)
		{
			output << "Running '" << test.first << "' ... ";
			auto started = now();

			try
			{
				test.second();
				output << L" success in " << duration_in_microseconds(started) << L" microseconds" << std::endl;
			}
			catch (const std::wstring& message)
			{
				output << L" FAILED in " << duration_in_microseconds(started) << L" microseconds" << std::endl;
				output << std::endl << message << std::endl << std::endl;
			}
			catch (const std::exception& e)
			{
				output << L" FAILED in " << duration_in_microseconds(started) << L" microseconds" << std::endl;
				output << std::endl << e.what() << std::endl << std::endl;
			}

			count += 1;
		}

		output << std::endl << L"Completed " << count << L" tests in " << duration_in_microseconds(started) <<
			L" microseconds" << std::endl << std::endl;
	}
};

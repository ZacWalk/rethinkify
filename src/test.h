#pragma once

// test.h — Lightweight test framework: assertions and test runner

#include "util.h"

class should
{
public:
	static void is_equal(const std::wstring_view expected, const std::wstring_view actual,
	                     const std::wstring_view message = L"Test")
	{
		if (actual != expected)
		{
			throw std::format(L"{}: expected '{}', got '{}'", message, expected, actual);
		}
	}

	static void is_equal(const int expected, const int actual, const std::wstring_view message = L"Test")
	{
		is_equal(str::to_str(expected), str::to_str(actual), message);
	}

	static void is_equal(const bool expected, const bool actual, const std::wstring_view message = L"Test")
	{
		is_equal(str::to_str(expected), str::to_str(actual), message);
	}

	static void is_equal_true(const bool actual, const std::wstring_view message = L"Test")
	{
		is_equal(true, actual, message);
	}
};

class tests
{
	static std::chrono::high_resolution_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	}

	static long long duration_in_microseconds(const std::chrono::high_resolution_clock::time_point& started)
	{
		const auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	}

	std::map<std::wstring_view, std::function<void()>> _tests;

public:
	void register_test(const std::wstring_view name, const std::function<void()>& f)
	{
		_tests[name] = f;
	}

	void run_all(std::wstringstream& output) const
	{
		const auto all_started = now();
		auto pass_count = 0;
		auto fail_count = 0;
		std::wstringstream details;

		for (auto& test : _tests)
		{
			details << "Running '" << test.first << "' ... ";
			auto test_started = now();

			try
			{
				test.second();
				details << L" success in " << duration_in_microseconds(test_started) << L" microseconds" << std::endl;
				pass_count += 1;
			}
			catch (const std::wstring& message)
			{
				details << L" FAILED in " << duration_in_microseconds(test_started) << L" microseconds" << std::endl;
				details << std::endl << message << std::endl << std::endl;
				fail_count += 1;
			}
			catch (const std::exception& e)
			{
				details << L" FAILED in " << duration_in_microseconds(test_started) << L" microseconds" << std::endl;
				details << std::endl << e.what() << std::endl << std::endl;
				fail_count += 1;
			}
		}

		const auto total = pass_count + fail_count;
		const auto elapsed = duration_in_microseconds(all_started);

		if (fail_count == 0)
			output << L"**" << total << L" tests passed** in " << elapsed << L" microseconds" << std::endl;
		else
			output << L"**" << fail_count << L" of " << total << L" tests failed** in " << elapsed << L" microseconds"
				<< std::endl;

		output << std::endl << details.str();
	}
};

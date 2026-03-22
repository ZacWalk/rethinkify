// test.h — Lightweight test framework: assertions and test runner

#pragma once

#include "platform.h"

class should
{
public:
	static void is_equal(const std::u8string_view expected, const std::u8string_view actual,
	                     const std::u8string_view message = u8"Test")
	{
		if (actual != expected)
		{
			throw pf::format(u8"{}: expected '{}', got '{}'", message, expected, actual);
		}
	}

	static void is_equal(const int expected, const int actual, const std::u8string_view message = u8"Test")
	{
		is_equal(to_str(expected), to_str(actual), message);
	}

	static void is_equal(const bool expected, const bool actual, const std::u8string_view message = u8"Test")
	{
		is_equal(to_str(expected), to_str(actual), message);
	}

	static void is_equal_true(const bool actual, const std::u8string_view message = u8"Test")
	{
		is_equal(true, actual, message);
	}
};

class tests
{
public:
	struct run_result
	{
		std::u8string output;
		int pass_count = 0;
		int fail_count = 0;
	};

private:
	static std::chrono::high_resolution_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	}

	static long long duration_in_microseconds(const std::chrono::high_resolution_clock::time_point& started)
	{
		const auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	}

	std::map<std::u8string_view, std::function<void()>> _tests;

public:
	void register_test(const std::u8string_view name, const std::function<void()>& f)
	{
		_tests[name] = f;
	}

	run_result run_all_result() const
	{
		const auto all_started = now();
		auto pass_count = 0;
		auto fail_count = 0;
		std::u8string details;

		for (auto& test : _tests)
		{
			details += pf::format(u8"Running '{}' ... ", test.first);
			auto test_started = now();

			try
			{
				test.second();
				details += pf::format(u8" success in {} microseconds\n", duration_in_microseconds(test_started));
				pass_count += 1;
			}
			catch (const std::u8string& message)
			{
				details += pf::format(u8" FAILED in {} microseconds\n\n{}\n\n", duration_in_microseconds(test_started),
				                      message);
				fail_count += 1;
			}
			catch (const std::exception& e)
			{
				details += pf::format(u8" FAILED in {} microseconds\n\n{}\n\n", duration_in_microseconds(test_started),
				                      std::u8string_view(reinterpret_cast<const char8_t*>(e.what())));
				fail_count += 1;
			}
		}

		const auto total = pass_count + fail_count;
		const auto elapsed = duration_in_microseconds(all_started);

		run_result result;
		result.pass_count = pass_count;
		result.fail_count = fail_count;
		if (fail_count == 0)
			result.output += pf::format(u8"**{} tests passed** in {} microseconds\n", total, elapsed);
		else
			result.output += pf::format(u8"**{} of {} tests failed** in {} microseconds\n", fail_count, total,
			                            elapsed);

		result.output += u8"\n";
		result.output += details;
		return result;
	}

	std::u8string run_all() const
	{
		return run_all_result().output;
	}
};

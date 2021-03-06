#pragma once

#include "util.h"

class should
{
public:

	static void Equal(const wchar_t* expected, const wchar_t* actual, const wchar_t* message = L"Test")
	{
		if (String::CompareNoCase(actual, expected) != 0)
		{
			throw String::Format(L"%s: expected '%s', got '%s'", message, expected, actual);
		}
	}

	static void Equal(const std::wstring& expected, const std::wstring& actual, const wchar_t* message = L"Test")
	{
		Equal(expected.c_str(), actual.c_str(), message);
	}

	static void Equal(int expected, int actual, const wchar_t* message = L"Test")
	{
		static const int size = 64;
		wchar_t expected_text[size], actual_text[size];
		_itow_s(expected, expected_text, size, 10);
		_itow_s(actual, actual_text, size, 10);
		Equal(expected_text, actual_text, message);
	}

	static void Equal(bool expected, bool actual, const wchar_t* message = L"Test")
	{
		Equal(String::From(expected), String::From(actual), message);
	}

	static void EqualTrue(bool actual, const wchar_t* message = L"Test")
	{
		Equal(true, actual, message);
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
		auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	};

	std::map<std::wstring, std::function<void()>> _tests;

public:

	inline void Register(const std::wstring& name, const std::function<void()>& f)
	{
		_tests[name] = f;
	}

	void Run(std::wstringstream& output) const
	{
		auto started = now();
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

		output << std::endl << L"Completed " << count << L" tests in " << duration_in_microseconds(started) << L" microseconds" << std::endl << std::endl;
	}
};

#pragma once

#include "util.h"

class Should
{
public:

	static void Equal(const char * expected, const char * actual, const char * message = "Test")
	{
		if (String::CompareNoCase(actual, expected) != 0)
		{
			throw String::Format("%s: expected '%s', got '%s'", message, expected, actual);
		}
	}

	static void Equal(const std::string &expected, const std::string &actual, const char * message = "Test")
	{
		Equal(expected.c_str(), actual.c_str(), message);
	}

	static void Equal(int expected, int actual, const char * message = "Test")
	{
		static const int size = 64;
		char expected_text[size], actual_text[size];
		_itoa_s(expected, expected_text, size, 10);
		_itoa_s(actual, actual_text, size, 10);
		Equal(expected_text, actual_text, message);
	}

	static void Equal(bool expected, bool actual, const char * message = "Test")
	{
		Equal(String::From(expected), String::From(actual), message);
	}

	static void EqualTrue(bool actual, const char * message = "Test")
	{
		Equal(true, actual, message);
	}

};


class Tests
{
private:

	static std::chrono::system_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	};

	static long long duration_in_microseconds(const std::chrono::system_clock::time_point &started)
	{
		auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	};

	std::map<std::string, std::function<void()>> _tests;

public:

	inline void Register(const std::string &name, const std::function<void()> &f)
	{
		_tests[name] = f;
	}

	void Run(std::stringstream &output)
	{
		auto started = now();
		auto count = 0;

		for (auto &test : _tests)
		{
			output << "Running '" << test.first << "' ... ";
			auto started = now();

			try
			{
				test.second();
				output << " success in " << duration_in_microseconds(started) << " microseconds" << std::endl;
			}
			catch (const std::string &message)
			{
				output << " FAILED in " << duration_in_microseconds(started) << " microseconds" << std::endl;
				output << std::endl << message << std::endl << std::endl;
			}
			catch (const std::exception &e)
			{
				output << " FAILED in " << duration_in_microseconds(started) << " microseconds" << std::endl;
				output << std::endl << e.what() << std::endl << std::endl;
			}

			count += 1;
		}

		output << std::endl << "Completed " << count << " tests in " << duration_in_microseconds(started) << " microseconds" << std::endl << std::endl;
	}
};

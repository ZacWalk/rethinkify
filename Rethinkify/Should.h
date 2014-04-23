#pragma once

#include "util.h"

class Should
{
public:

	static void Equal(const char * expected, const char * actual, const char * message = "Test")
	{
#ifndef MAC_PLUGIN
		if (String::CompareNoCase(actual, expected) != 0)
		{
			throw String::Format("%s: expected '%s', got '%s'", message, expected, actual);
		}
#else
		NSString *exp = [NSString stringWithUTF8String : expected];
		NSString *act = [NSString stringWithUTF8String : actual];

		if (NSOrderedSame != [exp caseInsensitiveCompare : act])
		{
			NSString *msg =
				[NSString stringWithFormat : @"%s: expected '%s', got '%s'",
				message, expected, actual];

			throw std::string([msg UTF8String]);
		}
#endif
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

#ifdef MAC_PLUGIN
	static NSTimeInterval now()
	{
		return[[NSDate date] timeIntervalSinceReferenceDate];
	};

	static long long duration_in_microseconds(const NSTimeInterval &started)
	{
		NSTimeInterval dur = now() - started;
		return (long long) (1000000.0*dur);
	};
#else
	static std::chrono::system_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	};

	static long long duration_in_microseconds(const std::chrono::system_clock::time_point &started)
	{
		auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	};
#endif

	struct TaskBase
	{
		std::string _name;

		TaskBase()
		{
		}

		virtual void Run() = 0;
	};

	template<class TFunc>
	struct TaskImp : public TaskBase
	{
		TFunc _f;
		TaskImp(const char* name, const TFunc &f) : _f(f) { _name = name; }
		void Run() { _f(); }
	};

	typedef std::shared_ptr<TaskBase> TestPtr;
	std::vector<TestPtr> _tests;

public:

	template<class TFunc>
	inline void Register(const char* name, const TFunc &f)
	{
		_tests.push_back(std::make_shared<TaskImp<TFunc>>(name, f));
	}

	void Run(std::stringstream &output)
	{
		auto started = now();
		auto count = 0;

		for (auto &test : _tests)
		{
			output << "Running '" << test->_name << "' ... ";
			auto started = now();

			try
			{
				test->Run();
				output << " success in " << duration_in_microseconds(started) << " microseconds" << std::endl;
			}
			catch (const std::string &message)
			{
				output << " FAILED in " << duration_in_microseconds(started) << " microseconds" << std::endl;
				output << std::endl << message << std::endl << std::endl;
			}

			count += 1;
		}

		output << std::endl << "Completed " << count << " tests in " << duration_in_microseconds(started) << " microseconds" << std::endl << std::endl;
	}
};

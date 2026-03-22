// command_calc.h — Calculator expression parser for console arithmetic evaluation

#pragma once

class calc_parser
{
public:
	explicit calc_parser(const std::u8string_view expression) : _text(expression)
	{
	}

	std::optional<double> parse()
	{
		_pos = 0;
		_error.clear();
		auto value = parse_expression();
		skip_ws();
		if (!_error.empty())
			return std::nullopt;
		if (_pos != _text.size())
		{
			_error = u8"Unexpected trailing input.";
			return std::nullopt;
		}
		return value;
	}

	std::u8string error() const { return _error; }

private:
	std::u8string_view _text;
	size_t _pos = 0;
	std::u8string _error;

	void skip_ws()
	{
		while (_pos < _text.size() && (_text[_pos] == u8' ' || _text[_pos] == u8'\t'))
			++_pos;
	}

	bool consume(const char8_t ch)
	{
		skip_ws();
		if (_pos < _text.size() && _text[_pos] == ch)
		{
			++_pos;
			return true;
		}
		return false;
	}

	double parse_expression()
	{
		double value = parse_term();
		while (_error.empty())
		{
			if (consume(u8'+')) value += parse_term();
			else if (consume(u8'-')) value -= parse_term();
			else break;
		}
		return value;
	}

	double parse_term()
	{
		double value = parse_factor();
		while (_error.empty())
		{
			if (consume(u8'*')) value *= parse_factor();
			else if (consume(u8'/'))
			{
				const auto rhs = parse_factor();
				if (rhs == 0.0)
				{
					_error = u8"Division by zero.";
					return 0.0;
				}
				value /= rhs;
			}
			else break;
		}
		return value;
	}

	double parse_factor()
	{
		skip_ws();
		if (consume(u8'+'))
			return parse_factor();
		if (consume(u8'-'))
			return -parse_factor();
		if (consume(u8'('))
		{
			const auto value = parse_expression();
			if (!consume(u8')'))
				_error = u8"Missing closing ')'.";
			return value;
		}
		return parse_number();
	}

	double parse_number()
	{
		skip_ws();
		const auto start = _pos;
		bool seen_digit = false;
		bool seen_dot = false;
		while (_pos < _text.size())
		{
			const auto ch = _text[_pos];
			if (ch >= u8'0' && ch <= u8'9')
			{
				seen_digit = true;
				++_pos;
				continue;
			}
			if (ch == u8'.' && !seen_dot)
			{
				seen_dot = true;
				++_pos;
				continue;
			}
			break;
		}

		if (!seen_digit)
		{
			_error = u8"Expected a number.";
			return 0.0;
		}

		try
		{
			const auto raw = _text.substr(start, _pos - start);
			const std::string temp(reinterpret_cast<const char*>(raw.data()), raw.size());
			return std::stod(temp);
		}
		catch (...)
		{
			_error = u8"Invalid number.";
			return 0.0;
		}
	}
};

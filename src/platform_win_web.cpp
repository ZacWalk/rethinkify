// HTTP client using WinInet. Handles web requests, downloads,
// form uploads, and network connectivity checks.

#include "pch.h"
#include "platform.h"

#define NOMINMAX
#include <windows.h>
#include <WinInet.h>

#pragma comment(lib, "wininet.lib")

static_assert(std::is_move_constructible_v<pf::web_request>);
static_assert(std::is_move_constructible_v<pf::web_response>);

bool pf::is_online()
{
	DWORD flags;
	return 0 != InternetGetConnectedState(&flags, 0);
}

std::u8string pf::url_encode(const std::u8string_view input)
{
	static constexpr auto hex_chars = u8"0123456789ABCDEF";
	std::u8string result;
	result.reserve(input.size());

	for (const auto c : input)
	{
		if ((c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z') ||
			(c >= u8'0' && c <= u8'9') || c == u8'-' || c == u8'_' || c == u8'.' || c == u8'~')
		{
			result += c;
		}
		else
		{
			const auto byte = static_cast<uint8_t>(c);
			result += u8'%';
			result += hex_chars[byte >> 4];
			result += hex_chars[byte & 0x0F];
		}
	}

	return result;
}

static int get_status_code(const HINTERNET h)
{
	DWORD result = 0;
	DWORD result_size = sizeof(result);
	if (!HttpQueryInfo(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &result, &result_size, nullptr))
	{
		return 0; // Return 0 if query fails
	}
	return static_cast<int>(result);
}

static std::u8string get_content_type(const HINTERNET request_handle)
{
	std::u8string result;
	DWORD result_size = 0;
	DWORD header_index = 0;

	// First call to get the required buffer size
	HttpQueryInfoA(request_handle, HTTP_QUERY_CONTENT_TYPE, nullptr, &result_size, &header_index);

	if (result_size > 0)
	{
		result.resize(result_size);
		header_index = 0; // Reset header index

		if (HttpQueryInfoA(request_handle, HTTP_QUERY_CONTENT_TYPE, result.data(), &result_size, &header_index))
		{
			// result_size now contains the actual string length (excluding null terminator)
			if (result_size > 0 && result_size <= result.size())
			{
				result.resize(result_size);
			}
			else
			{
				result.clear();
			}
		}
		else
		{
			result.clear();
		}
	}

	return result;
}

static std::u8string format_path(const pf::web_request& req)
{
	auto result = req.path;

	if (!req.query.empty())
	{
		bool is_first = true;
		result += u8"?";

		for (const auto& qp : req.query)
		{
			if (!is_first)
			{
				result += u8"&";
			}

			result += pf::url_encode(qp.first);
			result += u8"=";
			result += pf::url_encode(qp.second);
			is_first = false;
		}
	}

	return result;
}

// RAII wrapper for WinInet handles
class inet_handle
{
	HINTERNET _h;

public:
	explicit inet_handle(const HINTERNET handle = nullptr) : _h(handle)
	{
	}

	~inet_handle()
	{
		if (_h)
		{
			InternetCloseHandle(_h);
		}
	}

	HINTERNET detach()
	{
		const auto handle = _h;
		_h = nullptr;
		return handle;
	}

	// No copy constructor/assignment
	inet_handle(const inet_handle&) = delete;
	inet_handle& operator=(const inet_handle&) = delete;

	// Move constructor/assignment
	inet_handle(inet_handle&& other) noexcept : _h(other._h)
	{
		other._h = nullptr;
	}

	inet_handle& operator=(inet_handle&& other) noexcept
	{
		if (this != &other)
		{
			if (_h)
			{
				InternetCloseHandle(_h);
			}
			_h = other._h;
			other._h = nullptr;
		}
		return *this;
	}

	operator HINTERNET() const { return _h; }
	HINTERNET get() const { return _h; }
	bool is_valid() const { return _h != nullptr; }

	void reset(const HINTERNET handle = nullptr)
	{
		if (_h)
		{
			InternetCloseHandle(_h);
		}
		_h = handle;
	}
};

struct pf::web_host
{
	HINTERNET session_handle = nullptr;
	HINTERNET connection_handle = nullptr;
	bool secure = true;
};

pf::web_host_ptr pf::connect_to_host(const std::u8string_view host, const bool secure_in, const int port_in,
                                     const std::u8string_view user_agent)
{
	// InternetOpen and InternetConnect
	const std::wstring agent_str = user_agent.empty() ? utf8_to_utf16(g_app_name) : utf8_to_utf16(user_agent);
	inet_handle session_handle(InternetOpenW(agent_str.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));

	if (!session_handle.is_valid())
	{
		return nullptr; // Return empty response on failure
	}

	const auto hostW = utf8_to_utf16(host);
	const auto port = port_in == 0
		                  ? (secure_in ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)
		                  : port_in;
	inet_handle conn(::InternetConnect(session_handle, hostW.c_str(), port, nullptr, nullptr,
	                                   INTERNET_SERVICE_HTTP, 0, 0));

	if (!conn.is_valid())
	{
		return nullptr; // Return empty response on failure
	}

	return std::make_shared<web_host>(web_host{session_handle.detach(), conn.detach(), secure_in});
}

pf::web_response pf::send_request(const web_host_ptr& host, const web_request& req)
{
	web_response result;

	if (!host)
		return result;

	std::u8string content;
	std::u8string header_str;

	for (const auto& h : req.headers)
	{
		header_str += h.first;
		header_str += u8": ";
		header_str += h.second;
		header_str += u8"\r\n";
	}

	if (!req.body.empty())
	{
		content = req.body;
	}
	else if (!req.form_data.empty())
	{
		const std::u8string boundary = u8"54B8723DE6044695A68C838E8BF0CB00";

		for (const auto& f : req.form_data)
		{
			content += u8"--";
			content += boundary;
			content += u8"\r\n";
			content += u8"Content-Disposition: form-data; name=\"";
			content += f.first;
			content += u8"\"\r\n";
			content += u8"Content-Type: text/plain; charset=\"utf-8\"\r\n";
			content += u8"\r\n";
			content += f.second;
			content += u8"\r\n";
		}

		if (!req.upload_file_path.empty() && !req.file_form_data_name.empty())
		{
			std::u8string ct = u8"application/octet-stream";
			if (req.upload_file_path.extension() == u8".zip") ct = u8"application/x-zip-compressed";

			content += u8"--";
			content += boundary;
			content += u8"\r\n";
			content += u8"Content-Disposition: form-data; name=\"";
			content += req.file_form_data_name;
			content += u8"\"; filename=\"";
			content += req.file_name;
			content += u8"\"\r\n";
			content += u8"Content-Type: ";
			content += ct;
			content += u8"\r\n\r\n";

			auto fh = open_for_read(req.upload_file_path);
			if (fh)
			{
				std::vector<uint8_t> buf(65536);
				uint32_t bytes_read = 0;
				while (fh->read(buf.data(), static_cast<uint32_t>(buf.size()), &bytes_read) && bytes_read > 0)
				{
					content.append(reinterpret_cast<const char8_t*>(buf.data()), bytes_read);
				}
			}

			content += u8"\r\n";
		}

		content += u8"--";
		content += boundary;
		content += u8"--";
		header_str += u8"Content-Type: multipart/form-data; boundary=";
		header_str += boundary;
		header_str += u8"\r\n";
	}

	const auto wverb = req.verb == web_request_verb::GET ? L"GET" : L"POST";
	const auto wpath = utf8_to_utf16(format_path(req));
	auto flags = INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTH |
		INTERNET_FLAG_RELOAD;
	if (host->secure) flags |= INTERNET_FLAG_SECURE;

	inet_handle request_handle(HttpOpenRequest(host->connection_handle, wverb, wpath.c_str(), nullptr, nullptr, nullptr,
	                                           flags, 0));

	if (!request_handle.is_valid())
	{
		return result;
	}

	const auto headerW = utf8_to_utf16(header_str);

	if (content.empty())
	{
		// Simple request with no body — use HttpSendRequest which handles redirects properly
		if (!HttpSendRequest(request_handle, headerW.c_str(), static_cast<DWORD>(headerW.size()), nullptr, 0))
		{
			return result;
		}
	}
	else
	{
		// Request with body — use HttpSendRequestEx for chunked sending
		INTERNET_BUFFERS buffers = {};
		buffers.dwStructSize = sizeof(INTERNET_BUFFERS);
		buffers.lpcszHeader = headerW.c_str();
		buffers.dwHeadersTotal = buffers.dwHeadersLength = static_cast<DWORD>(headerW.size());
		buffers.dwBufferTotal = static_cast<DWORD>(content.size());

		if (!HttpSendRequestEx(request_handle, &buffers, nullptr, 0, 0))
		{
			return result;
		}

		constexpr size_t chunk_size = 8192;
		size_t total_written = 0;

		while (total_written < content.size())
		{
			const auto remaining = content.size() - total_written;
			const auto to_write = std::min(chunk_size, remaining);
			DWORD written = 0;

			if (!InternetWriteFile(request_handle, content.data() + total_written, static_cast<DWORD>(to_write),
			                       &written))
			{
				return result;
			}

			if (written == 0)
			{
				return result;
			}

			total_written += written;
		}

		if (!::HttpEndRequest(request_handle, nullptr, 0, 0))
		{
			return result;
		}
	}

	result.status_code = get_status_code(request_handle);
	result.content_type = get_content_type(request_handle);

	if (!req.download_file_path.empty())
	{
		const auto download_file = open_file_for_write(req.download_file_path);

		if (download_file)
		{
			uint8_t buffer[8192];
			DWORD read = 0;

			while (InternetReadFile(request_handle, buffer, sizeof(buffer), &read) && read > 0)
			{
				if (download_file->write(buffer, read) != read)
				{
					break;
				}
			}
		}
	}
	else
	{
		uint8_t buffer[8192];
		DWORD read = 0;

		while (InternetReadFile(request_handle, buffer, sizeof(buffer), &read) && read > 0)
		{
			result.body.append(buffer, buffer + read);
		}
	}

	return result;
}

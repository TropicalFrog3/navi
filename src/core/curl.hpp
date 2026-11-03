#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Curl
{
	struct HttpRequest_t
	{
		std::string method = "GET";
		std::string url;
		std::vector<std::pair<std::string, std::string>> headers;
		std::vector<uint8_t> body;
		int timeoutSec = 15;
	};

	struct HttpResponse_t
	{
		int statusCode = 0;
		std::unordered_map<std::string, std::string> headers; /* lowercase header keys */
		std::vector<uint8_t> body;
		std::string errorMsg;
	};

	using HttpTransportFunc_t = std::function<bool(const HttpRequest_t&, HttpResponse_t&)>;

	void setHttpTransportOverride(HttpTransportFunc_t fn);

	bool request(const HttpRequest_t& req, HttpResponse_t& outResp);

	int getString(const char* url, std::string& out);

	int downloadToFile(const std::string& url, const std::string& destPath, int timeoutSec = 60, const std::string& authHeader = "");
}

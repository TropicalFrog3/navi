#include "curl.hpp"
#include "atomic_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Local debug logging — safe for standalone helper binary
#define CURL_DEBUG(fmt, ...) fprintf(stderr, "[Curl] " fmt __VA_OPT__(,) __VA_ARGS__)
#define CURL_ERROR(fmt, ...) fprintf(stderr, "[Curl ERROR] " fmt __VA_OPT__(,) __VA_ARGS__)

static std::mutex s_transportMutex;
static Curl::HttpTransportFunc_t s_transportOverride;

void Curl::setHttpTransportOverride(HttpTransportFunc_t fn)
{
	std::lock_guard<std::mutex> lock(s_transportMutex);
	s_transportOverride = std::move(fn);
}

bool Curl::request(const HttpRequest_t& req, HttpResponse_t& outResp)
{
	HttpTransportFunc_t overrideFn;
	{
		std::lock_guard<std::mutex> lock(s_transportMutex);
		overrideFn = s_transportOverride;
	}
	if (overrideFn)
	{
		return overrideFn(req, outResp);
	}

	int pipe_out[2];
	if (pipe(pipe_out) == -1)
	{
		outResp.errorMsg = "Failed to create stdout pipe";
		return false;
	}

	int pipe_in[2] = {-1, -1};
	if (!req.body.empty())
	{
		if (pipe(pipe_in) == -1)
		{
			close(pipe_out[0]);
			close(pipe_out[1]);
			outResp.errorMsg = "Failed to create stdin pipe";
			return false;
		}
	}

	constexpr static const char* env[] =
	{
		"PATH='/usr/bin:/bin'",
		nullptr
	};

	std::vector<std::string> argsStr;
	argsStr.push_back("curl");
	argsStr.push_back("-s");
	argsStr.push_back("-S");
	argsStr.push_back("-i");
	argsStr.push_back("-L");
	argsStr.push_back("--max-redirs");
	argsStr.push_back("5");
	argsStr.push_back("-X");
	argsStr.push_back(req.method.empty() ? "GET" : req.method);
	argsStr.push_back("--connect-timeout");
	argsStr.push_back(std::to_string(req.timeoutSec > 0 ? req.timeoutSec : 15));
	argsStr.push_back("-m");
	argsStr.push_back(std::to_string((req.timeoutSec > 0 ? req.timeoutSec : 15) + 30));

	for (const auto& [k, v] : req.headers)
	{
		argsStr.push_back("-H");
		argsStr.push_back(k + ": " + v);
	}

	if (!req.body.empty())
	{
		argsStr.push_back("--data-binary");
		argsStr.push_back("@-");
	}

	argsStr.push_back(req.url);

	std::vector<char*> args;
	args.reserve(argsStr.size() + 1);
	for (auto& s : argsStr)
	{
		args.push_back(const_cast<char*>(s.c_str()));
	}
	args.push_back(nullptr);

	const pid_t pid = fork();
	if (pid == -1)
	{
		close(pipe_out[0]);
		close(pipe_out[1]);
		if (!req.body.empty())
		{
			close(pipe_in[0]);
			close(pipe_in[1]);
		}
		outResp.errorMsg = "Failed to fork curl process";
		return false;
	}

	if (pid == 0)
	{
		if (dup2(pipe_out[1], STDOUT_FILENO) == -1)
		{
			_exit(1);
		}
		close(pipe_out[0]);
		close(pipe_out[1]);

		if (!req.body.empty())
		{
			if (dup2(pipe_in[0], STDIN_FILENO) == -1)
			{
				_exit(1);
			}
			close(pipe_in[0]);
			close(pipe_in[1]);
		}
		else
		{
			int nullfd = open("/dev/null", O_RDONLY);
			if (nullfd != -1)
			{
				dup2(nullfd, STDIN_FILENO);
				close(nullfd);
			}
		}

		execve("/bin/curl", args.data(), const_cast<char**>(env));
		execve("/usr/bin/curl", args.data(), const_cast<char**>(env));
		execve("/run/current-system/sw/bin/curl", args.data(), const_cast<char**>(env));
		_exit(127);
	}

	close(pipe_out[1]);
	if (!req.body.empty())
	{
		close(pipe_in[0]);
	}

	fcntl(pipe_out[0], F_SETFL, fcntl(pipe_out[0], F_GETFL, 0) | O_NONBLOCK);
	if (!req.body.empty())
	{
		fcntl(pipe_in[1], F_SETFL, fcntl(pipe_in[1], F_GETFL, 0) | O_NONBLOCK);
	}

	std::vector<uint8_t> rawOutput;
	size_t bodyWritten = 0;
	bool inOpen = !req.body.empty();
	char readBuf[16384];
	bool timedOutOrError = false;

	while (pipe_out[0] != -1)
	{
		struct pollfd pfd[2];
		int nfds = 0;

		pfd[0].fd = pipe_out[0];
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		nfds++;

		if (inOpen)
		{
			pfd[1].fd = pipe_in[1];
			pfd[1].events = POLLOUT;
			pfd[1].revents = 0;
			nfds++;
		}

		int pollTimeoutMs = (std::max(1, req.timeoutSec) + 35) * 1000;
		int pr = poll(pfd, nfds, pollTimeoutMs);
		if (pr <= 0)
		{
			timedOutOrError = true;
			break;
		}

		if (inOpen && (pfd[1].revents & (POLLOUT | POLLERR | POLLHUP)))
		{
			if (pfd[1].revents & POLLOUT)
			{
				size_t toWrite = req.body.size() - bodyWritten;
				ssize_t w = write(pipe_in[1], req.body.data() + bodyWritten, toWrite);
				if (w > 0)
				{
					bodyWritten += static_cast<size_t>(w);
					if (bodyWritten >= req.body.size())
					{
						close(pipe_in[1]);
						inOpen = false;
					}
				}
				else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				{
					close(pipe_in[1]);
					inOpen = false;
				}
			}
			else
			{
				close(pipe_in[1]);
				inOpen = false;
			}
		}

		if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR))
		{
			ssize_t n = read(pipe_out[0], readBuf, sizeof(readBuf));
			if (n > 0)
			{
				rawOutput.insert(rawOutput.end(), readBuf, readBuf + n);
			}
			else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
			{
				close(pipe_out[0]);
				pipe_out[0] = -1;
				break;
			}
		}
	}

	if (inOpen)
	{
		close(pipe_in[1]);
	}
	if (pipe_out[0] != -1)
	{
		close(pipe_out[0]);
	}

	if (timedOutOrError)
	{
		kill(pid, SIGKILL);
	}

	int status = 0;
	waitpid(pid, &status, 0);

	// Parse HTTP headers and body from rawOutput
	size_t pos = 0;
	while (pos < rawOutput.size())
	{
		size_t lineEnd = pos;
		while (lineEnd < rawOutput.size() && rawOutput[lineEnd] != '\n')
		{
			lineEnd++;
		}
		if (lineEnd >= rawOutput.size()) break;

		std::string statusLine(reinterpret_cast<const char*>(rawOutput.data() + pos), lineEnd - pos);
		if (!statusLine.empty() && statusLine.back() == '\r')
		{
			statusLine.pop_back();
		}

		if (statusLine.rfind("HTTP/", 0) != 0)
		{
			pos = lineEnd + 1;
			continue;
		}

		int parsedCode = 0;
		auto space1 = statusLine.find(' ');
		if (space1 != std::string::npos)
		{
			try {
				parsedCode = std::stoi(statusLine.substr(space1 + 1));
			} catch (...) {}
		}

		std::unordered_map<std::string, std::string> currentHeaders;
		pos = lineEnd + 1;
		while (pos < rawOutput.size())
		{
			size_t hEnd = pos;
			while (hEnd < rawOutput.size() && rawOutput[hEnd] != '\n')
			{
				hEnd++;
			}

			std::string hLine(reinterpret_cast<const char*>(rawOutput.data() + pos), hEnd - pos);
			if (!hLine.empty() && hLine.back() == '\r')
			{
				hLine.pop_back();
			}

			pos = (hEnd < rawOutput.size()) ? hEnd + 1 : rawOutput.size();

			if (hLine.empty())
			{
				break;
			}

			auto colon = hLine.find(':');
			if (colon != std::string::npos)
			{
				std::string key = hLine.substr(0, colon);
				std::string val = hLine.substr(colon + 1);

				while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
				while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
				for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
				while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();

				currentHeaders[key] = val;
			}
		}

		outResp.statusCode = parsedCode;
		outResp.headers = std::move(currentHeaders);

		if (parsedCode == 100 || (parsedCode >= 300 && parsedCode < 400))
		{
			// Check if another HTTP response follows (redirect chained response)
			size_t peek = pos;
			while (peek < rawOutput.size() && (rawOutput[peek] == '\r' || rawOutput[peek] == '\n'))
			{
				peek++;
			}
			if (peek + 5 <= rawOutput.size() && memcmp(rawOutput.data() + peek, "HTTP/", 5) == 0)
			{
				pos = peek;
				continue;
			}
		}

		outResp.body.assign(rawOutput.begin() + pos, rawOutput.end());
		return true;
	}

	if (outResp.statusCode > 0)
	{
		return true;
	}

	outResp.errorMsg = "No valid HTTP response parsed from curl";
	return false;
}

//Spawn an external instance of curl, read it's stdout into out and return it's exit code
//It's necessary because SteamOS seems broken. Curling certain URLs
//will crash inside libssl.3.so (might have to do with broken certs, idk for sure).
int Curl::getString(const char* url, std::string& out)
{
	CURL_DEBUG("getString(%s)\n", url);

	int pipefd[2];

	if (pipe(pipefd) == -1)
	{
		CURL_ERROR("Failed to create pipe!\n");
		return 1;
	}

	CURL_DEBUG("Created pipe %i : %i\n", pipefd[0], pipefd[1]);

	constexpr static const char* env[] =
	{
		"PATH='/usr/bin:/bin'",
		nullptr
	};

	const char* args[] =
	{
		"--silent",
		"--connect-timeout", "15",
		url,
		nullptr
	};

	const pid_t pid = fork();
	if (pid == -1)
	{
		close(pipefd[0]);
		close(pipefd[1]);

		CURL_ERROR("Failed to fork!\n");
		return 1;
	}

	if (pid == 0)
	{
		if (dup2(pipefd[1], STDOUT_FILENO) == -1)
		{
			CURL_ERROR("Failed to dup2!\n");
			exit(1);
		}

		//No need for reading
		close(pipefd[0]);
		close(pipefd[1]);

		execve("/bin/curl", const_cast<char**>(args), const_cast<char**>(env));
		execve("/usr/bin/curl", const_cast<char**>(args), const_cast<char**>(env));
		//NixOS
		execve("/run/current-system/sw/bin/curl", const_cast<char**>(args), const_cast<char**>(env));

		CURL_DEBUG("Failed to execv curl!\n");
		exit(1);
	}

	//No need for writing
	close(pipefd[1]);

	CURL_DEBUG("Child PID %i\n", pid);

	std::ostringstream bufSS;
	char buf[8192];
	int numRead;

	while((numRead = read(pipefd[0], buf, sizeof(buf))) > 0)
	{
		bufSS << std::string(buf, numRead);
	}

	close(pipefd[0]);

	int status;
	if (waitpid(pid, &status, 0) == -1)
	{
		return 1;
	}

	if (!WIFEXITED(status))
	{
		return 1;
	}

	status = WEXITSTATUS(status);

	CURL_DEBUG("Exit Status: %i\n", status);

	out = bufSS.str();

	return status;
}

int Curl::downloadToFile(const std::string& url, const std::string& destPath, int timeoutSec, const std::string& authHeader)
{
	if (url.empty() || destPath.empty()) return -1;

	// Support local file paths and file:// scheme for testing and offline faking
	std::string localFilePath;
	if (url.rfind("file://", 0) == 0)
	{
		localFilePath = url.substr(7);
	}
	else if (url.rfind("/", 0) == 0)
	{
		localFilePath = url;
	}

	if (!localFilePath.empty())
	{
		std::error_code ec;
		std::filesystem::path p(destPath);
		if (p.has_parent_path())
		{
			std::filesystem::create_directories(p.parent_path(), ec);
		}
		std::filesystem::copy_file(localFilePath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
		return ec ? -1 : 200;
	}

	std::error_code ec;
	std::filesystem::path p(destPath);
	if (p.has_parent_path())
	{
		std::filesystem::create_directories(p.parent_path(), ec);
		if (ec) return -1;
	}

	const std::string tmpPath = AtomicFile::makeTempPath(destPath);
	AtomicFile::TempFileGuard guard(tmpPath);

	int pipefd[2];
	if (pipe(pipefd) == -1)
	{
		return -1;
	}

	const pid_t pid = fork();
	if (pid == -1)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}

	if (pid == 0)
	{
		if (dup2(pipefd[1], STDOUT_FILENO) == -1)
		{
			_exit(1);
		}
		close(pipefd[0]);
		close(pipefd[1]);

		int nullfd = open("/dev/null", O_WRONLY);
		if (nullfd != -1)
		{
			dup2(nullfd, STDERR_FILENO);
			close(nullfd);
		}

		std::vector<std::string> argsStr;
		argsStr.push_back("curl");
		argsStr.push_back("-s");
		argsStr.push_back("-S");
		argsStr.push_back("-L");
		argsStr.push_back("--max-redirs");
		argsStr.push_back("5");
		argsStr.push_back("-w");
		argsStr.push_back("%{http_code}");
		argsStr.push_back("-o");
		argsStr.push_back(tmpPath);
		argsStr.push_back("--connect-timeout");
		argsStr.push_back("15");
		argsStr.push_back("-m");
		argsStr.push_back(std::to_string(timeoutSec > 0 ? timeoutSec : 60));
		argsStr.push_back("-A");
		argsStr.push_back("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

		if (!authHeader.empty())
		{
			argsStr.push_back("-H");
			argsStr.push_back(authHeader);
		}

		argsStr.push_back(url);

		std::vector<char*> args;
		args.reserve(argsStr.size() + 1);
		for (auto& s : argsStr)
		{
			args.push_back(const_cast<char*>(s.c_str()));
		}
		args.push_back(nullptr);

		constexpr static const char* env[] =
		{
			"PATH=/usr/bin:/bin:/usr/local/bin",
			nullptr
		};

		execve("/bin/curl", args.data(), const_cast<char**>(env));
		execve("/usr/bin/curl", args.data(), const_cast<char**>(env));
		execve("/run/current-system/sw/bin/curl", args.data(), const_cast<char**>(env));

		_exit(1);
	}

	close(pipefd[1]);

	std::string statusStr;
	char buf[64];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0)
	{
		buf[n] = '\0';
		statusStr += buf;
	}
	close(pipefd[0]);

	int status = 0;
	if (waitpid(pid, &status, 0) == -1 || !WIFEXITED(status))
	{
		return -1;
	}

	int httpCode = 0;
	try
	{
		if (!statusStr.empty())
		{
			httpCode = std::stoi(statusStr);
		}
	}
	catch (...)
	{
		httpCode = 0;
	}

	if (WEXITSTATUS(status) != 0 || httpCode == 0)
	{
		return -1;
	}

	if (httpCode >= 200 && httpCode < 300)
	{
		std::error_code renameEc;
		std::filesystem::rename(tmpPath, destPath, renameEc);
		if (renameEc)
		{
			std::filesystem::copy_file(tmpPath, destPath, std::filesystem::copy_options::overwrite_existing, renameEc);
			if (renameEc) return -1;
		}
		guard.dismiss();
	}

	return httpCode;
}

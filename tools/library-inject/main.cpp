#include <link.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

static const char* LIBS[] = {
	"libcurl.so",
	"libcurl.so.3",
	"libcurl.so.4",
	"libcurl.so.4.0.0",
	"libcurl.so.4.1.0",
	"libcurl.so.4.2.0",
	"libcurl.so.4.3.0",
	"libcurl.so.4.4.0",
	"libcurl.so.4.5.0",
	"libcurl.so.4.6.0",
	"libcurl.so.4.7.0",
	"libcurl.so.4.8.0",
	nullptr
};

static const char* DIRS[] = {
	"/usr/lib/i386-linux-gnu",
	"/usr/lib32",
	"/usr/local/lib32",
	"/usr/local/lib",
	"/usr/lib",
	nullptr
};

static char s_redirectedPath[512];

char *la_objsearch(const char *name, __attribute__((unused)) uintptr_t *cookie, unsigned int flag)
{
	if (flag != LA_SER_ORIG || !name)
	{
		return const_cast<char*>(name);
	}

	bool match = false;
	for (size_t i = 0; LIBS[i] != nullptr; ++i)
	{
		if (strcmp(name, LIBS[i]) == 0)
		{
			match = true;
			break;
		}
	}

	if (!match)
	{
		return const_cast<char*>(name);
	}

	for (size_t i = 0; DIRS[i] != nullptr; ++i)
	{
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", DIRS[i], name);
		if (access(path, F_OK) == 0)
		{
			strncpy(s_redirectedPath, path, sizeof(s_redirectedPath) - 1);
			s_redirectedPath[sizeof(s_redirectedPath) - 1] = '\0';
			return s_redirectedPath;
		}
	}

	return const_cast<char*>(name);
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}

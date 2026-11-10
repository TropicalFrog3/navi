/**
 * @file  filewatcher.cpp
 * @brief Inotify wrapper for watching configuration and IPC file changes
 */
#include "filewatcher.hpp"

#include "log.hpp"

#include <cstring>
#include <filesystem>
#include <linux/limits.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>


//TODO: Investigate why gcc complains when put into CFileWatcher itself
void* watchLoop(void* args)
{
	auto watcher = reinterpret_cast<CFileWatcher*>(args);
	LOG_DEBUG("Started FileWatcher %u\n", watcher->notifyFd);

	for(;;)
	{
		LOG_DEBUG("Watching for changes...\n");

		struct pollfd fds[2];
		fds[0].fd = watcher->notifyFd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = watcher->wakeFd;
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		int ret = poll(fds, 2, -1);
		if (ret < 0)
		{
			// If poll was interrupted by a signal, try again
			if (errno == EINTR)
				continue;

			LOG_DEBUG("poll error: %d\n", errno);
			break;
		}

		if (fds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
		{
			break;
		}

		if (fds[0].revents & POLLIN)
		{
			char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
			ssize_t len = read(watcher->notifyFd, buffer, sizeof(buffer));
			if (len <= 0)
			{
				// If read was interrupted by a signal, try again
				if (len == -1 && errno == EINTR)
					continue;

				LOG_DEBUG("inotify read error or closed: %d\n", errno);
				break;
			}

			for (char *ptr = buffer; ptr < buffer + len;
					ptr += sizeof(struct inotify_event) + reinterpret_cast<struct inotify_event *>(ptr)->len)
			{
				const struct inotify_event *event = reinterpret_cast<const struct inotify_event *>(ptr);
				
				if (watcher->fileFdMap.contains(event->wd))
				{
					LOG_DEBUG("inotify %u(%s) -> %u (name: %s)\n", 
						event->wd, 
						watcher->fileFdMap[event->wd].c_str(), 
						event->mask,
						event->len ? event->name : "N/A");
					
					watcher->onModify(event->len ? event->name : nullptr);
				}
			}
		}
		else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			LOG_DEBUG("inotify fd error or hangup: %d\n", fds[0].revents);
			break;
		}
	}

	return nullptr;
}

CFileWatcher::CFileWatcher(const FileModifyEvent_t onModify)
{
	this->onModify = onModify;
	watchThread = 0;

	notifyFd = inotify_init();
	LOG_DEBUG("Created notify fd %i\n", notifyFd);
	wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	LOG_DEBUG("Created wake fd %i\n", wakeFd);
}

CFileWatcher::~CFileWatcher()
{
	stop();
}

bool CFileWatcher::addWatch(const char* path, uint32_t mask)
{
	int fd = inotify_add_watch(notifyFd, path, mask);
	if (fd == -1)
	{
		return false;
	}

	fileFdMap[fd] = path;
	LOG_DEBUG("Added %s (mask %u) to FileWatcher %i\n", path, mask, notifyFd);
	return fd != -1;
}

bool CFileWatcher::start()
{
	int code = pthread_create(&watchThread, nullptr, &watchLoop, this);
	if (code != 0)
	{
		watchThread = 0;
		return false;
	}
	return true;
}

void CFileWatcher::stop()
{
	if (wakeFd != -1)
	{
		uint64_t val = 1;
		[[maybe_unused]] ssize_t ret = write(wakeFd, &val, sizeof(val));
	}

	if (watchThread)
	{
		pthread_join(watchThread, nullptr);
		watchThread = 0;
	}

	if (wakeFd != -1)
	{
		close(wakeFd);
		wakeFd = -1;
	}

	if (notifyFd != -1)
	{
		close(notifyFd);
		notifyFd = -1;
	}
}

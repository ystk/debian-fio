/* This file contains functions which implement those POSIX and Linux functions
 * that MinGW and Microsoft don't provide. The implementations contain just enough
 * functionality to support fio.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/poll.h>

#include "../os-windows.h"

long sysconf(int name)
{
	long long val = -1;
	DWORD len;
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION processorInfo;
	SYSTEM_INFO sysInfo;
	MEMORYSTATUSEX status;

	switch (name)
	{
	case _SC_NPROCESSORS_ONLN:
		len = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		GetLogicalProcessorInformation(&processorInfo, &len);
		val = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		break;

	case _SC_PAGESIZE:
		GetSystemInfo(&sysInfo);
		val = sysInfo.dwPageSize;
		break;

	case _SC_PHYS_PAGES:
		status.dwLength = sizeof(status);
		GlobalMemoryStatusEx(&status);
		val = status.ullTotalPhys;
		break;
	default:
		log_err("sysconf(%d) is not implemented\n", name);
		break;
	}

	return val;
}

char *dl_error = NULL;

int dlclose(void *handle)
{
	return !FreeLibrary((HMODULE)handle);
}

void *dlopen(const char *file, int mode)
{
	HMODULE hMod;

	hMod = LoadLibrary(file);
	if (hMod == INVALID_HANDLE_VALUE)
		dl_error = (char*)"LoadLibrary failed";
	else
		dl_error = NULL;

	return hMod;
}

void *dlsym(void *handle, const char *name)
{
	FARPROC fnPtr;

	fnPtr = GetProcAddress((HMODULE)handle, name);
	if (fnPtr == NULL)
		dl_error = (char*)"GetProcAddress failed";
	else
		dl_error = NULL;

	return fnPtr;
}

char *dlerror(void)
{
	return dl_error;
}

int gettimeofday(struct timeval *restrict tp, void *restrict tzp)
{
	FILETIME fileTime;
	unsigned long long unix_time, windows_time;
	const time_t MILLISECONDS_BETWEEN_1601_AND_1970 = 11644473600000;

	/* Ignore the timezone parameter */
	(void)tzp;

	/*
	 * Windows time is stored as the number 100 ns intervals since January 1 1601.
	 * Conversion details from http://www.informit.com/articles/article.aspx?p=102236&seqNum=3
	 * Its precision is 100 ns but accuracy is only one clock tick, or normally around 15 ms.
	 */
	GetSystemTimeAsFileTime(&fileTime);
	windows_time = ((unsigned long long)fileTime.dwHighDateTime << 32) + fileTime.dwLowDateTime;
	/* Divide by 10,000 to convert to ms and subtract the time between 1601 and 1970 */
	unix_time = (((windows_time)/10000) - MILLISECONDS_BETWEEN_1601_AND_1970);
	/* unix_time is now the number of milliseconds since 1970 (the Unix epoch) */
	tp->tv_sec = unix_time / 1000;
	tp->tv_usec = (unix_time % 1000) * 1000;
	return 0;
}

void syslog(int priority, const char *message, ... /* argument */)
{
	log_err("%s is not implemented\n", __func__);
}

int sigaction(int sig, const struct sigaction *act,
		struct sigaction *oact)
{
	int rc = 0;
	void (*prev_handler)(int);

	prev_handler = signal(sig, act->sa_handler);
	if (oact != NULL)
		oact->sa_handler = prev_handler;

	if (prev_handler == SIG_ERR)
		rc = -1;

	return rc;
}

int lstat(const char * path, struct stat * buf)
{
	return stat(path, buf);
}

void *mmap(void *addr, size_t len, int prot, int flags,
		int fildes, off_t off)
{
	DWORD vaProt = 0;
	void* allocAddr = NULL;

	if (prot & PROT_NONE)
		vaProt |= PAGE_NOACCESS;

	if ((prot & PROT_READ) && !(prot & PROT_WRITE))
		vaProt |= PAGE_READONLY;

	if (prot & PROT_WRITE)
		vaProt |= PAGE_READWRITE;

	if ((flags & MAP_ANON) | (flags & MAP_ANONYMOUS))
	{
		allocAddr = VirtualAlloc(addr, len, MEM_COMMIT, vaProt);
	}

	return allocAddr;
}

int munmap(void *addr, size_t len)
{
	return !VirtualFree(addr, 0, MEM_RELEASE);
}

int fork(void)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

pid_t setsid(void)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

void openlog(const char *ident, int logopt, int facility)
{
	log_err("%s is not implemented\n", __func__);
}

void closelog(void)
{
	log_err("%s is not implemented\n", __func__);
}

int kill(pid_t pid, int sig)
{
	errno = ESRCH;
	return (-1);
}

/*
 * This is assumed to be used only by the network code,
 * and so doesn't try and handle any of the other cases
 */
int fcntl(int fildes, int cmd, ...)
{
	/*
	 * non-blocking mode doesn't work the same as in BSD sockets,
	 * so ignore it.
	 */
#if 0
	va_list ap;
	int val, opt, status;

	if (cmd == F_GETFL)
		return 0;
	else if (cmd != F_SETFL) {
		errno = EINVAL;
		return (-1);
	}

	va_start(ap, 1);

	opt = va_arg(ap, int);
	if (opt & O_NONBLOCK)
		val = 1;
	else
		val = 0;

	status = ioctlsocket((SOCKET)fildes, opt, &val);

	if (status == SOCKET_ERROR) {
		errno = EINVAL;
		val = -1;
	}

	va_end(ap);

	return val;
#endif
return 0;
}

/*
 * Get the value of a local clock source.
 * This implementation supports 2 clocks: CLOCK_MONOTONIC provides high-accuracy
 * relative time, while CLOCK_REALTIME provides a low-accuracy wall time.
 */
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	int rc = 0;

	if (clock_id == CLOCK_MONOTONIC)
	{
		static LARGE_INTEGER freq = {{0,0}};
		LARGE_INTEGER counts;

		QueryPerformanceCounter(&counts);
		if (freq.QuadPart == 0)
			QueryPerformanceFrequency(&freq);

		tp->tv_sec = counts.QuadPart / freq.QuadPart;
		/* Get the difference between the number of ns stored
		 * in 'tv_sec' and that stored in 'counts' */
		unsigned long long t = tp->tv_sec * freq.QuadPart;
		t = counts.QuadPart - t;
		/* 't' now contains the number of cycles since the last second.
		 * We want the number of nanoseconds, so multiply out by 1,000,000,000
		 * and then divide by the frequency. */
		t *= 1000000000;
		tp->tv_nsec = t / freq.QuadPart;
	}
	else if (clock_id == CLOCK_REALTIME)
	{
		/* clock_gettime(CLOCK_REALTIME,...) is just an alias for gettimeofday with a
		 * higher-precision field. */
		struct timeval tv;
		gettimeofday(&tv, NULL);
		tp->tv_sec = tv.tv_sec;
		tp->tv_nsec = tv.tv_usec * 1000;
	} else {
		errno = EINVAL;
		rc = -1;
	}

	return rc;
}

int mlock(const void * addr, size_t len)
{
	return !VirtualLock((LPVOID)addr, len);
}

int munlock(const void * addr, size_t len)
{
	return !VirtualUnlock((LPVOID)addr, len);
}

pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return -1;
}

int usleep(useconds_t useconds)
{
	Sleep(useconds / 1000);
	return 0;
}

char *basename(char *path)
{
	static char name[MAX_PATH];
	int i;

	if (path == NULL || strlen(path) == 0)
		return (char*)".";

	i = strlen(path) - 1;

	while (name[i] != '\\' && name[i] != '/' && i >= 0)
		i--;

	strcpy(name, path + i);

	return name;
}

int posix_fallocate(int fd, off_t offset, off_t len)
{
	const int BUFFER_SIZE = 64*1024*1024;
	int rc = 0;
	char *buf;
	unsigned int write_len;
	unsigned int bytes_written;
	off_t bytes_remaining = len;

	if (len == 0 || offset < 0)
		return EINVAL;

	buf = malloc(BUFFER_SIZE);

	if (buf == NULL)
		return ENOMEM;

	memset(buf, 0, BUFFER_SIZE);

	if (lseek(fd, offset, SEEK_SET) == -1)
		return errno;

	while (bytes_remaining > 0) {
		if (bytes_remaining < BUFFER_SIZE)
			write_len = (unsigned int)bytes_remaining;
		else
			write_len = BUFFER_SIZE;

		bytes_written = _write(fd, buf, write_len);
		if (bytes_written == -1) {
			rc = errno;
			break;
		}

		bytes_remaining -= bytes_written;
	}

	free(buf);
	return rc;
}

int ftruncate(int fildes, off_t length)
{
	BOOL bSuccess;
	int old_pos = tell(fildes);
	lseek(fildes, length, SEEK_SET);
	HANDLE hFile = (HANDLE)_get_osfhandle(fildes);
	bSuccess = SetEndOfFile(hFile);
	lseek(fildes, old_pos, SEEK_SET);
	return !bSuccess;
}

int fsync(int fildes)
{
	HANDLE hFile = (HANDLE)_get_osfhandle(fildes);
	return !FlushFileBuffers(hFile);
}

int nFileMappings = 0;
HANDLE fileMappings[1024];

int shmget(key_t key, size_t size, int shmflg)
{
	int mapid = -1;
	HANDLE hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, (PAGE_EXECUTE_READWRITE | SEC_RESERVE), size >> 32, size & 0xFFFFFFFF, NULL);
	if (hMapping != NULL) {
		fileMappings[nFileMappings] = hMapping;
		mapid = nFileMappings;
		nFileMappings++;
	} else {
		errno = ENOSYS;
	}

	return mapid;
}

void *shmat(int shmid, const void *shmaddr, int shmflg)
{
	void* mapAddr;
	MEMORY_BASIC_INFORMATION memInfo;
	mapAddr = MapViewOfFile(fileMappings[shmid], FILE_MAP_ALL_ACCESS, 0, 0, 0);
	VirtualQuery(mapAddr, &memInfo, sizeof(memInfo));
	mapAddr = VirtualAlloc(mapAddr, memInfo.RegionSize, MEM_COMMIT, PAGE_READWRITE);
	return mapAddr;
}

int shmdt(const void *shmaddr)
{
	return !UnmapViewOfFile(shmaddr);
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	if (cmd == IPC_RMID) {
		fileMappings[shmid] = INVALID_HANDLE_VALUE;
		return 0;
	} else {
		log_err("%s is not implemented\n", __func__);
	}
	return (-1);
}

int setuid(uid_t uid)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

int setgid(gid_t gid)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

int nice(int incr)
{
	if (incr != 0) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

int getrusage(int who, struct rusage *r_usage)
{
	const time_t SECONDS_BETWEEN_1601_AND_1970 = 11644473600;
	FILETIME cTime, eTime, kTime, uTime;
	time_t time;

	memset(r_usage, 0, sizeof(*r_usage));

	HANDLE hProcess = GetCurrentProcess();
	GetProcessTimes(hProcess, &cTime, &eTime, &kTime, &uTime);
	time = ((unsigned long long)uTime.dwHighDateTime << 32) + uTime.dwLowDateTime;
	/* Divide by 10,000,000 to get the number of seconds and move the epoch from
	 * 1601 to 1970 */
	time = (time_t)(((time)/10000000) - SECONDS_BETWEEN_1601_AND_1970);
	r_usage->ru_utime.tv_sec = time;
	/* getrusage() doesn't care about anything other than seconds, so set tv_usec to 0 */
	r_usage->ru_utime.tv_usec = 0;
	time = ((unsigned long long)kTime.dwHighDateTime << 32) + kTime.dwLowDateTime;
	/* Divide by 10,000,000 to get the number of seconds and move the epoch from
	 * 1601 to 1970 */
	time = (time_t)(((time)/10000000) - SECONDS_BETWEEN_1601_AND_1970);
	r_usage->ru_stime.tv_sec = time;
	r_usage->ru_stime.tv_usec = 0;
	return 0;
}

int posix_madvise(void *addr, size_t len, int advice)
{
	log_err("%s is not implemented\n", __func__);
	return ENOSYS;
}

/* Windows doesn't support advice for memory pages. Just ignore it. */
int msync(void *addr, size_t len, int flags)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return -1;
}

int fdatasync(int fildes)
{
	return fsync(fildes);
}

ssize_t pwrite(int fildes, const void *buf, size_t nbyte,
		off_t offset)
{
	long pos = tell(fildes);
	ssize_t len = write(fildes, buf, nbyte);
	lseek(fildes, pos, SEEK_SET);
	return len;
}

ssize_t pread(int fildes, void *buf, size_t nbyte, off_t offset)
{
	long pos = tell(fildes);
	ssize_t len = read(fildes, buf, nbyte);
	lseek(fildes, pos, SEEK_SET);
	return len;
}

ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return (-1);
}

long long strtoll(const char *restrict str, char **restrict endptr,
		int base)
{
	return _strtoi64(str, endptr, base);
}

char *strsep(char **stringp, const char *delim)
{
	char *orig = *stringp;
	BOOL gotMatch = FALSE;
	int i = 0;
	int j = 0;

	if (*stringp == NULL)
		return NULL;

	while ((*stringp)[i] != '\0') {
		j = 0;
		while (delim[j] != '\0') {
			if ((*stringp)[i] == delim[j]) {
				gotMatch = TRUE;
				(*stringp)[i] = '\0';
				*stringp = *stringp + i + 1;
				break;
			}
			j++;
		}
		if (gotMatch)
			break;

		i++;
	}

	if (!gotMatch)
		*stringp = NULL;

	return orig;
}

int poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
	struct timeval tv;
	struct timeval *to = NULL;
	fd_set readfds, writefds, exceptfds;
	int i;
	int rc;

	if (timeout != -1)
		to = &tv;

	to->tv_sec = timeout / 1000;
	to->tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	for (i = 0; i < nfds; i++)
	{
		if (fds[i].fd < 0) {
			fds[i].revents = 0;
			continue;
		}

		if (fds[i].events & POLLIN)
			FD_SET(fds[i].fd, &readfds);

		if (fds[i].events & POLLOUT)
			FD_SET(fds[i].fd, &writefds);

		FD_SET(fds[i].fd, &exceptfds);
	}

	rc = select(nfds, &readfds, &writefds, &exceptfds, to);

	if (rc != SOCKET_ERROR) {
		for (i = 0; i < nfds; i++)
		{
			if (fds[i].fd < 0) {
				continue;
			}

			if ((fds[i].events & POLLIN) && FD_ISSET(fds[i].fd, &readfds))
				fds[i].revents |= POLLIN;

			if ((fds[i].events & POLLOUT) && FD_ISSET(fds[i].fd, &writefds))
				fds[i].revents |= POLLOUT;

			if (FD_ISSET(fds[i].fd, &exceptfds))
				fds[i].revents |= POLLHUP;
		}
	}

	return rc;
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return -1;
}

DIR *opendir(const char *dirname)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return NULL;
}

int closedir(DIR *dirp)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return -1;
}

struct dirent *readdir(DIR *dirp)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return NULL;
}

uid_t geteuid(void)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return -1;
}

int inet_aton(char *addr)
{
	log_err("%s is not implemented\n", __func__);
	errno = ENOSYS;
	return 0;
}

const char* inet_ntop(int af, const void *restrict src,
		char *restrict dst, socklen_t size)
{
	INT status = SOCKET_ERROR;
	WSADATA wsd;
	char *ret = NULL;

	if (af != AF_INET && af != AF_INET6) {
		errno = EAFNOSUPPORT;
		return NULL;
	}

	WSAStartup(MAKEWORD(2,2), &wsd);

	if (af == AF_INET) {
		struct sockaddr_in si;
		DWORD len = size;
		memset(&si, 0, sizeof(si));
		si.sin_family = af;
		memcpy(&si.sin_addr, src, sizeof(si.sin_addr));
		status = WSAAddressToString((struct sockaddr*)&si, sizeof(si), NULL, dst, &len);
	} else if (af == AF_INET6) {
		struct sockaddr_in6 si6;
		DWORD len = size;
		memset(&si6, 0, sizeof(si6));
		si6.sin6_family = af;
		memcpy(&si6.sin6_addr, src, sizeof(si6.sin6_addr));
		status = WSAAddressToString((struct sockaddr*)&si6, sizeof(si6), NULL, dst, &len);
	}

	if (status != SOCKET_ERROR)
		ret = dst;
	else
		errno = ENOSPC;

	WSACleanup();
	return ret;
}

int inet_pton(int af, const char *restrict src, void *restrict dst)
{
	INT status = SOCKET_ERROR;
	WSADATA wsd;
	int ret = 1;

	if (af != AF_INET && af != AF_INET6) {
		errno = EAFNOSUPPORT;
		return -1;
	}

	WSAStartup(MAKEWORD(2,2), &wsd);

	if (af == AF_INET) {
		struct sockaddr_in si;
		INT len = sizeof(si);
		memset(&si, 0, sizeof(si));
		si.sin_family = af;
		status = WSAStringToAddressA((char*)src, af, NULL, (struct sockaddr*)&si, &len);
		if (status != SOCKET_ERROR)
			memcpy(dst, &si.sin_addr, sizeof(si.sin_addr));
	} else if (af == AF_INET6) {
		struct sockaddr_in6 si6;
		INT len = sizeof(si6);
		memset(&si6, 0, sizeof(si6));
		si6.sin6_family = af;
		status = WSAStringToAddressA((char*)src, af, NULL, (struct sockaddr*)&si6, &len);
		if (status != SOCKET_ERROR)
			memcpy(dst, &si6.sin6_addr, sizeof(si6.sin6_addr));
	}

	if (status == SOCKET_ERROR) {
		errno = ENOSPC;
		ret = 0;
	}

	WSACleanup();

	return ret;
}

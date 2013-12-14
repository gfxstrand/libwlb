/*
 * Copyright © 2012 Collabora, Ltd.
 * Copyright © 2013 Jason Ekstrand
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "wlb-private.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static inline int
set_cloexec_or_close(int fd)
{
	long flags;

	if (fd < 0)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		goto err;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

static int
create_tmpfile_cloexec(char *template)
{
	int fd;

#ifdef HAVE_MKOSTEMP
	fd = mkostemp(template, O_CLOEXEC);
	if (fd >= 0)
		unlink(template);
#else
	fd = mkstemp(template);
	if (fd >= 0) {
		fd = set_cloexec_or_close(fd);
		unlink(template);
	}
#endif

	return fd;
}

int
wlb_util_create_tmpfile(size_t size)
{
	static const char template[] = "/libwlb-shared-XXXXXX";
	const char *path;
	char *name;
	int fd, ret;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		errno = ENOENT;
		return -1;
	}

	name = malloc(strlen(path) + sizeof(template));
	if (!name)
		return -1;

	strcpy(name, path);
	strcat(name, template);

	fd = create_tmpfile_cloexec(name);

	free(name);

	if (fd < 0)
		return -1;

#ifdef HAVE_POSIX_FALLOCATE
	ret = posix_fallocate(fd, 0, size);
	if (ret != 0) {
		close(fd);
		errno = ret;
		return -1;
	}
#else
	ret = ftruncate(fd, size);
	if (ret < 0) {
		close(fd);
		return -1;
	}
#endif

	return fd;
}

int
wlb_log(enum wlb_log_level level, const char *format, ...)
{
	int nchars;
	va_list ap;

	va_start(ap, format);
	switch (level) {
	case WLB_LOG_LEVEL_ERROR:
	case WLG_LOG_LEVEL_WARNING:
		nchars = vfprintf(stderr, format, ap);
		break;
	case WLB_LOG_LEVEL_DEBUG:
	default:
		nchars = vfprintf(stdout, format, ap);
	}
	va_end(ap);

	return nchars;
}

void *
zalloc(size_t size)
{
	return calloc(1, size);
}

#include <stddef.h>
#include <sys/errno.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/conversion/dirent/getdirentries.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/conversion/common_at.h>
#include <darling/emulation/linux_premigration/vchroot_expand.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/fstatat.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/fstat.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/statfs.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/fstatfs.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/xnu_syscall/bsd/helper/xattr/attrlist_pack.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/fdpath.h>

/*
 * getattrlist(2), getattrlistat(2) (HAS_PATH) and fgetattrlist(2): the attributes of one file system object, or of
 * the volume holding it (ATTR_VOL_INFO), packed as described in attrlist_pack.h. Finder info and the resource fork
 * length come from the com.apple.FinderInfo and com.apple.ResourceFork extended attributes.
 */

#define XATTR_FINDER_INFO "com.apple.FinderInfo"
#define XATTR_RESOURCE_FORK "com.apple.ResourceFork"
#define ATTRLIST_LINUX_O_PATH 010000000
#define ATTRLIST_LINUX_O_NOFOLLOW 0400000

extern char *strcpy(char *dest, const char *src);
extern void __darling_errtrace_note(const char* what, unsigned long a, unsigned long b, unsigned long c,
	unsigned long d, unsigned long e, unsigned long f);

#ifndef ATTRLIST_GENERIC_HELPERS
#define ATTRLIST_GENERIC_HELPERS
// Last component of a path ("" for "/").
static const char* attrlist_basename(const char* path, char* buf, __SIZE_TYPE__ bufsize)
{
	__SIZE_TYPE__ len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		len--;
	__SIZE_TYPE__ start = len;
	while (start > 0 && path[start - 1] != '/')
		start--;
	if (len - start >= bufsize)
		len = start + bufsize - 1;
	memcpy(buf, path + start, len - start);
	buf[len - start] = 0;
	return (buf[0] == '/' || buf[0] == 0) ? "" : buf;
}

// Inode number of the directory holding the Linux path `path` (relative to dfd), 0 if unknown.
static uint64_t attrlist_parent_ino(int dfd, const char* path)
{
	char parent[4096];
	__SIZE_TYPE__ len = strlen(path);
	if (len >= sizeof(parent) - 4)
		return 0;
	memcpy(parent, path, len);
	while (len > 1 && parent[len - 1] == '/')
		len--;
	while (len > 0 && parent[len - 1] != '/')
		len--;
	if (len == 0)
		strcpy(parent, ".");
	else
		parent[len] = 0;
	uint64_t stx[32]; // struct statx: stx_ino at byte 32
	if (LINUX_SYSCALL(__NR_statx, dfd, parent, 0, 0x100 /* STATX_INO */, stx) < 0)
		return 0;
	return stx[4];
}
#endif

long
FUNC_NAME(int fd,

#if HAS_PATH
const char* path,
#endif

struct xnu_attrlist* alist, void *attributeBuffer, __SIZE_TYPE__ bufferSize, unsigned long options)
{
	int rv;
	char linux_path[4096];
	int linux_dfd;
	char namebuf[256];
	const char* name;
	struct stat64 st;
	struct bsd_statfs64 fs;
	char finder_info[32];
	int have_finder_info = 0;
	uint64_t rsrc_length = 0;
	char full_path[1024];
	int have_full_path = 0;

	if (!alist || !attributeBuffer)
		return -EFAULT;
	rv = attrpk_check(alist, options, 0);
	if (rv < 0) {
		__darling_errtrace_note("getattrlist", alist->bitmapcount, alist->commonattr, alist->volattr, alist->dirattr,
			alist->fileattr, ((unsigned long) alist->forkattr << 32) | (uint32_t) options);
		return rv;
	}

#if HAS_PATH
	if (!path)
		return -EFAULT;

	struct vchroot_expand_args vc;
	vc.flags = (options & ATTRPK_FSOPT_NOFOLLOW) ? 0 : VCHROOT_FOLLOW;
	vc.dfd = atfd(fd);

	strcpy(vc.path, path);
	rv = vchroot_expand(&vc);
	if (rv < 0)
		return rv;

	rv = sys_fstatat64(fd, path, &st, (options & ATTRPK_FSOPT_NOFOLLOW) ? 0x20 /* AT_SYMLINK_NOFOLLOW */ : 0);
	if (rv < 0)
		return rv;
	strcpy(linux_path, vc.path);
	linux_dfd = vc.dfd;
	name = attrlist_basename(path, namebuf, sizeof(namebuf));
	if (alist->volattr) {
		rv = sys_statfs64(path, &fs);
		if (rv < 0)
			return rv;
	}
#else
	rv = sys_fstat64(fd, &st);
	if (rv < 0)
		return rv;
	{
		char proc[64];
		__simple_sprintf(proc, "/proc/self/fd/%d", fd);
		long n = LINUX_SYSCALL(__NR_readlinkat, LINUX_AT_FDCWD, proc, linux_path, sizeof(linux_path) - 1);
		linux_path[n > 0 ? n : 0] = 0;
	}
	linux_dfd = LINUX_AT_FDCWD;
	name = attrlist_basename(linux_path, namebuf, sizeof(namebuf));
	if (alist->volattr) {
		rv = sys_fstatfs64(fd, &fs);
		if (rv < 0)
			return rv;
	}
#endif

	if (alist->commonattr & ATTRPK_CMN_FNDRINFO) {
#if HAS_PATH
		rv = LINUX_SYSCALL(__NR_getxattr, linux_path, XATTR_FINDER_INFO, finder_info, 32);
#else
		rv = LINUX_SYSCALL(__NR_fgetxattr, fd, XATTR_FINDER_INFO, finder_info, 32);
#endif
		have_finder_info = rv == 32;
	}
	if (alist->fileattr && !S_ISDIR(st.st_mode)) {
#if HAS_PATH
		rv = LINUX_SYSCALL(__NR_getxattr, linux_path, XATTR_RESOURCE_FORK, NULL, 0);
#else
		rv = LINUX_SYSCALL(__NR_fgetxattr, fd, XATTR_RESOURCE_FORK, NULL, 0);
#endif
		if (rv > 0)
			rsrc_length = rv;
	}

	if (alist->commonattr & ATTRPK_CMN_FULLPATH) {
#if HAS_PATH
		int pfd = LINUX_SYSCALL(__NR_openat, linux_dfd, linux_path, ATTRLIST_LINUX_O_PATH | LINUX_O_CLOEXEC
			| ((options & ATTRPK_FSOPT_NOFOLLOW) ? ATTRLIST_LINUX_O_NOFOLLOW : 0), 0);
		if (pfd >= 0) {
			have_full_path = fdpath(pfd, full_path, sizeof(full_path)) >= 0;
			LINUX_SYSCALL(__NR_close, pfd);
		}
#else
		have_full_path = fdpath(fd, full_path, sizeof(full_path)) >= 0;
#endif
	}

	struct attrpk_object obj = {
		.name = (alist->volattr || !name[0]) ? NULL : name,
		.full_path = have_full_path ? full_path : NULL,
		.st = &st,
		.parent_ino = linux_path[0] ? attrlist_parent_ino(linux_dfd, linux_path) : 0,
#if HAS_PATH
		.entry_dirfd = linux_dfd,
		.entry_path = linux_path,
#else
		.entry_dirfd = fd,
		.entry_path = ".",
#endif
		.finder_info = have_finder_info ? finder_info : NULL,
		.rsrc_length = rsrc_length,
		.fs = alist->volattr ? &fs : NULL,
	};
	if (!obj.name)
		obj.name = attrpk_volume_name(&obj);

	struct attrpk_masks m;
	attrpk_masks(alist, options, &obj, &m);
	uint32_t full = attrpk_size(alist, &m, &obj);
	char* buf = (char*) __builtin_alloca(full);
	uint32_t length = (options & ATTRPK_FSOPT_REPORT_FULLSIZE) || bufferSize >= full ? full : (uint32_t) bufferSize;
	attrpk_pack(buf, full, length, &m, &obj);
	memcpy(attributeBuffer, buf, bufferSize < full ? bufferSize : full);
	return 0;
}

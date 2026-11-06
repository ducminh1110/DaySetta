#include <darling/emulation/xnu_syscall/bsd/impl/xattr/getattrlistbulk.h>

#include <stdint.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/dirent/getdirentries.h>
#include <darling/emulation/conversion/xattr/getattrlist.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/fstatat.h>
#include <darling/emulation/xnu_syscall/bsd/impl/stat/fstat.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/xnu_syscall/bsd/helper/xattr/attrlist_pack.h>

extern void __darling_errtrace_note(const char* what, unsigned long a, unsigned long b, unsigned long c,
	unsigned long d, unsigned long e, unsigned long f);

/*
 * getattrlistbulk(2): one attribute record per directory entry (see attrlist_pack.h for the layout). Entries come
 * from getdents64; attributes from fstatat. When the caller's buffer fills up, the directory offset is moved back
 * to the first entry not returned, so the next call continues there. Apple's fts (ls, find, NSFileManager...)
 * is built on this call.
 */

long sys_getattrlistbulk(int dirfd, struct xnu_attrlist* alist, void* attributeBuffer, __SIZE_TYPE__ bufferSize,
	uint64_t options)
{
	if (!alist || !attributeBuffer)
		return -EFAULT;
	int rv = attrpk_check(alist, options, 1);
	if (rv < 0) {
		__darling_errtrace_note("getattrlistbulk", alist->commonattr, alist->volattr, alist->dirattr,
			alist->fileattr, alist->forkattr, options);
		return rv;
	}

	struct stat64 parent;
	long ret = sys_fstat64(dirfd, &parent);
	if (ret < 0)
		return ret;
	if (!S_ISDIR(parent.st_mode))
		return -ENOTDIR;

	char* out = attributeBuffer;
	__SIZE_TYPE__ used = 0;
	long count = 0;
	char dents[4096];

	for (;;) {
		long pos = LINUX_SYSCALL(__NR_lseek, dirfd, 0, 1 /* SEEK_CUR */);
		long n = LINUX_SYSCALL(__NR_getdents64, dirfd, dents, sizeof(dents));
		if (n < 0)
			return count ? count : errno_linux_to_bsd(n);
		if (n == 0)
			return count;

		for (char* d = dents; d < dents + n; d += ((struct linux_dirent64*) d)->d_reclen) {
			struct linux_dirent64* de = (struct linux_dirent64*) d;
			const char* name = de->d_name;
			if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
				pos = de->d_off;
				continue;
			}

			struct stat64 st;
			if (sys_fstatat64(dirfd, name, &st, 0x20 /* AT_SYMLINK_NOFOLLOW */) < 0) {
				pos = de->d_off; // vanished meanwhile
				continue;
			}

			struct attrpk_object obj = {
				.name = name,
				.st = &st,
				.parent_ino = parent.st_ino,
				.entry_dirfd = dirfd,
				.entry_path = name,
				.mount_point = st.st_dev != parent.st_dev,
				.bulk = 1,
			};
			struct attrpk_masks m;
			attrpk_masks(alist, options, &obj, &m);
			uint32_t length = attrpk_size(alist, &m, &obj);
			uint32_t size = (length + 7) & ~7u;

			if (used + size > bufferSize) {
				// Give the rest back to the next call.
				LINUX_SYSCALL(__NR_lseek, dirfd, pos, 0 /* SEEK_SET */);
				return count ? count : -ERANGE;
			}
			attrpk_pack(out + used, size, size, &m, &obj);
			used += size;
			count++;
			pos = de->d_off;
		}
	}
}

#ifndef DARLING_ATTRLIST_PACK_H
#define DARLING_ATTRLIST_PACK_H

/*
 * Attribute packing shared by getattrlist(2), fgetattrlist(2), getattrlistat(2) and getattrlistbulk(2), laid out
 * the way xnu's vfs_attrlist.c does it: a u32 length, ATTR_CMN_RETURNED_ATTRS first (when requested) and
 * ATTR_CMN_ERROR right after it (bulk only), then the common attributes in bit order, then either the volume
 * attributes or the directory, file and extended common (ATTR_CMNEXT_*, in forkattr with FSOPT_ATTR_CMN_EXTENDED)
 * ones, each fixed part rounded to 4 bytes. Variable-length data (names, paths) follows the fixed part and is
 * referenced by attrreference_t {offset from the reference, length}.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <darling/emulation/conversion/xattr/getattrlist.h>
#include <darling/emulation/conversion/stat/common.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/conversion/dirent/getdirentries.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

extern void *memcpy(void *dest, const void *src, __SIZE_TYPE__ n);
extern void *memset(void *s, int c, __SIZE_TYPE__ n);
extern __SIZE_TYPE__ strlen(const char *s);

#define ATTRPK_BITMAP_COUNT 5

#define ATTRPK_CMN_NAME 0x00000001u
#define ATTRPK_CMN_FNDRINFO 0x00004000u
#define ATTRPK_CMN_ERROR 0x20000000u
#define ATTRPK_CMN_RETURNED_ATTRS 0x80000000u
#define ATTRPK_VOL_INFO 0x80000000u
#define ATTRPK_FILE_RSRCLENGTH 0x00001000u

#define ATTRPK_FSOPT_NOFOLLOW 0x00000001
#define ATTRPK_FSOPT_REPORT_FULLSIZE 0x00000004
#define ATTRPK_FSOPT_PACK_INVAL_ATTRS 0x00000008
#define ATTRPK_FSOPT_ATTR_CMN_EXTENDED 0x00000020

// vnode types (sys/vnode.h)
enum { ATTRPK_VNON, ATTRPK_VREG, ATTRPK_VDIR, ATTRPK_VBLK, ATTRPK_VCHR, ATTRPK_VLNK, ATTRPK_VSOCK, ATTRPK_VFIFO };

// Sizes of the fixed part of each attribute, by bit; 0 = not supported here (request fails with EINVAL).
// attrreference_t is 8 bytes, struct timespec 16 (LP64), off_t 8, extentrecord 64.
static const uint8_t attrpk_common_sizes[32] = {
	8, 4, 8, 4, 4, 8, 8, 8, 4, 16, 16, 16, 16, 16, 32, 4, // NAME .. OWNERID
	4, 4, 4, 4, 4, 4, 8, 16, 16, 8, 8, 8, 16, 4, 4, 20,   // GRPID .. RETURNED_ATTRS
};
static const uint8_t attrpk_vol_sizes[32] = {
	4, 4, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 8, 8, 4, 8,       // FSTYPE .. MOUNTEDDEVICE
	8, 32, 16, 0, 8, 4, 4, 8, 0, 0, 0, 0, 8, 8, 40, 0,    // ENCODINGSUSED .. INFO (size 0, allowed)
};
static const uint8_t attrpk_dir_sizes[32] = { 4, 4, 4, 8, 4, 8 };
static const uint8_t attrpk_file_sizes[32] = { 4, 8, 8, 4, 4, 4, 4, 4, 8, 8, 8, 64, 8, 8, 64 };
// ATTR_CMNEXT_*: PRIVATESIZE, LINKID, REALDEVID, REALFSID, CLONEID, EXT_FLAGS, RECURSIVE_GENCOUNT, ATTRIBUTION_TAG,
// CLONE_REFCNT (RELPATH and NOFIRMLINKPATH unsupported).
static const uint8_t attrpk_cmnext_sizes[32] = { 0, 0, 0, 8, 8, 0, 4, 8, 8, 8, 8, 8, 4 };

// Attributes reported as valid; the others are packed as zeros, or left out (see attrpk_pack_inval()).
#define ATTRPK_VALID_COMMON 0xF63FFFFFu // all but EXTENDED_SECURITY, UUIDs and FULLPATH (valid when known)
#define ATTRPK_CMN_FULLPATH 0x08000000u
#define ATTRPK_VALID_VOL 0xC0F3FFFFu    // all but UUID, OWNER, QUOTA_SIZE, RESERVED_SIZE
#define ATTRPK_VALID_DIR 0x3Fu
#define ATTRPK_VALID_FILE 0x36FFu       // all but DATAEXTENTS, RSRCEXTENTS and FORKLIST
#define ATTRPK_VALID_CMNEXT 0x2D0u      // LINKID, REALDEVID, REALFSID, EXT_FLAGS

// What is known about the object being described.
struct attrpk_object {
	const char* name;                // ATTR_CMN_NAME (for volume requests: the volume name)
	const char* full_path;           // ATTR_CMN_FULLPATH, NULL if unknown
	const struct stat64* st;
	uint64_t parent_ino;
	int entry_dirfd;                 // directories: openat(entry_dirfd, entry_path) lists them (ATTR_DIR_ENTRYCOUNT)
	const char* entry_path;
	const char* finder_info;         // 32 bytes, or NULL for zeros
	uint64_t rsrc_length;
	const struct bsd_statfs64* fs;   // volume requests
	int mount_point;                 // ATTR_DIR_MOUNTSTATUS
	int bulk;                        // getattrlistbulk
};

static inline int attrpk_supported(uint32_t mask, const uint8_t sizes[32], uint32_t always_ok)
{
	for (int bit = 0; bit < 32; bit++)
		if ((mask & (1u << bit)) && !sizes[bit] && !(always_ok & (1u << bit)))
			return 0;
	return 1;
}

static inline uint32_t attrpk_group_size(uint32_t mask, const uint8_t sizes[32])
{
	uint32_t total = 0;
	for (int bit = 0; bit < 32; bit++)
		if (mask & (1u << bit))
			total += (sizes[bit] + 3) & ~3u;
	return total;
}

// 0 when the request can be served, else -EINVAL.
static inline int attrpk_check(const struct xnu_attrlist* al, uint64_t options, int bulk)
{
	uint32_t cmnext = (options & ATTRPK_FSOPT_ATTR_CMN_EXTENDED) ? al->forkattr : 0;
	if (al->bitmapcount != ATTRPK_BITMAP_COUNT)
		return -EINVAL;
	if (bulk && (!(al->commonattr & ATTRPK_CMN_RETURNED_ATTRS) || al->volattr))
		return -EINVAL;
	// CoreFoundation asks for volume attributes such as ATTR_VOL_CAPABILITIES without ATTR_VOL_INFO
	if (al->volattr && (al->dirattr || al->fileattr || al->forkattr))
		return -EINVAL;
	if ((al->forkattr && !cmnext) || !attrpk_supported(cmnext, attrpk_cmnext_sizes, 0)
		|| !attrpk_supported(al->commonattr, attrpk_common_sizes, 0)
		|| !attrpk_supported(al->volattr, attrpk_vol_sizes, ATTRPK_VOL_INFO)
		|| !attrpk_supported(al->dirattr, attrpk_dir_sizes, 0)
		|| !attrpk_supported(al->fileattr, attrpk_file_sizes, 0))
		return -EINVAL;
	return 0;
}

// Unsupported attributes are packed (as zeros) unless the caller asked which ones are returned and did not ask
// for FSOPT_PACK_INVAL_ATTRS.
static inline int attrpk_pack_inval(const struct xnu_attrlist* al, uint64_t options)
{
	return !(al->commonattr & ATTRPK_CMN_RETURNED_ATTRS) || (options & ATTRPK_FSOPT_PACK_INVAL_ATTRS);
}

struct attrpk_masks {
	uint32_t ret[5]; // returned: common, vol, dir, file, cmnext
	uint32_t pk[5];  // packed
};

static inline void attrpk_masks(const struct xnu_attrlist* al, uint64_t options, const struct attrpk_object* obj,
	struct attrpk_masks* m)
{
	int is_dir = S_ISDIR(obj->st->st_mode);
	int inval = attrpk_pack_inval(al, options);
	uint32_t cmnext = (options & ATTRPK_FSOPT_ATTR_CMN_EXTENDED) ? al->forkattr : 0;
	uint32_t req[5] = { al->commonattr, al->volattr, al->dirattr, al->fileattr, cmnext };

	m->ret[0] = al->commonattr & (ATTRPK_VALID_COMMON | (obj->full_path ? ATTRPK_CMN_FULLPATH : 0));
	// getattrlist (not bulk) ignores ATTR_CMN_ERROR: neither packed nor returned
	if (!obj->bulk)
		req[0] &= ~ATTRPK_CMN_ERROR, m->ret[0] &= ~ATTRPK_CMN_ERROR;
	m->ret[1] = al->volattr & ATTRPK_VALID_VOL;
	m->ret[2] = is_dir ? (al->dirattr & ATTRPK_VALID_DIR) : 0;
	m->ret[3] = is_dir ? 0 : (al->fileattr & ATTRPK_VALID_FILE);
	m->ret[4] = cmnext & ATTRPK_VALID_CMNEXT;
	for (int i = 0; i < 5; i++)
		m->pk[i] = inval ? req[i] : m->ret[i];
	// returned attributes are always there when requested
	m->pk[0] |= al->commonattr & ATTRPK_CMN_RETURNED_ATTRS;
	m->ret[0] |= al->commonattr & ATTRPK_CMN_RETURNED_ATTRS;
}

static inline uint32_t attrpk_align4(uint32_t n)
{
	return (n + 3) & ~3u;
}

static inline const char* attrpk_volume_name(const struct attrpk_object* obj)
{
	const char* mnt = obj->fs ? obj->fs->f_mntonname : "/";
	const char* last = mnt;
	for (const char* p = mnt; *p; p++)
		if (*p == '/' && p[1])
			last = p + 1;
	return (*last && *last != '/') ? last : "Macintosh HD";
}

// The strings referenced from the fixed part, in packing order.
static inline uint32_t attrpk_strings(const struct attrpk_masks* m, const struct attrpk_object* obj,
	const char* out[6])
{
	uint32_t n = 0;
	if (m->pk[0] & ATTRPK_CMN_NAME)
		out[n++] = obj->name ? obj->name : "";
	if (m->pk[0] & ATTRPK_CMN_FULLPATH)
		out[n++] = obj->full_path;
	if (m->pk[1] & 0x00001000u) // MOUNTPOINT
		out[n++] = obj->fs ? obj->fs->f_mntonname : "/";
	if (m->pk[1] & 0x00002000u) // NAME
		out[n++] = attrpk_volume_name(obj);
	if (m->pk[1] & 0x00008000u) // MOUNTEDDEVICE
		out[n++] = obj->fs ? obj->fs->f_mntfromname : "";
	if (m->pk[1] & 0x00100000u) // FSTYPENAME
		out[n++] = obj->fs ? obj->fs->f_fstypename : "hfs";
	return n;
}

// Size of the record (length word included, not rounded).
static inline uint32_t attrpk_size(const struct xnu_attrlist* al, const struct attrpk_masks* m,
	const struct attrpk_object* obj)
{
	const char* strs[6];
	uint32_t size = 4 + attrpk_group_size(m->pk[0], attrpk_common_sizes) + attrpk_group_size(m->pk[1], attrpk_vol_sizes)
		+ attrpk_group_size(m->pk[2], attrpk_dir_sizes) + attrpk_group_size(m->pk[3], attrpk_file_sizes)
		+ attrpk_group_size(m->pk[4], attrpk_cmnext_sizes);
	uint32_t n = attrpk_strings(m, obj, strs);
	for (uint32_t i = 0; i < n; i++)
		if (strs[i])
			size += attrpk_align4(strlen(strs[i]) + 1);
	return size;
}

#define ATTRPK_PUT(ptr, type, value) do { type _v = (type) (value); memcpy((ptr), &_v, sizeof(_v)); } while (0)

static inline int attrpk_vtype(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG: return ATTRPK_VREG;
	case S_IFDIR: return ATTRPK_VDIR;
	case S_IFBLK: return ATTRPK_VBLK;
	case S_IFCHR: return ATTRPK_VCHR;
	case S_IFLNK: return ATTRPK_VLNK;
	case S_IFSOCK: return ATTRPK_VSOCK;
	case S_IFIFO: return ATTRPK_VFIFO;
	}
	return ATTRPK_VNON;
}

static inline uint32_t attrpk_user_access(const struct stat64* st)
{
	uint32_t uid = (uint32_t) LINUX_SYSCALL0(__NR_geteuid);
	if (uid == 0)
		return 7;
	if (uid == st->st_uid)
		return (st->st_mode >> 6) & 7;
	return st->st_mode & 7; // group membership is not checked
}

static inline uint32_t attrpk_dir_entry_count(int dirfd, const char* name)
{
	char buf[1024];
	uint32_t count = 0;
	int fd = LINUX_SYSCALL(__NR_openat, dirfd, name, LINUX_O_RDONLY | LINUX_O_DIRECTORY | LINUX_O_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	for (;;) {
		long n = LINUX_SYSCALL(__NR_getdents64, fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		for (char* p = buf; p < buf + n; p += ((struct linux_dirent64*) p)->d_reclen) {
			const char* nm = ((struct linux_dirent64*) p)->d_name;
			if (!(nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))))
				count++;
		}
	}
	LINUX_SYSCALL(__NR_close, fd);
	return count;
}

static inline void attrpk_put_timespec(char* p, const struct timespec* ts)
{
	ATTRPK_PUT(p, int64_t, ts->tv_sec);
	ATTRPK_PUT(p + 8, int64_t, ts->tv_nsec);
}

// Writes an attrreference_t at `p` for string `s`, copying it to *var.
static inline void attrpk_put_ref(char* p, char** var, const char* s)
{
	uint32_t len = s ? strlen(s) + 1 : 0;
	ATTRPK_PUT(p, int32_t, *var - p);
	ATTRPK_PUT(p + 4, uint32_t, len);
	if (len) {
		memcpy(*var, s, len);
		*var += attrpk_align4(len);
	}
}

// Packs one record at `out` (room for `size` bytes, from attrpk_size()); the length word says `length`.
static inline void attrpk_pack(char* out, uint32_t size, uint32_t length, const struct attrpk_masks* m,
	const struct attrpk_object* obj)
{
	const struct stat64* st = obj->st;
	const struct bsd_statfs64* fs = obj->fs;
	int is_dir = S_ISDIR(st->st_mode);
	uint32_t pk_common = m->pk[0];

	memset(out, 0, size);
	ATTRPK_PUT(out, uint32_t, length);
	char* p = out + 4;
	char* var = out + 4 + attrpk_group_size(m->pk[0], attrpk_common_sizes)
		+ attrpk_group_size(m->pk[1], attrpk_vol_sizes) + attrpk_group_size(m->pk[2], attrpk_dir_sizes)
		+ attrpk_group_size(m->pk[3], attrpk_file_sizes) + attrpk_group_size(m->pk[4], attrpk_cmnext_sizes);

	// ATTR_CMN_RETURNED_ATTRS, then ATTR_CMN_ERROR, then the remaining common attributes in bit order
	if (pk_common & ATTRPK_CMN_RETURNED_ATTRS) {
		for (int i = 0; i < 5; i++)
			ATTRPK_PUT(p + 4 * i, uint32_t, m->ret[i]);
		p += 20;
	}
	if (pk_common & ATTRPK_CMN_ERROR)
		p += 4; // no error

	for (int bit = 0; bit < 31; bit++) {
		if (bit == 29 || !(pk_common & (1u << bit))) // ERROR was packed above
			continue;
		switch (bit) {
		case 0: attrpk_put_ref(p, &var, obj->name ? obj->name : ""); break; // NAME
		case 1: ATTRPK_PUT(p, uint32_t, st->st_dev); break;                  // DEVID
		case 2: ATTRPK_PUT(p, uint32_t, st->st_dev); break;                  // FSID (val[1] = 0)
		case 3: ATTRPK_PUT(p, uint32_t, attrpk_vtype(st->st_mode)); break;   // OBJTYPE
		case 4: ATTRPK_PUT(p, uint32_t, 16 /* VT_HFS */); break;              // OBJTAG
		case 5: ATTRPK_PUT(p, uint32_t, st->st_ino); break;                  // OBJID (generation 0)
		case 6: ATTRPK_PUT(p, uint32_t, st->st_ino); break;                  // OBJPERMANENTID
		case 7: ATTRPK_PUT(p, uint32_t, obj->parent_ino); break;             // PAROBJID
		case 9: attrpk_put_timespec(p, &st->st_birthtimespec); break;        // CRTIME
		case 10: attrpk_put_timespec(p, &st->st_mtimespec); break;           // MODTIME
		case 11: attrpk_put_timespec(p, &st->st_ctimespec); break;           // CHGTIME
		case 12: attrpk_put_timespec(p, &st->st_atimespec); break;           // ACCTIME
		case 14:                                                             // FNDRINFO
			if (obj->finder_info)
				memcpy(p, obj->finder_info, 32);
			break;
		case 15: ATTRPK_PUT(p, uint32_t, st->st_uid); break;                 // OWNERID
		case 16: ATTRPK_PUT(p, uint32_t, st->st_gid); break;                 // GRPID
		case 17: ATTRPK_PUT(p, uint32_t, st->st_mode); break;                // ACCESSMASK (with the file type, as on macOS)
		case 18: ATTRPK_PUT(p, uint32_t, st->st_flags); break;               // FLAGS
		case 19: ATTRPK_PUT(p, uint32_t, 1); break;                          // GEN_COUNT
		case 20: ATTRPK_PUT(p, uint32_t, 0); break;                          // DOCUMENT_ID: none
		case 21: ATTRPK_PUT(p, uint32_t, attrpk_user_access(st)); break;     // USERACCESS
		case 22: ATTRPK_PUT(p, int32_t, 0); break;                           // EXTENDED_SECURITY: empty
		case 25: ATTRPK_PUT(p, uint64_t, st->st_ino); break;                 // FILEID
		case 26: ATTRPK_PUT(p, uint64_t, obj->parent_ino); break;            // PARENTID
		case 27: attrpk_put_ref(p, &var, obj->full_path); break;             // FULLPATH
		case 28: attrpk_put_timespec(p, &st->st_birthtimespec); break;       // ADDEDTIME
		case 30: ATTRPK_PUT(p, uint32_t, 0); break;                          // DATA_PROTECT_FLAGS
		default: break;                                                      // zero: SCRIPT, BKUPTIME...
		}
		p += attrpk_align4(attrpk_common_sizes[bit]);
	}

	for (int bit = 0; bit < 31; bit++) {
		if (!(m->pk[1] & (1u << bit)))
			continue;
		uint64_t bsize = fs ? fs->f_bsize : 4096;
		switch (bit) {
		case 0: ATTRPK_PUT(p, uint32_t, fs ? fs->f_type : 0); break;                // FSTYPE
		case 1: ATTRPK_PUT(p, uint32_t, 0x482B /* 'H+' */); break;                  // SIGNATURE
		case 2: ATTRPK_PUT(p, uint64_t, fs ? fs->f_blocks * bsize : 0); break;     // SIZE
		case 3: ATTRPK_PUT(p, uint64_t, fs ? fs->f_bfree * bsize : 0); break;      // SPACEFREE
		case 4: ATTRPK_PUT(p, uint64_t, fs ? fs->f_bavail * bsize : 0); break;     // SPACEAVAIL
		case 5: ATTRPK_PUT(p, uint64_t, bsize); break;                             // MINALLOCATION
		case 6: ATTRPK_PUT(p, uint64_t, bsize); break;                             // ALLOCATIONCLUMP
		case 7: ATTRPK_PUT(p, uint32_t, fs ? fs->f_iosize : 4096); break;          // IOBLOCKSIZE
		case 8: case 9:                                                            // OBJCOUNT, FILECOUNT
			ATTRPK_PUT(p, uint32_t, fs ? fs->f_files - fs->f_ffree : 0); break;
		case 11: ATTRPK_PUT(p, uint32_t, fs ? fs->f_files : 0); break;             // MAXOBJCOUNT
		case 12: attrpk_put_ref(p, &var, fs ? fs->f_mntonname : "/"); break;      // MOUNTPOINT
		case 13: attrpk_put_ref(p, &var, attrpk_volume_name(obj)); break;         // NAME
		case 14: ATTRPK_PUT(p, uint32_t, fs ? fs->f_flags : 0); break;             // MOUNTFLAGS
		case 15: attrpk_put_ref(p, &var, fs ? fs->f_mntfromname : ""); break;    // MOUNTEDDEVICE
		case 16: ATTRPK_PUT(p, uint64_t, 0x1 /* MacRoman */); break;               // ENCODINGSUSED
		case 17: {                                                                 // CAPABILITIES
			// format: SYMBOLICLINKS, HARDLINKS, SPARSE_FILES, CASE_SENSITIVE, CASE_PRESERVING, FAST_STATFS,
			// 2TB_FILESIZE, 64BIT_OBJECT_IDS; interfaces: ATTRLIST, ADVLOCK, FLOCK, USERACCESS, EXTENDED_ATTR
			uint32_t caps[8] = { 0x20F46, 0x4B02, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0 };
			memcpy(p, caps, sizeof(caps));
			break;
		}
		case 20: attrpk_put_ref(p, &var, fs ? fs->f_fstypename : "hfs"); break;   // FSTYPENAME
		case 21: ATTRPK_PUT(p, uint32_t, fs ? fs->f_fssubtype : 0); break;         // FSSUBTYPE
		case 23: ATTRPK_PUT(p, uint64_t, fs ? (fs->f_blocks - fs->f_bfree) * bsize : 0); break; // SPACEUSED
		case 30: {                                                                 // ATTRIBUTES
			uint32_t attrs[10] = { ATTRPK_VALID_COMMON, ATTRPK_VALID_VOL, ATTRPK_VALID_DIR, ATTRPK_VALID_FILE, 0,
				ATTRPK_VALID_COMMON, ATTRPK_VALID_VOL, ATTRPK_VALID_DIR, ATTRPK_VALID_FILE, 0 };
			memcpy(p, attrs, sizeof(attrs));
			break;
		}
		default: break;                                                            // DIRCOUNT, UUID...: 0
		}
		p += attrpk_align4(attrpk_vol_sizes[bit]);
	}

	for (int bit = 0; bit < 6; bit++) {
		if (!(m->pk[2] & (1u << bit)))
			continue;
		if (is_dir) {
			switch (bit) {
			case 0: ATTRPK_PUT(p, uint32_t, st->st_nlink); break;
			case 1: ATTRPK_PUT(p, uint32_t, attrpk_dir_entry_count(obj->entry_dirfd, obj->entry_path)); break;
			case 2: ATTRPK_PUT(p, uint32_t, obj->mount_point ? 1 /* DIR_MNTSTATUS_MNTPOINT */ : 0); break;
			case 3: ATTRPK_PUT(p, uint64_t, (uint64_t) st->st_blocks * 512); break;
			case 4: ATTRPK_PUT(p, uint32_t, st->st_blksize); break;
			case 5: ATTRPK_PUT(p, uint64_t, st->st_size); break;
			}
		}
		p += attrpk_align4(attrpk_dir_sizes[bit]);
	}

	for (int bit = 0; bit < 15; bit++) {
		if (!(m->pk[3] & (1u << bit)))
			continue;
		if (!is_dir) {
			switch (bit) {
			case 0: ATTRPK_PUT(p, uint32_t, st->st_nlink); break;                    // LINKCOUNT
			case 1: ATTRPK_PUT(p, uint64_t, st->st_size + obj->rsrc_length); break;  // TOTALSIZE
			case 2: ATTRPK_PUT(p, uint64_t, (uint64_t) st->st_blocks * 512); break;  // ALLOCSIZE
			case 3: ATTRPK_PUT(p, uint32_t, st->st_blksize); break;                  // IOBLOCKSIZE
			case 4: ATTRPK_PUT(p, uint32_t, st->st_blksize); break;                  // CLUMPSIZE
			case 5: ATTRPK_PUT(p, uint32_t, st->st_rdev); break;                     // DEVTYPE
			case 7: ATTRPK_PUT(p, uint32_t, obj->rsrc_length ? 2 : 1); break;        // FORKCOUNT
			case 9: ATTRPK_PUT(p, uint64_t, st->st_size); break;                     // DATALENGTH
			case 10: ATTRPK_PUT(p, uint64_t, (uint64_t) st->st_blocks * 512); break; // DATAALLOCSIZE
			case 12: ATTRPK_PUT(p, uint64_t, obj->rsrc_length); break;               // RSRCLENGTH
			case 13: ATTRPK_PUT(p, uint64_t, obj->rsrc_length); break;               // RSRCALLOCSIZE
			default: break;                                                          // FILETYPE: 0
			}
		}
		p += attrpk_align4(attrpk_file_sizes[bit]);
	}

	for (int bit = 0; bit < 13; bit++) {
		if (!(m->pk[4] & (1u << bit)))
			continue;
		switch (bit) {
		case 4: ATTRPK_PUT(p, uint64_t, st->st_ino); break; // LINKID
		case 6: ATTRPK_PUT(p, uint32_t, st->st_dev); break; // REALDEVID
		case 7: ATTRPK_PUT(p, uint32_t, st->st_dev); break; // REALFSID (val[1] = 0)
		default: break;                                     // PRIVATESIZE, CLONEID, EXT_FLAGS...: 0
		}
		p += attrpk_align4(attrpk_cmnext_sizes[bit]);
	}
}

#endif // DARLING_ATTRLIST_PACK_H

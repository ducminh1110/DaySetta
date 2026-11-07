// Micro-benchmark for genuine macOS 27 libs on ds-exec: objc_msgSend, CFString ops, dispatch_async.  Built as arm64e by build.sh
// (stub dylibs only supply link-time symbols; the real libobjc/CoreFoundation/libdispatch come from the 27 shared cache).
typedef unsigned long size_t;
typedef long CFIndex;
struct ts { long tv_sec, tv_nsec; };
int printf(const char*, ...);
int clock_gettime(int, struct ts*);
void* objc_getClass(const char*);
void* sel_registerName(const char*);
void* objc_msgSend(void*, void*, ...);
void* CFStringCreateWithCString(void*, const char*, unsigned);
CFIndex CFStringGetLength(void*);
void* CFRetain(void*);
void CFRelease(void*);
void* CFStringCreateMutableCopy(void*, CFIndex, void*);
void CFStringAppend(void*, void*);
void* dispatch_get_global_queue(long, unsigned long);
void dispatch_async_f(void*, void*, void (*)(void*));
void* dispatch_semaphore_create(long);
long dispatch_semaphore_signal(void*);
long dispatch_semaphore_wait(void*, unsigned long long);
unsigned long long dispatch_time(unsigned long long, long long);

static double now(void) { struct ts t; clock_gettime(6 /* CLOCK_MONOTONIC_RAW? use 6 on Darwin = MONOTONIC_RAW */, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile long counter;
static void* sem;
static void work(void* c) { if (__atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED) == (long)c) dispatch_semaphore_signal(sem); }

int main(int argc, char** argv)
{
	long n = 1000000;
	void* nsobject = objc_getClass("NSObject");
	void* sel = sel_registerName("class");
	double t0 = now();
	for (long i = 0; i < n; i++) objc_msgSend(nsobject, sel);
	double t1 = now();
	printf("objc_msgSend x%ld: %.3f s (%.0f ns/op)\n", n, t1 - t0, (t1 - t0) / n * 1e9);

	void* s = CFStringCreateWithCString(0, "hello world", 0x08000100);
	t0 = now();
	long len = 0;
	for (long i = 0; i < n; i++) { len += CFStringGetLength(s); CFRetain(s); CFRelease(s); }
	t1 = now();
	printf("CFString length+retain+release x%ld: %.3f s (%.0f ns/op) len=%ld\n", n, t1 - t0, (t1 - t0) / n * 1e9, len);
	t0 = now();
	void* m = CFStringCreateMutableCopy(0, 0, s);
	for (long i = 0; i < n / 10; i++) CFStringAppend(m, s);
	t1 = now();
	printf("CFStringAppend x%ld: %.3f s (%.0f ns/op) len=%ld\n", n / 10, t1 - t0, (t1 - t0) / (n / 10) * 1e9, (long)CFStringGetLength(m));

	sem = dispatch_semaphore_create(0);
	void* q = dispatch_get_global_queue(0x15, 0);
	t0 = now();
	for (long i = 0; i < n; i++) dispatch_async_f(q, (void*)n, work);
	dispatch_semaphore_wait(sem, dispatch_time(0, 120000000000LL));
	t1 = now();
	printf("dispatch_async x%ld: %.3f s (%.0f ns/op) done=%ld\n", n, t1 - t0, (t1 - t0) / n * 1e9, counter);
	return 0;
}

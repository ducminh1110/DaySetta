// asroot PROGRAM [ARGS...]: run a program as root inside Darling (uid/gid are virtual, kept by darlingserver).
// Recovery apps (Recovery Springboard, macOS Update Assistant...) expect to run as root, as they do in recoveryOS.
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: asroot program [args...]\n");
		return 2;
	}
	if (setgid(0) != 0 || setuid(0) != 0)
		perror("asroot: setuid");
	execv(argv[1], argv + 1);
	perror("asroot: exec");
	return 127;
}

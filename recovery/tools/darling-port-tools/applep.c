/* Prints the apple[] strings a Darling process receives, with their addresses. */
#include <stdio.h>
int main(int argc, char **argv, char **envp, char **apple)
{
	printf("argv %p envp %p apple %p\n", (void *)argv, (void *)envp, (void *)apple);
	for (int i = 0; apple[i]; i++)
		printf("apple[%d] %p = %s\n", i, (void *)apple[i], apple[i]);
	return 0;
}

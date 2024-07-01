#include <stddef.h>
#include <stdint.h>

#include "../IO_library.c"

#define GUEST_NAME "guest3"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	void *file1 = fopen("lorem1.txt", "r", GUEST_NAME);
	void *file2 = fopen("lorem2.txt", "r", GUEST_NAME);
	
	char p1[40];
	unsigned int ret = fread(p1, 1, 30, &file1, GUEST_NAME);
	printf(p1);
	char p2[40];
	ret = fread(p2, 1, 30, &file1, GUEST_NAME);
	printf(p2);
	fclose(file1, GUEST_NAME);

	char p3[40];
	ret = fread(p3, 1, 30, &file2, GUEST_NAME);
	printf(p3);
	fclose(file2, GUEST_NAME);

	for(;;)
		asm("hlt");
}
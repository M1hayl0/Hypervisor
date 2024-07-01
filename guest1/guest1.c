#include <stddef.h>
#include <stdint.h>

#include "../IO_library.c"

#define GUEST_NAME "guest1"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	void *file1 = fopen("lorem1.txt", "a+", GUEST_NAME);
	char p1[30];
	unsigned int ret = fread(p1, 1, 26, &file1, GUEST_NAME);
	printf(p1);
	char p2[20] = "LOREM IPSUM\n";
	ret = fwrite(p2, 1, 12, &file1, GUEST_NAME);
	fclose(file1, GUEST_NAME);

	void *file3 = fopen("lorem3.txt", "a+", GUEST_NAME);
	char p3[20] = "LOREM IPSUM\n";
	ret = fwrite(p3, 1, 12, &file3, GUEST_NAME);
	printf(p3);
	char p4[20] = "IPSUM LOREM\n";
	ret = fwrite(p4, 1, 12, &file3, GUEST_NAME);
	fclose(file3, GUEST_NAME);

	for(;;)
		asm("hlt");
}
#include <stddef.h>
#include <stdint.h>

#include "../IO_library.c"

#define GUEST_NAME "guest5"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	void *file = fopen("image.ppm", "wb", GUEST_NAME);

    int width = 101;
    int height = 101;

	char p1[10] = "P3\n";
    fwrite(p1, 1, 3, &file, GUEST_NAME);
	char p2[10] = "101 101\n";
    fwrite(p2, 1, 8, &file, GUEST_NAME);
	char p3[10] = "255\n";
    fwrite(p3, 1, 4, &file, GUEST_NAME);

	char cyan[15] = "000 240 250\n";
	char red[15] = "250 060 030\n";
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
			if(x % 10 == 0 || y % 10 == 0) {
				fwrite(red, 1, 12, &file, GUEST_NAME);
			} else {
				fwrite(cyan, 1, 12, &file, GUEST_NAME);
			}
        }
    }

    fclose(file, GUEST_NAME);

	for(;;)
		asm("hlt");
}
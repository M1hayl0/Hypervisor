#include <stddef.h>
#include <stdint.h>

#define CONSOLE_PORT 0xE9
#define FILE_PORT 0x0278

#define OPEN_FILE '0'
#define CLOSE_FILE '1'
#define READ_FILE '2'
#define WRITE_FILE '3'

//8 bit
void outb(uint16_t port, uint8_t value) {
	asm volatile("outb %0,%1" : : "a" (value), "Nd" (port) : "memory");
}

//8 bit
uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

//32 bit
void outl(uint16_t port, uint32_t value) {
	asm volatile("outl %0,%1" : : "a" (value), "Nd" (port) : "memory");
}

//32 bit
uint32_t inl(uint16_t port) {
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void printf(const char *str) {
    while(*str) {
		outb(CONSOLE_PORT, *str);
        str++;
    }
}

char *scanf() {
    static char ret[512];
    int i = 0;
    char c;
    while((c = inb(CONSOLE_PORT)) != '\n') {
        ret[i++] = c;
        if(i >= 511) break;
    }
    ret[i] = '\0';
    return ret;
}

void sendToFilePort(const char *str) {
    while(*str) {
        outb(FILE_PORT, *str);
        str++;
    }
}

void uintToStr(unsigned int num, char *str) {
    char temp[20];
    int i = 0;

    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    while (num > 0) {
        temp[i++] = num % 10 + '0';
        num /= 10;
    }

    int j = 0;
    while (i > 0) {
        str[j++] = temp[--i];
    }
    str[j] = '\0';
}

void ptrToStr(const void *ptr, char *str) {
    uintptr_t value = (uintptr_t) ptr;
    char *hexDigits = "0123456789ABCDEF";
    int i = 0;
    for(int shift = sizeof(uintptr_t) * 8 - 4; shift >= 0; shift -= 4) {
        str[i++] = hexDigits[(value >> shift) & 0xF];
    }
    str[i] = '\0';
}

void *fopen(const char *filename, char *modes, const char *guest) {
    // 0#filename#modes#guestx/##
    char strToSend[512];
    int i = 0;
    strToSend[i++] = OPEN_FILE;
    strToSend[i++] = '#';
    while(*filename) {
        strToSend[i++] = *filename;
        filename++;
    }
    strToSend[i++] = '#';
    while(*modes) {
        strToSend[i++] = *modes;
        modes++;
    }
    strToSend[i++] = '#';
    while(*guest) {
        strToSend[i++] = *guest;
        guest++;
    }
    strToSend[i++] = '/';
    strToSend[i++] = '#';
    strToSend[i++] = '#';
    strToSend[i] = '\0';
    sendToFilePort(strToSend);

    uint64_t leftHalf, rightHalf, fileHandle;
    leftHalf = inl(FILE_PORT);
    rightHalf = inl(FILE_PORT);
	
    fileHandle = ((uint64_t) leftHalf << 32) | ((uint64_t) rightHalf & 0xFFFFFFFF);

	return (void *) fileHandle;
}

int fclose(void *file, const char *guest) {
    // 1#file#guestx/##
    char strToSend[512];
    char filePtrStr[128];
    int i = 0;
    strToSend[i++] = CLOSE_FILE;
    strToSend[i++] = '#';
    ptrToStr(file, filePtrStr);
    for(int j = 0; filePtrStr[j] != '\0'; j++) {
        strToSend[i++] = filePtrStr[j];
    }
    strToSend[i++] = '#';
    while(*guest) {
        strToSend[i++] = *guest;
        guest++;
    }
    strToSend[i++] = '/';
    strToSend[i++] = '#';
    strToSend[i++] = '#';
    strToSend[i] = '\0';
    sendToFilePort(strToSend);

    int ret;
    ret = inl(FILE_PORT);
    return ret;
}

unsigned int fread(void *ptr, unsigned int size, unsigned int n, void **file, const char *guest) {
    // 2#ptr#size#n#file#guestx/##
    char strToSend[512];
    char filePtrStr[256];
    char ptrStr[256];
    char nStr[20];
    char sizeStr[20];
    int i = 0;
    strToSend[i++] = READ_FILE;
    strToSend[i++] = '#';
    ptrToStr(ptr, ptrStr);
    for(int j = 0; ptrStr[j] != '\0'; j++) {
        strToSend[i++] = ptrStr[j];
    }
    strToSend[i++] = '#';
    uintToStr(size, sizeStr);
    for(int j = 0; sizeStr[j] != '\0'; j++) {
        strToSend[i++] = sizeStr[j];
    }
    strToSend[i++] = '#';
    uintToStr(n, nStr);
    for(int j = 0; nStr[j] != '\0'; j++) {
        strToSend[i++] = nStr[j];
    }
    strToSend[i++] = '#';
    ptrToStr(*file, filePtrStr);
    for(int j = 0; filePtrStr[j] != '\0'; j++) {
        strToSend[i++] = filePtrStr[j];
    }
    strToSend[i++] = '#';
    while(*guest) {
        strToSend[i++] = *guest;
        guest++;
    }
    strToSend[i++] = '/';
    strToSend[i++] = '#';
    strToSend[i++] = '#';
    strToSend[i] = '\0';
    sendToFilePort(strToSend);

    int ret;
    ret = inl(FILE_PORT);

    uint64_t leftHalf, rightHalf, fileHandle;
    leftHalf = inl(FILE_PORT);
    rightHalf = inl(FILE_PORT);
    fileHandle = ((uint64_t) leftHalf << 32) | ((uint64_t) rightHalf & 0xFFFFFFFF);
    *file = (void *) fileHandle;

    return ret;
}

unsigned int fwrite(void *ptr, unsigned int size, unsigned int n, void **file, const char *guest) {
    // 3#ptr#size#n#file#guestx/##
    char strToSend[512];
    char filePtrStr[256];
    char ptrStr[256];
    char nStr[20];
    char sizeStr[20];
    int i = 0;
    strToSend[i++] = WRITE_FILE;
    strToSend[i++] = '#';
    ptrToStr(ptr, ptrStr);
    for(int j = 0; ptrStr[j] != '\0'; j++) {
        strToSend[i++] = ptrStr[j];
    }
    strToSend[i++] = '#';
    uintToStr(size, sizeStr);
    for(int j = 0; sizeStr[j] != '\0'; j++) {
        strToSend[i++] = sizeStr[j];
    }
    strToSend[i++] = '#';
    uintToStr(n, nStr);
    for(int j = 0; nStr[j] != '\0'; j++) {
        strToSend[i++] = nStr[j];
    }
    strToSend[i++] = '#';
    ptrToStr(*file, filePtrStr);
    for(int j = 0; filePtrStr[j] != '\0'; j++) {
        strToSend[i++] = filePtrStr[j];
    }
    strToSend[i++] = '#';
    while(*guest) {
        strToSend[i++] = *guest;
        guest++;
    }
    strToSend[i++] = '/';
    strToSend[i++] = '#';
    strToSend[i++] = '#';
    strToSend[i] = '\0';
    sendToFilePort(strToSend);

    int ret;
    ret = inl(FILE_PORT);

    uint64_t leftHalf, rightHalf, fileHandle;
    leftHalf = inl(FILE_PORT);
    rightHalf = inl(FILE_PORT);
    fileHandle = ((uint64_t) leftHalf << 32) | ((uint64_t) rightHalf & 0xFFFFFFFF);
    *file = (void *) fileHandle;

    return ret;
}
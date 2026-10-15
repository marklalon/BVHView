#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>

FILE* FileOpenUtf8(const char* filename, const char* mode);
unsigned char* LoadFileDataUtf8(const char* filename, int* dataSize);

#endif

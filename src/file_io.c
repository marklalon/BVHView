#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"
#include "file_io.h"

#if defined(_WIN32) && !defined(PLATFORM_WEB)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#ifdef Rectangle
#undef Rectangle
#endif

FILE* FileOpenUtf8(const char* filename, const char* mode)
{
    int pathLength = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    int modeLength = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
    if (pathLength > 0 && modeLength > 0)
    {
        wchar_t* widePath = malloc((size_t)pathLength * sizeof(wchar_t));
        wchar_t* wideMode = malloc((size_t)modeLength * sizeof(wchar_t));
        if (widePath != NULL && wideMode != NULL)
        {
            MultiByteToWideChar(CP_UTF8, 0, filename, -1, widePath, pathLength);
            MultiByteToWideChar(CP_UTF8, 0, mode, -1, wideMode, modeLength);
            FILE* file = _wfopen(widePath, wideMode);
            free(widePath);
            free(wideMode);
            if (file != NULL) return file;
        }
        else
        {
            free(widePath);
            free(wideMode);
        }
    }

    // Keep compatibility with paths supplied in the active legacy code page.
    return fopen(filename, mode);
}
#else
FILE* FileOpenUtf8(const char* filename, const char* mode)
{
    return fopen(filename, mode);
}
#endif

unsigned char* LoadFileDataUtf8(const char* filename, int* dataSize)
{
    *dataSize = 0;
    FILE* file = FileOpenUtf8(filename, "rb");
    if (file == NULL) return NULL;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }
    long fileSize = ftell(file);
    if (fileSize <= 0 || fileSize > 2147483647L || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    unsigned char* data = MemAlloc((unsigned int)fileSize);
    if (data == NULL)
    {
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(data, 1, (size_t)fileSize, file);
    fclose(file);
    if (bytesRead != (size_t)fileSize)
    {
        MemFree(data);
        return NULL;
    }

    *dataSize = (int)bytesRead;
    return data;
}

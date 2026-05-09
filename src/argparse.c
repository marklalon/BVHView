#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "raymath.h"
#include "argparse.h"
#include "math_utils.h"

char* ArgFind(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) > 4 && argv[i][0] == '-' && argv[i][1] == '-' &&
            strstr(argv[i] + 2, name) == argv[i] + 2) {
            char* argStart = strchr(argv[i], '=');
            return argStart ? argStart + 1 : NULL;
        }
    }
    return NULL;
}

float ArgFloat(int argc, char** argv, const char* name, float defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    errno = 0;
    float output = strtof(value, NULL);
    if (errno == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return output; }
    printf("ERROR: Could not parse value '%s' given for option '%s' as float\n", value, name);
    return defaultValue;
}

int ArgInt(int argc, char** argv, const char* name, int defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    errno = 0;
    int output = (int)strtol(value, NULL, 10);
    if (errno == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return output; }
    printf("ERROR: Could not parse value '%s' given for option '%s' as int\n", value, name);
    return defaultValue;
}

bool ArgBool(int argc, char** argv, const char* name, bool defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    if (strcmp(value, "true") == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return true; }
    if (strcmp(value, "false") == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return false; }
    printf("ERROR: Could not parse value '%s' given for option '%s' as bool\n", value, name);
    return defaultValue;
}

int ArgEnum(int argc, char** argv, const char* name, int optionCount, const char* options[], int defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    for (int i = 0; i < optionCount; i++) {
        if (strcmp(value, options[i]) == 0) {
            printf("INFO: Parsed option '%s' as '%s'\n", name, value);
            return i;
        }
    }
    printf("ERROR: Could not parse value '%s' given for option '%s' as enum\n", value, name);
    return defaultValue;
}

const char* ArgStr(int argc, char** argv, const char* name, const char* defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    printf("INFO: Parsed option '%s' as '%s'\n", name, value);
    return value;
}

Color ArgColor(int argc, char** argv, const char* name, Color defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    int cx, cy, cz;
    if (sscanf(value, "%i,%i,%i", &cx, &cy, &cz) == 3) {
        printf("INFO: Parsed option '%s' as '%s'\n", name, value);
        return (Color){ ClampInt(cx, 0, 255), ClampInt(cy, 0, 255), ClampInt(cz, 0, 255) };
    }
    printf("ERROR: Could not parse value '%s' given for option '%s' as color\n", value, name);
    return defaultValue;
}

Vector3 ArgVector3(int argc, char** argv, const char* name, Vector3 defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) return defaultValue;
    float cx, cy, cz;
    if (sscanf(value, "%f,%f,%f", &cx, &cy, &cz) == 3) {
        printf("INFO: Parsed option '%s' as '%s'\n", name, value);
        return (Vector3){ cx, cy, cz };
    }
    printf("ERROR: Could not parse value '%s' given for option '%s' as color\n", value, name);
    return defaultValue;
}

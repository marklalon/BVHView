/*******************************************************************************************
*
*    models.h - Model loading declarations
*
*******************************************************************************************/

#ifndef MODELS_H
#define MODELS_H

#include "raylib.h"

extern const char* capsuleOBJ;

Model LoadOBJFromMemory(const char *fileText);

#endif // MODELS_H

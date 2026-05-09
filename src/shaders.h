/*******************************************************************************************
*
*    shaders.h - Shader GLSL strings, ShaderUniforms struct, function declarations
*
*******************************************************************************************/

#ifndef SHADERS_H
#define SHADERS_H

#include "raylib.h"

#define AO_CAPSULES_MAX 32
#define SHADOW_CAPSULES_MAX 64

extern const char* shaderVS;
extern const char* shaderFS;

typedef struct ShaderUniforms
{
    int isCapsule;
    int capsulePosition;
    int capsuleRotation;
    int capsuleHalfLength;
    int capsuleRadius;
    int capsuleStart;
    int capsuleVector;

    int shadowCapsuleCount;
    int shadowCapsuleStarts;
    int shadowCapsuleVectors;
    int shadowCapsuleRadii;
    int shadowLookupTable;
    int shadowLookupResolution;

    int aoCapsuleCount;
    int aoCapsuleStarts;
    int aoCapsuleVectors;
    int aoCapsuleRadii;
    int aoLookupTable;
    int aoLookupResolution;

    int cameraPosition;

    int objectColor;
    int objectSpecularity;
    int objectGlossiness;
    int objectOpacity;
    int useTexture;

    int sunStrength;
    int sunDir;
    int sunColor;
    int skyStrength;
    int skyColor;
    int ambientStrength;
    int groundStrength;

    int exposure;

} ShaderUniforms;

void ShaderUniformsInit(ShaderUniforms* uniforms, Shader shader);

#endif // SHADERS_H

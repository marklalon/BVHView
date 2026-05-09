/*******************************************************************************************
*
*    character_data.h - Character data structures and functions
*
*******************************************************************************************/

#ifndef CHARACTER_DATA_H
#define CHARACTER_DATA_H

#include <stdbool.h>
#include "raylib.h"
#include "bvh_data.h"
#include "glb_data.h"
#include "transform_data.h"

enum
{
    CHARACTERS_MAX = 6,
};

typedef struct CharacterData {
    int count;
    int active;
    BVHData bvhData[CHARACTERS_MAX];
    float scales[CHARACTERS_MAX];
    char names[CHARACTERS_MAX][128];
    float autoScales[CHARACTERS_MAX];
    Color colors[CHARACTERS_MAX];
    float opacities[CHARACTERS_MAX];
    float radii[CHARACTERS_MAX];
    char filePaths[CHARACTERS_MAX][512];
    TransformData xformData[CHARACTERS_MAX];
    TransformData xformTmp0[CHARACTERS_MAX];
    TransformData xformTmp1[CHARACTERS_MAX];
    TransformData xformTmp2[CHARACTERS_MAX];
    TransformData xformTmp3[CHARACTERS_MAX];
    char* jointNamesCombo[CHARACTERS_MAX];
    bool colorPickerActive;
    bool hasSkinnedMesh;
    bool isGLB[CHARACTERS_MAX];
    GLBData glbData[CHARACTERS_MAX];
} CharacterData;

void CharacterDataInit(CharacterData* data, int argc, char** argv);
void CharacterDataFree(CharacterData* data);
bool CharacterDataLoadFromFile(CharacterData* data, const char* path, char* errMsg, int errMsgSize);

// Extracted helpers
const char* ExtractFilename(const char* path);
char* BuildComboString(const char* names[], int count);

#endif // CHARACTER_DATA_H

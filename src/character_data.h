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
    // One extra trailing slot reserved for CharacterDataReplaceAt staging.
    // Per-slot arrays below are sized CHARACTERS_BUFFER_MAX so the stage slot
    // (index CHARACTERS_MAX) is always usable without exceeding array bounds.
    CHARACTERS_BUFFER_MAX = CHARACTERS_MAX + 1,
};

typedef struct CharacterData {
    int count;
    int active;
    BVHData bvhData[CHARACTERS_BUFFER_MAX];
    float scales[CHARACTERS_BUFFER_MAX];
    char names[CHARACTERS_BUFFER_MAX][128];
    float autoScales[CHARACTERS_BUFFER_MAX];
    Color colors[CHARACTERS_BUFFER_MAX];
    float opacities[CHARACTERS_BUFFER_MAX];
    float radii[CHARACTERS_BUFFER_MAX];
    char filePaths[CHARACTERS_BUFFER_MAX][512];
    TransformData xformData[CHARACTERS_BUFFER_MAX];
    TransformData xformTmp0[CHARACTERS_BUFFER_MAX];
    TransformData xformTmp1[CHARACTERS_BUFFER_MAX];
    TransformData xformTmp2[CHARACTERS_BUFFER_MAX];
    TransformData xformTmp3[CHARACTERS_BUFFER_MAX];
    char* jointNamesCombo[CHARACTERS_BUFFER_MAX];
    bool colorPickerActive;
    bool hasSkinnedMesh;
    bool isGLB[CHARACTERS_BUFFER_MAX];
    bool visible[CHARACTERS_BUFFER_MAX];
    GLBData glbData[CHARACTERS_BUFFER_MAX];
} CharacterData;

void CharacterDataInit(CharacterData* data, int argc, char** argv);
void CharacterDataFree(CharacterData* data);
bool CharacterDataLoadFromFile(CharacterData* data, const char* path, char* errMsg, int errMsgSize);

// Replace the character at `slot` with the file at `path`, preserving the slot's
// styling (color/opacity/radius) and the positions of all other characters.
// Returns false (and writes errMsg) if the new file cannot be loaded.
bool CharacterDataReplaceAt(CharacterData* data, int slot, const char* path, char* errMsg, int errMsgSize);

// Extracted helpers
const char* ExtractFilename(const char* path);
char* BuildComboString(const char* names[], int count);

#endif // CHARACTER_DATA_H

/*******************************************************************************************
*
*    character_data.c - Character data implementation
*
*******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "raylib.h"
#include "character_data.h"
#include "math_utils.h"
#include "argparse.h"
#include "transform_data.h"

const char* ExtractFilename(const char* path)
{
    const char* filename = path;
    while (strchr(filename, '/')) { filename = strchr(filename, '/') + 1; }
    while (strchr(filename, '\\')) { filename = strchr(filename, '\\') + 1; }
    return filename;
}

char* BuildComboString(const char* names[], int count)
{
    int totalSize = 1;
    for (int i = 0; i < count; i++) totalSize += (i > 0 ? 1 : 0) + (int)strlen(names[i]);
    char* result = malloc(totalSize);
    result[0] = '\0';
    for (int i = 0; i < count; i++)
    {
        if (i > 0) strcat(result, ";");
        strcat(result, names[i]);
    }
    return result;
}

void CharacterDataInit(CharacterData* data, int argc, char** argv)
{
    data->count = 0;
    data->active = 0;
    data->colors[0] = ArgColor(argc, argv, "-color0", ORANGE);
    data->colors[1] = ArgColor(argc, argv, "-color1", (Color){ 38, 134, 157, 255 });
    data->colors[2] = ArgColor(argc, argv, "-color2", PINK);
    data->colors[3] = ArgColor(argc, argv, "-color3", LIME);
    data->colors[4] = ArgColor(argc, argv, "-color4", VIOLET);
    data->colors[5] = ArgColor(argc, argv, "-color5", MAROON);
    srand(1234);
    for (int i = 6; i < CHARACTERS_BUFFER_MAX; i++)
        data->colors[i] = (Color){ rand() % 255, rand() % 255, rand() % 255 };
    for (int i = 0; i < CHARACTERS_BUFFER_MAX; i++)
    {
        BVHDataInit(&data->bvhData[i]);
        data->scales[i] = 1.0f;
        data->names[i][0] = '\0';
        data->autoScales[i] = 1.0f;
        data->opacities[i] = ArgFloat(argc, argv, "capsuleOpacity", 1.0f);
        data->radii[i] = ArgFloat(argc, argv, "maxCapsuleRadius", 0.04f);
        data->filePaths[i][0] = '\0';
        TransformDataInit(&data->xformData[i]);
        TransformDataInit(&data->xformTmp0[i]);
        TransformDataInit(&data->xformTmp1[i]);
        TransformDataInit(&data->xformTmp2[i]);
        TransformDataInit(&data->xformTmp3[i]);
        data->jointNamesCombo[i] = NULL;
        data->isGLB[i] = false;
        data->visible[i] = true;
        GLBDataInit(&data->glbData[i]);
    }
    data->colorPickerActive = ArgBool(argc, argv, "colorPickerActive", false);
    data->hasSkinnedMesh = false;
}

// Free per-character resources at slot i without changing data->count.
static void CharacterDataFreeSlot(CharacterData* data, int i)
{
    if (data->isGLB[i]) GLBDataFree(&data->glbData[i]);
    else BVHDataFree(&data->bvhData[i]);
    TransformDataFree(&data->xformData[i]);
    TransformDataFree(&data->xformTmp0[i]);
    TransformDataFree(&data->xformTmp1[i]);
    TransformDataFree(&data->xformTmp2[i]);
    TransformDataFree(&data->xformTmp3[i]);
    free(data->jointNamesCombo[i]);
    data->jointNamesCombo[i] = NULL;
    data->filePaths[i][0] = '\0';
    data->names[i][0] = '\0';
    data->isGLB[i] = false;
}

// Reset per-character pointer fields at slot i to safe defaults (no allocations).
static void CharacterDataResetSlot(CharacterData* data, int i)
{
    BVHDataInit(&data->bvhData[i]);
    GLBDataInit(&data->glbData[i]);
    TransformDataInit(&data->xformData[i]);
    TransformDataInit(&data->xformTmp0[i]);
    TransformDataInit(&data->xformTmp1[i]);
    TransformDataInit(&data->xformTmp2[i]);
    TransformDataInit(&data->xformTmp3[i]);
    data->jointNamesCombo[i] = NULL;
    data->filePaths[i][0] = '\0';
    data->names[i][0] = '\0';
    data->isGLB[i] = false;
}

// Move per-character data from sourceSlot to destSlot. destSlot must have been
// freed already (CharacterDataFreeSlot). sourceSlot is reset afterwards so its
// pointers won't be double-freed. Slot-styling (colors/opacities/radii) is not
// moved — it belongs to the destination slot.
static void CharacterDataMoveSlot(CharacterData* data, int destSlot, int sourceSlot)
{
    data->bvhData[destSlot]    = data->bvhData[sourceSlot];
    data->glbData[destSlot]    = data->glbData[sourceSlot];
    data->xformData[destSlot]  = data->xformData[sourceSlot];
    data->xformTmp0[destSlot]  = data->xformTmp0[sourceSlot];
    data->xformTmp1[destSlot]  = data->xformTmp1[sourceSlot];
    data->xformTmp2[destSlot]  = data->xformTmp2[sourceSlot];
    data->xformTmp3[destSlot]  = data->xformTmp3[sourceSlot];
    data->scales[destSlot]     = data->scales[sourceSlot];
    data->autoScales[destSlot] = data->autoScales[sourceSlot];
    memcpy(data->names[destSlot],     data->names[sourceSlot],     sizeof(data->names[0]));
    memcpy(data->filePaths[destSlot], data->filePaths[sourceSlot], sizeof(data->filePaths[0]));
    data->jointNamesCombo[destSlot] = data->jointNamesCombo[sourceSlot];
    data->isGLB[destSlot]   = data->isGLB[sourceSlot];
    data->visible[destSlot] = data->visible[sourceSlot];

    CharacterDataResetSlot(data, sourceSlot);
}

void CharacterDataFree(CharacterData* data)
{
    for (int i = 0; i < data->count; i++)
    {
        if (data->isGLB[i]) GLBDataFree(&data->glbData[i]);
        else BVHDataFree(&data->bvhData[i]);
        TransformDataFree(&data->xformData[i]);
        TransformDataFree(&data->xformTmp0[i]);
        TransformDataFree(&data->xformTmp1[i]);
        TransformDataFree(&data->xformTmp2[i]);
        TransformDataFree(&data->xformTmp3[i]);
        free(data->jointNamesCombo[i]);
        data->jointNamesCombo[i] = NULL;
        data->filePaths[i][0] = '\0';
        data->names[i][0] = '\0';
        data->isGLB[i] = false;
        data->visible[i] = true;
    }
    // Also clean up the staging slot so any residual allocations from a failed
    // CharacterDataReplaceAt attempt are freed.
    CharacterDataFreeSlot(data, CHARACTERS_MAX);
    CharacterDataResetSlot(data, CHARACTERS_MAX);
    data->count = 0;
    data->active = 0;
    data->hasSkinnedMesh = false;
}

// Load a file into the given slot. Caller owns slot bounds and count bookkeeping.
// On failure the slot may contain partial state; reset/free it before reuse.
static bool LoadIntoSlot(CharacterData* data, int slot, const char* path, char* errMsg, int errMsgSize)
{
    printf("INFO: Loading '%s'\n", path);
    const char* ext = strrchr(path, '.');
    bool isGLB = (ext != NULL) && (strcmp(ext, ".glb") == 0 || strcmp(ext, ".GLB") == 0 || strcmp(ext, ".gltf") == 0 || strcmp(ext, ".GLTF") == 0);
    if (isGLB)
    {
        if (!GLBDataLoad(&data->glbData[slot], path, errMsg, errMsgSize)) { printf("INFO: Failed to Load '%s'\n", path); return false; }
        data->isGLB[slot] = true;
        GLBData* glb = &data->glbData[slot];
        int jointCount = glb->model.skeleton.boneCount;
        int* parents = malloc(jointCount * sizeof(int));
        bool* endSite = malloc(jointCount * sizeof(bool));
        for (int j = 0; j < jointCount; j++)
        {
            int origIdx = glb->topoOrder[j];
            int origParent = glb->model.skeleton.bones[origIdx].parent;
            parents[j] = (origParent == -1) ? -1 : glb->invTopoOrder[origParent];
            endSite[j] = false;
        }
        TransformDataResizeSimple(&data->xformData[slot], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp0[slot], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp1[slot], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp2[slot], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp3[slot], jointCount, parents, endSite);
        free(parents); free(endSite);
        snprintf(data->filePaths[slot], 512, "%s", path);
        snprintf(data->names[slot], 128, "%s", ExtractFilename(path));
        // Joint names combo
        int comboTotalSize = 0;
        for (int j = 0; j < jointCount; j++) comboTotalSize += (j > 0 ? 1 : 0) + (int)strlen(glb->model.skeleton.bones[glb->topoOrder[j]].name);
        comboTotalSize++;
        data->jointNamesCombo[slot] = malloc(comboTotalSize);
        data->jointNamesCombo[slot][0] = '\0';
        for (int j = 0; j < jointCount; j++)
        {
            if (j > 0) strcat(data->jointNamesCombo[slot], ";");
            strcat(data->jointNamesCombo[slot], glb->model.skeleton.bones[glb->topoOrder[j]].name);
        }
        TransformDataSampleFrameGLB(&data->xformData[slot], glb, 0, 1.0f);
        TransformDataForwardKinematics(&data->xformData[slot]);
        float height = TransformDataGetMaxHeight(&data->xformData[slot]);
        data->scales[slot] = height > 10.0f ? 0.01f : 1.0f;
        data->autoScales[slot] = 1.8f / height;
        TransformDataSampleFrameGLB(&data->xformData[slot], glb, 0, data->scales[slot]);
        TransformDataForwardKinematics(&data->xformData[slot]);
        data->visible[slot] = true;
        if (glb->model.meshCount > 0) data->hasSkinnedMesh = true;
        return true;
    }
    else
    {
        if (BVHDataLoad(&data->bvhData[slot], path, errMsg, errMsgSize))
        {
            data->isGLB[slot] = false;
            TransformDataResize(&data->xformData[slot], &data->bvhData[slot]);
            TransformDataResize(&data->xformTmp0[slot], &data->bvhData[slot]);
            TransformDataResize(&data->xformTmp1[slot], &data->bvhData[slot]);
            TransformDataResize(&data->xformTmp2[slot], &data->bvhData[slot]);
            TransformDataResize(&data->xformTmp3[slot], &data->bvhData[slot]);
            snprintf(data->filePaths[slot], 512, "%s", path);
            snprintf(data->names[slot], 128, "%s", ExtractFilename(path));
            data->scales[slot] = 1.0f;
            if (data->bvhData[slot].frameCount > 0)
            {
                TransformDataSampleFrame(&data->xformData[slot], &data->bvhData[slot], 0, 1.0f);
                TransformDataForwardKinematics(&data->xformData[slot]);
                float height = TransformDataGetMaxHeight(&data->xformData[slot]);
                data->scales[slot] = height > 10.0f ? 0.01f : 1.0f;
                data->autoScales[slot] = 1.8f / height;
            }
            else
            {
                // No motion data: compute rest pose from joint offsets
                TransformDataSampleFrame(&data->xformData[slot], &data->bvhData[slot], 0, 1.0f);
                TransformDataForwardKinematics(&data->xformData[slot]);
                float height = TransformDataGetMaxHeight(&data->xformData[slot]);
                data->scales[slot] = height > 10.0f ? 0.01f : 1.0f;
                data->autoScales[slot] = (height > 0.0f) ? 1.8f / height : 1.0f;
            }
            // Joint names combo
            int comboTotalSize = 0;
            for (int i = 0; i < data->bvhData[slot].jointCount; i++)
                comboTotalSize += (i > 0 ? 1 : 0) + (int)strlen(data->bvhData[slot].joints[i].name);
            comboTotalSize++;
            data->jointNamesCombo[slot] = malloc(comboTotalSize);
            data->jointNamesCombo[slot][0] = '\0';
            for (int i = 0; i < data->bvhData[slot].jointCount; i++)
            {
                if (i > 0) strcat(data->jointNamesCombo[slot], ";");
                strcat(data->jointNamesCombo[slot], data->bvhData[slot].joints[i].name);
            }
            data->visible[slot] = true;
            return true;
        }
        else { printf("INFO: Failed to Load '%s'\n", path); return false; }
    }
}

bool CharacterDataReplaceAt(CharacterData* data, int slot, const char* path, char* errMsg, int errMsgSize)
{
    if (slot < 0 || slot >= data->count)
    {
        snprintf(errMsg, errMsgSize, "Error: Invalid slot %d for replace (count=%d)", slot, data->count);
        return false;
    }

    // Stage into the dedicated trailing slot (CHARACTERS_MAX). The stage slot is
    // sized into CHARACTERS_BUFFER_MAX-length arrays and is never counted as a
    // user-visible character, so this works even at full capacity.
    int stage = CHARACTERS_MAX;
    if (!LoadIntoSlot(data, stage, path, errMsg, errMsgSize))
    {
        // Clean up any partial state left at the stage slot so the next call starts fresh.
        CharacterDataFreeSlot(data, stage);
        CharacterDataResetSlot(data, stage);
        return false;
    }

    // Free the old slot's content, then move the staged data on top of it.
    CharacterDataFreeSlot(data, slot);
    CharacterDataMoveSlot(data, slot, stage);

    // Recompute hasSkinnedMesh across the loaded characters (LoadIntoSlot may have
    // set it true, but a removed character could have been the only skinned one).
    bool hasSkinned = false;
    for (int i = 0; i < data->count; i++)
    {
        if (data->isGLB[i] && data->glbData[i].model.meshCount > 0) { hasSkinned = true; break; }
    }
    data->hasSkinnedMesh = hasSkinned;

    return true;
}

bool CharacterDataLoadFromFile(CharacterData* data, const char* path, char* errMsg, int errMsgSize)
{
    if (data->count >= CHARACTERS_MAX)
    {
        snprintf(errMsg, 512, "Error: Maximum number of animation files loaded (%i)", CHARACTERS_MAX);
        return false;
    }
    if (!LoadIntoSlot(data, data->count, path, errMsg, errMsgSize))
        return false;
    data->count++;
    return true;
}

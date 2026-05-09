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
    for (int i = 6; i < CHARACTERS_MAX; i++)
        data->colors[i] = (Color){ rand() % 255, rand() % 255, rand() % 255 };
    for (int i = 0; i < CHARACTERS_MAX; i++)
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
        GLBDataInit(&data->glbData[i]);
    }
    data->colorPickerActive = ArgBool(argc, argv, "colorPickerActive", false);
    data->hasSkinnedMesh = false;
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
    }
}

bool CharacterDataLoadFromFile(CharacterData* data, const char* path, char* errMsg, int errMsgSize)
{
    printf("INFO: Loading '%s'\n", path);
    if (data->count == CHARACTERS_MAX)
    { snprintf(errMsg, 512, "Error: Maximum number of animation files loaded (%i)", CHARACTERS_MAX); return false; }
    const char* ext = strrchr(path, '.');
    bool isGLB = (ext != NULL) && (strcmp(ext, ".glb") == 0 || strcmp(ext, ".GLB") == 0 || strcmp(ext, ".gltf") == 0 || strcmp(ext, ".GLTF") == 0);
    if (isGLB)
    {
        if (!GLBDataLoad(&data->glbData[data->count], path, errMsg, errMsgSize)) { printf("INFO: Failed to Load '%s'\n", path); return false; }
        data->isGLB[data->count] = true;
        GLBData* glb = &data->glbData[data->count];
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
        TransformDataResizeSimple(&data->xformData[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp0[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp1[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp2[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp3[data->count], jointCount, parents, endSite);
        free(parents); free(endSite);
        snprintf(data->filePaths[data->count], 512, "%s", path);
        snprintf(data->names[data->count], 128, "%s", ExtractFilename(path));
        // Joint names combo
        int comboTotalSize = 0;
        for (int j = 0; j < jointCount; j++) comboTotalSize += (j > 0 ? 1 : 0) + (int)strlen(glb->model.skeleton.bones[glb->topoOrder[j]].name);
        comboTotalSize++;
        data->jointNamesCombo[data->count] = malloc(comboTotalSize);
        data->jointNamesCombo[data->count][0] = '\0';
        for (int j = 0; j < jointCount; j++)
        {
            if (j > 0) strcat(data->jointNamesCombo[data->count], ";");
            strcat(data->jointNamesCombo[data->count], glb->model.skeleton.bones[glb->topoOrder[j]].name);
        }
        TransformDataSampleFrameGLB(&data->xformData[data->count], glb, 0, 1.0f);
        TransformDataForwardKinematics(&data->xformData[data->count]);
        float height = TransformDataGetMaxHeight(&data->xformData[data->count]);
        data->scales[data->count] = height > 10.0f ? 0.01f : 1.0f;
        data->autoScales[data->count] = 1.8f / height;
        TransformDataSampleFrameGLB(&data->xformData[data->count], glb, 0, data->scales[data->count]);
        TransformDataForwardKinematics(&data->xformData[data->count]);
        data->count++;
        if (glb->model.meshCount > 0) data->hasSkinnedMesh = true;
        return true;
    }
    else
    {
        if (BVHDataLoad(&data->bvhData[data->count], path, errMsg, errMsgSize))
        {
            data->isGLB[data->count] = false;
            TransformDataResize(&data->xformData[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp0[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp1[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp2[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp3[data->count], &data->bvhData[data->count]);
            snprintf(data->filePaths[data->count], 512, "%s", path);
            snprintf(data->names[data->count], 128, "%s", ExtractFilename(path));
            data->scales[data->count] = 1.0f;
            if (data->bvhData[data->count].frameCount > 0)
            {
                TransformDataSampleFrame(&data->xformData[data->count], &data->bvhData[data->count], 0, 1.0f);
                TransformDataForwardKinematics(&data->xformData[data->count]);
                float height = TransformDataGetMaxHeight(&data->xformData[data->count]);
                data->scales[data->count] = height > 10.0f ? 0.01f : 1.0f;
                data->autoScales[data->count] = 1.8f / height;
            }
            else data->autoScales[data->count] = 1.0f;
            // Joint names combo
            int comboTotalSize = 0;
            for (int i = 0; i < data->bvhData[data->count].jointCount; i++)
                comboTotalSize += (i > 0 ? 1 : 0) + (int)strlen(data->bvhData[data->count].joints[i].name);
            comboTotalSize++;
            data->jointNamesCombo[data->count] = malloc(comboTotalSize);
            data->jointNamesCombo[data->count][0] = '\0';
            for (int i = 0; i < data->bvhData[data->count].jointCount; i++)
            {
                if (i > 0) strcat(data->jointNamesCombo[data->count], ";");
                strcat(data->jointNamesCombo[data->count], data->bvhData[data->count].joints[i].name);
            }
            data->count++;
            return true;
        }
        else { printf("INFO: Failed to Load '%s'\n", path); return false; }
    }
}

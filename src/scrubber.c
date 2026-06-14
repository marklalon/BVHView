/*******************************************************************************************
*
*    scrubber.c - Scrubber functions implementation
*
*******************************************************************************************/

#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "scrubber.h"
#include "character_data.h"
#include "glb_data.h"
#include "math_utils.h"
#include "argparse.h"

void ScrubberSettingsInit(ScrubberSettings* settings, int argc, char** argv)
{
    settings->playing = ArgBool(argc, argv, "playing", true);
    settings->looping = ArgBool(argc, argv, "looping", true);
    settings->inplace = ArgBool(argc, argv, "inplace", false);
    settings->playTime = ArgFloat(argc, argv, "playTime", 0.0f);
    settings->playSpeed = ArgFloat(argc, argv, "playSpeed", 1.0f);
    settings->frameSnap = ArgBool(argc, argv, "frameSnap", false);
    settings->sampleMode = ArgEnum(argc, argv, "sampleMode", 3, (const char*[]){ "nearest", "linear", "cubic" }, 1);
    settings->timeLimit = 0.0f;
    settings->frameLimit = 0;
    settings->frameMin = 0;
    settings->frameMax = 0;
    settings->frameMinSelect = 0;
    settings->frameMaxSelect = 0;
    settings->frameMinEdit = false;
    settings->frameMaxEdit = false;
    settings->timeMin = 0.0f;
    settings->timeMax = 0.0f;
}

int ScrubberGetFrameCount(CharacterData* characterData, int index)
{
    if (characterData->isGLB[index])
    {
        int animIdx = characterData->glbData[index].activeAnim;
        if (characterData->glbData[index].animCount > 0)
            return GLBDataGetSourceFrameCount(&characterData->glbData[index], animIdx);
        return 0;
    }
    return characterData->bvhData[index].frameCount;
}

float ScrubberGetFrameTime(CharacterData* characterData, int index)
{
    if (characterData->isGLB[index])
        return GLBDataGetSourceFrameTime(&characterData->glbData[index], characterData->glbData[index].activeAnim);
    return characterData->bvhData[index].frameTime;
}

void ScrubberSettingsRecomputeLimits(ScrubberSettings* settings, CharacterData* characterData)
{
    settings->frameLimit = 0;
    settings->timeLimit = 0.0f;
    for (int i = 0; i < characterData->count; i++)
    {
        int frameCount = ScrubberGetFrameCount(characterData, i);
        float frameTime = ScrubberGetFrameTime(characterData, i);
        if (frameCount > 0)
        {
            settings->frameLimit = MaxInt(settings->frameLimit, frameCount - 1);
            settings->timeLimit = Max(settings->timeLimit, (frameCount - 1) * frameTime);
        }
    }
}

void ScrubberSettingsInitMaxs(ScrubberSettings* settings, CharacterData* characterData)
{
    if (characterData->count == 0) return;
    int frameCount = ScrubberGetFrameCount(characterData, characterData->active);
    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);
    settings->frameMax = (frameCount > 0) ? frameCount - 1 : 0;
    settings->frameMaxSelect = settings->frameMax;
    settings->timeMax = settings->frameMax * frameTime;
    settings->frameMin = 0;
    settings->frameMinSelect = settings->frameMin;
    settings->timeMin = 0.0f;
    settings->playTime = Clamp(settings->playTime, settings->timeMin, settings->timeMax);
}

void ScrubberSettingsClamp(ScrubberSettings* settings, CharacterData* characterData)
{
    if (characterData->count == 0) return;
    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);
    int activeFrameCount = ScrubberGetFrameCount(characterData, characterData->active);
    int activeFrameMax = activeFrameCount - 1;
    settings->frameMax = ClampInt(settings->frameMax, 0, activeFrameMax);
    settings->frameMaxSelect = settings->frameMax;
    settings->timeMax = settings->frameMax * frameTime;
    settings->frameMin = ClampInt(settings->frameMin, 0, settings->frameMax);
    settings->frameMinSelect = settings->frameMin;
    settings->timeMin = settings->frameMin * frameTime;
    settings->playTime = Clamp(settings->playTime, settings->timeMin, settings->timeMax);
}

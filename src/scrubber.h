/*******************************************************************************************
*
*    scrubber.h - ScrubberSettings struct and function declarations
*
*******************************************************************************************/

#ifndef SCRUBBER_H
#define SCRUBBER_H

#include <stdbool.h>

// Forward declarations
typedef struct CharacterData CharacterData;

typedef struct ScrubberSettings {
    bool playing;
    bool looping;
    bool inplace;
    float playTime;
    float playSpeed;
    bool frameSnap;
    int sampleMode;
    float timeLimit;
    int frameLimit;
    int frameMin;
    int frameMax;
    int frameMinSelect;
    int frameMaxSelect;
    bool frameMinEdit;
    bool frameMaxEdit;
    float timeMin;
    float timeMax;
} ScrubberSettings;

void ScrubberSettingsInit(ScrubberSettings* settings, int argc, char** argv);
int ScrubberGetFrameCount(CharacterData* characterData, int index);
float ScrubberGetFrameTime(CharacterData* characterData, int index);
void ScrubberSettingsRecomputeLimits(ScrubberSettings* settings, CharacterData* characterData);
void ScrubberSettingsInitMaxs(ScrubberSettings* settings, CharacterData* characterData);
void ScrubberSettingsClamp(ScrubberSettings* settings, CharacterData* characterData);

#endif // SCRUBBER_H

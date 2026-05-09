/*******************************************************************************************
*
*    gui.h - GUI function declarations
*
*******************************************************************************************/

#ifndef GUI_H
#define GUI_H

#include "raylib.h"
#include "camera.h"

// Forward declarations
typedef struct CharacterData CharacterData;
typedef struct ScrubberSettings ScrubberSettings;
typedef struct RenderSettings RenderSettings;
typedef struct CapsuleData CapsuleData;

// Include external file dialog header for GuiWindowFileDialogState type
#include "../examples/custom_file_dialog/gui_window_file_dialog.h"

void GuiOrbitCamera(OrbitCamera* camera, CharacterData* characterData, int argc, char** argv);
int GetJointDepth(int jointIndex, const int* parents);
void GuiSkeletonPanel(OrbitCamera* camera, CharacterData* characterData, int screenWidth, int screenHeight);
void GuiRenderSettings(RenderSettings* settings, CapsuleData* capsuleData, int screenWidth, int screenHeight);
void GuiCharacterData(CharacterData* characterData, GuiWindowFileDialogState* fileDialogState, ScrubberSettings* scrubberSettings, char* errMsg, int argc, char** argv);
void GuiScrubberSettings(ScrubberSettings* settings, CharacterData* characterData, int screenWidth, int screenHeight);

#endif // GUI_H

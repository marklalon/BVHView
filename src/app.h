/*******************************************************************************************
*
*    app.h - ApplicationState struct and ApplicationUpdate declaration
*
*******************************************************************************************/

#ifndef APP_H
#define APP_H

#include "raylib.h"
#include "shaders.h"
#include "character_data.h"
#include "capsule_data.h"
#include "scrubber.h"
#include "render_settings.h"
#include "camera.h"

// Include external file dialog header for GuiWindowFileDialogState type
// NOTE: GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION is defined in gui.c, not here
#include "../examples/custom_file_dialog/gui_window_file_dialog.h"

// Structure containing all of the application state
typedef struct ApplicationState {
    int argc;
    char** argv;
    int screenWidth;
    int screenHeight;
    OrbitCamera camera;
    Shader shader;
    ShaderUniforms uniforms;
    Mesh groundPlaneMesh;
    Model groundPlaneModel;
    Model capsuleModel;
    CharacterData characterData;
    CapsuleData capsuleData;
    ScrubberSettings scrubberSettings;
    RenderSettings renderSettings;
    GuiWindowFileDialogState fileDialogState;
    char errMsg[512];

    // PageUp/PageDown file switching
    char* fileList[4096];
    int fileListCount;
    int fileListIndex;
    char lastScannedDir[512];

    // Camera preservation across file switch
    bool restoreCameraAfterSwitch;
    Vector3 savedCamPos;
    Vector3 savedCamTarget;
} ApplicationState;

void ApplicationUpdate(void* voidApplicationState);
void OnFileLoaded(ApplicationState* app);
void ApplicationCleanup(ApplicationState* app);

#endif // APP_H

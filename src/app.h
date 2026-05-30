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

#if defined(_WIN32) && !defined(PLATFORM_WEB)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>

// windows.h (even with WIN32_LEAN_AND_MEAN) defines Rectangle as a macro,
// which conflicts with raylib's Rectangle struct.
#undef Rectangle

#define BVHVIEW_PATH_BUFFER_SIZE 4096
#define BVHVIEW_REUSE_MAILSLOT "\\\\.\\mailslot\\BVHViewReuse"
#endif

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

    // ArrowUp/Down file switching, PageUp/Down group switching
    char* fileList[4096];
    int fileListCount;
    int fileListIndex;
    char lastScannedDir[512];

    // Prefix-based grouping (split filename on '_', first segment = group)
    char* groupNames[4096];
    int groupCount;
    int groupStartIndex[4096];
    int currentGroupIndex;

    // Camera preservation across file switch
    bool restoreCameraAfterSwitch;
    Vector3 savedCamPos;
    Vector3 savedCamTarget;

#if defined(_WIN32) && !defined(PLATFORM_WEB)
    HANDLE reuseMailslot;
#endif
} ApplicationState;

void ApplicationUpdate(void* voidApplicationState);
void OnFileLoaded(ApplicationState* app);
void ApplicationCleanup(ApplicationState* app);

#endif // APP_H

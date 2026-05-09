/*******************************************************************************************
*
*    main.c - Main entry point
*
*******************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "app.h"
#include "gui.h"
#include "shaders.h"
#include "models.h"
#include "character_data.h"
#include "capsule_data.h"
#include "scrubber.h"
#include "render_settings.h"
#include "argparse.h"
#include "profile.h"

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char** argv)
{
    PROFILE_INIT();
    PROFILE_TICKERS_INIT();

    ApplicationState app;
    app.argc = argc;
    app.argv = argv;
    app.screenWidth = ArgInt(argc, argv, "screenWidth", 1920);
    app.screenHeight = ArgInt(argc, argv, "screenHeight", 1080);

    SetConfigFlags(FLAG_VSYNC_HINT);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(app.screenWidth, app.screenHeight, "BVHView");
    SetTargetFPS(0);

    GuiInitDarkMode();

    OrbitCameraInit(&app.camera, argc, argv);

    app.shader = LoadShaderFromMemory(shaderVS, shaderFS);
    ShaderUniformsInit(&app.uniforms, app.shader);

    app.groundPlaneMesh = GenMeshPlane(2.0f, 2.0f, 1, 1);
    app.groundPlaneModel = LoadModelFromMesh(app.groundPlaneMesh);
    app.groundPlaneModel.materials[0].shader = app.shader;

    app.capsuleModel = LoadOBJFromMemory(capsuleOBJ);
    app.capsuleModel.materials[0].shader = app.shader;

    CharacterDataInit(&app.characterData, argc, argv);
    CapsuleDataInit(&app.capsuleData);
    ScrubberSettingsInit(&app.scrubberSettings, argc, argv);
    RenderSettingsInit(&app.renderSettings, argc, argv);
    CapsuleDataUpdateShadowLookupTable(&app.capsuleData, app.renderSettings.sunLightConeAngle);

    app.fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
    app.errMsg[0] = '\0';

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-') continue;
        CharacterDataLoadFromFile(&app.characterData, argv[i], app.errMsg, 512);
    }

    if (app.characterData.count > 0)
    {
        app.characterData.active = app.characterData.count - 1;
        if (app.characterData.hasSkinnedMesh) { app.renderSettings.drawMeshes = true; app.renderSettings.drawCapsules = false; }
        CapsuleDataUpdateForCharacters(&app.capsuleData, &app.characterData);
        ScrubberSettingsRecomputeLimits(&app.scrubberSettings, &app.characterData);
        ScrubberSettingsInitMaxs(&app.scrubberSettings, &app.characterData);
        char windowTitle[528];
        snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app.characterData.filePaths[app.characterData.active]);
        SetWindowTitle(windowTitle);
    }

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop_arg(ApplicationUpdate, &app, 0, 1);
#else
    while (!WindowShouldClose())
    {
        ApplicationUpdate(&app);
    }
#endif

    CapsuleDataFree(&app.capsuleData);
    CharacterDataFree(&app.characterData);
    UnloadModel(app.capsuleModel);
    UnloadModel(app.groundPlaneModel);
    UnloadShader(app.shader);
    CloseWindow();

    return 0;
}

/*******************************************************************************************
*
*    app.c - ApplicationUpdate implementation
*
*******************************************************************************************/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"
#include "app.h"
#include "gui.h"
#include "drawing.h"
#include "geometry.h"
#include "math_utils.h"
#include "models.h"
#include "transform_data.h"
#include "profile.h"
#include "argparse.h"
#include "camera.h"

#define RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT 24
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

// Shared key-repeat state for keys that should repeat when held
typedef struct {
    int lastPressedKey;
    double pressTime;
    int repeatCount;
} KeyRepeatState;

// Returns true if a key-press action should fire (initial press or timed repeat).
// Use IsKeyDown to check if the key is held, then call this to gate the action.
static bool KeyRepeatShouldFire(KeyRepeatState* state, int key)
{
    if (key != state->lastPressedKey)
    {
        // New key press: fire immediately
        state->lastPressedKey = key;
        state->repeatCount = 0;
        state->pressTime = GetTime();
        return true;
    }
    // Key still held: repeat using standard Windows key-repeat timing
    double now = GetTime();
    double elapsed = now - state->pressTime;
    double initialDelay = 0.400;
    double repeatInterval = 0.100;
    double interval = (state->repeatCount == 0) ? initialDelay : repeatInterval;

    if (elapsed >= interval)
    {
        state->pressTime = now;
        state->repeatCount++;
        return true;
    }
    return false;
}

static void KeyRepeatReset(KeyRepeatState* state)
{
    state->lastPressedKey = -1;
    state->repeatCount = 0;
}

void OrbitCameraInit(OrbitCamera* camera, int argc, char** argv)
{
    memset(&camera->cam3d, 0, sizeof(Camera3D));
    camera->cam3d.position = (Vector3){ 2.0f, 3.0f, 5.0f };
    camera->cam3d.target = (Vector3){ -0.5f, 1.0f, 0.0f };
    camera->cam3d.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera->cam3d.fovy = ArgFloat(argc, argv, "cameraFOV", 45.0f);
    camera->cam3d.projection = CAMERA_PERSPECTIVE;
    camera->azimuth = ArgFloat(argc, argv, "cameraAzimuth", 0.0f);
    camera->altitude = ArgFloat(argc, argv, "cameraAltitude", 0.4f);
    camera->distance = ArgFloat(argc, argv, "cameraDistance", 4.0f);
    camera->offset = ArgVector3(argc, argv, "cameraOffset", Vector3Zero());
    camera->track = ArgBool(argc, argv, "cameraTrack", true);
    camera->trackBone = ArgInt(argc, argv, "cameraTrackBone", 0);
    camera->showSkeletonPanel = false;
    camera->selectedBone = 0;
}

void OrbitCameraUpdate(OrbitCamera* camera, Vector3 target, float azimuthDelta, float altitudeDelta, float offsetDeltaX, float offsetDeltaY, float mouseWheel, float dt)
{
    camera->azimuth = camera->azimuth + 1.0f * dt * -azimuthDelta;
    camera->altitude = Clamp(camera->altitude + 1.0f * dt * altitudeDelta, -0.2 * PI, 0.45f * PI);
    camera->distance = Clamp(camera->distance + 50.0f * dt * -mouseWheel, 0.1f, 100.0f);
    Quaternion rotationAzimuth = QuaternionFromAxisAngle((Vector3){0, 1, 0}, camera->azimuth);
    Vector3 position = Vector3RotateByQuaternion((Vector3){0, 0, camera->distance}, rotationAzimuth);
    Vector3 axis = Vector3Normalize(Vector3CrossProduct(position, (Vector3){0, 1, 0}));
    Quaternion rotationAltitude = QuaternionFromAxisAngle(axis, camera->altitude);
    Vector3 localOffset = (Vector3){ dt * offsetDeltaX, dt * -offsetDeltaY, 0.0f };
    localOffset = Vector3RotateByQuaternion(localOffset, rotationAzimuth);
    camera->offset = Vector3Add(camera->offset, Vector3RotateByQuaternion(localOffset, rotationAltitude));
    Vector3 cameraTarget = Vector3Add(camera->offset, target);
    Vector3 eye = Vector3Add(cameraTarget, Vector3RotateByQuaternion(position, rotationAltitude));
    camera->cam3d.target = cameraTarget;
    camera->cam3d.position = eye;
}

// Extract the directory portion from a file path
static void ExtractDirectory(const char* path, char* dir, int dirSize)
{
    // Find last path separator directly on the input
    const char* lastSep = NULL;
    for (const char* p = path; *p; p++)
    {
        if (*p == '/' || *p == '\\')
            lastSep = p;
    }
    if (lastSep != NULL)
    {
        int len = (int)(lastSep - path);
        if (len >= dirSize) len = dirSize - 1;
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    else
    {
        snprintf(dir, dirSize, ".");
    }
}

// Free the file list (preserves index for rebuild)
static void FreeFileList(ApplicationState* app)
{
    for (int i = 0; i < app->fileListCount; i++)
    {
        free(app->fileList[i]);
        app->fileList[i] = NULL;
    }
    app->fileListCount = 0;

    for (int i = 0; i < app->groupCount; i++)
    {
        free(app->groupNames[i]);
        app->groupNames[i] = NULL;
    }
    app->groupCount = 0;
}

// Return the raw bone name (without leading spaces) for the currently selected bone,
// or NULL if there is no valid selection.
static const char* GetSelectedBoneName(CharacterData* characterData, int selectedBone)
{
    int ci = characterData->active;
    if (ci < 0 || ci >= characterData->count) return NULL;
    if (selectedBone < 0 || selectedBone >= characterData->xformData[ci].jointCount) return NULL;

    if (characterData->isGLB[ci])
    {
        GLBData* glb = &characterData->glbData[ci];
        int origIdx = glb->topoOrder[selectedBone];
        return glb->model.skeleton.bones[origIdx].name;
    }
    else
    {
        return characterData->bvhData[ci].joints[selectedBone].name;
    }
}

void ApplicationCleanup(ApplicationState* app)
{
    FreeFileList(app);
    UnloadDirectoryFiles(app->fileDialogState.dirFiles);
    app->fileDialogState.dirFiles.count = 0;
    app->fileDialogState.dirFiles.paths = NULL;
}

// Build sorted file list of .bvh/.glb/.gltf files in the directory of the given path.
// Skips scanning if the directory hasn't changed since the last call.
static void BuildFileList(ApplicationState* app, const char* currentFilePath)
{
    char dir[512];
    ExtractDirectory(currentFilePath, dir, sizeof(dir));

    // If directory is the same as last scanned, just find current index and return
    if (app->lastScannedDir[0] != '\0' && strcmp(dir, app->lastScannedDir) == 0)
    {
        // Find current file index in existing list
        for (int i = 0; i < app->fileListCount; i++)
        {
            if (strcmp(app->fileList[i], currentFilePath) == 0)
            {
                app->fileListIndex = i;
                break;
            }
        }
        // Update current group index to match new file index
        for (int g = app->groupCount - 1; g >= 0; g--)
        {
            if (app->fileListIndex >= app->groupStartIndex[g])
            {
                app->currentGroupIndex = g;
                break;
            }
        }
        return;
    }

    // Free previous list before scanning new directory
    FreeFileList(app);

    // Save current directory
    snprintf(app->lastScannedDir, sizeof(app->lastScannedDir), "%s", dir);

    // Scan directory
    FilePathList files = LoadDirectoryFiles(dir);
    if (files.count <= 0)
    {
        UnloadDirectoryFiles(files);
        return;
    }

    // Collect matching files into a temporary array
    char* matched[4096];
    int matchedCount = 0;

    for (int i = 0; i < files.count && matchedCount < 4096; i++)
    {
        const char* fullPath = files.paths[i];  // LoadDirectoryFiles returns full paths
        const char* name = ExtractFilename(fullPath);
        const char* ext = strrchr(name, '.');
        if (ext && (strcmp(ext, ".bvh") == 0 || strcmp(ext, ".BVH") == 0 ||
                    strcmp(ext, ".glb") == 0 || strcmp(ext, ".GLB") == 0 ||
                    strcmp(ext, ".gltf") == 0 || strcmp(ext, ".GLTF") == 0))
        {
            // Use the full path directly from LoadDirectoryFiles
            matched[matchedCount] = strdup(fullPath);
            matchedCount++;
        }
    }

    UnloadDirectoryFiles(files);

    if (matchedCount <= 1)
    {
        // Not enough files to switch; free and clear
        for (int i = 0; i < matchedCount; i++) free(matched[i]);
        app->lastScannedDir[0] = '\0';
        return;
    }

    // Sort by filename (case-insensitive)
    for (int i = 0; i < matchedCount - 1; i++)
    {
        for (int j = i + 1; j < matchedCount; j++)
        {
            const char* nameI = ExtractFilename(matched[i]);
            const char* nameJ = ExtractFilename(matched[j]);
            if (stricmp(nameI, nameJ) > 0)
            {
                char* tmp = matched[i];
                matched[i] = matched[j];
                matched[j] = tmp;
            }
        }
    }

    // Transfer to app->fileList and find current index
    for (int i = 0; i < matchedCount; i++)
    {
        app->fileList[i] = matched[i];
        if (strcmp(app->fileList[i], currentFilePath) == 0)
            app->fileListIndex = i;
    }
    app->fileListCount = matchedCount;

    // Build prefix-based groups from sorted file list
    // Files are already sorted by filename, so same-prefix files are adjacent.
    // Prefix = part of filename (without extension) before the first '_'.
    app->groupCount = 0;
    app->currentGroupIndex = 0;

    for (int i = 0; i < matchedCount; i++)
    {
        const char* fileName = ExtractFilename(app->fileList[i]);
        char nameNoExt[256];
        snprintf(nameNoExt, sizeof(nameNoExt), "%s", fileName);
        char* dot = strrchr(nameNoExt, '.');
        if (dot) *dot = '\0';

        char* underscore = strchr(nameNoExt, '_');
        if (underscore) *underscore = '\0';

        // Check if this prefix starts a new group
        if (app->groupCount == 0 || strcmp(nameNoExt, app->groupNames[app->groupCount - 1]) != 0)
        {
            app->groupNames[app->groupCount] = strdup(nameNoExt);
            app->groupStartIndex[app->groupCount] = i;
            app->groupCount++;
        }
    }

    // Find current group index based on current file index
    for (int g = app->groupCount - 1; g >= 0; g--)
    {
        if (app->fileListIndex >= app->groupStartIndex[g])
        {
            app->currentGroupIndex = g;
            break;
        }
    }
}

// Common post-load initialization: set active, update capsules, scrubber, window title, build file list
void OnFileLoaded(ApplicationState* app)
{
    app->characterData.active = app->characterData.count - 1;
    if (app->characterData.hasSkinnedMesh)
    {
        app->renderSettings.drawMeshes = true;
        app->renderSettings.drawCapsules = false;
    }
    CapsuleDataUpdateForCharacters(&app->capsuleData, &app->characterData);
    ScrubberSettingsRecomputeLimits(&app->scrubberSettings, &app->characterData);
    ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);

    // Build file list first so we know the count/index for the title suffix
    BuildFileList(app, app->characterData.filePaths[app->characterData.active]);

    char windowTitle[528];
    if (app->fileListCount > 1)
    {
        snprintf(windowTitle, sizeof(windowTitle), "%s (%d/%d) - BVHView",
                 app->characterData.filePaths[app->characterData.active],
                 app->fileListIndex + 1,
                 app->fileListCount);
    }
    else
    {
        snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView",
                 app->characterData.filePaths[app->characterData.active]);
    }
    SetWindowTitle(windowTitle);
}

void ApplicationUpdate(void* voidApplicationState)
{
    ApplicationState* app = voidApplicationState;

    // Process File Dialog
    if (app->fileDialogState.SelectFilePressed)
    {
        if (IsFileExtension(app->fileDialogState.fileNameText, ".bvh") || IsFileExtension(app->fileDialogState.fileNameText, ".glb") || IsFileExtension(app->fileDialogState.fileNameText, ".gltf"))
        {
            char fileNameToLoad[2048];
            snprintf(fileNameToLoad, sizeof(fileNameToLoad), "%s/%s", app->fileDialogState.dirPathText, app->fileDialogState.fileNameText);
            if (CharacterDataLoadFromFile(&app->characterData, fileNameToLoad, app->errMsg, 512))
            {
                OnFileLoaded(app);
            }
        }
        else snprintf(app->errMsg, 512, "Error: File '%.*s' is not a supported animation file (.bvh, .glb, .gltf).", 400, app->fileDialogState.fileNameText);
        app->fileDialogState.SelectFilePressed = false;
    }

    // Process Dragged and Dropped Files
    if (IsFileDropped())
    {
        FilePathList droppedFiles = LoadDroppedFiles();
        int prevBvhCount = app->characterData.count;
        for (int i = 0; i < droppedFiles.count; i++)
        {
            if (CharacterDataLoadFromFile(&app->characterData, droppedFiles.paths[i], app->errMsg, 512))
                app->characterData.active = app->characterData.count - 1;
        }
        UnloadDroppedFiles(droppedFiles);
        if (app->characterData.count > prevBvhCount)
        {
            OnFileLoaded(app);
        }
    }

    // Process Key Presses
    if (IsKeyPressed(KEY_H) && !app->fileDialogState.windowActive)
        app->renderSettings.drawUI = !app->renderSettings.drawUI;

    // Ctrl+C: copy selected bone name (without leading spaces) to clipboard
    if (!app->fileDialogState.windowActive && IsKeyPressed(KEY_C) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
    {
        const char* boneName = GetSelectedBoneName(&app->characterData, app->camera.selectedBone);
        if (boneName != NULL)
        {
            SetClipboardText(boneName);
        }
    }

    // Space: toggle play/pause
    if (!app->fileDialogState.windowActive && IsKeyPressed(KEY_SPACE))
    {
        app->scrubberSettings.playing = !app->scrubberSettings.playing;
    }

    // Tab: cycle active character
    if (!app->fileDialogState.windowActive && IsKeyPressed(KEY_TAB) && app->characterData.count > 1)
    {
        int next = app->characterData.active + 1;
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
            next = app->characterData.active - 1;
        if (next < 0) next = app->characterData.count - 1;
        if (next >= app->characterData.count) next = 0;
        app->characterData.active = next;
        ScrubberSettingsClamp(&app->scrubberSettings, &app->characterData);
        char windowTitle[528];
        snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app->characterData.filePaths[app->characterData.active]);
        SetWindowTitle(windowTitle);
    }

    // ArrowLeft/ArrowRight: switch to previous/next file in the same directory (repeat when held)
    if (!app->fileDialogState.windowActive && app->fileListCount > 1)
    {
        static KeyRepeatState repeatState = { -1, 0.0, 0 };

        int direction = 0;
        int currentKey = -1;
        if (IsKeyDown(KEY_LEFT)) { direction = -1; currentKey = KEY_LEFT; }
        else if (IsKeyDown(KEY_RIGHT)) { direction = 1; currentKey = KEY_RIGHT; }

        if (direction != 0)
        {
            if (KeyRepeatShouldFire(&repeatState, currentKey))
            {
                int startIndex = app->fileListIndex;

                // Save the absolute camera view before switching.
                app->savedCamPos = app->camera.cam3d.position;
                app->savedCamTarget = app->camera.cam3d.target;
                app->restoreCameraAfterSwitch = true;

                // Try candidate files in the requested direction, skipping ones that fail.
                bool loaded = false;
                for (int attempt = 1; attempt < app->fileListCount; attempt++)
                {
                    int candidate = (startIndex + direction * attempt + app->fileListCount) % app->fileListCount;

                    CharacterDataFree(&app->characterData);
                    if (CharacterDataLoadFromFile(&app->characterData, app->fileList[candidate], app->errMsg, 512))
                    {
                        app->fileListIndex = candidate;
                        loaded = true;
                        OnFileLoaded(app);
                        break;
                    }
                }

                // If nothing else could be loaded, restore the current file and cancel camera restore.
                if (!loaded)
                {
                    app->fileListIndex = startIndex;
                    app->restoreCameraAfterSwitch = false;
                    CharacterDataFree(&app->characterData);
                    if (CharacterDataLoadFromFile(&app->characterData, app->fileList[startIndex], app->errMsg, 512))
                        OnFileLoaded(app);
                }
            }
        }
        else
        {
            KeyRepeatReset(&repeatState);
        }
    }

    // ArrowUp/ArrowDown: switch selected bone in skeleton (repeat when held)
    if (!app->fileDialogState.windowActive && app->characterData.count > 0)
    {
        int ci = app->characterData.active;
        int jointCount = app->characterData.xformData[ci].jointCount;

        if (jointCount > 1)
        {
            static KeyRepeatState repeatState = { -1, 0.0, 0 };

            int direction = 0;
            int currentKey = -1;
            if (IsKeyDown(KEY_UP)) { direction = -1; currentKey = KEY_UP; }
            else if (IsKeyDown(KEY_DOWN)) { direction = 1; currentKey = KEY_DOWN; }

            if (direction != 0)
            {
                if (KeyRepeatShouldFire(&repeatState, currentKey))
                {
                    app->camera.selectedBone += direction;
                    if (app->camera.selectedBone < 0)
                        app->camera.selectedBone = jointCount - 1;
                    if (app->camera.selectedBone >= jointCount)
                        app->camera.selectedBone = 0;
                    if (app->camera.track)
                        app->camera.trackBone = app->camera.selectedBone;
                }
            }
            else
            {
                KeyRepeatReset(&repeatState);
            }
        }
    }

    // PageUp/PageDown: switch to previous/next group (jump to first file of each group, repeat when held)
    if (!app->fileDialogState.windowActive && app->groupCount > 1)
    {
        static KeyRepeatState repeatState = { -1, 0.0, 0 };

        int direction = 0;
        int currentKey = -1;
        if (IsKeyDown(KEY_PAGE_UP)) { direction = -1; currentKey = KEY_PAGE_UP; }
        else if (IsKeyDown(KEY_PAGE_DOWN)) { direction = 1; currentKey = KEY_PAGE_DOWN; }

        if (direction != 0)
        {
            if (KeyRepeatShouldFire(&repeatState, currentKey))
            {
                int targetGroup = (app->currentGroupIndex + direction + app->groupCount) % app->groupCount;
                int targetIndex = app->groupStartIndex[targetGroup];

                // Save the absolute camera view before switching.
                app->savedCamPos = app->camera.cam3d.position;
                app->savedCamTarget = app->camera.cam3d.target;
                app->restoreCameraAfterSwitch = true;

                CharacterDataFree(&app->characterData);
                if (CharacterDataLoadFromFile(&app->characterData, app->fileList[targetIndex], app->errMsg, 512))
                {
                    app->fileListIndex = targetIndex;
                    app->currentGroupIndex = targetGroup;
                    OnFileLoaded(app);
                }
                else
                {
                    // Fallback: restore current file
                    app->restoreCameraAfterSwitch = false;
                    CharacterDataFree(&app->characterData);
                    int startIndex = app->fileListIndex;
                    if (CharacterDataLoadFromFile(&app->characterData, app->fileList[startIndex], app->errMsg, 512))
                        OnFileLoaded(app);
                }
            }
        }
        else
        {
            KeyRepeatReset(&repeatState);
        }
    }

    PROFILE_BEGIN(Update);

    // Tick time forward
    if (app->scrubberSettings.playing)
    {
        app->scrubberSettings.playTime += app->scrubberSettings.playSpeed * GetFrameTime();
        if (app->scrubberSettings.playTime > app->scrubberSettings.timeMax)
        {
            float loopSpan = app->scrubberSettings.timeMax - app->scrubberSettings.timeMin;
            if (app->scrubberSettings.looping && loopSpan >= 1e-8f)
                app->scrubberSettings.playTime = fmodf(app->scrubberSettings.playTime - app->scrubberSettings.timeMin, loopSpan) + app->scrubberSettings.timeMin;
            else app->scrubberSettings.playTime = app->scrubberSettings.timeMax;
        }
    }

    // Sample Animation Data
    for (int i = 0; i < app->characterData.count; i++)
    {
        if (!app->characterData.visible[i]) continue;
        if (app->characterData.isGLB[i])
        {
            TransformDataSampleFrameGLB(&app->characterData.xformData[i], &app->characterData.glbData[i], app->scrubberSettings.playTime, app->characterData.scales[i]);
        }
        else if (app->scrubberSettings.sampleMode == 0)
        {
            TransformDataSampleFrameNearest(&app->characterData.xformData[i], &app->characterData.bvhData[i], app->scrubberSettings.playTime, app->characterData.scales[i]);
        }
        else if (app->scrubberSettings.sampleMode == 1)
        {
            TransformDataSampleFrameLinear(&app->characterData.xformData[i], &app->characterData.xformTmp0[i], &app->characterData.xformTmp1[i], &app->characterData.bvhData[i], app->scrubberSettings.playTime, app->characterData.scales[i]);
        }
        else
        {
            TransformDataSampleFrameCubic(&app->characterData.xformData[i], &app->characterData.xformTmp0[i], &app->characterData.xformTmp1[i], &app->characterData.xformTmp2[i], &app->characterData.xformTmp3[i], &app->characterData.bvhData[i], app->scrubberSettings.playTime, app->characterData.scales[i]);
        }
        if (app->scrubberSettings.inplace)
        {
            app->characterData.xformData[i].localPositions[0].x = 0.0f;
            app->characterData.xformData[i].localPositions[0].z = 0.0f;
            Quaternion verticalRotation = QuaternionInvert(QuaternionNormalize((Quaternion){ 0.0f, app->characterData.xformData[i].localRotations[0].y, 0.0f, app->characterData.xformData[i].localRotations[0].w }));
            app->characterData.xformData[i].localRotations[0] = QuaternionMultiply(verticalRotation, app->characterData.xformData[i].localRotations[0]);
        }
        TransformDataForwardKinematics(&app->characterData.xformData[i]);
    }

    // Update Camera
    Vector3 cameraTarget = (Vector3){ 0.0f, 1.0f, 0.0f };
    if (app->characterData.count > 0 && app->camera.track && app->camera.trackBone < app->characterData.xformData[app->characterData.active].jointCount)
        cameraTarget = app->characterData.xformData[app->characterData.active].globalPositions[app->camera.trackBone];
    if (!app->fileDialogState.windowActive)
    {
        bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool middleDown = IsMouseButtonDown(2);
        float mouseWheel = GetMouseWheelMove();
        if (app->camera.showSkeletonPanel && app->renderSettings.drawUI)
        {
            Rectangle skeletonPanel = { 220.0f, 10.0f, 260.0f, 620.0f };
            if (CheckCollisionPointRec(GetMousePosition(), skeletonPanel)) mouseWheel = 0.0f;
        }
        OrbitCameraUpdate(&app->camera, cameraTarget, (middleDown && !shiftHeld) ? GetMouseDelta().x : 0.0f, (middleDown && !shiftHeld) ? GetMouseDelta().y : 0.0f, (middleDown && shiftHeld) ? -GetMouseDelta().x : 0.0f, (middleDown && shiftHeld) ? -GetMouseDelta().y : 0.0f, mouseWheel, GetFrameTime());
    }

    // Restore camera position after file switch (after OrbitCameraUpdate has run)
    if (app->restoreCameraAfterSwitch)
    {
        // Recompute offset so future frames maintain the saved view
        app->camera.offset = Vector3Subtract(app->savedCamTarget, cameraTarget);
        app->camera.cam3d.position = app->savedCamPos;
        app->camera.cam3d.target = app->savedCamTarget;
        app->restoreCameraAfterSwitch = false;
    }

    // Create Capsules
    CapsuleDataReset(&app->capsuleData);
    for (int i = 0; i < app->characterData.count; i++)
    {
        if (!app->characterData.visible[i]) continue;
        CapsuleDataAppendFromTransformData(&app->capsuleData, &app->characterData.xformData[i], app->characterData.radii[i], app->characterData.colors[i], app->characterData.opacities[i], !app->renderSettings.drawEndSites);
    }

    PROFILE_END(Update);

    // Rendering
    Frustum frustum = FrustumFromCameraMatrices(GetCameraProjectionMatrix(&app->camera.cam3d, app->screenHeight / app->screenWidth), GetCameraViewMatrix(&app->camera.cam3d));
    BeginDrawing();
    PROFILE_BEGIN(Rendering);
    ClearBackground(app->renderSettings.backgroundColor);
    BeginMode3D(app->camera.cam3d);

    // Set shader uniforms
    Vector3 sunColorValue = { app->renderSettings.sunColor.r / 255.0f, app->renderSettings.sunColor.g / 255.0f, app->renderSettings.sunColor.b / 255.0f };
    Vector3 skyColorValue = { app->renderSettings.skyColor.r / 255.0f, app->renderSettings.skyColor.g / 255.0f, app->renderSettings.skyColor.b / 255.0f };
    float objectSpecularity = 0.5f;
    float objectGlossiness = 10.0f;
    float objectOpacity = 1.0f;
    Vector3 sunLightPosition = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, 1.0f }, QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, app->renderSettings.sunAzimuth));
    Vector3 sunLightAxis = Vector3Normalize(Vector3CrossProduct(sunLightPosition, (Vector3){ 0.0f, 1.0f, 0.0f }));
    Vector3 sunLightDir = Vector3Negate(Vector3RotateByQuaternion(sunLightPosition, QuaternionFromAxisAngle(sunLightAxis, app->renderSettings.sunAltitude)));
    SetShaderValue(app->shader, app->uniforms.cameraPosition, &app->camera.cam3d.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.exposure, &app->renderSettings.exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.sunDir, &sunLightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.sunStrength, &app->renderSettings.sunLightStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.sunColor, &sunColorValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.skyStrength, &app->renderSettings.skyLightStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.skyColor, &skyColorValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.ambientStrength, &app->renderSettings.ambientLightStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.groundStrength, &app->renderSettings.groundLightStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectSpecularity, &objectSpecularity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectGlossiness, &objectGlossiness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectOpacity, &objectOpacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.aoLookupResolution, &app->capsuleData.aoLookupResolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(app->shader, app->uniforms.shadowLookupResolution, &app->capsuleData.shadowLookupResolution, SHADER_UNIFORM_VEC2);
    SetShaderValueTexture(app->shader, app->uniforms.aoLookupTable, app->capsuleData.aoLookupTable);
    SetShaderValueTexture(app->shader, app->uniforms.shadowLookupTable, app->capsuleData.shadowLookupTable);

    // Draw Ground
    PROFILE_BEGIN(RenderingGround);
    if (app->renderSettings.drawChecker)
    {
        int groundIsCapsule = 0;
        int groundUseTexture = 0;
        Vector3 groundColor = { 0.65f, 0.65f, 0.65f };
        SetShaderValue(app->shader, app->uniforms.isCapsule, &groundIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.useTexture, &groundUseTexture, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.objectColor, &groundColor, SHADER_UNIFORM_VEC3);
        for (int i = 0; i < 11; i++)
        {
            for (int j = 0; j < 11; j++)
            {
                Vector3 groundSegmentPosition = { (((float)i / 10) - 0.5f) * 20.0f, 0.0f, (((float)j / 10) - 0.5f) * 20.0f };
                if (!FrustumContainsSphere(frustum, groundSegmentPosition, sqrtf(2.0f))) continue;
                PROFILE_BEGIN(RenderingGroundSegment);
                PROFILE_BEGIN(RenderingGroundSegmentAO);
                app->capsuleData.aoCapsuleCount = 0;
                if (app->renderSettings.drawCapsules && app->renderSettings.drawAO)
                    CapsuleDataUpdateAOCapsulesForGroundSegment(&app->capsuleData, groundSegmentPosition);
                int aoCapsuleCount = MinInt(app->capsuleData.aoCapsuleCount, AO_CAPSULES_MAX);
                PROFILE_END(RenderingGroundSegmentAO);
                SetShaderValue(app->shader, app->uniforms.aoCapsuleCount, &aoCapsuleCount, SHADER_UNIFORM_INT);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleStarts, app->capsuleData.aoCapsuleStarts, SHADER_UNIFORM_VEC3, aoCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleVectors, app->capsuleData.aoCapsuleVectors, SHADER_UNIFORM_VEC3, aoCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleRadii, app->capsuleData.aoCapsuleRadii, SHADER_UNIFORM_FLOAT, aoCapsuleCount);
                PROFILE_BEGIN(RenderingGroundSegmentShadow);
                app->capsuleData.shadowCapsuleCount = 0;
                if (app->renderSettings.drawCapsules && app->renderSettings.drawShadows)
                    CapsuleDataUpdateShadowCapsulesForGroundSegment(&app->capsuleData, groundSegmentPosition, sunLightDir, app->renderSettings.sunLightConeAngle);
                int shadowCapsuleCount = MinInt(app->capsuleData.shadowCapsuleCount, SHADOW_CAPSULES_MAX);
                PROFILE_END(RenderingGroundSegmentShadow);
                SetShaderValue(app->shader, app->uniforms.shadowCapsuleCount, &shadowCapsuleCount, SHADER_UNIFORM_INT);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleStarts, app->capsuleData.shadowCapsuleStarts, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleVectors, app->capsuleData.shadowCapsuleVectors, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleRadii, app->capsuleData.shadowCapsuleRadii, SHADER_UNIFORM_FLOAT, shadowCapsuleCount);
                DrawModel(app->groundPlaneModel, groundSegmentPosition, 1.0f, WHITE);
                PROFILE_END(RenderingGroundSegment);
            }
        }
    }
    PROFILE_END(RenderingGround);

    // Draw GLB Meshes
    if (app->renderSettings.drawMeshes)
    {
        int meshIsCapsule = 0;
        int meshOccluderCount = 0;
        SetShaderValue(app->shader, app->uniforms.isCapsule, &meshIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.aoCapsuleCount, &meshOccluderCount, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.shadowCapsuleCount, &meshOccluderCount, SHADER_UNIFORM_INT);
        for (int i = 0; i < app->characterData.count; i++)
        {
            if (!app->characterData.visible[i]) continue;
            if (!app->characterData.isGLB[i]) continue;
            GLBData* glb = &app->characterData.glbData[i];
            if (glb->model.meshCount == 0) continue;
            Vector3 meshColor = { app->characterData.colors[i].r / 255.0f, app->characterData.colors[i].g / 255.0f, app->characterData.colors[i].b / 255.0f };
            float meshOpacity = app->characterData.opacities[i];
            Model drawModel = glb->model;
            for (int materialIndex = 0; materialIndex < drawModel.materialCount; materialIndex++)
                drawModel.materials[materialIndex].shader = app->shader;
            drawModel.transform = GLBDataGetModelTransform(glb, app->characterData.scales[i], app->scrubberSettings.inplace);
            SetShaderValue(app->shader, app->uniforms.objectColor, &meshColor, SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.objectOpacity, &meshOpacity, SHADER_UNIFORM_FLOAT);
            int modelHasTexture = 0;
            if (app->renderSettings.drawTexture)
            {
                unsigned int defaultTexId = rlGetTextureIdDefault();
                for (int materialIndex = 0; materialIndex < drawModel.materialCount; materialIndex++)
                {
                    Texture2D tex = drawModel.materials[materialIndex].maps[MATERIAL_MAP_ALBEDO].texture;
                    if (tex.id > 0 && tex.id != defaultTexId && (tex.width > 1 || tex.height > 1)) { modelHasTexture = 1; break; }
                }
            }
            SetShaderValue(app->shader, app->uniforms.useTexture, &modelHasTexture, SHADER_UNIFORM_INT);
            if (meshOpacity < 1.0f) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
            DrawModel(drawModel, Vector3Zero(), 1.0f, WHITE);
            if (meshOpacity < 1.0f) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
        }
    }

    // Draw Capsules
    PROFILE_BEGIN(RenderingCapsules);
    if (app->renderSettings.drawCapsules)
    {
        for (int i = 0; i < app->capsuleData.capsuleCount; i++)
        {
            app->capsuleData.capsuleSort[i].index = i;
            app->capsuleData.capsuleSort[i].value = Vector3Distance(app->camera.cam3d.position, app->capsuleData.capsulePositions[i]);
        }
        qsort(app->capsuleData.capsuleSort, app->capsuleData.capsuleCount, sizeof(CapsuleSort), CapsuleSortCompareLess);
        int capsuleIsCapsule = 1;
        int capsuleUseTexture = 0;
        SetShaderValue(app->shader, app->uniforms.isCapsule, &capsuleIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.useTexture, &capsuleUseTexture, SHADER_UNIFORM_INT);
        for (int i = 0; i < app->capsuleData.capsuleCount; i++)
        {
            int j = app->capsuleData.capsuleSort[i].index;
            Vector3 capsulePosition = app->capsuleData.capsulePositions[j];
            float capsuleHalfLength = app->capsuleData.capsuleHalfLengths[j];
            float capsuleRadius = app->capsuleData.capsuleRadii[j];
            if (!FrustumContainsSphere(frustum, capsulePosition, capsuleHalfLength + capsuleRadius)) continue;
            PROFILE_BEGIN(RenderingCapsulesCapsule);
            if (app->capsuleData.capsuleOpacities[j] < 1.0f) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
            Quaternion capsuleRotation = app->capsuleData.capsuleRotations[j];
            Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
            Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
            SetShaderValue(app->shader, app->uniforms.objectColor, &app->capsuleData.capsuleColors[j], SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.objectOpacity, &app->capsuleData.capsuleOpacities[j], SHADER_UNIFORM_FLOAT);
            SetShaderValue(app->shader, app->uniforms.capsulePosition, &app->capsuleData.capsulePositions[j], SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.capsuleRotation, &app->capsuleData.capsuleRotations[j], SHADER_UNIFORM_VEC4);
            SetShaderValue(app->shader, app->uniforms.capsuleHalfLength, &app->capsuleData.capsuleHalfLengths[j], SHADER_UNIFORM_FLOAT);
            SetShaderValue(app->shader, app->uniforms.capsuleRadius, &app->capsuleData.capsuleRadii[j], SHADER_UNIFORM_FLOAT);
            SetShaderValue(app->shader, app->uniforms.capsuleStart, &capsuleStart, SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.capsuleVector, &capsuleVector, SHADER_UNIFORM_VEC3);
            PROFILE_BEGIN(RenderingCapsulesCapsuleAO);
            app->capsuleData.aoCapsuleCount = 0;
            if (app->renderSettings.drawAO) CapsuleDataUpdateAOCapsulesForCapsule(&app->capsuleData, j);
            int aoCapsuleCount = MinInt(app->capsuleData.aoCapsuleCount, AO_CAPSULES_MAX);
            PROFILE_END(RenderingCapsulesCapsuleAO);
            SetShaderValue(app->shader, app->uniforms.aoCapsuleCount, &aoCapsuleCount, SHADER_UNIFORM_INT);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleStarts, app->capsuleData.aoCapsuleStarts, SHADER_UNIFORM_VEC3, aoCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleVectors, app->capsuleData.aoCapsuleVectors, SHADER_UNIFORM_VEC3, aoCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleRadii, app->capsuleData.aoCapsuleRadii, SHADER_UNIFORM_FLOAT, aoCapsuleCount);
            PROFILE_BEGIN(RenderingCapsulesCapsuleShadow);
            app->capsuleData.shadowCapsuleCount = 0;
            if (app->renderSettings.drawShadows) CapsuleDataUpdateShadowCapsulesForCapsule(&app->capsuleData, j, sunLightDir, app->renderSettings.sunLightConeAngle);
            int shadowCapsuleCount = MinInt(app->capsuleData.shadowCapsuleCount, SHADOW_CAPSULES_MAX);
            PROFILE_END(RenderingCapsulesCapsuleShadow);
            SetShaderValue(app->shader, app->uniforms.shadowCapsuleCount, &shadowCapsuleCount, SHADER_UNIFORM_INT);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleStarts, app->capsuleData.shadowCapsuleStarts, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleVectors, app->capsuleData.shadowCapsuleVectors, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleRadii, app->capsuleData.shadowCapsuleRadii, SHADER_UNIFORM_FLOAT, shadowCapsuleCount);
            DrawModel(app->capsuleModel, Vector3Zero(), 1.0f, WHITE);
            if (app->capsuleData.capsuleOpacities[j] < 1.0f) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
            PROFILE_END(RenderingCapsulesCapsule);
        }
    }
    PROFILE_END(RenderingCapsules);

    if (app->renderSettings.drawGrid) DrawGrid(20, 1.0f);
    if (app->renderSettings.drawOrigin) DrawTransform((Vector3){ 0.0f, 0.01f, 0.0f }, QuaternionIdentity(), 1.0f);

    rlDrawRenderBatchActive();
    rlDisableDepthTest();

    if (app->renderSettings.drawWireframes) DrawWireFrames(&app->capsuleData, DARKGRAY);
    if (app->renderSettings.drawSkeleton)
    {
        for (int i = 0; i < app->characterData.count; i++)
        {
            if (!app->characterData.visible[i]) continue;
            DrawSkeleton(&app->characterData.xformData[i], app->renderSettings.drawEndSites, DARKGRAY, GRAY, (i == app->characterData.active) ? app->camera.selectedBone : -1);
        }
    }
    if (app->renderSettings.drawTransforms)
    {
        for (int i = 0; i < app->characterData.count; i++)
        {
            if (!app->characterData.visible[i]) continue;
            DrawTransforms(&app->characterData.xformData[i]);
        }
    }

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    EndMode3D();
    PROFILE_END(Rendering);

    // Draw UI
    PROFILE_BEGIN(Gui);
    if (app->renderSettings.drawUI)
    {
        if (app->fileDialogState.windowActive) GuiLock();
        DrawText(app->errMsg, 250, 20, 15, RED);
        if (app->characterData.count == 0)
            DrawText("Drag and Drop .bvh / .glb / .gltf files to open them.", app->screenWidth / 2 - 370, app->screenHeight / 2 - 15, 30, DARKGRAY);
        GuiRenderSettings(&app->renderSettings, &app->capsuleData, app->screenWidth, app->screenHeight);
        if (app->renderSettings.drawFPS) DrawFPS(230, 10);
        GuiOrbitCamera(&app->camera, &app->characterData, app->argc, app->argv);
        if (app->camera.showSkeletonPanel) GuiSkeletonPanel(&app->camera, &app->characterData, app->screenWidth, app->screenHeight);
        GuiCharacterData(&app->characterData, &app->fileDialogState, &app->scrubberSettings, app->errMsg, app->argc, app->argv);
        if (app->characterData.colorPickerActive)
        {
            GuiCustomGroupBox((Rectangle){ app->screenWidth - 180, 500, 160, 150 }, "Color Picker");
            GuiColorPicker((Rectangle){ app->screenWidth - 165, 525, 110, 110 }, NULL, &app->characterData.colors[app->characterData.active]);
        }
        GuiScrubberSettings(&app->scrubberSettings, &app->characterData, app->screenWidth, app->screenHeight);
        if (app->fileDialogState.windowActive) GuiUnlock();
        GuiWindowFileDialog(&app->fileDialogState);
    }
    PROFILE_END(Gui);

#if defined(ENABLE_PROFILE) && defined(_WIN32)
    PROFILE_TICKERS_UPDATE();
    for (int i = 0; i < globalProfileRecords.num; i++)
    {
        GuiLabel((Rectangle){ 260, 10 + (float)i * 20, 200, 20 }, globalProfileRecords.records[i]->name);
        GuiLabel((Rectangle){ 450, 10 + (float)i * 20, 100, 20 }, TextFormat("%6.1f us", globalProfileTickers.times[i]));
        GuiLabel((Rectangle){ 550, 10 + (float)i * 20, 100, 20 }, TextFormat("%i calls", globalProfileTickers.samples[i]));
    }
#endif

    EndDrawing();
}

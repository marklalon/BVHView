/*******************************************************************************************
*
*    app.c - ApplicationUpdate implementation
*
*******************************************************************************************/

// Needed for CompareStringEx() and SORT_DIGITSASNUMBERS (Windows 7+) in
// ExplorerStricmp.
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"
#include "app.h"
#include "studio_light.h"
#include "gui.h"
#include "drawing.h"
#include "geometry.h"
#include "math_utils.h"
#include "models.h"
#include "transform_data.h"
#include "profile.h"
#include "argparse.h"
#include "camera.h"

#if defined(_WIN32) && !defined(PLATFORM_WEB)
#define MAILSLOT_NO_MESSAGE ((DWORD)-1)
#endif

// Portable fallback for non-Windows platforms and Windows API/conversion failures.
// Digit runs compare numerically, and underscores sort before other characters to
// preserve the filename ordering used by the Windows comparison below.
static int NaturalStricmp(const char* a, const char* b)
{
    while (*a && *b)
    {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b))
        {
            // Ignore leading zeroes, then compare the significant digit counts.
            while (*a == '0') a++;
            while (*b == '0') b++;

            const char* digitsA = a;
            const char* digitsB = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;

            ptrdiff_t lengthA = a - digitsA;
            ptrdiff_t lengthB = b - digitsB;
            if (lengthA != lengthB) return lengthA < lengthB ? -1 : 1;
            while (digitsA < a)
            {
                if (*digitsA != *digitsB) return *digitsA < *digitsB ? -1 : 1;
                digitsA++;
                digitsB++;
            }
            continue;
        }

        if (*a == '_' && *b != '_') return -1;
        if (*b == '_' && *a != '_') return 1;

        unsigned char charA = (unsigned char)tolower((unsigned char)*a);
        unsigned char charB = (unsigned char)tolower((unsigned char)*b);
        if (charA != charB) return charA < charB ? -1 : 1;
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

#if defined(_WIN32)
// Convert a complete UTF-8 filename to UTF-16. Typical names stay on the stack;
// longer names use an exact-sized allocation instead of being truncated.
static wchar_t* FilenameToWide(const char* text, wchar_t* stackBuffer, int stackCapacity)
{
    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (required <= 0) return NULL;

    wchar_t* output = stackBuffer;
    if (required > stackCapacity)
    {
        output = malloc((size_t)required * sizeof(*output));
        if (output == NULL) return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, output, required) != required)
    {
        if (output != stackBuffer) free(output);
        return NULL;
    }
    return output;
}
#endif

// Case-insensitive natural filename comparison using Windows' locale collation.
// Punctuation remains significant ("Rabbit_Run" sorts before "Rabbit2"), while
// digit runs compare numerically ("file2" sorts before "file10").
static int ExplorerStricmp(const char* a, const char* b)
{
#if defined(_WIN32)
    enum { STACK_CAPACITY = 512 };
    wchar_t stackA[STACK_CAPACITY];
    wchar_t stackB[STACK_CAPACITY];
    wchar_t* wideA = FilenameToWide(a, stackA, STACK_CAPACITY);
    if (wideA == NULL) return NaturalStricmp(a, b);

    wchar_t* wideB = FilenameToWide(b, stackB, STACK_CAPACITY);
    if (wideB == NULL)
    {
        if (wideA != stackA) free(wideA);
        return NaturalStricmp(a, b);
    }

    int result = CompareStringEx(NULL, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
        wideA, -1, wideB, -1, NULL, NULL, 0);

    if (wideA != stackA) free(wideA);
    if (wideB != stackB) free(wideB);

    if (result == CSTR_LESS_THAN) return -1;
    if (result == CSTR_EQUAL) return 0;
    if (result == CSTR_GREATER_THAN) return 1;
#endif
    return NaturalStricmp(a, b);
}

static int CompareFilePathsByFilename(const void* left, const void* right)
{
    const char* pathLeft = *(const char* const*)left;
    const char* pathRight = *(const char* const*)right;
    return ExplorerStricmp(ExtractFilename(pathLeft), ExtractFilename(pathRight));
}

static float SRGBChannelToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
}

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
    camera->cam3d.position = (Vector3){ 2.0f, 2.0f, 5.0f };
    camera->cam3d.target = CAMERA_DEFAULT_TARGET;
    camera->cam3d.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera->cam3d.fovy = ArgFloat(argc, argv, "cameraFOV", 45.0f);
    camera->cam3d.projection = CAMERA_PERSPECTIVE;
    camera->azimuth = ArgFloat(argc, argv, "cameraAzimuth", 0.0f);
    camera->altitude = ArgFloat(argc, argv, "cameraAltitude", 0.4);
    camera->distance = ArgFloat(argc, argv, "cameraDistance", 4.0f);
    camera->offset = ArgVector3(argc, argv, "cameraOffset", Vector3Zero());
    camera->track = ArgBool(argc, argv, "cameraTrack", false);
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
    double buildStartTime = GetTime();
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
    int directoryEntryCount = (int)files.count;
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
    double scanFinishedTime = GetTime();

    if (matchedCount <= 1)
    {
        // Not enough files to switch; free and clear
        for (int i = 0; i < matchedCount; i++) free(matched[i]);
        app->lastScannedDir[0] = '\0';
        return;
    }

    // Sort by filename using the platform's default locale collation (matches
    // Windows Explorer). qsort keeps this O(n log n) for large directories.
    qsort(matched, (size_t)matchedCount, sizeof(matched[0]), CompareFilePathsByFilename);
    double sortFinishedTime = GetTime();

    // Transfer to app->fileList and find current index
    for (int i = 0; i < matchedCount; i++)
    {
        app->fileList[i] = matched[i];
        if (strcmp(app->fileList[i], currentFilePath) == 0)
            app->fileListIndex = i;
    }
    app->fileListCount = matchedCount;

    // Build prefix-based groups from sorted file list.
    // Files are already sorted by filename, so same-prefix files are adjacent.
    // Prefix = the filename (without extension) with its trailing action/variant
    // segments removed: truncate at the second-to-last '_' when there are at
    // least two underscores, otherwise at the only '_' (for example,
    // "Walk_01" -> "Walk", "Alligator_DeadUp_1" -> "Alligator",
    // "FEP_MagmaDemon_Attack01_1" -> "FEP_MagmaDemon").
    app->groupCount = 0;
    app->currentGroupIndex = 0;

    for (int i = 0; i < matchedCount; i++)
    {
        const char* fileName = ExtractFilename(app->fileList[i]);
        char nameNoExt[256];
        snprintf(nameNoExt, sizeof(nameNoExt), "%s", fileName);
        char* dot = strrchr(nameNoExt, '.');
        if (dot) *dot = '\0';

        char* lastUnderscore = strrchr(nameNoExt, '_');
        if (lastUnderscore)
        {
            char* secondToLast = NULL;
            for (char* p = lastUnderscore - 1; p > nameNoExt; p--)
            {
                if (*p == '_') { secondToLast = p; break; }
            }
            if (secondToLast)
                *secondToLast = '\0';
            else
                *lastUnderscore = '\0';
        }

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

    double buildFinishedTime = GetTime();
    printf("INFO: Indexed %d/%d animation files into %d groups "
           "(scan %.3f s, sort %.3f s, group %.3f s, total %.3f s)\n",
           matchedCount, directoryEntryCount, app->groupCount,
           scanFinishedTime - buildStartTime,
           sortFinishedTime - scanFinishedTime,
           buildFinishedTime - sortFinishedTime,
           buildFinishedTime - buildStartTime);
    fflush(stdout);
}

// Common post-load initialization. Directory indexing is intentionally deferred
// until the user first requests file navigation.
void OnFileLoaded(ApplicationState* app)
{
    app->characterData.active = app->characterData.count - 1;

    // Keep an existing directory index only when it contains the loaded file.
    // This preserves the cache during keyboard navigation while preventing a
    // newly opened file from inheriting stale index and title information.
    bool cachedFileFound = false;
    for (int i = 0; i < app->fileListCount; i++)
    {
        if (strcmp(app->fileList[i], app->characterData.filePaths[app->characterData.active]) == 0)
        {
            app->fileListIndex = i;
            cachedFileFound = true;
            break;
        }
    }
    if (app->fileListCount > 0 && !cachedFileFound)
    {
        FreeFileList(app);
        app->fileListIndex = 0;
        app->lastScannedDir[0] = '\0';
    }

    if (!app->firstFileLoaded)
    {
        if (app->characterData.hasSkinnedMesh)
        {
            app->renderSettings.drawMeshes = true;
            app->renderSettings.drawCapsules = false;
        }
        app->firstFileLoaded = true;
    }
    CapsuleDataUpdateForCharacters(&app->capsuleData, &app->characterData);
    ScrubberSettingsRecomputeLimits(&app->scrubberSettings, &app->characterData);
    ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);

    // Auto-play animation unless user explicitly paused
    if (!app->scrubberSettings.userPaused && ScrubberHasValidAnimation(&app->characterData, app->characterData.active))
        app->scrubberSettings.playing = true;

    char windowTitle[600];
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

#if defined(_WIN32) && !defined(PLATFORM_WEB)
static void ProcessReuseMailslot(ApplicationState* app)
{
    HANDLE mailslot = (HANDLE)app->reuseMailslot;
    if (mailslot == INVALID_HANDLE_VALUE) return;

    DWORD nextSize = 0;
    DWORD messageCount = 0;
    while (GetMailslotInfo(mailslot, NULL, &nextSize, &messageCount, NULL) &&
           messageCount > 0 && nextSize != MAILSLOT_NO_MESSAGE)
    {
        char filePath[BVHVIEW_PATH_BUFFER_SIZE];
        DWORD bytesRead = 0;
        if (!ReadFile(mailslot, filePath, sizeof(filePath) - 1, &bytesRead, NULL) || bytesRead == 0)
            break;

        filePath[bytesRead < sizeof(filePath) ? bytesRead : sizeof(filePath) - 1] = '\0';
        if (CharacterDataLoadFromFile(&app->characterData, filePath, app->errMsg, 512))
        {
            app->characterData.active = app->characterData.count - 1;
            OnFileLoaded(app);
        }
    }
}
#endif

void ApplicationUpdate(void* voidApplicationState)
{
    ApplicationState* app = voidApplicationState;

#if defined(_WIN32) && !defined(PLATFORM_WEB)
    ProcessReuseMailslot(app);
#endif

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
        else
        {
            char fullPath[2048];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", app->fileDialogState.dirPathText, app->fileDialogState.fileNameText);
            snprintf(app->errMsg, 512, "Error: File '%.*s' is not supported (.bvh/.glb/.gltf).", 400, fullPath);
        }
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
        app->scrubberSettings.userPaused = app->scrubberSettings.playing ? false : true;
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
        ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);
        char windowTitle[528];
        snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app->characterData.filePaths[app->characterData.active]);
        SetWindowTitle(windowTitle);
    }

    // ArrowLeft/ArrowRight: replace the active character with the previous/next
    // file in *its own* directory, leaving other characters untouched.
    if (!app->fileDialogState.windowActive && app->characterData.count > 0)
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
                int activeSlot = app->characterData.active;

                // Refresh the file list to the active character's directory (no-op if unchanged).
                BuildFileList(app, app->characterData.filePaths[activeSlot]);

                if (app->fileListCount > 1)
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
                        if (CharacterDataReplaceAt(&app->characterData, activeSlot, app->fileList[candidate], app->errMsg, 512))
                        {
                            app->fileListIndex = candidate;
                            loaded = true;
                            break;
                        }
                    }

                    if (loaded)
                    {
                        // Post-replace refresh (mirrors OnFileLoaded, but keeps `active` on the replaced slot).
                        if (!app->firstFileLoaded)
                        {
                            if (app->characterData.hasSkinnedMesh)
                            {
                                app->renderSettings.drawMeshes = true;
                                app->renderSettings.drawCapsules = false;
                            }
                            app->firstFileLoaded = true;
                        }
                        CapsuleDataUpdateForCharacters(&app->capsuleData, &app->characterData);
                        ScrubberSettingsRecomputeLimits(&app->scrubberSettings, &app->characterData);
                        ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);

                        // Auto-play animation unless user explicitly paused
                        if (!app->scrubberSettings.userPaused && ScrubberHasValidAnimation(&app->characterData, app->characterData.active))
                            app->scrubberSettings.playing = true;

                        char windowTitle[600];
                        snprintf(windowTitle, sizeof(windowTitle), "%s (%d/%d) - BVHView",
                                 app->characterData.filePaths[activeSlot],
                                 app->fileListIndex + 1,
                                 app->fileListCount);
                        SetWindowTitle(windowTitle);
                    }
                    else
                    {
                        // No candidate could be loaded — nothing changed, drop the camera restore.
                        app->restoreCameraAfterSwitch = false;
                    }
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

    // PageUp/PageDown: lazily index the directory, then switch to the
    // previous/next group (jump to its first file, repeat when held).
    if (!app->fileDialogState.windowActive && app->characterData.count > 0)
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
                int activeSlot = app->characterData.active;
                BuildFileList(app, app->characterData.filePaths[activeSlot]);

                if (app->groupCount > 1)
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
        }
        else
        {
            KeyRepeatReset(&repeatState);
        }
    }

    PROFILE_BEGIN(Update);

    // Bind/rest pose only exists for GLB; force it off whenever the active file is BVH
    bool bindPoseAvailable = app->characterData.count > 0 && app->characterData.isGLB[app->characterData.active];
    if (!bindPoseAvailable)
        app->renderSettings.drawBindPose = false;

    // Track bind pose toggle transitions to auto-resume playback
    static bool prevBindPose = false;
    bool bindPoseJustDisabled = prevBindPose && !app->renderSettings.drawBindPose;
    prevBindPose = app->renderSettings.drawBindPose;

    // Bind/Rest pose preview pauses the animation (passive — keep userPaused)
    if (app->renderSettings.drawBindPose)
        app->scrubberSettings.playing = false;
    else if (bindPoseJustDisabled && !app->scrubberSettings.userPaused && ScrubberHasValidAnimation(&app->characterData, app->characterData.active))
        app->scrubberSettings.playing = true;

    // No skeleton or animation too short → stop playback
    if (!ScrubberHasValidAnimation(&app->characterData, app->characterData.active))
        app->scrubberSettings.playing = false;

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
        if (app->renderSettings.drawBindPose && app->characterData.isGLB[i])
        {
            TransformDataSampleRestPoseGLB(&app->characterData.xformData[i], &app->characterData.glbData[i], app->characterData.scales[i]);
        }
        else if (app->characterData.isGLB[i])
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
    Vector3 cameraTarget = CAMERA_DEFAULT_TARGET;
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
    // End sites are ignored for capsules (they produce degenerate zero-length capsules)
    // and for skeleton lines (they are represented by the capsule geometry instead).
    for (int i = 0; i < app->characterData.count; i++)
    {
        if (!app->characterData.visible[i]) continue;
        CapsuleDataAppendFromTransformData(&app->capsuleData, &app->characterData.xformData[i], app->characterData.radii[i], app->characterData.colors[i], app->characterData.opacities[i], true);
    }

    PROFILE_END(Update);

    // Rendering
    Frustum frustum = FrustumFromCameraMatrices(GetCameraProjectionMatrix(&app->camera.cam3d, app->screenHeight / app->screenWidth), GetCameraViewMatrix(&app->camera.cam3d));
    BeginDrawing();
    PROFILE_BEGIN(Rendering);
    ClearBackground(app->renderSettings.backgroundColor);
    BeginMode3D(app->camera.cam3d);
    rlDisableColorBlend();  // Disable alpha blending for screen-door transparency

    // Set shader uniforms
    float objectSpecularity = 0.5f;
    float objectGlossiness = 10.0f;
    float objectOpacity = 1.0f;
    Vector3 sunLightPosition = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, 1.0f }, QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, app->renderSettings.lightAzimuth));
    Vector3 sunLightAxis = Vector3Normalize(Vector3CrossProduct(sunLightPosition, (Vector3){ 0.0f, 1.0f, 0.0f }));
    Vector3 lightDir = Vector3Negate(Vector3RotateByQuaternion(sunLightPosition, QuaternionFromAxisAngle(sunLightAxis, app->renderSettings.lightAltitude)));
    SetShaderValue(app->shader, app->uniforms.cameraPosition, &app->camera.cam3d.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.exposure, &app->renderSettings.exposure, SHADER_UNIFORM_FLOAT);
    int enableLighting = app->renderSettings.enableLighting;
    SetShaderValue(app->shader, app->uniforms.enableLighting, &enableLighting, SHADER_UNIFORM_INT);
    // Legacy lighting uniforms (sunStrength and ambientStrength are now configurable via UI)
    SetShaderValue(app->shader, app->uniforms.sunDir, &lightDir, SHADER_UNIFORM_VEC3);
    float sunStrengthVal = app->renderSettings.sunStrength;
    float skyStrengthVal = 0.15f;
    float ambientStrengthVal = app->renderSettings.ambientStrength;
    float groundStrengthVal = 0.1f;
    Vector3 sunColorVal = { 255.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f };
    Vector3 skyColorVal = { 174.0f/255.0f, 183.0f/255.0f, 190.0f/255.0f };
    SetShaderValue(app->shader, app->uniforms.sunStrength, &sunStrengthVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.sunColor, &sunColorVal, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.skyStrength, &skyStrengthVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.skyColor, &skyColorVal, SHADER_UNIFORM_VEC3);
    SetShaderValue(app->shader, app->uniforms.ambientStrength, &ambientStrengthVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.groundStrength, &groundStrengthVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectSpecularity, &objectSpecularity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectGlossiness, &objectGlossiness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(app->shader, app->uniforms.objectOpacity, &objectOpacity, SHADER_UNIFORM_FLOAT);
    int usePBR = 0;
    SetShaderValue(app->shader, app->uniforms.usePBR, &usePBR, SHADER_UNIFORM_INT);
    SetShaderValue(app->shader, app->uniforms.aoLookupResolution, &app->capsuleData.aoLookupResolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(app->shader, app->uniforms.shadowLookupResolution, &app->capsuleData.shadowLookupResolution, SHADER_UNIFORM_VEC2);
    // Core PBR maps occupy slots 0..5. Keep lookup and environment textures on
    // fixed slots so DrawMesh() cannot overwrite or unbind them.
    int aoLookupSlot = 6;
    int shadowLookupSlot = 7;
    int environmentSlot = 8;
    rlActiveTextureSlot(aoLookupSlot);
    rlEnableTexture(app->capsuleData.aoLookupTable.id);
    rlActiveTextureSlot(shadowLookupSlot);
    rlEnableTexture(app->capsuleData.shadowLookupTable.id);
    if (app->studioLight.id != 0)
    {
        rlActiveTextureSlot(environmentSlot);
        rlEnableTextureCubemap(app->studioLight.id);
    }
    rlActiveTextureSlot(0);
    SetShaderValue(app->shader, app->uniforms.aoLookupTable, &aoLookupSlot, SHADER_UNIFORM_INT);
    SetShaderValue(app->shader, app->uniforms.shadowLookupTable, &shadowLookupSlot, SHADER_UNIFORM_INT);
    SetShaderValue(app->shader, app->uniforms.environmentMap, &environmentSlot, SHADER_UNIFORM_INT);
    float environmentMaxLod = app->studioLight.mipmaps > 0 ? (float)(app->studioLight.mipmaps - 1) : 0.0f;
    SetShaderValue(app->shader, app->uniforms.environmentMaxLod, &environmentMaxLod, SHADER_UNIFORM_FLOAT);

    // Draw Ground — diffuse checker with AO/shadow occlusion, no IBL tint
    PROFILE_BEGIN(RenderingGround);
    if (app->renderSettings.drawChecker)
    {
        int groundIsCapsule = 0;
        int groundUseTexture = 0;
        int groundUsePBR = 0;
        Vector3 groundColor = { 0.65f, 0.65f, 0.65f };
        SetShaderValue(app->shader, app->uniforms.isCapsule, &groundIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.useTexture, &groundUseTexture, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.usePBR, &groundUsePBR, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.objectColor, &groundColor, SHADER_UNIFORM_VEC3);
        int groundEnableLighting = 1;
        float groundExposure = 0.9f;
        float groundSunStrength = 0.25f;
        float groundAmbientStrength = 1.0f;
        SetShaderValue(app->shader, app->uniforms.enableLighting, &groundEnableLighting, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.exposure, &groundExposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app->shader, app->uniforms.sunStrength, &groundSunStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app->shader, app->uniforms.ambientStrength, &groundAmbientStrength, SHADER_UNIFORM_FLOAT);
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
                if (app->renderSettings.drawCapsules)
                    CapsuleDataUpdateShadowCapsulesForGroundSegment(&app->capsuleData, groundSegmentPosition, lightDir, app->renderSettings.lightConeAngle);
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
        SetShaderValue(app->shader, app->uniforms.enableLighting, &enableLighting, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.usePBR, &usePBR, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.sunStrength, &sunStrengthVal, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app->shader, app->uniforms.ambientStrength, &ambientStrengthVal, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app->shader, app->uniforms.exposure, &app->renderSettings.exposure, SHADER_UNIFORM_FLOAT);
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
            Matrix modelTransform = GLBDataGetModelTransform(glb, app->characterData.scales[i], app->scrubberSettings.inplace);
            unsigned int defaultTexId = rlGetTextureIdDefault();

            // Base defaults (per-material values override below)
            SetShaderValue(app->shader, app->uniforms.objectColor, &meshColor, SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.objectOpacity, &meshOpacity, SHADER_UNIFORM_FLOAT);
            int defaultAlphaMode = 0;
            float defaultAlphaCutoff = 0.5f;
            SetShaderValue(app->shader, app->uniforms.alphaMode, &defaultAlphaMode, SHADER_UNIFORM_INT);
            SetShaderValue(app->shader, app->uniforms.alphaCutoff, &defaultAlphaCutoff, SHADER_UNIFORM_FLOAT);
            int noTexture = 0;
            SetShaderValue(app->shader, app->uniforms.useTexture, &noTexture, SHADER_UNIFORM_INT);

            // Draw each mesh individually with per-material alpha
            for (int meshIdx = 0; meshIdx < glb->model.meshCount; meshIdx++)
            {
                Mesh mesh = glb->model.meshes[meshIdx];
                int matIdx = glb->model.meshMaterial[meshIdx];
                if (matIdx < 0 || matIdx >= glb->model.materialCount) continue;
                Material material = glb->model.materials[matIdx];
                material.shader = app->shader;

                // Per-material glTF PBR factors and alpha settings.
                int alphaMode = 0;
                float alphaCutoff = 0.5f;
                GLBMaterialInfo* info = NULL;
                if (glb->materialInfo != NULL && matIdx < glb->materialInfoCount)
                {
                    info = &glb->materialInfo[matIdx];
                    alphaMode = info->alphaMode;
                    alphaCutoff = info->alphaCutoff;
                }
                SetShaderValue(app->shader, app->uniforms.alphaMode, &alphaMode, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.alphaCutoff, &alphaCutoff, SHADER_UNIFORM_FLOAT);

                // Per-material texture checks. PBR maps are bound by DrawMesh().
                int hasTexture = 0;
                int hasMetalness = 0;
                int hasRoughness = 0;
                int hasNormal = 0;
                int hasOcclusion = 0;
                int hasEmission = 0;
                if (app->renderSettings.drawTexture)
                {
                    Texture2D tex = material.maps[MATERIAL_MAP_ALBEDO].texture;
                    if (tex.id > 0 && tex.id != defaultTexId)
                        hasTexture = 1;
                    tex = material.maps[MATERIAL_MAP_METALNESS].texture;
                    hasMetalness = tex.id > 0 && tex.id != defaultTexId;
                    tex = material.maps[MATERIAL_MAP_ROUGHNESS].texture;
                    hasRoughness = tex.id > 0 && tex.id != defaultTexId;
                    tex = material.maps[MATERIAL_MAP_NORMAL].texture;
                    hasNormal = tex.id > 0 && tex.id != defaultTexId;
                    tex = material.maps[MATERIAL_MAP_OCCLUSION].texture;
                    hasOcclusion = tex.id > 0 && tex.id != defaultTexId;
                    tex = material.maps[MATERIAL_MAP_EMISSION].texture;
                    hasEmission = tex.id > 0 && tex.id != defaultTexId;
                }
                SetShaderValue(app->shader, app->uniforms.useTexture, &hasTexture, SHADER_UNIFORM_INT);

                int materialUsePBR = (app->renderSettings.drawPBR && info != NULL && info->hasPBR) ? 1 : 0;
                float metallic = materialUsePBR ? info->metallicFactor : 0.0f;
                float roughness = materialUsePBR ? info->roughnessFactor : 1.0f;
                float materialNormalScale = materialUsePBR ? info->normalScale : 1.0f;
                float materialOcclusionStrength = materialUsePBR ? info->occlusionStrength : 1.0f;
                Vector3 materialEmission = materialUsePBR ? info->emissionFactor : (Vector3){ 0.0f, 0.0f, 0.0f };
                Vector3 surfaceColor = meshColor;
                float surfaceOpacity = meshOpacity;
                int baseColorUV = 0, metallicRoughnessUV = 0, normalUV = 0, occlusionUV = 0, emissionUV = 0;
                if (materialUsePBR)
                {
                    surfaceColor = (Vector3){ info->baseColorFactor.x, info->baseColorFactor.y, info->baseColorFactor.z };
                    if (!hasTexture)
                    {
                        surfaceColor.x *= SRGBChannelToLinear(meshColor.x);
                        surfaceColor.y *= SRGBChannelToLinear(meshColor.y);
                        surfaceColor.z *= SRGBChannelToLinear(meshColor.z);
                    }
                    if (alphaMode != 0) surfaceOpacity *= info->baseColorFactor.w;
                    baseColorUV = info->baseColorUV == 1;
                    metallicRoughnessUV = info->metallicRoughnessUV == 1;
                    normalUV = info->normalUV == 1;
                    occlusionUV = info->occlusionUV == 1;
                    emissionUV = info->emissionUV == 1;
                }
                SetShaderValue(app->shader, app->uniforms.usePBR, &materialUsePBR, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.useMetalnessTexture, &hasMetalness, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.useRoughnessTexture, &hasRoughness, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.useNormalTexture, &hasNormal, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.useOcclusionTexture, &hasOcclusion, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.useEmissionTexture, &hasEmission, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.metallicFactor, &metallic, SHADER_UNIFORM_FLOAT);
                SetShaderValue(app->shader, app->uniforms.roughnessFactor, &roughness, SHADER_UNIFORM_FLOAT);
                SetShaderValue(app->shader, app->uniforms.normalScale, &materialNormalScale, SHADER_UNIFORM_FLOAT);
                SetShaderValue(app->shader, app->uniforms.occlusionStrength, &materialOcclusionStrength, SHADER_UNIFORM_FLOAT);
                SetShaderValue(app->shader, app->uniforms.emissionFactor, &materialEmission, SHADER_UNIFORM_VEC3);
                SetShaderValue(app->shader, app->uniforms.baseColorUV, &baseColorUV, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.metallicRoughnessUV, &metallicRoughnessUV, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.normalUV, &normalUV, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.occlusionUV, &occlusionUV, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.emissionUV, &emissionUV, SHADER_UNIFORM_INT);
                SetShaderValue(app->shader, app->uniforms.objectColor, &surfaceColor, SHADER_UNIFORM_VEC3);
                SetShaderValue(app->shader, app->uniforms.objectOpacity, &surfaceOpacity, SHADER_UNIFORM_FLOAT);

                // Promote opaque materials with user-lowered opacity to screen-door
                if (surfaceOpacity < 1.0f && alphaMode == 0)
                {
                    alphaMode = 2;
                    SetShaderValue(app->shader, app->uniforms.alphaMode, &alphaMode, SHADER_UNIFORM_INT);
                }

                // Screen-door (alphaMode 2) uses discard; no depth-mask hack needed
                bool needsDepthMaskHack = (surfaceOpacity < 1.0f && alphaMode != 2);
                bool disableCulling = info != NULL && info->doubleSided;
                if (disableCulling) { rlDrawRenderBatchActive(); rlDisableBackfaceCulling(); }
                if (needsDepthMaskHack) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
                DrawMesh(mesh, material, modelTransform);

                // Wireframe overlay
                if (app->renderSettings.drawWireframes)
                {
                    rlDrawRenderBatchActive();
                    rlEnableWireMode();
                    int wireUsePBR = 0;
                    SetShaderValue(app->shader, app->uniforms.usePBR, &wireUsePBR, SHADER_UNIFORM_INT);
                    SetShaderValue(app->shader, app->uniforms.useTexture, &noTexture, SHADER_UNIFORM_INT);
                    int opaqueAlpha = 0;
                    SetShaderValue(app->shader, app->uniforms.alphaMode, &opaqueAlpha, SHADER_UNIFORM_INT);
                    Vector3 wireColor = { meshColor.x * 0.25f, meshColor.y * 0.25f, meshColor.z * 0.25f };
                    SetShaderValue(app->shader, app->uniforms.objectColor, &wireColor, SHADER_UNIFORM_VEC3);
                    DrawMesh(mesh, material, modelTransform);
                    rlDrawRenderBatchActive();
                    rlDisableWireMode();
                    // Restore per-mesh state (objectColor is per-character but kept in sync)
                    SetShaderValue(app->shader, app->uniforms.objectColor, &surfaceColor, SHADER_UNIFORM_VEC3);
                    SetShaderValue(app->shader, app->uniforms.useTexture, &hasTexture, SHADER_UNIFORM_INT);
                    SetShaderValue(app->shader, app->uniforms.alphaMode, &alphaMode, SHADER_UNIFORM_INT);
                    SetShaderValue(app->shader, app->uniforms.usePBR, &materialUsePBR, SHADER_UNIFORM_INT);
                }

                if (needsDepthMaskHack) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
                if (disableCulling) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
            }
        }
    }

    // Draw Capsules
    PROFILE_BEGIN(RenderingCapsules);
    if (app->renderSettings.drawCapsules)
    {
        int capsuleUsePBR = 0;
        SetShaderValue(app->shader, app->uniforms.usePBR, &capsuleUsePBR, SHADER_UNIFORM_INT);
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
            CapsuleDataUpdateShadowCapsulesForCapsule(&app->capsuleData, j, lightDir, app->renderSettings.lightConeAngle);
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

    rlActiveTextureSlot(aoLookupSlot);
    rlDisableTexture();
    rlActiveTextureSlot(shadowLookupSlot);
    rlDisableTexture();
    if (app->studioLight.id != 0)
    {
        rlActiveTextureSlot(environmentSlot);
        rlDisableTextureCubemap();
    }
    rlActiveTextureSlot(0);

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
            // End sites skipped here — represented by capsule geometry instead
            DrawSkeleton(&app->characterData.xformData[i], false, DARKGRAY, GRAY, (i == app->characterData.active) ? app->camera.selectedBone : -1);
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
    rlEnableColorBlend();  // Re-enable alpha blending for 2D GUI
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
        GuiRenderSettings(&app->renderSettings, &app->capsuleData, bindPoseAvailable, app->screenWidth, app->screenHeight);
        if (app->renderSettings.drawFPS) DrawFPS(230, 10);
        GuiOrbitCamera(&app->camera, &app->characterData, &app->renderSettings, &app->capsuleData, app->argc, app->argv);
        if (app->camera.showSkeletonPanel) GuiSkeletonPanel(&app->camera, &app->characterData, app->screenWidth, app->screenHeight);
        GuiCharacterData(&app->characterData, &app->fileDialogState, &app->scrubberSettings, app->errMsg, app->argc, app->argv);
        if (app->characterData.colorPickerActive)
        {
            GuiCustomGroupBox((Rectangle){ app->screenWidth - 180, 500, 160, 150 }, "Color Picker");
            GuiColorPicker((Rectangle){ app->screenWidth - 165, 525, 110, 110 }, NULL, &app->characterData.colors[app->characterData.active]);
        }
        // Hide the animation playback panel when there's no meaningful animation
        if (!app->renderSettings.drawBindPose && ScrubberHasValidAnimation(&app->characterData, app->characterData.active))
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

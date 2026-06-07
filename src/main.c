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
HRESULT WINAPI URLDownloadToFileA(void* caller, const char* url, const char* fileName, DWORD reserved, void* callback);
#endif

#if defined(_WIN32) && !defined(PLATFORM_WEB)
// Definitions and declarations excluded by WIN32_LEAN_AND_MEAN
#define SW_RESTORE 9

// Function declarations (excluded by WIN32_LEAN_AND_MEAN)
BOOL WINAPI IsIconic(HWND hWnd);
BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow);
BOOL WINAPI SetForegroundWindow(HWND hWnd);
BOOL WINAPI EnumWindows(BOOL (CALLBACK *lpEnumFunc)(HWND, LPARAM), LPARAM lParam);
int WINAPI GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);

// Data passed to EnumWindows callback to find a BVHView window
typedef struct {
    HWND foundHwnd;
} FindBvhViewData;

static BOOL CALLBACK FindBvhViewCallback(HWND hwnd, LPARAM lParam)
{
    FindBvhViewData* data = (FindBvhViewData*)lParam;
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0)
    {
        // Window title ends with " - BVHView" or is exactly "BVHView"
        size_t len = strlen(title);
        if ((len >= 10 && strcmp(title + len - 10, " - BVHView") == 0) ||
            strcmp(title, "BVHView") == 0)
        {
            data->foundHwnd = hwnd;
            return FALSE; // stop enumeration
        }
    }
    return TRUE; // continue enumeration
}

// Send a file path to an existing BVHView instance via mailslot IPC.
// Returns true if a running instance was found and the message was sent.
static bool SendFileToExistingInstance(const char* filePath)
{
    HANDLE mailslot = CreateFileA(BVHVIEW_REUSE_MAILSLOT, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mailslot == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    DWORD bytesToWrite = (DWORD)(strlen(filePath) + 1);
    bool sent = WriteFile(mailslot, filePath, bytesToWrite, &bytesWritten, NULL) && bytesWritten == bytesToWrite;
    CloseHandle(mailslot);
    if (!sent) return false;

    FindBvhViewData data = { NULL };
    EnumWindows(FindBvhViewCallback, (LPARAM)&data);
    HWND hwnd = data.foundHwnd;
    if (!hwnd) return true;

    // Bring the window to front and restore if minimized
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    return true;
}

static bool IsBvhViewProtocolArg(const char* arg)
{
    return _strnicmp(arg, "bvhview://", 10) == 0;
}

static bool IsQueryFlag(const char* start, size_t len, const char* name)
{
    size_t nameLen = strlen(name);
    if (len < nameLen) return false;
    if (_strnicmp(start, name, nameLen) != 0) return false;
    return len == nameLen || start[nameLen] == '=';
}

static bool ProtocolArgHasReuseFlag(const char* protocolUrl)
{
    const char* query = strchr(protocolUrl, '?');
    if (!query) return false;

    const char* param = query + 1;
    while (*param != '\0')
    {
        const char* end = strchr(param, '&');
        size_t len = end ? (size_t)(end - param) : strlen(param);
        if (IsQueryFlag(param, len, "--reuse") || IsQueryFlag(param, len, "reuse"))
            return true;

        if (!end) break;
        param = end + 1;
    }

    return false;
}

static bool ReuseRequested(int argc, char** argv)
{
    if (ArgBool(argc, argv, "reuse", false)) return true;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--reuse") == 0) return true;
        if (IsBvhViewProtocolArg(argv[i]) && ProtocolArgHasReuseFlag(argv[i])) return true;
    }

    return false;
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void UrlDecode(const char* src, char* dst, size_t dstSize)
{
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; i++)
    {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0')
        {
            int hi = HexValue(src[i + 1]);
            int lo = HexValue(src[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        dst[out++] = (src[i] == '+') ? ' ' : src[i];
    }
    dst[out] = '\0';
}

static bool ExtractProtocolSourceUrl(const char* protocolUrl, char* sourceUrl, size_t sourceUrlSize)
{
    const char* query = strchr(protocolUrl, '?');
    if (!query) return false;

    const char* urlParam = strstr(query + 1, "url=");
    if (!urlParam) return false;
    urlParam += 4;

    char encodedUrl[4096];
    size_t encodedLen = 0;
    while (urlParam[encodedLen] != '\0' && urlParam[encodedLen] != '&' && encodedLen + 1 < sizeof(encodedUrl))
    {
        encodedUrl[encodedLen] = urlParam[encodedLen];
        encodedLen++;
    }
    encodedUrl[encodedLen] = '\0';

    UrlDecode(encodedUrl, sourceUrl, sourceUrlSize);
    return sourceUrl[0] != '\0';
}

static void FileNameFromUrl(const char* sourceUrl, char* fileName, size_t fileNameSize)
{
    const char* end = sourceUrl + strlen(sourceUrl);
    const char* query = strpbrk(sourceUrl, "?#");
    if (query) end = query;

    const char* start = end;
    while (start > sourceUrl && *(start - 1) != '/' && *(start - 1) != '\\') start--;

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= fileNameSize) len = snprintf(fileName, fileNameSize, "motion.bvh");
    else
    {
        memcpy(fileName, start, len);
        fileName[len] = '\0';
    }

    for (size_t i = 0; fileName[i] != '\0'; i++)
    {
        if (strchr("\\/:*?\"<>|", fileName[i])) fileName[i] = '_';
    }

    if (strlen(fileName) < 4 || _stricmp(fileName + strlen(fileName) - 4, ".bvh") != 0)
    {
        size_t used = strlen(fileName);
        if (used + 4 < fileNameSize) strcat(fileName, ".bvh");
    }
}

static bool FileUrlToPath(const char* fileUrl, char* localPath, size_t localPathSize)
{
    // Handle file:///C:/... or file://C:/...
    const char* hostPart = fileUrl + 7; // skip "file://"
    if (hostPart[0] == '/')
    {
        hostPart++; // skip extra slash: file:///C:/...
        if (hostPart[0] == '/' && hostPart[1] != '\0')
            hostPart++; // handle file:////C:/... edge case
    }

    size_t len = strlen(hostPart);
    if (len >= localPathSize) return false;
    memcpy(localPath, hostPart, len + 1);

    // Convert forward slashes to backslashes on Windows
    for (size_t i = 0; localPath[i] != '\0'; i++)
        if (localPath[i] == '/') localPath[i] = '\\';

    return (GetFileAttributesA(localPath) != INVALID_FILE_ATTRIBUTES);
}

static bool DownloadProtocolArg(const char* protocolArg, char* outputPath, size_t outputPathSize, char* errMsg, size_t errMsgSize)
{
    char sourceUrl[4096];
    if (!ExtractProtocolSourceUrl(protocolArg, sourceUrl, sizeof(sourceUrl)))
    {
        snprintf(errMsg, errMsgSize, "Could not parse BVHView link: %s", protocolArg);
        return false;
    }

    // file:// protocol: open local file directly
    if (_strnicmp(sourceUrl, "file://", 7) == 0)
    {
        char localPath[BVHVIEW_PATH_BUFFER_SIZE];
        if (!FileUrlToPath(sourceUrl, localPath, sizeof(localPath)))
        {
            snprintf(errMsg, errMsgSize, "file not found: %s", sourceUrl);
            return false;
        }
        snprintf(outputPath, outputPathSize, "%s", localPath);
        return true;
    }

    if (_strnicmp(sourceUrl, "http://", 7) != 0 && _strnicmp(sourceUrl, "https://", 8) != 0)
    {
        snprintf(errMsg, errMsgSize, "Only http and https BVH links are supported.");
        return false;
    }

    char tempPath[MAX_PATH];
    DWORD tempPathLen = GetTempPathA((DWORD)sizeof(tempPath), tempPath);
    if (tempPathLen == 0 || tempPathLen >= sizeof(tempPath))
    {
        snprintf(errMsg, errMsgSize, "Could not get temporary directory for '%s'.", sourceUrl);
        return false;
    }

    char tempDir[MAX_PATH + 16];
    snprintf(tempDir, sizeof(tempDir), "%sBVHView", tempPath);
    CreateDirectoryA(tempDir, NULL);

    char fileName[MAX_PATH];
    FileNameFromUrl(sourceUrl, fileName, sizeof(fileName));
    snprintf(outputPath, outputPathSize, "%s\\%llu-%s", tempDir, (unsigned long long)GetTickCount64(), fileName);

    HRESULT hr = URLDownloadToFileA(NULL, sourceUrl, outputPath, 0, NULL);
    if (FAILED(hr))
    {
        snprintf(errMsg, errMsgSize, "Could not download BVH link: %s", sourceUrl);
        return false;
    }

    return true;
}
#endif

int main(int argc, char** argv)
{
    PROFILE_INIT();
    PROFILE_TICKERS_INIT();

#if defined(_WIN32) && !defined(PLATFORM_WEB)
    // --reuse: if true, try to send the file to an existing BVHView instance.
    // Accept --reuse, --reuse=true, and bvhview://open?--reuse&url=...
    bool reuse = ReuseRequested(argc, argv);
    if (reuse)
    {
        // Find the first non-option argument (file path or protocol URL)
        for (int i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-') continue;

            const char* loadPath = argv[i];
            char downloadedPath[BVHVIEW_PATH_BUFFER_SIZE];
            char errMsg[512];

            if (IsBvhViewProtocolArg(argv[i]))
            {
                if (!DownloadProtocolArg(argv[i], downloadedPath, sizeof(downloadedPath), errMsg, sizeof(errMsg)))
                {
                    printf("ERROR: %s\n", errMsg);
                    return 1;
                }
                loadPath = downloadedPath;
            }
            else
            {
                char fullPath[BVHVIEW_PATH_BUFFER_SIZE];
                DWORD fullPathLen = GetFullPathNameA(loadPath, sizeof(fullPath), fullPath, NULL);
                if (fullPathLen > 0 && fullPathLen < sizeof(fullPath))
                    loadPath = fullPath;
            }

            if (SendFileToExistingInstance(loadPath))
            {
                printf("Sent file to existing BVHView instance: %s\n", loadPath);
                return 0;
            }
            else
            {
                printf("No existing BVHView instance found. Starting new one.\n");
                reuse = false;
                break;
            }
        }
    }
#endif

    ApplicationState app;
    app.argc = argc;
    app.argv = argv;
    app.screenWidth = ArgInt(argc, argv, "screenWidth", 1920);
    app.screenHeight = ArgInt(argc, argv, "screenHeight", 1080);
    app.firstFileLoaded = false;
#if defined(_WIN32) && !defined(PLATFORM_WEB)
    app.reuseMailslot = CreateMailslotA(BVHVIEW_REUSE_MAILSLOT, 0, 0, NULL);
#endif

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
    app.fileListCount = 0;
    app.fileListIndex = 0;
    app.lastScannedDir[0] = '\0';
    app.restoreCameraAfterSwitch = false;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-') continue;

        const char* loadPath = argv[i];
#if defined(_WIN32) && !defined(PLATFORM_WEB)
    char downloadedPath[BVHVIEW_PATH_BUFFER_SIZE];
        if (IsBvhViewProtocolArg(argv[i]))
        {
            if (!DownloadProtocolArg(argv[i], downloadedPath, sizeof(downloadedPath), app.errMsg, 512)) continue;
            loadPath = downloadedPath;
        }
#endif

        CharacterDataLoadFromFile(&app.characterData, loadPath, app.errMsg, 512);
    }

    if (app.characterData.count > 0)
    {
        OnFileLoaded(&app);
    }

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop_arg(ApplicationUpdate, &app, 0, 1);
#else
    while (!WindowShouldClose())
    {
        ApplicationUpdate(&app);
    }
#endif

    ApplicationCleanup(&app);
    CapsuleDataFree(&app.capsuleData);
    CharacterDataFree(&app.characterData);
    UnloadModel(app.capsuleModel);
    UnloadModel(app.groundPlaneModel);
    UnloadShader(app.shader);
#if defined(_WIN32) && !defined(PLATFORM_WEB)
    if (app.reuseMailslot && app.reuseMailslot != INVALID_HANDLE_VALUE)
        CloseHandle(app.reuseMailslot);
#endif
    CloseWindow();

    return 0;
}

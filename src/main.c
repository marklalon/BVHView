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
#define BVHVIEW_PATH_BUFFER_SIZE 4096

static bool IsBvhViewProtocolArg(const char* arg)
{
    return _strnicmp(arg, "bvhview://", 10) == 0;
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
            snprintf(errMsg, errMsgSize, "Local BVH file not found or invalid path.");
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
        snprintf(errMsg, errMsgSize, "Could not get temporary directory.");
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
    CloseWindow();

    return 0;
}

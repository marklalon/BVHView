/*******************************************************************************************
*
*    BVHView - A simple BVH animation viewer written using raylib
*
*  This is a simple viewer for the .bvh animation file format made using raylib. For more
*  info on the motivation behind it and information on features and documentation please 
*  see: https://theorangeduck.com/page/bvhview
*
*  The program itself essentially consists of the following components:
*
*     - A parser for the BVH file format
*     - A set of functions for sampling data from particular frames of the BVH file.
*     - A set of functions for creating capsules from the skeleton structure of the BVH data 
*       and animation transforms.
*     - A (relatively) efficient and high quality shader for rendering capsules that includes 
*       nice lighting, soft shadows, and some CPU based culling to limit the amount of work
*       required by the GPU.
*
*  Coding style is roughly meant to follow the rest of raylib and community contributions
*  are very welcome.
*
*******************************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <float.h>
#include <errno.h>

#include "raylib.h"
#include "rcamera.h"
#include "raymath.h"
#include "rlgl.h"
#include "build/raylib/raylib/src/external/cgltf.h"

// Private stb_image with full JPEG/PNG/BMP/GIF/PSD/HDR support.
// raylib's libraylib.a compiles stb_image with STBI_NO_JPEG (SUPPORT_FILEFORMAT_JPG=0),
// so we need our own copy to decode embedded JPEG textures from glTF/GLB.
// STB_IMAGE_STATIC gives the symbols internal linkage, avoiding collisions with libraylib.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "build/raylib/raylib/src/external/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT 24
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "../examples/custom_file_dialog/gui_window_file_dialog.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

//----------------------------------------------------------------------------------
// Profiling
//----------------------------------------------------------------------------------

// Un-comment to enable profiling
//#define ENABLE_PROFILE

// Profiling only available on Windows
#if defined(ENABLE_PROFILE) && defined(_WIN32)

#include <profileapi.h>

enum
{
    // Max number of profile records (profiled code locations) 
    PROFILE_RECORD_MAX = 512,
    
    // Maximum number of timer samples per record
    PROFILE_RECORD_SAMPLE_MAX = 128,
};

// A single record for a profiled code location with a cyclic buffer of start and end times.
typedef struct 
{
    const char* name;
    uint32_t idx;
    uint32_t num;
    
    struct {
        LARGE_INTEGER start;
        LARGE_INTEGER end;
    } samples[PROFILE_RECORD_SAMPLE_MAX];
    
} ProfileRecord;

// Structure containing space for all profiled code locations
typedef struct
{
    uint32_t num;
    LARGE_INTEGER freq;
    ProfileRecord* records[PROFILE_RECORD_MAX];
    
} ProfileRecordData;

// Global variable storing all the profile record data
static ProfileRecordData globalProfileRecords;

// Init the Profile Record Data. Must be called at program start
static void ProfileRecordDataInit()
{
    globalProfileRecords.num = 0;
    QueryPerformanceFrequency(&globalProfileRecords.freq);
    memset(globalProfileRecords.records, 0, sizeof(ProfileRecord*) * PROFILE_RECORD_MAX);
}

// If uninitialized, then initialize the profile record, then store the start time
static inline void ProfileRecordBegin(ProfileRecord* record, const char* name)
{
    if (!record->name && globalProfileRecords.num < PROFILE_RECORD_MAX)
    {
        record->name = name;
        record->idx = 0;
        record->num = 0;
        globalProfileRecords.records[globalProfileRecords.num] = record;
        globalProfileRecords.num++;
    }
  
    QueryPerformanceCounter(&record->samples[record->idx].start);
}

// Store the end time and increment the record sample num
static inline void ProfileRecordEnd(ProfileRecord* record)
{
    QueryPerformanceCounter(&record->samples[record->idx].end);
    record->idx = (record->idx + 1) % PROFILE_RECORD_SAMPLE_MAX;
    record->num++;
}

// Tickers record a rolling average of Profile Record durations in microseconds
typedef struct
{
    uint64_t unitScale; 
    double alpha;
    uint32_t samples[PROFILE_RECORD_MAX];
    uint64_t iterations[PROFILE_RECORD_MAX];
    double averages[PROFILE_RECORD_MAX];
    double times[PROFILE_RECORD_MAX];
    
} ProfileTickers;

// Global profile tickers data
static ProfileTickers globalProfileTickers;

// Initialize ticker data
static inline void ProfileTickersInit()
{
    globalProfileTickers.unitScale = 1000000; // Microseconds
    globalProfileTickers.alpha = 0.9f;
    memset(globalProfileTickers.samples, 0, sizeof(uint32_t) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.iterations, 0, sizeof(uint64_t) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.averages, 0, sizeof(double) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.times, 0, sizeof(double) * PROFILE_RECORD_MAX);
}

// Update tickers and compute the rolling average of the duration
static inline void ProfileTickersUpdate()
{
    for (int i = 0; i < globalProfileRecords.num; i++)
    {
        ProfileRecord* record = globalProfileRecords.records[i];
        
        if (record && record->name)
        {
            globalProfileTickers.samples[i] = record->num;
            
            int bufferedSampleNum = record->num < PROFILE_RECORD_SAMPLE_MAX ? record->num : PROFILE_RECORD_SAMPLE_MAX;
            
            for (int j = 0; j < bufferedSampleNum; j++)
            {
                double time = (double)((
                    record->samples[j].end.QuadPart - 
                    record->samples[j].start.QuadPart) * globalProfileTickers.unitScale) / 
                        (double)globalProfileRecords.freq.QuadPart;
                    
                globalProfileTickers.iterations[i]++;
                globalProfileTickers.averages[i] = globalProfileTickers.alpha * globalProfileTickers.averages[i] + (1.0 - globalProfileTickers.alpha) * time;
                globalProfileTickers.times[i] = globalProfileTickers.averages[i] / (1.0 - pow(globalProfileTickers.alpha, globalProfileTickers.iterations[i]));
            }
            
            // Flush Samples
            record->idx = 0;
            record->num = 0;
        }
    }
}

#define PROFILE_INIT() ProfileRecordDataInit(); 
#define PROFILE_BEGIN(NAME) static ProfileRecord __PROFILE_RECORD_##NAME; ProfileRecordBegin(&__PROFILE_RECORD_##NAME, #NAME);
#define PROFILE_END(NAME) ProfileRecordEnd(&__PROFILE_RECORD_##NAME);

#define PROFILE_TICKERS_INIT() ProfileTickersInit();
#define PROFILE_TICKERS_UPDATE() ProfileTickersUpdate()

#else
#define PROFILE_INIT() 
#define PROFILE_BEGIN(NAME) 
#define PROFILE_END(NAME) 

#define PROFILE_TICKERS_INIT()
#define PROFILE_TICKERS_UPDATE()
#endif

//----------------------------------------------------------------------------------
// Additional Raylib Functions
//----------------------------------------------------------------------------------

static inline float Max(float x, float y)
{
    return x > y ? x : y;
}

static inline float Min(float x, float y)
{
    return x < y ? x : y;
}

static inline float Saturate(float x)
{
    return Clamp(x, 0.0f, 1.0f);
}

static inline float Square(float x)
{
    return x * x;
}

static inline int ClampInt(int x, int min, int max)
{
    return x < min ? min : x > max ? max : x;
}

static inline int MaxInt(int x, int y)
{
    return x > y ? x : y;
}

static inline int MinInt(int x, int y)
{
    return x < y ? x : y;
}

// This is a safe version of QuaternionBetween which returns a 180 deg rotation
// at the singularity where vectors are facing exactly in opposite directions
static inline Quaternion QuaternionBetween(Vector3 p, Vector3 q)
{
    Vector3 c = Vector3CrossProduct(p, q);

    Quaternion o = {
        c.x,
        c.y,
        c.z,
        sqrtf(Vector3DotProduct(p, p) * Vector3DotProduct(q, q)) + Vector3DotProduct(p, q),
    };
    
    return QuaternionLength(o) < 1e-8f ?
        QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, PI) :
        QuaternionNormalize(o);
}

// Puts the quaternion in the hemisphere closest to the identity
static inline Quaternion QuaternionAbsolute(Quaternion q)
{
    if (q.w < 0.0f)
    {
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
        q.w = -q.w;
    }

    return q;
}

// Quaternion exponent, log, and angle axis functions (see: https://theorangeduck.com/page/exponential-map-angle-axis-angular-velocity)

static inline Quaternion QuaternionExp(Vector3 v)
{
    float halfangle = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);

    if (halfangle < 1e-4f)
    {
        return QuaternionNormalize((Quaternion){ v.x, v.y, v.z, 1.0f });
    }
    else
    {
        float c = cosf(halfangle);
        float s = sinf(halfangle) / halfangle;
        return (Quaternion){ s * v.x, s * v.y, s * v.z, c };
    }
}

static inline Vector3 QuaternionLog(Quaternion q)
{
    float length = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z);

    if (length < 1e-4f)
    {
        return (Vector3){ q.x, q.y, q.z };
    }
    else
    {
        float halfangle = atan2f(length, q.w);
        return Vector3Scale((Vector3){ q.x, q.y, q.z }, halfangle / length);
    }
}

static inline Vector3 QuaternionToScaledAngleAxis(Quaternion q)
{
    return Vector3Scale(QuaternionLog(q), 2.0f);
}

static inline Quaternion QuaternionFromScaledAngleAxis(Vector3 v)
{
    return QuaternionExp(Vector3Scale(v, 0.5f));
}

// Cubic Interpolation (see: https://theorangeduck.com/page/cubic-interpolation-quaternions)

static inline Vector3 Vector3Hermite(Vector3 p0, Vector3 p1, Vector3 v0, Vector3 v1, float alpha)
{
    float x = alpha;
    float w0 = 2*x*x*x - 3*x*x + 1;
    float w1 = 3*x*x - 2*x*x*x;
    float w2 = x*x*x - 2*x*x + x;
    float w3 = x*x*x - x*x;

    return Vector3Add(
        Vector3Add(Vector3Scale(p0, w0), Vector3Scale(p1, w1)),
        Vector3Add(Vector3Scale(v0, w2), Vector3Scale(v1, w3)));
}

static inline Vector3 Vector3InterpolateCubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float alpha)
{
    Vector3 v1 = Vector3Scale(Vector3Add(Vector3Subtract(p1, p0), Vector3Subtract(p2, p1)), 0.5f);
    Vector3 v2 = Vector3Scale(Vector3Add(Vector3Subtract(p2, p1), Vector3Subtract(p3, p2)), 0.5f);
    return Vector3Hermite(p1, p2, v1, v2, alpha);
}

static inline Quaternion QuaternionHermite(Quaternion r0, Quaternion r1, Vector3 v0, Vector3 v1, float alpha)
{
    float x = alpha;
    float w1 = 3*x*x - 2*x*x*x;
    float w2 = x*x*x - 2*x*x + x;
    float w3 = x*x*x - x*x;

    Vector3 r1r0 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r1, QuaternionInvert(r0))));

    return QuaternionMultiply(QuaternionFromScaledAngleAxis(
        Vector3Add(Vector3Add(Vector3Scale(r1r0, w1), Vector3Scale(v0, w2)), Vector3Scale(v1, w3))), r0);
}

static inline Quaternion QuaternionInterpolateCubic(Quaternion r0, Quaternion r1, Quaternion r2, Quaternion r3, float alpha)
{
    Vector3 r1r0 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r1, QuaternionInvert(r0))));
    Vector3 r2r1 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r2, QuaternionInvert(r1))));
    Vector3 r3r2 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r3, QuaternionInvert(r2))));

    Vector3 v1 = Vector3Scale(Vector3Add(r1r0, r2r1), 0.5f);
    Vector3 v2 = Vector3Scale(Vector3Add(r2r1, r3r2), 0.5f);
    return QuaternionHermite(r1, r2, v1, v2, alpha);
}

// Frustum culling (based off https://github.com/JeffM2501/raylibExtras)

typedef struct
{
    Vector4 back;
    Vector4 front;
    Vector4 bottom;
    Vector4 top;
    Vector4 right;
    Vector4 left;
    
} Frustum;

static inline Vector4 FrustumPlaneNormalize(Vector4 plane)
{
    float magnitude = sqrtf(Square(plane.x) + Square(plane.y) + Square(plane.z));
    plane.x /= magnitude;
    plane.y /= magnitude;
    plane.z /= magnitude;
    plane.w /= magnitude;
    return plane;
}

static inline Frustum FrustumFromCameraMatrices(Matrix projection, Matrix modelview)
{
    Matrix planes = { 0 };
    planes.m0 = modelview.m0 * projection.m0 + modelview.m1 * projection.m4 + modelview.m2 * projection.m8 + modelview.m3 * projection.m12;
    planes.m1 = modelview.m0 * projection.m1 + modelview.m1 * projection.m5 + modelview.m2 * projection.m9 + modelview.m3 * projection.m13;
    planes.m2 = modelview.m0 * projection.m2 + modelview.m1 * projection.m6 + modelview.m2 * projection.m10 + modelview.m3 * projection.m14;
    planes.m3 = modelview.m0 * projection.m3 + modelview.m1 * projection.m7 + modelview.m2 * projection.m11 + modelview.m3 * projection.m15;
    planes.m4 = modelview.m4 * projection.m0 + modelview.m5 * projection.m4 + modelview.m6 * projection.m8 + modelview.m7 * projection.m12;
    planes.m5 = modelview.m4 * projection.m1 + modelview.m5 * projection.m5 + modelview.m6 * projection.m9 + modelview.m7 * projection.m13;
    planes.m6 = modelview.m4 * projection.m2 + modelview.m5 * projection.m6 + modelview.m6 * projection.m10 + modelview.m7 * projection.m14;
    planes.m7 = modelview.m4 * projection.m3 + modelview.m5 * projection.m7 + modelview.m6 * projection.m11 + modelview.m7 * projection.m15;
    planes.m8 = modelview.m8 * projection.m0 + modelview.m9 * projection.m4 + modelview.m10 * projection.m8 + modelview.m11 * projection.m12;
    planes.m9 = modelview.m8 * projection.m1 + modelview.m9 * projection.m5 + modelview.m10 * projection.m9 + modelview.m11 * projection.m13;
    planes.m10 = modelview.m8 * projection.m2 + modelview.m9 * projection.m6 + modelview.m10 * projection.m10 + modelview.m11 * projection.m14;
    planes.m11 = modelview.m8 * projection.m3 + modelview.m9 * projection.m7 + modelview.m10 * projection.m11 + modelview.m11 * projection.m15;
    planes.m12 = modelview.m12 * projection.m0 + modelview.m13 * projection.m4 + modelview.m14 * projection.m8 + modelview.m15 * projection.m12;
    planes.m13 = modelview.m12 * projection.m1 + modelview.m13 * projection.m5 + modelview.m14 * projection.m9 + modelview.m15 * projection.m13;
    planes.m14 = modelview.m12 * projection.m2 + modelview.m13 * projection.m6 + modelview.m14 * projection.m10 + modelview.m15 * projection.m14;
    planes.m15 = modelview.m12 * projection.m3 + modelview.m13 * projection.m7 + modelview.m14 * projection.m11 + modelview.m15 * projection.m15;

    Frustum frustum;
    frustum.back = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m2, planes.m7 - planes.m6, planes.m11 - planes.m10, planes.m15 - planes.m14 });
    frustum.front = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m2, planes.m7 + planes.m6, planes.m11 + planes.m10, planes.m15 + planes.m14 });
    frustum.bottom = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m1, planes.m7 + planes.m5, planes.m11 + planes.m9, planes.m15 + planes.m13 });
    frustum.top = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m1, planes.m7 - planes.m5, planes.m11 - planes.m9, planes.m15 - planes.m13 });
    frustum.left = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m0, planes.m7 + planes.m4, planes.m11 + planes.m8, planes.m15 + planes.m12 });
    frustum.right = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m0, planes.m7 - planes.m4, planes.m11 - planes.m8, planes.m15 - planes.m12 });
    return frustum;
}

static inline float FrustumPlaneDistanceToPoint(Vector4 plane, Vector3 position)
{
    return (plane.x * position.x + plane.y * position.y + plane.z * position.z + plane.w);
}

static inline bool FrustumContainsSphere(Frustum frustum, Vector3 position, float radius)
{
    if (FrustumPlaneDistanceToPoint(frustum.back, position) < -radius) { return false; }
    if (FrustumPlaneDistanceToPoint(frustum.front, position) < -radius) { return false; }
    if (FrustumPlaneDistanceToPoint(frustum.bottom, position) < -radius) { return false; }
    if (FrustumPlaneDistanceToPoint(frustum.top, position) < -radius) { return false; }
    if (FrustumPlaneDistanceToPoint(frustum.left, position) < -radius) { return false; }
    if (FrustumPlaneDistanceToPoint(frustum.right, position) < -radius) { return false; }
    return true;
}

//----------------------------------------------------------------------------------
// Command Line Args
//----------------------------------------------------------------------------------

// Finds an argument on the command line with the given name (in the format "--argName=argValue") and returns the argValue as a string
static inline char* ArgFind(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; i++)
    {
        if (strlen(argv[i]) > 4 &&
          argv[i][0] == '-' &&
          argv[i][1] == '-' &&
          strstr(argv[i] + 2, name) == argv[i] + 2)
        {
            char* argStart = strchr(argv[i], '=');
            return argStart ? argStart + 1 : NULL;
        }
    }

    return NULL;
}

// Parse a float argument from the command line
static inline float ArgFloat(int argc, char** argv, const char* name, float defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    errno = 0;
    float output = strtof(value, NULL);
    if (errno == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return output; }

    printf("ERROR: Could not parse value '%s' given for option '%s' as float\n", value, name);
    return defaultValue;
}

// Parse an integer argument from the command line
static inline int ArgInt(int argc, char** argv, const char* name, int defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    errno = 0;
    int output = (int)strtol(value, NULL, 10);
    if (errno == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return output; }

    printf("ERROR: Could not parse value '%s' given for option '%s' as int\n", value, name);
    return defaultValue;
}

// Parse a boolean argument from the command line
static inline int ArgBool(int argc, char** argv, const char* name, bool defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }
    if (strcmp(value, "true") == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return true; }
    if (strcmp(value, "false") == 0) { printf("INFO: Parsed option '%s' as '%s'\n", name, value); return false; }

    printf("ERROR: Could not parse value '%s' given for option '%s' as bool\n", value, name);
    return defaultValue;
}

// Parse an enum argument from the command line
static inline int ArgEnum(int argc, char** argv, const char* name, int optionCount, const char* options[], int defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    for (int i = 0; i < optionCount; i++)
    {
        if (strcmp(value, options[i]) == 0)
        {
            printf("INFO: Parsed option '%s' as '%s'\n", name, value);
            return i;
        }
    }

    printf("ERROR: Could not parse value '%s' given for option '%s' as enum\n", value, name);
    return defaultValue;
}

// Parse a string argument from the command line
static inline const char* ArgStr(int argc, char** argv, const char* name, const char* defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    printf("INFO: Parsed option '%s' as '%s'\n", name, value);
    return value;
}

// Parse a color argument from the command line
static inline Color ArgColor(int argc, char** argv, const char* name, Color defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    int cx, cy, cz;
    if (sscanf(value, "%i,%i,%i", &cx, &cy, &cz) == 3)
    {
        printf("INFO: Parsed option '%s' as '%s'\n", name, value);
        return (Color){ ClampInt(cx, 0, 255), ClampInt(cy, 0, 255), ClampInt(cz, 0, 255) };
    }

    printf("ERROR: Could not parse value '%s' given for option '%s' as color\n", value, name);
    return defaultValue;
}

// Parse a vector3 argument from the command line
static inline Vector3 ArgVector3(int argc, char** argv, const char* name, Vector3 defaultValue)
{
    char* value = ArgFind(argc, argv, name);
    if (!value) { return defaultValue; }

    float cx, cy, cz;
    if (sscanf(value, "%f,%f,%f", &cx, &cy, &cz) == 3)
    {
        printf("INFO: Parsed option '%s' as '%s'\n", name, value);
        return (Vector3){ cx, cy, cz };
    }

    printf("ERROR: Could not parse value '%s' given for option '%s' as color\n", value, name);
    return defaultValue;
}

//----------------------------------------------------------------------------------
// Camera
//----------------------------------------------------------------------------------

// Basic Orbit Camera with simple controls
typedef struct {

    Camera3D cam3d;
    float azimuth;
    float altitude;
    float distance;
    Vector3 offset;
    bool track;
    int trackBone;

} OrbitCamera;

static inline void OrbitCameraInit(OrbitCamera* camera, int argc, char** argv)
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
}

static inline void OrbitCameraUpdate(
    OrbitCamera* camera,
    Vector3 target,
    float azimuthDelta,
    float altitudeDelta,
    float offsetDeltaX,
    float offsetDeltaY,
    float mouseWheel,
    float dt)
{
    camera->azimuth = camera->azimuth + 1.0f * dt * -azimuthDelta;
    camera->altitude = Clamp(camera->altitude + 1.0f * dt * altitudeDelta, 0.0, 0.4f * PI);
    camera->distance = Clamp(camera->distance +  40.0f * dt * -mouseWheel, 0.1f, 100.0f);
    
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

//----------------------------------------------------------------------------------
// Parser
//----------------------------------------------------------------------------------

enum
{
    PARSER_ERR_MAX = 512,
};

// Simple parser that keeps track of rows, and cols in a string and so can provide slightly 
// nicer error messages. Has ability to peek at next character and advance the input
typedef struct {

    const char* filename;
    int offset;
    const char* data;
    int row;
    int col;
    char err[PARSER_ERR_MAX];

} Parser;

// Initialize the Parser
static inline void ParserInit(Parser* par, const char* filename, const char* data)
{
    par->filename = filename;
    par->offset = 0;
    par->data = data;
    par->row = 0;
    par->col = 0;
    par->err[0] = '\0';
}

// Peek at the next character in the stream
static inline char ParserPeek(const Parser* par)
{
    return par->data[par->offset];
}

// Peek forward N steps in the stream. Does not check the stream is long enough.
static inline char ParserPeekForward(const Parser* par, int steps)
{
    return par->data[par->offset + steps];
}

// Checks the current character matches the given input
static inline bool ParserMatch(const Parser* par, char match)
{
    return match == par->data[par->offset];
}

// Checks the current character matches one of the given characters
static inline bool ParserOneOf(const Parser* par, const char* matches)
{
    return strchr(matches, par->data[par->offset]);
}

// Checks the following characters in the stream match the prefix (in a caseless way)
static inline bool ParserStartsWithCaseless(const Parser* par, const char* prefix)
{
    const char* start = par->data + par->offset;
    while (*prefix)
    {
        if (tolower(*prefix) != tolower(*start)) { return false; }
        prefix++;
        start++;
    }

    return true;
}

// Advances the stream forward one
static inline void ParserInc(Parser* par)
{
    if (par->data[par->offset] == '\n')
    {
        par->row++;
        par->col = 0;
    }
    else
    {
        par->col++;
    }

    par->offset++;
}

// Advances the stream forward "num" characters
static inline void ParserAdvance(Parser* par, int num)
{
    for (int i = 0; i < num; i++) { ParserInc(par); }
}

// Gets the human readable name of a particular character
static inline char* ParserCharName(char c)
{
    static char parserCharName[2];

    switch (c)
    {
        case '\0': return "end of file";
        case '\r': return "new line";
        case '\n': return "new line";
        case '\t': return "tab";
        case '\v': return "vertical tab";
        case '\b': return "backspace";
        case '\f': return "form feed";
        default:
            parserCharName[0] = c;
            parserCharName[1] = '\0';
            return parserCharName;
    }
}

// Prints a formatted error to the parser error buffer
#define ParserError(par, fmt, ...) \
    snprintf(par->err, PARSER_ERR_MAX, "%s:%i:%i: error: " fmt, par->filename, par->row, par->col, ##__VA_ARGS__)

//----------------------------------------------------------------------------------
// BVH File Data
//----------------------------------------------------------------------------------

// Types of "channels" that are possible in the BVH format
enum
{
    CHANNEL_X_POSITION = 0,
    CHANNEL_Y_POSITION = 1,
    CHANNEL_Z_POSITION = 2,
    CHANNEL_X_ROTATION = 3,
    CHANNEL_Y_ROTATION = 4,
    CHANNEL_Z_ROTATION = 5,
    CHANNELS_MAX = 6,
};

// Data associated with a single "joint" in the BVH format
typedef struct
{
    int parent;
    char* name;
    Vector3 offset;
    int channelCount;
    char channels[CHANNELS_MAX];
    bool endSite;

} BVHJointData;

static inline void BVHJointDataInit(BVHJointData* data)
{
    data->parent = -1;
    data->name = NULL;
    data->offset = (Vector3){ 0.0f, 0.0f, 0.0f };
    data->channelCount = 0;
    data->endSite = false;
}

static inline void BVHJointDataRename(BVHJointData* data, const char* name)
{
    data->name = realloc(data->name, strlen(name) + 1);
    strcpy(data->name, name);
}

static inline void BVHJointDataFree(BVHJointData* data)
{
    free(data->name);
}

// Data structure matching what is present in the BVH file format
typedef struct
{
    // Hierarchy Data

    int jointCount;
    BVHJointData* joints;

    // Motion Data

    int frameCount;
    int channelCount;
    float frameTime;
    float* motionData;

} BVHData;

static inline void BVHDataInit(BVHData* bvh)
{
    bvh->jointCount = 0;
    bvh->joints = NULL;
    bvh->frameCount = 0;
    bvh->channelCount = 0;
    bvh->frameTime = 0.0f;
    bvh->motionData = NULL;
}

static inline void BVHDataFree(BVHData* bvh)
{
    for (int i = 0; i < bvh->jointCount; i++)
    {
        BVHJointDataFree(&bvh->joints[i]);
    }
    free(bvh->joints);

    free(bvh->motionData);
}

static inline int BVHDataAddJoint(BVHData* bvh)
{
    bvh->joints = (BVHJointData*)realloc(bvh->joints, (bvh->jointCount + 1) * sizeof(BVHJointData));
    bvh->jointCount++;
    BVHJointDataInit(&bvh->joints[bvh->jointCount - 1]);
    return bvh->jointCount - 1;
}

//----------------------------------------------------------------------------------
// BVH Parser
//----------------------------------------------------------------------------------

// Parse any whitespace
static void BVHParseWhitespace(Parser* par)
{
    while (ParserOneOf(par, " \r\t\v")) { ParserInc(par); }
}

// Parse the given string (in a non-case sensitive way). I've found that in practice
// many BVH files don't respect case sensitivity so parsing any keywords in a non-case
// sensitive way seems safer.
static bool BVHParseString(Parser* par, const char* string)
{
    if (ParserStartsWithCaseless(par, string))
    {
        ParserAdvance(par, strlen(string));
        return true;
    }
    else
    {
        ParserError(par, "expected '%s' at '%s'", string, ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse any whitespace followed by a newline
static bool BVHParseNewline(Parser* par)
{
    BVHParseWhitespace(par);

    if (ParserMatch(par, '\n'))
    {
        ParserInc(par);
        BVHParseWhitespace(par);
        return true;
    }
    else
    {
        ParserError(par, "expected newline at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse any whitespace and then an identifier for the name of a joint
static bool BVHParseJointName(BVHJointData* jnt, Parser* par)
{
    BVHParseWhitespace(par);

    char buffer[256];
    int chrnum = 0;
    while (chrnum < 255 && ParserOneOf(par,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:-."))
    {
        buffer[chrnum] = ParserPeek(par);
        chrnum++;
        ParserInc(par);
    }
    buffer[chrnum] = '\0';

    if (chrnum > 0)
    {
        BVHJointDataRename(jnt, buffer);
        BVHParseWhitespace(par);
        return true;
    }
    else
    {
        ParserError(par, "expected joint name at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse a float value
static bool BVHParseFloat(float* out, Parser* par)
{
    BVHParseWhitespace(par);

    char* end;
    errno = 0;
    (*out) = strtod(par->data + par->offset, &end);

    if (errno == 0)
    {
        ParserAdvance(par, end - (par->data + par->offset));
        return true;
    }
    else
    {
        ParserError(par, "expected float at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse an integer value
static bool BVHParseInt(int* out, Parser* par)
{
    BVHParseWhitespace(par);

    char* end;
    errno = 0;
    (*out) = (int)strtol(par->data + par->offset, &end, 10);

    if (errno == 0)
    {
        ParserAdvance(par, end - (par->data + par->offset));
        return true;
    }
    else
    {
        ParserError(par, "expected integer at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse the "joint offset" part of the BVH File
static bool BVHParseJointOffset(BVHJointData* jnt, Parser* par)
{
    if (!BVHParseString(par, "OFFSET")) { return false; }
    if (!BVHParseFloat(&jnt->offset.x, par)) { return false; }
    if (!BVHParseFloat(&jnt->offset.y, par)) { return false; }
    if (!BVHParseFloat(&jnt->offset.z, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    return true;
}

// Parse a channel type and return it in "channel"
static bool BVHParseChannelEnum(
    char* channel,
    Parser* par,
    const char* channelName,
    char channelValue)
{
    BVHParseWhitespace(par);
    if (!BVHParseString(par, channelName)) { return false; }
    BVHParseWhitespace(par);
    *channel = channelValue;
    return true;
}

// Parse a channel type and return it in "channel"
static bool BVHParseChannel(char* channel, Parser* par)
{
    BVHParseWhitespace(par);

    if (ParserPeek(par) == '\0')
    {
        ParserError(par, "expected channel at end of file");
        return false;
    }

    // Here we are safe to peek forward an extra character since we've already
    // checked the current character is not the null terminator.

    if (ParserPeek(par) == 'X' && ParserPeekForward(par, 1) == 'p')
    {
        return BVHParseChannelEnum(channel, par, "Xposition", CHANNEL_X_POSITION);
    }

    if (ParserPeek(par) == 'Y' && ParserPeekForward(par, 1) == 'p')
    {
        return BVHParseChannelEnum(channel, par, "Yposition", CHANNEL_Y_POSITION);
    }

    if (ParserPeek(par) == 'Z' && ParserPeekForward(par, 1) == 'p')
    {
        return BVHParseChannelEnum(channel, par, "Zposition", CHANNEL_Z_POSITION);
    }

    if (ParserPeek(par) == 'X' && ParserPeekForward(par, 1) == 'r')
    {
        return BVHParseChannelEnum(channel, par, "Xrotation", CHANNEL_X_ROTATION);
    }

    if (ParserPeek(par) == 'Y' && ParserPeekForward(par, 1) == 'r')
    {
        return BVHParseChannelEnum(channel, par, "Yrotation", CHANNEL_Y_ROTATION);
    }

    if (ParserPeek(par) == 'Z' && ParserPeekForward(par, 1) == 'r')
    {
        return BVHParseChannelEnum(channel, par, "Zrotation", CHANNEL_Z_ROTATION);
    }

    ParserError(par, "expected channel type");
    return false;
}

// Parse the "channels" part of the BVH file format
static bool BVHParseJointChannels(BVHJointData* jnt, Parser* par)
{
    if (!BVHParseString(par, "CHANNELS")) { return false; }
    if (!BVHParseInt(&jnt->channelCount, par)) { return false; }

    for (int i = 0; i < jnt->channelCount; i++)
    {
        if (!BVHParseChannel(&jnt->channels[i], par)) { return false; }
    }

    if (!BVHParseNewline(par)) { return false; }

    return true;
}

// Parse a joint in the BVH file format
static bool BVHParseJoints(BVHData* bvh, int parent, Parser* par)
{
    while (ParserOneOf(par, "JEje")) // Either "JOINT" or "End Site"
    {
        int j = BVHDataAddJoint(bvh);
        bvh->joints[j].parent = parent;

        if (ParserMatch(par, 'J'))
        {
            if (!BVHParseString(par, "JOINT")) { return false; }
            if (!BVHParseJointName(&bvh->joints[j], par)) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseString(par, "{")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
            if (!BVHParseJointChannels(&bvh->joints[j], par)) { return false; }
            if (!BVHParseJoints(bvh, j, par)) { return false; }
            if (!BVHParseString(par, "}")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
        }
        else if (ParserMatch(par, 'E'))
        {
            bvh->joints[j].endSite = true;

            if (!BVHParseString(par, "End Site")) { return false; }
            BVHJointDataRename(&bvh->joints[j], "End Site");
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseString(par, "{")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
            if (!BVHParseString(par, "}")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
        }
    }

    return true;
}

// Parse the frame count
static bool BVHParseFrames(BVHData* bvh, Parser* par)
{
    if (!BVHParseString(par, "Frames:")) { return false; }
    if (!BVHParseInt(&bvh->frameCount, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    return true;
}

// Parse the frame time
static bool BVHParseFrameTime(BVHData* bvh, Parser* par)
{
    if (!BVHParseString(par, "Frame Time:")) { return false; }
    if (!BVHParseFloat(&bvh->frameTime, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (bvh->frameTime == 0.0f) { bvh->frameTime = 1.0f / 60.0f; }
    return true;
}

// Parse the motion data part of the BVH file format
static bool BVHParseMotionData(BVHData* bvh, Parser* par)
{
    int channelCount = 0;
    for (int i = 0; i < bvh->jointCount; i++)
    {
        channelCount += bvh->joints[i].channelCount;
    }

    bvh->channelCount = channelCount;
    bvh->motionData = malloc(bvh->frameCount * channelCount * sizeof(float));

    for (int i = 0; i < bvh->frameCount; i++)
    {
        for (int j = 0; j < channelCount; j++)
        {
            if (!BVHParseFloat(&bvh->motionData[i * channelCount + j], par)) { return false; }
        }

        if (!BVHParseNewline(par)) { return false; }
    }

    return true;
}

// Parse the entire BVH file format
static bool BVHParse(BVHData* bvh, Parser* par)
{
    // Hierarchy Data

    if (!BVHParseString(par, "HIERARCHY")) { return false; }
    if (!BVHParseNewline(par)) { return false; }

    int j = BVHDataAddJoint(bvh);

    if (!BVHParseString(par, "ROOT")) { return false; }
    if (!BVHParseJointName(&bvh->joints[j], par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseString(par, "{")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
    if (!BVHParseJointChannels(&bvh->joints[j], par)) { return false; }
    if (!BVHParseJoints(bvh, j, par)) { return false; }
    if (!BVHParseString(par, "}")) { return false; }
    if (!BVHParseNewline(par)) { return false; }

    // Motion Data

    if (!BVHParseString(par, "MOTION")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseFrames(bvh, par)) { return false; }
    if (!BVHParseFrameTime(bvh, par)) { return false; }
    if (!BVHParseMotionData(bvh, par)) { return false; }

    return true;
}

// Load the given file and parse the contents as a BVH file.
static bool BVHDataLoad(BVHData* bvh, const char* filename, char* errMsg, int errMsgSize)
{
    // Read file Contents

    FILE* f = fopen(filename, "rb");

    if (f == NULL)
    {
        snprintf(errMsg, errMsgSize, "Error: Could not find file '%s'\n", filename);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long int length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = malloc(length + 1);
    fread(buffer, 1, length, f);
    buffer[length] = '\n';
    fclose(f);
    
    // Free and re-init in case we are re-using an old buffer 
    BVHDataFree(bvh); 
    BVHDataInit(bvh);

    // Parse BVH
    Parser par;
    ParserInit(&par, filename, buffer);
    bool result = BVHParse(bvh, &par);

    // Free contents and return result
    free(buffer);

    if (!result)
    {
        snprintf(errMsg, errMsgSize, "Error: Could not parse BVH file:\n    %s", par.err);
    }
    else
    {
        errMsg[0] = '\0';
        printf("INFO: parsed '%s' successfully\n", filename);
    }

    return result;
}

//----------------------------------------------------------------------------------
// Transform Data
//----------------------------------------------------------------------------------

// Structure for containing a sampled pose as joint transforms
typedef struct
{
    int jointCount;
    int* parents;
    bool* endSite;
    Vector3* localPositions;
    Quaternion* localRotations;
    Vector3* globalPositions;
    Quaternion* globalRotations;

} TransformData;

static inline void TransformDataInit(TransformData* data)
{
    data->jointCount = 0;
    data->parents = NULL;
    data->endSite = NULL;
    data->localPositions = NULL;
    data->localRotations = NULL;
    data->globalPositions = NULL;
    data->globalRotations = NULL;
}

// Resize the transform buffer according to the given BVH data and record the joint
// parents and end-sites.
static inline void TransformDataResize(TransformData* data, BVHData* bvh)
{
    data->jointCount = bvh->jointCount;
    data->parents = realloc(data->parents, data->jointCount * sizeof(int));
    data->endSite = realloc(data->endSite, data->jointCount * sizeof(bool));
    data->localPositions = realloc(data->localPositions, data->jointCount * sizeof(Vector3));
    data->localRotations = realloc(data->localRotations, data->jointCount * sizeof(Quaternion));
    data->globalPositions = realloc(data->globalPositions, data->jointCount * sizeof(Vector3));
    data->globalRotations = realloc(data->globalRotations, data->jointCount * sizeof(Quaternion));

    for (int i = 0; i < data->jointCount; i++)
    {
        data->endSite[i] = bvh->joints[i].endSite;
        data->parents[i] = bvh->joints[i].parent;
    }
}

static inline void TransformDataFree(TransformData* data)
{
    free(data->parents);
    free(data->endSite);
    free(data->localPositions);
    free(data->localRotations);
    free(data->globalPositions);
    free(data->globalRotations);
}

// Sample joint transforms from a given frame of the BVH file and with a given scale
static void TransformDataSampleFrame(TransformData* data, BVHData* bvh, int frame, float scale)
{
    // Clamp the frame index in range.
    frame = frame < 0 ? 0 : frame >= bvh->frameCount ? bvh->frameCount - 1 : frame;

    int offset = 0;
    for (int i = 0; i < bvh->jointCount; i++)
    {
        Vector3 position = Vector3Scale(bvh->joints[i].offset, scale);
        Quaternion rotation = QuaternionIdentity();

        for (int c = 0; c < bvh->joints[i].channelCount; c++)
        {
            switch (bvh->joints[i].channels[c])
            {
                case CHANNEL_X_POSITION:
                    position.x = scale * bvh->motionData[frame * bvh->channelCount + offset];
                    offset++;
                    break;

                case CHANNEL_Y_POSITION:
                    position.y = scale * bvh->motionData[frame * bvh->channelCount + offset];
                    offset++;
                    break;

                case CHANNEL_Z_POSITION:
                    position.z = scale * bvh->motionData[frame * bvh->channelCount + offset];
                    offset++;
                    break;

                case CHANNEL_X_ROTATION:
                    rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle(
                        (Vector3){1, 0, 0}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset]));
                    offset++;
                    break;

                case CHANNEL_Y_ROTATION:
                    rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle(
                        (Vector3){0, 1, 0}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset]));
                    offset++;
                    break;

                case CHANNEL_Z_ROTATION:
                    rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle(
                        (Vector3){0, 0, 1}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset]));
                    offset++;
                    break;
            }
        }

        data->localPositions[i] = position;
        data->localRotations[i] = rotation;
    }

    assert(offset == bvh->channelCount);
}

// Sample the nearest frame to the given time
static void TransformDataSampleFrameNearest(TransformData* data, BVHData* bvh, float time, float scale)
{
    int frame = ClampInt((int)(time / bvh->frameTime + 0.5f), 0, bvh->frameCount - 1);
    TransformDataSampleFrame(data, bvh, frame, scale);
}

// Perform a basic linear interpolation of the frame data in the BVH file
static void TransformDataSampleFrameLinear(
    TransformData* data,
    TransformData* tmp0,
    TransformData* tmp1,
    BVHData* bvh,
    float time,
    float scale)
{
    const float alpha = fmod(time / bvh->frameTime, 1.0f);
    int frame0 = ClampInt((int)(time / bvh->frameTime) + 0, 0, bvh->frameCount - 1);
    int frame1 = ClampInt((int)(time / bvh->frameTime) + 1, 0, bvh->frameCount - 1);

    TransformDataSampleFrame(tmp0, bvh, frame0, scale);
    TransformDataSampleFrame(tmp1, bvh, frame1, scale);

    for (int i = 0; i < data->jointCount; i++)
    {
        data->localPositions[i] = Vector3Lerp(tmp0->localPositions[i], tmp1->localPositions[i], alpha);
        data->localRotations[i] = QuaternionSlerp(tmp0->localRotations[i], tmp1->localRotations[i], alpha);
    }
}

// Perform a cubic interpolation of the frame data in the BVH file
static void TransformDataSampleFrameCubic(
    TransformData* data,
    TransformData* tmp0,
    TransformData* tmp1,
    TransformData* tmp2,
    TransformData* tmp3,
    BVHData* bvh,
    float time,
    float scale)
{
    const float alpha = fmod(time / bvh->frameTime, 1.0f);
    int frame0 = ClampInt((int)(time / bvh->frameTime) - 1, 0, bvh->frameCount - 1);
    int frame1 = ClampInt((int)(time / bvh->frameTime) + 0, 0, bvh->frameCount - 1);
    int frame2 = ClampInt((int)(time / bvh->frameTime) + 1, 0, bvh->frameCount - 1);
    int frame3 = ClampInt((int)(time / bvh->frameTime) + 2, 0, bvh->frameCount - 1);

    TransformDataSampleFrame(tmp0, bvh, frame0, scale);
    TransformDataSampleFrame(tmp1, bvh, frame1, scale);
    TransformDataSampleFrame(tmp2, bvh, frame2, scale);
    TransformDataSampleFrame(tmp3, bvh, frame3, scale);

    for (int i = 0; i < data->jointCount; i++)
    {
        data->localPositions[i] = Vector3InterpolateCubic(
            tmp0->localPositions[i], tmp1->localPositions[i],
            tmp2->localPositions[i], tmp3->localPositions[i], alpha);

        data->localRotations[i] = QuaternionInterpolateCubic(
            tmp0->localRotations[i], tmp1->localRotations[i],
            tmp2->localRotations[i], tmp3->localRotations[i], alpha);
    }
}

// Compute format kinematics on the transform buffer
static void TransformDataForwardKinematics(TransformData* data)
{
    for (int i = 0; i < data->jointCount; i++)
    {
        int p = data->parents[i];
        assert(p <= i);

        if (p == -1)
        {
            data->globalPositions[i] = data->localPositions[i];
            data->globalRotations[i] = data->localRotations[i];
        }
        else
        {
            data->globalPositions[i] = Vector3Add(Vector3RotateByQuaternion(data->localPositions[i], data->globalRotations[p]), data->globalPositions[p]);
            data->globalRotations[i] = QuaternionMultiply(data->globalRotations[p], data->localRotations[i]);
        }
    }
}

//----------------------------------------------------------------------------------
// GLB Data
//----------------------------------------------------------------------------------

// Structure for storing GLB model and animation data
typedef struct
{
    Model model;                    // Loaded GLB model (with skeleton)
    ModelAnimation* animations;     // Loaded animations array
    int animCount;                  // Number of animations
    int activeAnim;                 // Currently active animation index
    float frameTime;                // Internal sampling step used by raylib's GLTF loader
    int* sourceFrameCounts;         // Original glTF timeline frame counts per animation
    float* sourceFrameTimes;        // Original glTF timeline frame times per animation
    float* sourceDurations;         // Original glTF animation durations in seconds
    cgltf_data* sourceData;         // Parsed glTF data for exact runtime sampling
    cgltf_skin* sourceSkin;         // Skin used for exact runtime sampling
    Transform* sourceRestPose;      // Default local joint transforms in original bone order
    Transform* sourceLocalPose;     // Temporary local pose buffer in original bone order
    Transform* sourceGlobalPose;    // Temporary global pose buffer in original bone order
    Transform* sourceRootPose;      // External parent world transforms for root joints
    int* topoOrder;                 // Maps sorted index -> original bone index
    int* invTopoOrder;              // Maps original bone index -> sorted index
} GLBData;

// Initialize GLBData to safe state
static inline void GLBDataInit(GLBData* data)
{
    data->model = (Model){ 0 };
    data->animations = NULL;
    data->animCount = 0;
    data->activeAnim = 0;
    data->frameTime = 1.0f / 30.0f;
    data->sourceFrameCounts = NULL;
    data->sourceFrameTimes = NULL;
    data->sourceDurations = NULL;
    data->sourceData = NULL;
    data->sourceSkin = NULL;
    data->sourceRestPose = NULL;
    data->sourceLocalPose = NULL;
    data->sourceGlobalPose = NULL;
    data->sourceRootPose = NULL;
    data->topoOrder = NULL;
    data->invTopoOrder = NULL;
}

// Free GLB model and animation data
static inline void GLBDataFree(GLBData* data)
{
    if (data->animations != NULL)
    {
        UnloadModelAnimations(data->animations, data->animCount);
        data->animations = NULL;
    }
    // Check if model was loaded by verifying if bones or meshes exist
    if (data->model.skeleton.boneCount > 0 || data->model.meshCount > 0)
    {
        UnloadModel(data->model);
    }
    free(data->sourceFrameCounts);
    free(data->sourceFrameTimes);
    free(data->sourceDurations);
    free(data->sourceRestPose);
    free(data->sourceLocalPose);
    free(data->sourceGlobalPose);
    free(data->sourceRootPose);
    if (data->sourceData != NULL) cgltf_free(data->sourceData);
    free(data->topoOrder);
    free(data->invTopoOrder);
    data->model = (Model){ 0 };
    data->animCount = 0;
    data->activeAnim = 0;
    data->frameTime = 1.0f / 30.0f;
    data->sourceFrameCounts = NULL;
    data->sourceFrameTimes = NULL;
    data->sourceDurations = NULL;
    data->sourceData = NULL;
    data->sourceSkin = NULL;
    data->sourceRestPose = NULL;
    data->sourceLocalPose = NULL;
    data->sourceGlobalPose = NULL;
    data->sourceRootPose = NULL;
    data->topoOrder = NULL;
    data->invTopoOrder = NULL;
}

static inline int GLBDataGetSourceFrameCount(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return 0;

    animIdx = ClampInt(animIdx, 0, data->animCount - 1);

    if (data->sourceFrameCounts != NULL && data->sourceFrameCounts[animIdx] > 0)
        return data->sourceFrameCounts[animIdx];

    return data->animations[animIdx].keyframeCount;
}

static inline float GLBDataGetSourceFrameTime(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return data->frameTime;

    animIdx = ClampInt(animIdx, 0, data->animCount - 1);

    if (data->sourceFrameTimes != NULL && data->sourceFrameTimes[animIdx] > 0.0f)
        return data->sourceFrameTimes[animIdx];

    return data->frameTime;
}

static inline float GLBDataGetSourceDuration(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return 0.0f;

    animIdx = ClampInt(animIdx, 0, data->animCount - 1);

    if (data->sourceDurations != NULL && data->sourceDurations[animIdx] > 0.0f)
        return data->sourceDurations[animIdx];

    return (GLBDataGetSourceFrameCount(data, animIdx) - 1) * GLBDataGetSourceFrameTime(data, animIdx);
}

static Matrix GLBMatrixFromCgltf(const cgltf_float* m)
{
    return (Matrix){
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
}

static Matrix GLBTransformToMatrix(Transform transform)
{
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
            QuaternionToMatrix(transform.rotation)),
        MatrixTranslate(transform.translation.x, transform.translation.y, transform.translation.z));
}

static void GLBDataUpdateModelAnimationVertexBuffers(Model model)
{
    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
    {
        Mesh* mesh = &model.meshes[meshIndex];
        if (mesh->boneWeights == NULL || mesh->boneIndices == NULL || mesh->animVertices == NULL || mesh->animNormals == NULL) continue;

        bool bufferUpdateRequired = false;
        int boneCounter = 0;
        int vertexValuesCount = mesh->vertexCount * 3;

        for (int vertexIndex = 0; vertexIndex < vertexValuesCount; vertexIndex += 3)
        {
            mesh->animVertices[vertexIndex + 0] = 0.0f;
            mesh->animVertices[vertexIndex + 1] = 0.0f;
            mesh->animVertices[vertexIndex + 2] = 0.0f;
            mesh->animNormals[vertexIndex + 0] = 0.0f;
            mesh->animNormals[vertexIndex + 1] = 0.0f;
            mesh->animNormals[vertexIndex + 2] = 0.0f;

            for (int weightIndex = 0; weightIndex < 4; weightIndex++, boneCounter++)
            {
                float boneWeight = mesh->boneWeights[boneCounter];
                int boneIndex = mesh->boneIndices[boneCounter];

                if (boneWeight == 0.0f) continue;
                if (boneIndex < 0 || boneIndex >= model.skeleton.boneCount) continue;

                Vector3 animVertex = { mesh->vertices[vertexIndex], mesh->vertices[vertexIndex + 1], mesh->vertices[vertexIndex + 2] };
                animVertex = Vector3Transform(animVertex, model.boneMatrices[boneIndex]);
                mesh->animVertices[vertexIndex + 0] += animVertex.x * boneWeight;
                mesh->animVertices[vertexIndex + 1] += animVertex.y * boneWeight;
                mesh->animVertices[vertexIndex + 2] += animVertex.z * boneWeight;
                bufferUpdateRequired = true;

                if (mesh->normals != NULL)
                {
                    Vector3 animNormal = { mesh->normals[vertexIndex], mesh->normals[vertexIndex + 1], mesh->normals[vertexIndex + 2] };
                    animNormal = Vector3Transform(animNormal, MatrixTranspose(MatrixInvert(model.boneMatrices[boneIndex])));
                    mesh->animNormals[vertexIndex + 0] += animNormal.x * boneWeight;
                    mesh->animNormals[vertexIndex + 1] += animNormal.y * boneWeight;
                    mesh->animNormals[vertexIndex + 2] += animNormal.z * boneWeight;
                }
            }
        }

        if (bufferUpdateRequired)
        {
            // NOTE: mesh->vboId is indexed by RL_DEFAULT_SHADER_ATTRIB_LOCATION_* (position=0, texcoord=1, normal=2, color=3, tangent=4),
            // not by SHADER_LOC_VERTEX_* (which has TEXCOORD02 at slot 2 and shifts NORMAL to slot 3 — that would target the color VBO).
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], mesh->animVertices, mesh->vertexCount * 3 * sizeof(float), 0);
            if (mesh->normals != NULL) rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh->animNormals, mesh->vertexCount * 3 * sizeof(float), 0);
        }
    }
}

static void GLBDataUpdateModelPose(GLBData* glb, const Transform* globalPose)
{
    if (glb->model.currentPose == NULL || glb->model.boneMatrices == NULL || glb->model.skeleton.bindPose == NULL) return;

    int boneCount = glb->model.skeleton.boneCount;
    for (int boneIndex = 0; boneIndex < boneCount; boneIndex++)
    {
        glb->model.currentPose[boneIndex] = globalPose[boneIndex];

        Matrix bindPoseMatrix = GLBTransformToMatrix(glb->model.skeleton.bindPose[boneIndex]);
        Matrix currentPoseMatrix = GLBTransformToMatrix(glb->model.currentPose[boneIndex]);
        glb->model.boneMatrices[boneIndex] = MatrixMultiply(MatrixInvert(bindPoseMatrix), currentPoseMatrix);
    }

    GLBDataUpdateModelAnimationVertexBuffers(glb->model);
}

static float GLBMatrixMaxAbsDiff(Matrix a, Matrix b)
{
    const float* pa = (const float*)&a;
    const float* pb = (const float*)&b;
    float maxDiff = 0.0f;

    for (int i = 0; i < 16; i++)
    {
        float diff = fabsf(pa[i] - pb[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    return maxDiff;
}

static void GLBMeshUndoWorldTransform(Mesh* mesh, Matrix inverseWorldMatrix, Matrix inverseWorldNormalMatrix)
{
    if (mesh->vertices != NULL)
    {
        for (int vertexIndex = 0; vertexIndex < mesh->vertexCount; vertexIndex++)
        {
            int base = vertexIndex * 3;
            Vector3 vertex = { mesh->vertices[base + 0], mesh->vertices[base + 1], mesh->vertices[base + 2] };
            vertex = Vector3Transform(vertex, inverseWorldMatrix);
            mesh->vertices[base + 0] = vertex.x;
            mesh->vertices[base + 1] = vertex.y;
            mesh->vertices[base + 2] = vertex.z;
        }

        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION] != 0)
        {
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], mesh->vertices, mesh->vertexCount * 3 * sizeof(float), 0);
        }
    }

    if (mesh->normals != NULL)
    {
        for (int normalIndex = 0; normalIndex < mesh->vertexCount; normalIndex++)
        {
            int base = normalIndex * 3;
            Vector3 normal = { mesh->normals[base + 0], mesh->normals[base + 1], mesh->normals[base + 2] };
            normal = Vector3Normalize(Vector3Transform(normal, inverseWorldNormalMatrix));
            mesh->normals[base + 0] = normal.x;
            mesh->normals[base + 1] = normal.y;
            mesh->normals[base + 2] = normal.z;
        }

        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] != 0)
        {
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh->normals, mesh->vertexCount * 3 * sizeof(float), 0);
        }
    }

    if (mesh->tangents != NULL)
    {
        for (int tangentIndex = 0; tangentIndex < mesh->vertexCount; tangentIndex++)
        {
            int base = tangentIndex * 4;
            Vector3 tangent = { mesh->tangents[base + 0], mesh->tangents[base + 1], mesh->tangents[base + 2] };
            tangent = Vector3Normalize(Vector3Transform(tangent, inverseWorldMatrix));
            mesh->tangents[base + 0] = tangent.x;
            mesh->tangents[base + 1] = tangent.y;
            mesh->tangents[base + 2] = tangent.z;
        }

        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT] != 0)
        {
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT], mesh->tangents, mesh->vertexCount * 4 * sizeof(float), 0);
        }
    }

    if (mesh->animVertices != NULL && mesh->vertices != NULL)
    {
        memcpy(mesh->animVertices, mesh->vertices, mesh->vertexCount * 3 * sizeof(float));
    }

    if (mesh->animNormals != NULL && mesh->normals != NULL)
    {
        memcpy(mesh->animNormals, mesh->normals, mesh->vertexCount * 3 * sizeof(float));
    }
}

static void GLBDataUndoSkinnedMeshNodeTransforms(GLBData* data)
{
    if (data->sourceData == NULL || data->model.meshes == NULL) return;

    int meshIndex = 0;
    int correctedMeshCount = 0;

    for (cgltf_size nodeIndex = 0; nodeIndex < data->sourceData->nodes_count; nodeIndex++)
    {
        cgltf_node* node = &data->sourceData->nodes[nodeIndex];
        if (node->mesh == NULL) continue;

        cgltf_float worldTransform[16] = { 0 };
        cgltf_node_transform_world(node, worldTransform);
        Matrix worldMatrix = GLBMatrixFromCgltf(worldTransform);
        Matrix worldNormalMatrix = MatrixTranspose(MatrixInvert(worldMatrix));

        bool shouldUndoTransform = (node->skin != NULL) && (GLBMatrixMaxAbsDiff(worldMatrix, MatrixIdentity()) > 1e-6f);
        Matrix inverseWorldMatrix = shouldUndoTransform ? MatrixInvert(worldMatrix) : MatrixIdentity();
        Matrix inverseWorldNormalMatrix = shouldUndoTransform ? MatrixInvert(worldNormalMatrix) : MatrixIdentity();

        for (cgltf_size primitiveIndex = 0; primitiveIndex < node->mesh->primitives_count; primitiveIndex++)
        {
            if (node->mesh->primitives[primitiveIndex].type != cgltf_primitive_type_triangles) continue;
            if (meshIndex >= data->model.meshCount) return;

            if (shouldUndoTransform)
            {
                GLBMeshUndoWorldTransform(&data->model.meshes[meshIndex], inverseWorldMatrix, inverseWorldNormalMatrix);
                correctedMeshCount++;
            }

            meshIndex++;
        }
    }

    if (correctedMeshCount > 0)
    {
        printf("INFO: Removed baked node transforms from %d skinned GLB mesh primitives\n", correctedMeshCount);
    }
}

static Matrix GLBDataGetModelTransform(const GLBData* glb, float scale, bool inplace)
{
    Matrix transform = MatrixScale(scale, scale, scale);

    if (inplace && glb->model.currentPose != NULL && glb->topoOrder != NULL && glb->model.skeleton.boneCount > 0)
    {
        int rootBone = glb->topoOrder[0];
        Transform rootPose = glb->model.currentPose[rootBone];

        Quaternion yawRotation = { 0.0f, rootPose.rotation.y, 0.0f, rootPose.rotation.w };
        if (QuaternionLength(yawRotation) < 1e-8f) yawRotation = QuaternionIdentity();
        else yawRotation = QuaternionNormalize(yawRotation);

        Matrix translation = MatrixTranslate(-rootPose.translation.x * scale, 0.0f, -rootPose.translation.z * scale);
        Matrix rotation = QuaternionToMatrix(QuaternionInvert(yawRotation));
        transform = MatrixMultiply(transform, MatrixMultiply(translation, rotation));
    }

    return MatrixMultiply(glb->model.transform, transform);
}

static Transform GLBNodeLocalTransform(const cgltf_node* node)
{
    Transform transform = {
        .translation = { 0.0f, 0.0f, 0.0f },
        .rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .scale = { 1.0f, 1.0f, 1.0f }
    };

    if (node == NULL) return transform;

    if (node->has_matrix)
    {
        MatrixDecompose(GLBMatrixFromCgltf(node->matrix), &transform.translation, &transform.rotation, &transform.scale);
        transform.rotation = QuaternionNormalize(transform.rotation);
        return transform;
    }

    if (node->has_translation)
    {
        transform.translation = (Vector3){ node->translation[0], node->translation[1], node->translation[2] };
    }

    if (node->has_rotation)
    {
        transform.rotation = QuaternionNormalize((Quaternion){ node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] });
    }

    if (node->has_scale)
    {
        transform.scale = (Vector3){ node->scale[0], node->scale[1], node->scale[2] };
    }

    return transform;
}

static int GLBFindSkinJointIndex(const cgltf_skin* skin, const cgltf_node* node)
{
    if (skin == NULL || node == NULL) return -1;

    for (cgltf_size i = 0; i < skin->joints_count; i++)
    {
        if (skin->joints[i] == node) return (int)i;
    }

    return -1;
}

static bool GLBGetPoseAtTime(
    cgltf_interpolation_type interpolationType,
    cgltf_accessor* input,
    cgltf_accessor* output,
    float time,
    void* data)
{
    if (interpolationType >= cgltf_interpolation_type_max_enum) return false;
    if (input == NULL || output == NULL || input->count == 0) return false;

    float tstart = 0.0f;
    float tend = 0.0f;
    int keyframe = 0;

    if (input->count == 1)
    {
        if (!cgltf_accessor_read_float(input, 0, &tstart, 1)) return false;
        tend = tstart;
    }
    else
    {
        for (int i = 0; i < (int)input->count - 1; i++)
        {
            if (!cgltf_accessor_read_float(input, i, &tstart, 1)) return false;
            if (!cgltf_accessor_read_float(input, i + 1, &tend, 1)) return false;

            keyframe = i;
            if ((tstart <= time) && (time < tend)) break;
        }
    }

    if (FloatEquals(tend, tstart)) interpolationType = cgltf_interpolation_type_step;

    float duration = fmaxf(tend - tstart, EPSILON);
    float t = Clamp((time - tstart) / duration, 0.0f, 1.0f);

    if (output->component_type != cgltf_component_type_r_32f) return false;

    if (output->type == cgltf_type_vec3)
    {
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 3);
                *(Vector3*)data = (Vector3){ tmp[0], tmp[1], tmp[2] };
            } break;
            case cgltf_interpolation_type_linear:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 3);
                Vector3 v1 = { tmp[0], tmp[1], tmp[2] };
                cgltf_accessor_read_float(output, keyframe + 1, tmp, 3);
                Vector3 v2 = { tmp[0], tmp[1], tmp[2] };
                *(Vector3*)data = Vector3Lerp(v1, v2, t);
            } break;
            case cgltf_interpolation_type_cubic_spline:
            {
                float tmp[3] = { 0.0f };
                cgltf_accessor_read_float(output, 3*keyframe + 1, tmp, 3);
                Vector3 v1 = { tmp[0], tmp[1], tmp[2] };
                cgltf_accessor_read_float(output, 3*keyframe + 2, tmp, 3);
                Vector3 tangent1 = { tmp[0], tmp[1], tmp[2] };
                cgltf_accessor_read_float(output, 3*(keyframe + 1) + 1, tmp, 3);
                Vector3 v2 = { tmp[0], tmp[1], tmp[2] };
                cgltf_accessor_read_float(output, 3*(keyframe + 1), tmp, 3);
                Vector3 tangent2 = { tmp[0], tmp[1], tmp[2] };
                *(Vector3*)data = Vector3CubicHermite(v1, tangent1, v2, tangent2, t);
            } break;
            default: return false;
        }
    }
    else if (output->type == cgltf_type_vec4)
    {
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 4);
                *(Quaternion*)data = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] });
            } break;
            case cgltf_interpolation_type_linear:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, keyframe, tmp, 4);
                Quaternion v1 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] });
                cgltf_accessor_read_float(output, keyframe + 1, tmp, 4);
                Quaternion v2 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] });
                *(Quaternion*)data = QuaternionNormalize(QuaternionSlerp(v1, v2, t));
            } break;
            case cgltf_interpolation_type_cubic_spline:
            {
                float tmp[4] = { 0.0f };
                cgltf_accessor_read_float(output, 3*keyframe + 1, tmp, 4);
                Quaternion v1 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] });
                cgltf_accessor_read_float(output, 3*keyframe + 2, tmp, 4);
                Vector4 outTangent1 = { tmp[0], tmp[1], tmp[2], 0.0f };
                cgltf_accessor_read_float(output, 3*(keyframe + 1) + 1, tmp, 4);
                Quaternion v2 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] });
                cgltf_accessor_read_float(output, 3*(keyframe + 1), tmp, 4);
                Vector4 inTangent2 = { tmp[0], tmp[1], tmp[2], 0.0f };

                if (Vector4DotProduct(v1, v2) < 0.0f)
                {
                    v2 = Vector4Negate(v2);
                }

                outTangent1 = Vector4Scale(outTangent1, duration);
                inTangent2 = Vector4Scale(inTangent2, duration);
                *(Quaternion*)data = QuaternionNormalize(QuaternionCubicHermiteSpline(v1, outTangent1, v2, inTangent2, t));
            } break;
            default: return false;
        }
    }
    else return false;

    return true;
}

static bool GLBAnimationTargetsSkinJoint(const cgltf_skin* skin, const cgltf_node* node)
{
    if (skin == NULL || node == NULL) return false;

    for (cgltf_size i = 0; i < skin->joints_count; i++)
    {
        if (skin->joints[i] == node) return true;
    }

    return false;
}

static bool GLBDataLoadSourceTiming(GLBData* data, const char* filename)
{
    if (data->animCount <= 0) return true;

    cgltf_options options = { 0 };
    cgltf_result result = cgltf_parse_file(&options, filename, &data->sourceData);

    if (result != cgltf_result_success)
    {
        printf("WARN: Failed to parse GLB timing data for '%s'\n", filename);
        return false;
    }

    result = cgltf_load_buffers(&options, data->sourceData, filename);
    if (result != cgltf_result_success)
    {
        printf("WARN: Failed to load GLB timing buffers for '%s'\n", filename);
        cgltf_free(data->sourceData);
        data->sourceData = NULL;
        return false;
    }

    data->sourceSkin = (data->sourceData->skins_count > 0) ? &data->sourceData->skins[0] : NULL;

    int boneCount = data->model.skeleton.boneCount;
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        data->sourceRestPose[boneIdx] = (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
        data->sourceRootPose[boneIdx] = (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };

        if (data->sourceSkin != NULL && boneIdx < (int)data->sourceSkin->joints_count)
        {
            const cgltf_node* node = data->sourceSkin->joints[boneIdx];
            data->sourceRestPose[boneIdx] = GLBNodeLocalTransform(node);

            if (data->model.skeleton.bones[boneIdx].parent == -1 && node != NULL && node->parent != NULL)
            {
                cgltf_float worldMatrix[16] = { 0 };
                cgltf_node_transform_world(node->parent, worldMatrix);
                MatrixDecompose(GLBMatrixFromCgltf(worldMatrix),
                    &data->sourceRootPose[boneIdx].translation,
                    &data->sourceRootPose[boneIdx].rotation,
                    &data->sourceRootPose[boneIdx].scale);
                data->sourceRootPose[boneIdx].rotation = QuaternionNormalize(data->sourceRootPose[boneIdx].rotation);
            }
        }
    }

    const cgltf_skin* skin = data->sourceSkin;
    int parsedAnimCount = (data->animCount < (int)data->sourceData->animations_count) ? data->animCount : (int)data->sourceData->animations_count;

    for (int a = 0; a < parsedAnimCount; a++)
    {
        cgltf_animation* anim = &data->sourceData->animations[a];
        float duration = 0.0f;
        float minDelta = FLT_MAX;
        int maxInputCount = 0;
        bool hasTimeSamples = false;

        for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
        {
            cgltf_animation_channel* channel = &anim->channels[channelIdx];
            cgltf_animation_sampler* sampler = channel->sampler;

            if (skin != NULL && !GLBAnimationTargetsSkinJoint(skin, channel->target_node)) continue;
            if (sampler == NULL || sampler->input == NULL || sampler->input->count == 0) continue;

            int inputCount = (int)sampler->input->count;
            if (inputCount > maxInputCount) maxInputCount = inputCount;

            float prevTime = 0.0f;
            bool hasPrevTime = false;

            for (int sampleIdx = 0; sampleIdx < inputCount; sampleIdx++)
            {
                float time = 0.0f;
                if (!cgltf_accessor_read_float(sampler->input, sampleIdx, &time, 1)) break;

                hasTimeSamples = true;
                if (time > duration) duration = time;

                if (hasPrevTime)
                {
                    float delta = time - prevTime;
                    if (delta > 1e-6f && delta < minDelta) minDelta = delta;
                }

                prevTime = time;
                hasPrevTime = true;
            }
        }

        if (!hasTimeSamples) continue;

        int frameCount = maxInputCount > 0 ? maxInputCount : 1;
        float frameTime = data->frameTime;

        if (minDelta < FLT_MAX)
        {
            frameTime = minDelta;
            frameCount = 1 + (int)(duration / frameTime + 0.5f);
        }
        else if (frameCount > 1 && duration > 0.0f)
        {
            frameTime = duration / (float)(frameCount - 1);
        }

        if (frameCount < 1) frameCount = 1;
        if (frameTime <= 0.0f) frameTime = data->frameTime;

        data->sourceFrameCounts[a] = frameCount;
        data->sourceFrameTimes[a] = frameTime;
        data->sourceDurations[a] = duration;
    }

    return true;
}

// Compute topological order so every parent bone appears before its children.
// GLTF does not guarantee this ordering in the joints array.
// topoOrder[sortedIdx] = originalBoneIdx
// invTopoOrder[originalBoneIdx] = sortedIdx
static void ComputeTopoOrder(int boneCount, BoneInfo* bones, int* topoOrder, int* invTopoOrder)
{
    for (int i = 0; i < boneCount; i++) invTopoOrder[i] = -1;

    bool* placed = (bool*)calloc(boneCount, sizeof(bool));
    int idx = 0;
    while (idx < boneCount)
    {
        int prevIdx = idx;
        for (int i = 0; i < boneCount; i++)
        {
            if (placed[i]) continue;
            int p = bones[i].parent;
            if (p == -1 || invTopoOrder[p] != -1)
            {
                topoOrder[idx] = i;
                invTopoOrder[i] = idx;
                placed[i] = true;
                idx++;
            }
        }
        if (idx == prevIdx) break; // cycle guard
    }
    free(placed);
}

// Load a GLB/GLTF model and its animations
static bool GLBDataLoad(GLBData* data, const char* filename, char* errMsg, int errMsgSize)
{
    printf("INFO: Loading GLB '%s'\n", filename);

    // Check file extension
    const char* ext = strrchr(filename, '.');
    if (ext == NULL ||
        (strcmp(ext, ".glb") != 0 && strcmp(ext, ".GLB") != 0 &&
         strcmp(ext, ".gltf") != 0 && strcmp(ext, ".GLTF") != 0))
    {
        snprintf(errMsg, errMsgSize, "Error: File '%s' is not a .glb/.gltf file", filename);
        return false;
    }

    // Load model (this also loads the skeleton)
    data->model = LoadModel(filename);
    
    // Check that model has a skeleton (we only care about bones, not meshes)
    if (data->model.skeleton.boneCount == 0)
    {
        if (data->model.meshCount > 0) UnloadModel(data->model);
        snprintf(errMsg, errMsgSize, "Error: Model '%s' has no skeleton (no bones)", filename);
        printf("ERROR: %s\n", errMsg);
        return false;
    }

    // Load animations
    data->animations = LoadModelAnimations(filename, &data->animCount);
    if (data->animCount == 0)
    {
        UnloadModel(data->model);
        snprintf(errMsg, errMsgSize, "Error: Model '%s' has no animations", filename);
        printf("ERROR: %s\n", errMsg);
        return false;
    }

    data->activeAnim = 0;
    data->frameTime = 1.0f / 60.0f; // raylib resamples GLTF at GLTF_FRAMERATE = 60 fps
    data->sourceFrameCounts = (int*)malloc(data->animCount * sizeof(int));
    data->sourceFrameTimes = (float*)malloc(data->animCount * sizeof(float));
    data->sourceDurations = (float*)malloc(data->animCount * sizeof(float));

    int bc = data->model.skeleton.boneCount;
    data->sourceRestPose = (Transform*)malloc(bc * sizeof(Transform));
    data->sourceLocalPose = (Transform*)malloc(bc * sizeof(Transform));
    data->sourceGlobalPose = (Transform*)malloc(bc * sizeof(Transform));
    data->sourceRootPose = (Transform*)malloc(bc * sizeof(Transform));

    if (data->sourceFrameCounts == NULL || data->sourceFrameTimes == NULL || data->sourceDurations == NULL ||
        data->sourceRestPose == NULL || data->sourceLocalPose == NULL || data->sourceGlobalPose == NULL || data->sourceRootPose == NULL)
    {
        GLBDataFree(data);
        snprintf(errMsg, errMsgSize, "Error: Out of memory while loading animation timing for '%s'", filename);
        printf("ERROR: %s\n", errMsg);
        return false;
    }

    for (int a = 0; a < data->animCount; a++)
    {
        data->sourceFrameCounts[a] = data->animations[a].keyframeCount;
        data->sourceFrameTimes[a] = data->frameTime;
        data->sourceDurations[a] = (data->animations[a].keyframeCount - 1) * data->frameTime;
    }

    GLBDataLoadSourceTiming(data, filename);
    GLBDataUndoSkinnedMeshNodeTransforms(data);

    // Reload any textures that raylib failed to decode (raylib's libraylib.a was
    // compiled with SUPPORT_FILEFORMAT_JPG=0). Use our own stb_image which has
    // full JPEG/PNG/BMP/GIF/PSD/HDR support.
    {
        unsigned int defaultTexId = rlGetTextureIdDefault();
        for (int matIdx = 0; matIdx < data->model.materialCount; matIdx++)
        {
            Texture2D tex = data->model.materials[matIdx].maps[MATERIAL_MAP_ALBEDO].texture;
            bool alreadyLoaded = (tex.id > 0 && tex.id != defaultTexId && (tex.width > 1 || tex.height > 1));
            if (alreadyLoaded) continue;

            // Find the matching cgltf material (raylib material index = cgltf index + 1)
            if (data->sourceData == NULL) continue;
            int cgltfIdx = matIdx - 1;
            if (cgltfIdx < 0 || cgltfIdx >= (int)data->sourceData->materials_count) continue;

            cgltf_material* cgltfMat = &data->sourceData->materials[cgltfIdx];
            if (!cgltfMat->has_pbr_metallic_roughness) continue;

            cgltf_texture* cgltfTex = cgltfMat->pbr_metallic_roughness.base_color_texture.texture;
            if (cgltfTex == NULL || cgltfTex->image == NULL) continue;

            cgltf_image* cgltfImg = cgltfTex->image;

            // Re-read the image buffer by copying it respecting buffer_view stride
            if (cgltfImg->buffer_view == NULL || cgltfImg->buffer_view->buffer->data == NULL) continue;

            cgltf_buffer_view* bv = cgltfImg->buffer_view;
            int imgSize = (int)bv->size;
            if (imgSize <= 0) continue;

            unsigned char* imgData = (unsigned char*)malloc(imgSize);
            if (imgData == NULL) continue;

            int offset = (int)bv->offset;
            int stride = (int)(bv->stride ? bv->stride : 1);
            unsigned char* src = (unsigned char*)bv->buffer->data;
            for (int k = 0; k < imgSize; k++)
            {
                imgData[k] = src[offset];
                offset += stride;
            }

            int w, h, comp;
            unsigned char* pixels = stbi_load_from_memory(imgData, imgSize, &w, &h, &comp, 4);
            free(imgData);

            if (pixels != NULL)
            {
                Image img = { 0 };
                img.data = pixels;
                img.width = w;
                img.height = h;
                img.mipmaps = 1;
                img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                Texture2D newTex = LoadTextureFromImage(img);
                stbi_image_free(pixels);
                data->model.materials[matIdx].maps[MATERIAL_MAP_ALBEDO].texture = newTex;
                printf("INFO: Reloaded GLB texture (material %d) via stb_image: %dx%d\n", matIdx, w, h);
            }
        }
    }

    // Compute topological bone order (parent always before child)
    data->topoOrder    = (int*)malloc(bc * sizeof(int));
    data->invTopoOrder = (int*)malloc(bc * sizeof(int));
    ComputeTopoOrder(bc, data->model.skeleton.bones, data->topoOrder, data->invTopoOrder);

    printf("INFO: Loaded '%s' - %d bones, %d animations\n",
        filename, bc, data->animCount);

    return true;
}

static bool TransformDataSampleFrameGLBExact(
    TransformData* data,
    GLBData* glb,
    float time,
    float scale)
{
    if (glb->animCount == 0) return false;
    if (glb->sourceData == NULL || glb->sourceSkin == NULL) return false;
    if (glb->sourceRestPose == NULL || glb->sourceLocalPose == NULL || glb->sourceGlobalPose == NULL || glb->sourceRootPose == NULL) return false;
    if (glb->topoOrder == NULL) return false;

    int animIdx = glb->activeAnim;
    if (animIdx < 0 || animIdx >= (int)glb->sourceData->animations_count) return false;

    cgltf_animation* anim = &glb->sourceData->animations[animIdx];
    float duration = GLBDataGetSourceDuration(glb, animIdx);
    float timeClamped = Clamp(time, 0.0f, duration > 0.0f ? duration : time);

    int boneCount = glb->model.skeleton.boneCount;
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        glb->sourceLocalPose[boneIdx] = glb->sourceRestPose[boneIdx];
    }

    for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
    {
        cgltf_animation_channel* channel = &anim->channels[channelIdx];
        if (channel->sampler == NULL) return false;

        int boneIndex = GLBFindSkinJointIndex(glb->sourceSkin, channel->target_node);
        if (boneIndex < 0 || boneIndex >= boneCount) continue;

        switch (channel->target_path)
        {
            case cgltf_animation_path_type_translation:
            {
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].translation))
                    return false;
            } break;
            case cgltf_animation_path_type_rotation:
            {
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].rotation))
                    return false;
                glb->sourceLocalPose[boneIndex].rotation = QuaternionNormalize(glb->sourceLocalPose[boneIndex].rotation);
            } break;
            case cgltf_animation_path_type_scale:
            {
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].scale))
                    return false;
            } break;
            default: break;
        }
    }

    for (int sortedIdx = 0; sortedIdx < boneCount; sortedIdx++)
    {
        int boneIdx = glb->topoOrder[sortedIdx];
        int parentIdx = glb->model.skeleton.bones[boneIdx].parent;
        Transform localPose = glb->sourceLocalPose[boneIdx];

        if (parentIdx == -1)
        {
            Transform rootPose = glb->sourceRootPose[boneIdx];
            glb->sourceGlobalPose[boneIdx].rotation = QuaternionNormalize(QuaternionMultiply(rootPose.rotation, localPose.rotation));
            glb->sourceGlobalPose[boneIdx].scale = Vector3Multiply(localPose.scale, rootPose.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Multiply(localPose.translation, rootPose.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3RotateByQuaternion(glb->sourceGlobalPose[boneIdx].translation, rootPose.rotation);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Add(glb->sourceGlobalPose[boneIdx].translation, rootPose.translation);
        }
        else
        {
            Transform parentPose = glb->sourceGlobalPose[parentIdx];
            glb->sourceGlobalPose[boneIdx].rotation = QuaternionNormalize(QuaternionMultiply(parentPose.rotation, localPose.rotation));
            glb->sourceGlobalPose[boneIdx].scale = Vector3Multiply(localPose.scale, parentPose.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Multiply(localPose.translation, parentPose.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3RotateByQuaternion(glb->sourceGlobalPose[boneIdx].translation, parentPose.rotation);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Add(glb->sourceGlobalPose[boneIdx].translation, parentPose.translation);
        }
    }

    GLBDataUpdateModelPose(glb, glb->sourceGlobalPose);

    int n = data->jointCount;
    if (n > boneCount) n = boneCount;

    for (int i = 0; i < n; i++)
    {
        int orig = glb->topoOrder[i];
        Vector3 gPos = glb->sourceGlobalPose[orig].translation;
        Quaternion gRot = glb->sourceGlobalPose[orig].rotation;
        int parent = data->parents[i];

        if (parent == -1)
        {
            data->localPositions[i] = Vector3Scale(gPos, scale);
            data->localRotations[i] = gRot;
        }
        else
        {
            int origParent = glb->topoOrder[parent];
            Vector3 pPos = glb->sourceGlobalPose[origParent].translation;
            Quaternion pRot = glb->sourceGlobalPose[origParent].rotation;
            Quaternion invPRot = QuaternionInvert(pRot);

            Vector3 delta = Vector3Subtract(gPos, pPos);
            data->localPositions[i] = Vector3Scale(Vector3RotateByQuaternion(delta, invPRot), scale);
            data->localRotations[i] = QuaternionNormalize(QuaternionMultiply(invPRot, gRot));
        }
    }

    return true;
}

// Sample the current animation time from GLB into TransformData.
// Prefer exact glTF channel sampling; fall back to raylib's 60 fps sampled poses.
static void TransformDataSampleFrameGLB(
    TransformData* data,
    GLBData* glb,
    float time,
    float scale)
{
    if (TransformDataSampleFrameGLBExact(data, glb, time, scale)) return;

    if (glb->animCount == 0) { return; }
    if (glb->model.currentPose == NULL) { return; }
    if (glb->model.boneMatrices == NULL) { return; }
    if (glb->topoOrder == NULL) { return; }

    int animIdx = glb->activeAnim;
    ModelAnimation anim = glb->animations[animIdx];
    float frame = time / glb->frameTime;

    // Clamp to valid range while keeping fractional part for raylib's built-in lerp
    float frameClamped = frame;
    if (frameClamped < 0.0f) frameClamped = 0.0f;
    if (frameClamped > (float)(anim.keyframeCount - 1)) frameClamped = (float)(anim.keyframeCount - 1);

    UpdateModelAnimation(glb->model, anim, frameClamped);

    int n = data->jointCount;
    if (n > glb->model.skeleton.boneCount) n = glb->model.skeleton.boneCount;

    for (int i = 0; i < n; i++)
    {
        int orig = glb->topoOrder[i];
        Vector3 gPos    = glb->model.currentPose[orig].translation;
        Quaternion gRot = glb->model.currentPose[orig].rotation;
        int parent = data->parents[i];

        if (parent == -1)
        {
            data->localPositions[i] = Vector3Scale(gPos, scale);
            data->localRotations[i] = gRot;
        }
        else
        {
            int origParent       = glb->topoOrder[parent];
            Vector3 pPos         = glb->model.currentPose[origParent].translation;
            Quaternion pRot      = glb->model.currentPose[origParent].rotation;
            Quaternion invPRot   = QuaternionInvert(pRot);

            Vector3 delta = Vector3Subtract(gPos, pPos);
            data->localPositions[i] = Vector3Scale(Vector3RotateByQuaternion(delta, invPRot), scale);
            data->localRotations[i] = QuaternionMultiply(invPRot, gRot);
        }
    }
}

// Resize TransformData with explicit joint count and bone/parent info
// This is needed when loading GLB data (no BVHData available)
static inline void TransformDataResizeSimple(
    TransformData* data,
    int jointCount,
    int* parents,
    bool* endSite)
{
    data->jointCount = jointCount;
    data->parents = realloc(data->parents, jointCount * sizeof(int));
    data->endSite = realloc(data->endSite, jointCount * sizeof(bool));
    data->localPositions = realloc(data->localPositions, jointCount * sizeof(Vector3));
    data->localRotations = realloc(data->localRotations, jointCount * sizeof(Quaternion));
    data->globalPositions = realloc(data->globalPositions, jointCount * sizeof(Vector3));
    data->globalRotations = realloc(data->globalRotations, jointCount * sizeof(Quaternion));

    for (int i = 0; i < jointCount; i++)
    {
        data->parents[i] = parents[i];
        data->endSite[i] = endSite[i];
    }
}

//----------------------------------------------------------------------------------
// Character Data
//----------------------------------------------------------------------------------

// Maximum number of characters to allow in the scene
enum
{
    CHARACTERS_MAX = 6,
};

// All the data required for all of the characters we want to have in the scene
typedef struct {

    // Total number of characters
    int count;
    
    // Character which is "active" or selected
    int active;

    // Character BVH Data
    BVHData bvhData[CHARACTERS_MAX];
    
    // Scales of each character
    float scales[CHARACTERS_MAX];
    
    // Names of each character
    char names[CHARACTERS_MAX][128];
    
    // Automatic scaling for each character
    float autoScales[CHARACTERS_MAX];
    
    // Color of each character
    Color colors[CHARACTERS_MAX];
    
    // Opacity of each character
    float opacities[CHARACTERS_MAX];
    
    // Maximum capsule radius of each character
    float radii[CHARACTERS_MAX];
    
    // Original file path for each character
    char filePaths[CHARACTERS_MAX][512];
    
    // Transform buffers for each character
    TransformData xformData[CHARACTERS_MAX];
    TransformData xformTmp0[CHARACTERS_MAX];
    TransformData xformTmp1[CHARACTERS_MAX];
    TransformData xformTmp2[CHARACTERS_MAX];
    TransformData xformTmp3[CHARACTERS_MAX];
    
    // Joint combo string for each character
    char* jointNamesCombo[CHARACTERS_MAX];

    // If the color picker is active
    bool colorPickerActive;

    // True if any loaded character has a skinned mesh
    bool hasSkinnedMesh;
    
    // GLB-specific data
    bool isGLB[CHARACTERS_MAX];
    GLBData glbData[CHARACTERS_MAX];

} CharacterData;

// Initializes all the CharacterData to a safe state
static inline void CharacterDataInit(CharacterData* data, int argc, char** argv)
{  
    data->count = 0;
    data->active = 0;

    data->colors[0] = ArgColor(argc, argv, "-color0", ORANGE);
    data->colors[1] = ArgColor(argc, argv, "-color1", (Color){ 38, 134, 157, 255 });
    data->colors[2] = ArgColor(argc, argv, "-color2", PINK);
    data->colors[3] = ArgColor(argc, argv, "-color3", LIME);
    data->colors[4] = ArgColor(argc, argv, "-color4", VIOLET);
    data->colors[5] = ArgColor(argc, argv, "-color5", MAROON);

    srand(1234);
    for (int i = 6; i < CHARACTERS_MAX; i++)
    {
        data->colors[i] = (Color){ rand() % 255, rand() % 255, rand() % 255  };
    }

    for (int i = 0; i < CHARACTERS_MAX; i++)
    {
        BVHDataInit(&data->bvhData[i]);
        data->scales[i] = 1.0f;
        data->names[i][0] = '\0';
        data->autoScales[i] = 1.0f;
        data->opacities[i] = ArgFloat(argc, argv, "capsuleOpacity", 1.0f);
        data->radii[i] = ArgFloat(argc, argv, "maxCapsuleRadius", 0.04f);
        data->filePaths[i][0] = '\0';
        TransformDataInit(&data->xformData[i]);
        TransformDataInit(&data->xformTmp0[i]);
        TransformDataInit(&data->xformTmp1[i]);
        TransformDataInit(&data->xformTmp2[i]);
        TransformDataInit(&data->xformTmp3[i]);
        data->jointNamesCombo[i] = NULL;
        data->isGLB[i] = false;
        GLBDataInit(&data->glbData[i]);
    }

    data->colorPickerActive = ArgBool(argc, argv, "colorPickerActive", false);
    data->hasSkinnedMesh = false;
}

static inline void CharacterDataFree(CharacterData* data)
{
    for (int i = 0; i < data->count; i++)
    {
        if (data->isGLB[i])
        {
            GLBDataFree(&data->glbData[i]);
        }
        else
        {
            BVHDataFree(&data->bvhData[i]);
        }
        TransformDataFree(&data->xformData[i]);
        TransformDataFree(&data->xformTmp0[i]);
        TransformDataFree(&data->xformTmp1[i]);
        TransformDataFree(&data->xformTmp2[i]);
        TransformDataFree(&data->xformTmp3[i]);
        free(data->jointNamesCombo[i]);
    }
}

// Attempt to load a new character from the given file path
static bool CharacterDataLoadFromFile(
    CharacterData* data,
    const char* path,
    char* errMsg,
    int errMsgSize)
{
    printf("INFO: Loading '%s'\n", path);

    if (data->count == CHARACTERS_MAX)
    {
        snprintf(errMsg, 512, "Error: Maximum number of animation files loaded (%i)", CHARACTERS_MAX);
        return false;
    }

    // Detect file type by extension
    const char* ext = strrchr(path, '.');
    bool isGLB = (ext != NULL) && (strcmp(ext, ".glb") == 0 || strcmp(ext, ".GLB") == 0 ||
                  strcmp(ext, ".gltf") == 0 || strcmp(ext, ".GLTF") == 0);

    if (isGLB)
    {
        // --- GLB/GLTF loading path ---
        if (!GLBDataLoad(&data->glbData[data->count], path, errMsg, errMsgSize))
        {
            printf("INFO: Failed to Load '%s'\n", path);
            return false;
        }

        data->isGLB[data->count] = true;

        // Build parent and endSite arrays in topological order
        GLBData* glb = &data->glbData[data->count];
        int jointCount = glb->model.skeleton.boneCount;
        int* parents = malloc(jointCount * sizeof(int));
        bool* endSite = malloc(jointCount * sizeof(bool));
        for (int j = 0; j < jointCount; j++)
        {
            int origIdx    = glb->topoOrder[j];
            int origParent = glb->model.skeleton.bones[origIdx].parent;
            parents[j] = (origParent == -1) ? -1 : glb->invTopoOrder[origParent];
            endSite[j] = false;
        }

        TransformDataResizeSimple(&data->xformData[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp0[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp1[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp2[data->count], jointCount, parents, endSite);
        TransformDataResizeSimple(&data->xformTmp3[data->count], jointCount, parents, endSite);

        free(parents);
        free(endSite);

        snprintf(data->filePaths[data->count], 512, "%s", path);

        const char* filename = path;
        while (strchr(filename, '/')) { filename = strchr(filename, '/') + 1; }
        while (strchr(filename, '\\')) { filename = strchr(filename, '\\') + 1; }

        snprintf(data->names[data->count], 128, "%s", filename);

        // Joint names combo in topological order (matches xformData indexing)
        int comboTotalSize = 0;
        for (int j = 0; j < jointCount; j++)
        {
            int origIdx = glb->topoOrder[j];
            comboTotalSize += (j > 0 ? 1 : 0) + (int)strlen(glb->model.skeleton.bones[origIdx].name);
        }
        comboTotalSize++;

        data->jointNamesCombo[data->count] = malloc(comboTotalSize);
        data->jointNamesCombo[data->count][0] = '\0';
        for (int j = 0; j < jointCount; j++)
        {
            int origIdx = glb->topoOrder[j];
            if (j > 0) strcat(data->jointNamesCombo[data->count], ";");
            strcat(data->jointNamesCombo[data->count], glb->model.skeleton.bones[origIdx].name);
        }

        // Sample frame 0 at unit scale to measure skeleton height for auto-scale
        TransformDataSampleFrameGLB(&data->xformData[data->count], glb, 0, 1.0f);
        TransformDataForwardKinematics(&data->xformData[data->count]);

        float height = 1e-8f;
        for (int j = 0; j < data->xformData[data->count].jointCount; j++)
        {
            height = Max(height, data->xformData[data->count].globalPositions[j].y);
        }
        data->scales[data->count]    = height > 10.0f ? 0.01f : 1.0f;
        data->autoScales[data->count] = 1.8f / height;

        // Re-sample with detected scale so initial pose is correct
        TransformDataSampleFrameGLB(&data->xformData[data->count], glb, 0, data->scales[data->count]);
        TransformDataForwardKinematics(&data->xformData[data->count]);

        data->count++;

        // Auto-toggle: skinned mesh loaded -> draw mesh, not capsules
        if (glb->model.meshCount > 0)
        {
            data->hasSkinnedMesh = true;
        }

        return true;
    }
    else
    {
        // --- BVH loading path ---
        if (BVHDataLoad(&data->bvhData[data->count], path, errMsg, errMsgSize))
        {
            data->isGLB[data->count] = false;

            TransformDataResize(&data->xformData[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp0[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp1[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp2[data->count], &data->bvhData[data->count]);
            TransformDataResize(&data->xformTmp3[data->count], &data->bvhData[data->count]);

            snprintf(data->filePaths[data->count], 512, "%s", path);

            const char* filename = path;
            while (strchr(filename, '/')) { filename = strchr(filename, '/') + 1; }
            while (strchr(filename, '\\')) { filename = strchr(filename, '\\') + 1; }

            snprintf(data->names[data->count], 128, "%s", filename);
            data->scales[data->count] = 1.0f;

            // Auto-Scaling and unit detection

            if (data->bvhData[data->count].frameCount > 0)
            {
                TransformDataSampleFrame(&data->xformData[data->count], &data->bvhData[data->count], 0, 1.0f);
                TransformDataForwardKinematics(&data->xformData[data->count]);

                float height = 1e-8f;
                for (int j = 0; j < data->xformData[data->count].jointCount; j++)
                {
                    height = Max(height, data->xformData[data->count].globalPositions[j].y);
                }

                data->scales[data->count] = height > 10.0f ? 0.01f : 1.0f;
                data->autoScales[data->count] = 1.8 / height;
            }
            else
            {
                data->autoScales[data->count] = 1.0f;
            }
            
            // Joint names combo

            int comboTotalSize = 0;
            for (int i = 0; i < data->bvhData[data->count].jointCount; i++)
            {
                comboTotalSize += (i > 0 ? 1 : 0) + strlen(data->bvhData[data->count].joints[i].name);
            }
            comboTotalSize++;

            data->jointNamesCombo[data->count] = malloc(comboTotalSize);
            data->jointNamesCombo[data->count][0] = '\0';
            for (int i = 0; i < data->bvhData[data->count].jointCount; i++)
            {
                if (i > 0)
                {
                    strcat(data->jointNamesCombo[data->count], ";");
                }
                strcat(data->jointNamesCombo[data->count], data->bvhData[data->count].joints[i].name);
            }

            // Done

            data->count++;

            return true;
        }
        else
        {
            printf("INFO: Failed to Load '%s'\n", path);
            return false;
        }
    }
}

//----------------------------------------------------------------------------------
// Geometric Functions
//----------------------------------------------------------------------------------

// Returns the time parameter along a line segment closest to another point
static inline float NearestPointOnLineSegment(
    Vector3 lineStart,
    Vector3 lineVector,
    Vector3 point)
{
    Vector3 ap = Vector3Subtract(point, lineStart);
    float lengthsq = Vector3LengthSqr(lineVector);
    return lengthsq < 1e-8f ? 0.5f : Saturate(Vector3DotProduct(lineVector, ap) / lengthsq);
}

// Returns the time parameters along two line segments at the closest point between the two
static inline void NearestPointBetweenLineSegments(
    float* nearestTime0,
    float* nearestTime1,
    Vector3 line0Start,
    Vector3 line0End,
    Vector3 line1Start,
    Vector3 line1End)
{
    Vector3 line0Vec = Vector3Subtract(line0End, line0Start);
    Vector3 line1Vec = Vector3Subtract(line1End, line1Start);
    float d0 = Vector3LengthSqr(Vector3Subtract(line1Start, line0Start));
    float d1 = Vector3LengthSqr(Vector3Subtract(line1End, line0Start));
    float d2 = Vector3LengthSqr(Vector3Subtract(line1Start, line0End));
    float d3 = Vector3LengthSqr(Vector3Subtract(line1End, line0End));

    *nearestTime0 = (d2 < d0 || d2 < d1 || d3 < d0 || d3 < d1) ? 1.0f : 0.0f;
    *nearestTime1 = NearestPointOnLineSegment(line1Start, line1Vec, Vector3Add(line0Start, Vector3Scale(line0Vec, *nearestTime0)));
    *nearestTime0 = NearestPointOnLineSegment(line0Start, line0Vec, Vector3Add(line1Start, Vector3Scale(line1Vec, *nearestTime1)));
}

// Returns the time parameter for a line segment closest to the plane
static inline float NearestPointBetweenLineSegmentAndPlane(Vector3 lineStart, Vector3 lineVector, Vector3 planePosition, Vector3 planeNormal)
{
    float denom = Vector3DotProduct(planeNormal, lineVector);
    if (fabs(denom) < 1e-8f)
    {
        return 0.5f;
    }
  
    return Saturate(Vector3DotProduct(Vector3Subtract(planePosition, lineStart), planeNormal) / denom);
}

// Returns the time parameter for a line segment closest to the ground plane
static inline float NearestPointBetweenLineSegmentAndGroundPlane(Vector3 lineStart, Vector3 lineVector)
{
    return fabs(lineVector.y) < 1e-8f ? 0.5f : Saturate((-lineStart.y) / lineVector.y);
}

// Returns the time parameter and nearest point on the ground between a line segment and ground segment 
static inline void NearestPointBetweenLineSegmentAndGroundSegment(
    float* nearestTimeOnLine,
    Vector3* nearestPointOnGround,
    Vector3 lineStart,
    Vector3 lineEnd,
    Vector3 groundMins,
    Vector3 groundMaxs)
{
    Vector3 lineVec = Vector3Subtract(lineEnd, lineStart);
  
    // Check Against Plane

    *nearestTimeOnLine = NearestPointBetweenLineSegmentAndGroundPlane(lineStart, lineVec);
    *nearestPointOnGround = (Vector3){
        lineStart.x + (*nearestTimeOnLine) * lineVec.x,
        0.0f,
        lineStart.z + (*nearestTimeOnLine) * lineVec.z,
    };

    // If point is inside plane bounds it must be the nearest

    if (nearestPointOnGround->x >= groundMins.x &&
        nearestPointOnGround->x <= groundMaxs.x &&
        nearestPointOnGround->z >= groundMins.z &&
        nearestPointOnGround->z <= groundMaxs.z)
    {
        return;
    }

    // Check against four edges

    Vector3 edgeStart0 =  (Vector3){ groundMins.x, 0.0f, groundMins.z };
    Vector3 edgeEnd0 = (Vector3){ groundMins.x, 0.0f, groundMaxs.z };
    
    Vector3 edgeStart1 = (Vector3){ groundMins.x, 0.0f, groundMaxs.z };
    Vector3 edgeEnd1 = (Vector3){ groundMaxs.x, 0.0f, groundMaxs.z };
    
    Vector3 edgeStart2 = (Vector3){ groundMaxs.x, 0.0f, groundMaxs.z };
    Vector3 edgeEnd2 = (Vector3){ groundMaxs.x, 0.0f, groundMins.z };
    
    Vector3 edgeStart3 = (Vector3){ groundMaxs.x, 0.0f, groundMins.z };
    Vector3 edgeEnd3 = (Vector3){ groundMins.x, 0.0f, groundMins.z };

    float nearestTimeOnLine0, nearestTimeOnLine1, nearestTimeOnLine2, nearestTimeOnLine3;
    float nearestTimeOnEdge0, nearestTimeOnEdge1, nearestTimeOnEdge2, nearestTimeOnEdge3;

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine0,
        &nearestTimeOnEdge0,
        lineStart, lineEnd,
        edgeStart0, edgeEnd0);

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine1,
        &nearestTimeOnEdge1,
        lineStart, lineEnd,
        edgeStart1, edgeEnd1);

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine2,
        &nearestTimeOnEdge2,
        lineStart, lineEnd,
        edgeStart2, edgeEnd2);

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine3,
        &nearestTimeOnEdge3,
        lineStart, lineEnd,
        edgeStart3, edgeEnd3);

    Vector3 nearestPointOnLine0 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine0));
    Vector3 nearestPointOnLine1 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine1));
    Vector3 nearestPointOnLine2 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine2));
    Vector3 nearestPointOnLine3 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine3));
    
    Vector3 nearestPointOnEdge0 = Vector3Add(edgeStart0, Vector3Scale(Vector3Subtract(edgeEnd0, edgeStart0), nearestTimeOnEdge0));
    Vector3 nearestPointOnEdge1 = Vector3Add(edgeStart1, Vector3Scale(Vector3Subtract(edgeEnd1, edgeStart1), nearestTimeOnEdge1));
    Vector3 nearestPointOnEdge2 = Vector3Add(edgeStart2, Vector3Scale(Vector3Subtract(edgeEnd2, edgeStart2), nearestTimeOnEdge2));
    Vector3 nearestPointOnEdge3 = Vector3Add(edgeStart3, Vector3Scale(Vector3Subtract(edgeEnd3, edgeStart3), nearestTimeOnEdge3));

    float distance0 = Vector3Distance(nearestPointOnLine0, nearestPointOnEdge0);
    float distance1 = Vector3Distance(nearestPointOnLine1, nearestPointOnEdge1);
    float distance2 = Vector3Distance(nearestPointOnLine2, nearestPointOnEdge2);
    float distance3 = Vector3Distance(nearestPointOnLine3, nearestPointOnEdge3);

    if (distance0 <= distance1 && distance0 <= distance2 && distance0 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine0;
        *nearestPointOnGround = nearestPointOnEdge0;
        return;
    }

    if (distance1 <= distance0 && distance1 <= distance2 && distance1 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine1;
        *nearestPointOnGround = nearestPointOnEdge1;
        return;
    }

    if (distance2 <= distance0 && distance2 <= distance1 && distance2 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine2;
        *nearestPointOnGround = nearestPointOnEdge2;
        return;
    }

    if (distance3 <= distance0 && distance3 <= distance1 && distance3 <= distance2)
    {
        *nearestTimeOnLine = nearestTimeOnLine3;
        *nearestPointOnGround = nearestPointOnEdge3;
        return;
    }
    
    assert(false);
    *nearestTimeOnLine = nearestTimeOnLine0;
    *nearestPointOnGround = nearestPointOnEdge1;
    return;
}

static inline Vector3 ProjectPointOntoSweptLine(Vector3 sweptLineStart, Vector3 sweptLineVec, Vector3 sweptLineSweepVec, Vector3 position)
{
    Vector3 w = Vector3Subtract(position, sweptLineStart);
    Vector3 u = Vector3Normalize(sweptLineVec);
    Vector3 v = Vector3Normalize(sweptLineSweepVec);
    
    // x (u * u) + y (u * v) = w * u
    // x (v * u) + y (v * v) = w * v
    
    // Solved using Cramer's Rule in 2D
    float a1 = Vector3DotProduct(u, u);
    float b1 = Vector3DotProduct(u, v);
    float c1 = Vector3DotProduct(w, u);
    float a2 = Vector3DotProduct(v, u);
    float b2 = Vector3DotProduct(v, v);
    float c2 = Vector3DotProduct(w, v);
    
    float x = ((c1 * b2) - (b1 * c2)) / (a1 * b2 - b1 * a2);
    float y = (c1 - x * a1) / b1;
    
    x = Clamp(x, 0.0f, Vector3Length(sweptLineVec));
    y = Clamp(y, 0.0f, Vector3Length(sweptLineSweepVec));
    
    return Vector3Add(sweptLineStart, Vector3Add(Vector3Scale(u, x), Vector3Scale(v, y)));
}

// Returns the time parameter and nearest point on between a line segment and swept line segment
static inline void NearestPointBetweenLineSegmentAndSweptLine(
    float* nearestTimeOnLine,
    Vector3* nearestPointOnSweptLine,
    Vector3 lineStart,
    Vector3 lineEnd,
    Vector3 sweptLineStart,
    Vector3 sweptLineEnd,
    Vector3 sweptLineSweepVector)
{
    Vector3 lineVec = Vector3Subtract(lineEnd, lineStart);
    Vector3 sweptLineVec = Vector3Subtract(sweptLineEnd, sweptLineStart);
   
    Vector3 planeNormal = Vector3Length(sweptLineVec) < 1e-8f ? 
        Vector3Normalize(Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f }, sweptLineSweepVector)) :
        Vector3Normalize(Vector3CrossProduct(sweptLineVec, sweptLineSweepVector));
    
    // Check Against Plane

    float nearestTimeOnLine0 = NearestPointBetweenLineSegmentAndPlane(lineStart, lineVec, sweptLineStart, planeNormal);
    Vector3 nearestPointOnLine0 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine0));
    
    Vector3 nearestPointOnSweptLine0;
    
    if (Vector3Length(sweptLineVec) > 1e-8f)
    {
        nearestPointOnSweptLine0 = ProjectPointOntoSweptLine(
            sweptLineStart, 
            sweptLineVec, 
            sweptLineSweepVector, 
            nearestPointOnLine0);
    }
    else
    {
        float nearestTimeOnSweptLine = NearestPointOnLineSegment(
            sweptLineStart,
            sweptLineSweepVector,
            nearestPointOnLine0);
            
        nearestPointOnSweptLine0 = Vector3Add(sweptLineStart, Vector3Scale(sweptLineSweepVector, nearestTimeOnSweptLine));
    }
    
    float distance0 = Vector3Distance(nearestPointOnLine0, nearestPointOnSweptLine0);
    
    // Check against three edges

    Vector3 edgeStart1 = sweptLineStart;
    Vector3 edgeEnd1 = Vector3Add(sweptLineStart, sweptLineSweepVector);
    
    Vector3 edgeStart2 = sweptLineEnd;
    Vector3 edgeEnd2 = Vector3Add(sweptLineEnd, sweptLineSweepVector);
    
    Vector3 edgeStart3 = sweptLineStart;
    Vector3 edgeEnd3 = sweptLineEnd;

    float nearestTimeOnLine1, nearestTimeOnLine2, nearestTimeOnLine3;
    float nearestTimeOnEdge1, nearestTimeOnEdge2, nearestTimeOnEdge3;

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine1,
        &nearestTimeOnEdge1,
        lineStart, lineEnd,
        edgeStart1, edgeEnd1);

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine2,
        &nearestTimeOnEdge2,
        lineStart, lineEnd,
        edgeStart2, edgeEnd2);

    NearestPointBetweenLineSegments(
        &nearestTimeOnLine3,
        &nearestTimeOnEdge3,
        lineStart, lineEnd,
        edgeStart3, edgeEnd3);

    Vector3 nearestPointOnLine1 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine1));
    Vector3 nearestPointOnLine2 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine2));
    Vector3 nearestPointOnLine3 = Vector3Add(lineStart, Vector3Scale(lineVec, nearestTimeOnLine3));
    
    Vector3 nearestPointOnSweptLine1 = Vector3Add(edgeStart1, Vector3Scale(Vector3Subtract(edgeEnd1, edgeStart1), nearestTimeOnEdge1));
    Vector3 nearestPointOnSweptLine2 = Vector3Add(edgeStart2, Vector3Scale(Vector3Subtract(edgeEnd2, edgeStart2), nearestTimeOnEdge2));
    Vector3 nearestPointOnSweptLine3 = Vector3Add(edgeStart3, Vector3Scale(Vector3Subtract(edgeEnd3, edgeStart3), nearestTimeOnEdge3));

    float distance1 = Vector3Distance(nearestPointOnLine1, nearestPointOnSweptLine1);
    float distance2 = Vector3Distance(nearestPointOnLine2, nearestPointOnSweptLine2);
    float distance3 = Vector3Distance(nearestPointOnLine3, nearestPointOnSweptLine3);

    if (distance0 <= distance1 && distance0 <= distance2 && distance0 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine0;
        *nearestPointOnSweptLine = nearestPointOnSweptLine0;
        return;
    }

    if (distance1 <= distance0 && distance1 <= distance2 && distance1 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine1;
        *nearestPointOnSweptLine = nearestPointOnSweptLine1;
        return;
    }

    if (distance2 <= distance0 && distance2 <= distance1 && distance2 <= distance3)
    {
        *nearestTimeOnLine = nearestTimeOnLine2;
        *nearestPointOnSweptLine = nearestPointOnSweptLine2;
        return;
    }

    if (distance3 <= distance0 && distance3 <= distance1 && distance3 <= distance2)
    {
        *nearestTimeOnLine = nearestTimeOnLine3;
        *nearestPointOnSweptLine = nearestPointOnSweptLine3;
        return;
    }
    
    // Unreachable
    assert(false);
    *nearestTimeOnLine = nearestTimeOnLine0;
    *nearestPointOnSweptLine = nearestPointOnSweptLine0;
    return;
}

// Analytical capsule and sphere occlusion functions taken from here:
// https://www.shadertoy.com/view/3stcD4


// This is the number of times the radius away from the sphere where
// the ambient occlusion drops off to zero. This is important for various
// acceleration methods to filter out capsules which are too far away and
// so not casting any ambient occlusion
#define AO_RATIO_MAX 4.0

static inline float SphereOcclusionLookup(float nlAngle, float h)
{
    float nl = cosf(nlAngle);
    float h2 = h*h;
    
    float res = Max(nl, 0.0) / h2;
    float k2 = 1.0 - h2*nl*nl;
    if (k2 > 1e-4f)
    {
        res = nl * acosf(Clamp(-nl*sqrtf((h2 - 1.0f) / Max(1.0f - nl*nl, 1e-8f)), -1.0f, 1.0f)) - sqrtf(k2*(h2 - 1.0f));
        res = (res / h2 + atanf(sqrt(k2 / (h2 - 1.0f)))) / PI;
    }

    float decay = Max(1.0f - (h - 1.0f) / ((float)AO_RATIO_MAX - 1.0f), 0.0f);
    
    return 1.0f - res * decay;
}

static inline float SphereOcclusion(Vector3 pos, Vector3 nor, Vector3 sph, float rad)
{
    Vector3 di = Vector3Subtract(sph, pos);
    float l = Vector3Length(di);
    float nlAngle = acosf(Clamp(Vector3DotProduct(nor, Vector3Scale(di, 1.0f / Max(l, 1e-8f))), -1.0f, 1.0f));
    float h  = l < rad ? 1.0 : l / rad;
    return SphereOcclusionLookup(nlAngle, h);
}

static inline float SphereIntersectionArea(float r1, float r2, float d)
{
    if (Min(r1, r2) <= Max(r1, r2) - d)
    {
        return 1.0f - Max(cosf(r1), cosf(r2));
    }
    else if (r1 + r2 <= d)
    {
        return 0.0f;
    }

    float delta = fabs(r1 - r2);
    float x = 1.0f - Saturate((d - delta) / Max(r1 + r2 - delta, 1e-8f));
    float area = Square(x) * (-2.0f * x + 3.0f);

    return area * (1.0f - Max(cosf(r1), cosf(r2)));
}

static inline float SphereDirectionalOcclusionLookup(float phi, float theta, float coneAngle)
{
    return 1.0f - SphereIntersectionArea(theta, coneAngle / 2.0f, phi) / (1.0f - cosf(coneAngle / 2.0f));
}

static inline float SphereDirectionalOcclusion(
    Vector3 pos, 
    Vector3 sphere, 
    float radius,
    Vector3 coneDir, 
    float coneAngle)
{
    Vector3 occluder = Vector3Subtract(sphere, pos);
    float occluderLen2 = Vector3DotProduct(occluder, occluder);
    Vector3 occluderDir = Vector3Scale(occluder, 1.0f / Max(sqrtf(occluderLen2), 1e-8f));

    float phi = acosf(Clamp(Vector3DotProduct(occluderDir, Vector3Negate(coneDir)), -1.0f, 1.0f));
    float theta = acosf(Clamp(sqrtf(occluderLen2 / (Square(radius) + occluderLen2)), -1.0f, 1.0f));
    
    return SphereDirectionalOcclusionLookup(phi, theta, coneAngle);
}

// Get the start point of the capsule line segment
static inline Vector3 CapsuleStart(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    return Vector3Add(capsulePosition,
        Vector3RotateByQuaternion((Vector3){+capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation));
}

// Get the end point of the capsule line segment
static inline Vector3 CapsuleEnd(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    return Vector3Add(capsulePosition,
        Vector3RotateByQuaternion((Vector3){-capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation));
}

// Get the vector from the start to the end of the capsule line segment
static inline Vector3 CapsuleVector(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);

    return Vector3Subtract(Vector3Add(capsulePosition,
        Vector3RotateByQuaternion((Vector3){-capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation)), capsuleStart);
}

static inline float CapsuleDirectionalOcclusion(
    Vector3 pos, Vector3 capStart, Vector3 capVec,
    float capRadius, Vector3 coneDir, float coneAngle)
{
    Vector3 ba = capVec;
    Vector3 pa = Vector3Subtract(capStart, pos);
    Vector3 cba = Vector3Subtract(Vector3Scale(Vector3Negate(coneDir), Vector3DotProduct(Vector3Negate(coneDir), ba)), ba);
    float t = Saturate(Vector3DotProduct(pa, cba) / Max(Vector3DotProduct(cba, cba), 1e-8f));

    return SphereDirectionalOcclusion(pos, Vector3Add(capStart, Vector3Scale(ba, t)), capRadius, coneDir, coneAngle);
}

//----------------------------------------------------------------------------------
// Capsule Data
//----------------------------------------------------------------------------------

// Basic type useful for sorting according to some value
typedef struct
{
    int index;
    float value;
} CapsuleSort;

static inline int CapsuleSortCompareGreater(const void* lhs, const void* rhs)
{
    const CapsuleSort* lhsSort = lhs;
    const CapsuleSort* rhsSort = rhs;
    return lhsSort->value > rhsSort->value ? 1 : -1;
}

static inline int CapsuleSortCompareLess(const void* lhs, const void* rhs)
{
    const CapsuleSort* lhsSort = lhs;
    const CapsuleSort* rhsSort = rhs;
    return lhsSort->value < rhsSort->value ? 1 : -1;
}

// Structure containing all of the data required for all of the capsules which are to be rendered.
typedef struct
{
    // Data for all the capsules which are in the scene
    int capsuleCount;
    Vector3* capsulePositions;
    Quaternion* capsuleRotations;
    float* capsuleRadii;
    float* capsuleHalfLengths;
    Vector3* capsuleColors;
    float* capsuleOpacities;
    CapsuleSort* capsuleSort;

    // Buffers for all the capsules casting ambient occlusion
    int aoCapsuleCount;
    Vector3* aoCapsuleStarts;
    Vector3* aoCapsuleVectors;
    float* aoCapsuleRadii;
    CapsuleSort* aoCapsuleSort;

    // Buffers for all the capsules casting shadows
    int shadowCapsuleCount;
    Vector3* shadowCapsuleStarts;
    Vector3* shadowCapsuleVectors;
    float* shadowCapsuleRadii;
    CapsuleSort* shadowCapsuleSort;
    
    // Lookup table for the capsule ambient occlusion function
    Image aoLookupImage;
    Texture2D aoLookupTable;
    Vector2 aoLookupResolution;

    // Lookup table for the capsule shadow function
    Image shadowLookupImage;
    Texture2D shadowLookupTable;
    Vector2 shadowLookupResolution;

} CapsuleData;

// Compute the capsule ambient occlusion lookup table
static void CapsuleDataUpdateAOLookupTable(CapsuleData* data)
{
    int width = (int)data->aoLookupResolution.x;
    int height = (int)data->aoLookupResolution.y;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float nlAngle = (((float)x) / (width - 1)) * PI;
            float h = 1.0f + (AO_RATIO_MAX - 1.0f) * (((float)y) / (height - 1));
            ((unsigned char*)data->aoLookupImage.data)[y * width + x] = (unsigned char)Clamp(255.0 * SphereOcclusionLookup(nlAngle, h), 0.0, 255.0);
        }
    }

    UpdateTexture(data->aoLookupTable, data->aoLookupImage.data);
}

// Compute the capsule shadow lookup table for a given coneAngle
static void CapsuleDataUpdateShadowLookupTable(CapsuleData* data, float coneAngle)
{
    int width = (int)data->shadowLookupResolution.x;
    int height = (int)data->shadowLookupResolution.y;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float phi = (((float)x) / (width - 1)) * PI;
            float theta = ((float)y) / (height - 1) * (PI / 2.0f);
            ((unsigned char*)data->shadowLookupImage.data)[y * width + x] = (unsigned char)Clamp(255.0 * SphereDirectionalOcclusionLookup(phi, theta, coneAngle), 0.0, 255.0);
        }
    }
    
    UpdateTexture(data->shadowLookupTable, data->shadowLookupImage.data);
}

static void CapsuleDataInit(CapsuleData* data)
{
    // Init
  
    data->capsuleCount = 0;
    data->capsulePositions = NULL;
    data->capsuleRotations = NULL;
    data->capsuleRadii = NULL;
    data->capsuleHalfLengths = NULL;
    data->capsuleColors = NULL;
    data->capsuleOpacities = NULL;
    data->capsuleSort = NULL;

    data->aoCapsuleCount = 0;
    data->aoCapsuleStarts = NULL;
    data->aoCapsuleVectors = NULL;
    data->aoCapsuleRadii = NULL;
    data->aoCapsuleSort = NULL;

    data->shadowCapsuleCount = 0;
    data->shadowCapsuleStarts = NULL;
    data->shadowCapsuleVectors = NULL;
    data->shadowCapsuleRadii = NULL;
    data->shadowCapsuleSort = NULL;
    
    // Capsule AO Lookup Table
    
    data->aoLookupImage = (Image){
        .data = calloc(32 * 32, 1),
        .width = 32,
        .height = 32,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
        .mipmaps = 1
    };
    
    data->aoLookupTable = LoadTextureFromImage(data->aoLookupImage);
    data->aoLookupResolution = (Vector2){ (float)data->aoLookupImage.width, (float)data->aoLookupImage.height };
    SetTextureWrap(data->aoLookupTable, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(data->aoLookupTable, TEXTURE_FILTER_BILINEAR);
    
    CapsuleDataUpdateAOLookupTable(data);
    
    // Capsule Shadow Lookup Table
    
    data->shadowLookupImage = (Image){
        .data = calloc(256 * 128, 1),
        .width = 256,
        .height = 128,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
        .mipmaps = 1
    };
    
    data->shadowLookupTable = LoadTextureFromImage(data->shadowLookupImage);
    data->shadowLookupResolution = (Vector2){ (float)data->shadowLookupImage.width, (float)data->shadowLookupImage.height };
    SetTextureWrap(data->shadowLookupTable, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(data->shadowLookupTable, TEXTURE_FILTER_BILINEAR);
    
    CapsuleDataUpdateShadowLookupTable(data, 0.2f);
}

static void CapsuleDataResize(CapsuleData* data, int maxCapsuleCount)
{
    data->capsulePositions = realloc(data->capsulePositions, maxCapsuleCount * sizeof(Vector3));
    data->capsuleRotations = realloc(data->capsuleRotations, maxCapsuleCount * sizeof(Quaternion));
    data->capsuleRadii = realloc(data->capsuleRadii, maxCapsuleCount * sizeof(float));
    data->capsuleHalfLengths = realloc(data->capsuleHalfLengths, maxCapsuleCount * sizeof(float));
    data->capsuleColors = realloc(data->capsuleColors, maxCapsuleCount * sizeof(Vector3));
    data->capsuleOpacities = realloc(data->capsuleOpacities, maxCapsuleCount * sizeof(float));
    data->capsuleSort = realloc(data->capsuleSort, maxCapsuleCount * sizeof(CapsuleSort));

    data->aoCapsuleStarts = realloc(data->aoCapsuleStarts, maxCapsuleCount * sizeof(Vector3));
    data->aoCapsuleVectors = realloc(data->aoCapsuleVectors, maxCapsuleCount * sizeof(Vector3));
    data->aoCapsuleRadii = realloc(data->aoCapsuleRadii, maxCapsuleCount * sizeof(float));
    data->aoCapsuleSort = realloc(data->aoCapsuleSort, maxCapsuleCount * sizeof(CapsuleSort));

    data->shadowCapsuleStarts = realloc(data->shadowCapsuleStarts, maxCapsuleCount * sizeof(Vector3));
    data->shadowCapsuleVectors = realloc(data->shadowCapsuleVectors, maxCapsuleCount * sizeof(Vector3));
    data->shadowCapsuleRadii = realloc(data->shadowCapsuleRadii, maxCapsuleCount * sizeof(float));
    data->shadowCapsuleSort = realloc(data->shadowCapsuleSort, maxCapsuleCount * sizeof(CapsuleSort));
}

static void CapsuleDataFree(CapsuleData* data)
{
    free(data->capsulePositions);
    free(data->capsuleRotations);
    free(data->capsuleRadii);
    free(data->capsuleHalfLengths);
    free(data->capsuleColors);
    free(data->capsuleOpacities);
    free(data->capsuleSort);

    free(data->aoCapsuleStarts);
    free(data->aoCapsuleVectors);
    free(data->aoCapsuleRadii);
    free(data->aoCapsuleSort);

    free(data->shadowCapsuleStarts);
    free(data->shadowCapsuleVectors);
    free(data->shadowCapsuleRadii);
    free(data->shadowCapsuleSort);
    
    UnloadImage(data->aoLookupImage);
    UnloadTexture(data->aoLookupTable);
    UnloadImage(data->shadowLookupImage);
    UnloadTexture(data->shadowLookupTable);
}

static inline void CapsuleDataReset(CapsuleData* data)
{
    data->capsuleCount = 0;
    data->aoCapsuleCount = 0;
    data->shadowCapsuleCount = 0;
}

// Append capsules to the capsule data based off the joint transforms
static void CapsuleDataAppendFromTransformData(CapsuleData* data, TransformData* xforms, float maxCapsuleRadius, Color color, float opacity, bool ignoreEndSite)
{
    for (int i = 0; i < xforms->jointCount; i++)
    {
        int p = xforms->parents[i];
        
        if (p == -1) { continue; }
        if (ignoreEndSite && xforms->endSite[i]) { continue; }

        float capsuleHalfLength = Vector3Length(xforms->localPositions[i]) / 2.0f;
        float capsuleRadius = Min(maxCapsuleRadius, capsuleHalfLength) + (i % 2) * 0.001f;

        if (capsuleRadius < 0.001f) { continue; }

        Vector3 capsulePosition = Vector3Scale(Vector3Add(xforms->globalPositions[i], xforms->globalPositions[p]), 0.5f);
        Quaternion capsuleRotation = QuaternionMultiply(
            xforms->globalRotations[p],
            QuaternionBetween((Vector3){ 1.0f, 0.0f, 0.0f }, Vector3Normalize(xforms->localPositions[i])));

        data->capsulePositions[data->capsuleCount] = capsulePosition;
        data->capsuleRotations[data->capsuleCount] = capsuleRotation;
        data->capsuleHalfLengths[data->capsuleCount] = capsuleHalfLength;
        data->capsuleRadii[data->capsuleCount] = capsuleRadius;
        data->capsuleColors[data->capsuleCount] = (Vector3){ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
        data->capsuleOpacities[data->capsuleCount] = opacity;
        data->capsuleCount++;
    }
}

// Gather all of the capsules which are potentially casting ambient occlusion on a ground segment.
static void CapsuleDataUpdateAOCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition)
{
    data->aoCapsuleCount = 0;

    for (int i = 0; i < data->capsuleCount; i++)
    {
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        
        // Check if bounding spheres are more than AO_RATIO_MAX away from each other
        if (Vector3Distance(groundSegmentPosition, capsulePosition) - sqrtf(2.0f) > capsuleHalfLength + AO_RATIO_MAX * capsuleRadius)
        {
            continue;
        }
        
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);

        float capsuleTime;
        Vector3 groundPoint;
        NearestPointBetweenLineSegmentAndGroundSegment(
            &capsuleTime,
            &groundPoint,
            capsuleStart,
            capsuleEnd,
            (Vector3){ groundSegmentPosition.x - 1.0f, 0.0f, groundSegmentPosition.z - 1.0f },
            (Vector3){ groundSegmentPosition.x + 1.0f, 0.0f, groundSegmentPosition.z + 1.0f });
    
        Vector3 capsulePoint = Vector3Add(capsuleStart, Vector3Scale(capsuleVector, capsuleTime));
        
        // Check if the nearest point on the ground is more than AO_RATIO_MAX away
        if (Vector3Distance(groundPoint, capsulePoint) > AO_RATIO_MAX * capsuleRadius)
        {
            continue;
        }
        
        // Compute the actual occlusion for the closest point on the ground
        float capsuleOcclusion = Vector3Distance(groundPoint, capsulePoint) < capsuleRadius ? 0.0f :
            SphereOcclusion(groundPoint, (Vector3){ 0.0f, 1.0f, 0.0f }, capsulePoint, capsuleRadius);

        if (capsuleOcclusion < 0.99f)
        {
            data->aoCapsuleSort[data->aoCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion };
            data->aoCapsuleCount++;
        }
    }

    qsort(data->aoCapsuleSort, data->aoCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);

    for (int i = 0; i < data->aoCapsuleCount; i++)
    {
        int j = data->aoCapsuleSort[i].index;
        data->aoCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

// Gather all of the capsules which are potentially casting ambient occlusion on another capsule.
static void CapsuleDataUpdateAOCapsulesForCapsule(CapsuleData* data, int capsuleIndex)
{
    Vector3 queryCapsulePosition = data->capsulePositions[capsuleIndex];
    float queryCapsuleHalfLength = data->capsuleHalfLengths[capsuleIndex];
    float queryCapsuleRadius = data->capsuleRadii[capsuleIndex];
    Quaternion queryCapsuleRotation = data->capsuleRotations[capsuleIndex];
    Vector3 queryCapsuleStart = CapsuleStart(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleEnd = CapsuleEnd(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleVector = CapsuleVector(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);

    data->aoCapsuleCount = 0;

    for (int i = 0; i < data->capsuleCount; i++)
    {
        if (i == capsuleIndex) { continue; }
        
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleRadius = data->capsuleRadii[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];

        // Check if the bounding sphers are more than AO_RATIO_MAX away from each other
        if (Vector3Distance(queryCapsulePosition, capsulePosition) - queryCapsuleHalfLength - queryCapsuleRadius > 
            capsuleHalfLength + AO_RATIO_MAX * capsuleRadius)
        {
            continue;
        }

        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        
        float capsuleTime, queryTime;
        NearestPointBetweenLineSegments(
            &capsuleTime,
            &queryTime,
            capsuleStart,
            capsuleEnd,
            queryCapsuleStart,
            queryCapsuleEnd);

        Vector3 capsulePoint = Vector3Add(capsuleStart, Vector3Scale(capsuleVector, capsuleTime));
        Vector3 queryPoint = Vector3Add(queryCapsuleStart, Vector3Scale(queryCapsuleVector, queryTime));
        
        // Check if the nearest points on the two capsules are more than AO_RATIO_MAX away
        if (Vector3Distance(queryPoint, capsulePoint) - queryCapsuleRadius > AO_RATIO_MAX * capsuleRadius)
        {
            continue;
        }
        
        // Compute the actual occlusion at the nearest point
        Vector3 surfaceNormal = Vector3Normalize(Vector3Subtract(capsulePoint, queryPoint));
        Vector3 surfacePoint = Vector3Add(queryPoint, Vector3Scale(surfaceNormal, queryCapsuleRadius));
        float capsuleOcclusion = Vector3Distance(queryPoint, capsulePoint) <= queryCapsuleRadius + capsuleRadius ? 0.0f :
            SphereOcclusion(surfacePoint, surfaceNormal, capsulePoint, capsuleRadius);

        if (capsuleOcclusion < 0.99f)
        {
            data->aoCapsuleSort[data->aoCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion };
            data->aoCapsuleCount++;
        }
    }

    qsort(data->aoCapsuleSort, data->aoCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);

    for (int i = 0; i < data->aoCapsuleCount; i++)
    {
        int j = data->aoCapsuleSort[i].index;
        data->aoCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

// Gather all of the capsules which are potentially casting shadows on a ground segment.
static void CapsuleDataUpdateShadowCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition, Vector3 lightDir, float lightConeAngle)
{
    Vector3 lightRay = Vector3Scale(lightDir, 10.0f);

    data->shadowCapsuleCount = 0;

    for (int i = 0; i < data->capsuleCount; i++)
    {
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        
        float midRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsulePosition, lightRay);
        Vector3 groundCapsuleMid = Vector3Add(capsulePosition, Vector3Scale(lightRay, midRayTime));
        float maxRatio = 4.0f; // This is a kind of fudge-factor as the soft shadow don't have a fixed falloff

        // Check if the ground segment is more than maxRatio away from the shadow point at the center of the capsule 
        if (Vector3Distance(groundSegmentPosition, groundCapsuleMid) - sqrtf(2.0f) > capsuleHalfLength + maxRatio * capsuleRadius)
        {
            continue;
        }
      
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);

        // Find the projected shadow point for the capsule start and end points
        // I think for the ground the darkest part of the shadow is always one of these
        float startRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsuleStart, lightRay);
        float endRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsuleEnd, lightRay);

        Vector3 groundCapsuleStart = Vector3Add(capsuleStart, Vector3Scale(lightRay, startRayTime));
        Vector3 groundCapsuleEnd = Vector3Add(capsuleEnd, Vector3Scale(lightRay, endRayTime));
        
        groundCapsuleStart.x = Clamp(groundCapsuleStart.x, groundSegmentPosition.x - 1.0f, groundSegmentPosition.x + 1.0f);
        groundCapsuleStart.z = Clamp(groundCapsuleStart.z, groundSegmentPosition.z - 1.0f, groundSegmentPosition.z + 1.0f);
        groundCapsuleEnd.x = Clamp(groundCapsuleEnd.x, groundSegmentPosition.x - 1.0f, groundSegmentPosition.x + 1.0f);
        groundCapsuleEnd.z = Clamp(groundCapsuleEnd.z, groundSegmentPosition.z - 1.0f, groundSegmentPosition.z + 1.0f);
        
        // Check if both points are more than maxRatio away from the ground segment
        if (Vector3Distance(groundSegmentPosition, groundCapsuleStart) - sqrtf(2.0f) > maxRatio * capsuleRadius &&
            Vector3Distance(groundSegmentPosition, groundCapsuleEnd) - sqrtf(2.0f) > maxRatio * capsuleRadius)
        {
            continue;
        }
        
        // Compute the actual occlusion at both points and take the min
        float capsuleOcclusion = Min(
            CapsuleDirectionalOcclusion(groundCapsuleStart, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle),
            CapsuleDirectionalOcclusion(groundCapsuleEnd, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle));

        if (capsuleOcclusion < 0.99f)
        {
            data->shadowCapsuleSort[data->shadowCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion };
            data->shadowCapsuleCount++;
        }
    }

    qsort(data->shadowCapsuleSort, data->shadowCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);

    for (int i = 0; i < data->shadowCapsuleCount; i++)
    {
        int j = data->shadowCapsuleSort[i].index;
        data->shadowCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

// Gather all of the capsules which are potentially casting shadows on another capsule.
static void CapsuleDataUpdateShadowCapsulesForCapsule(CapsuleData* data, int capsuleIndex, Vector3 lightDir, float lightConeAngle)
{
    Vector3 lightRay = Vector3Scale(lightDir, 10.0f);
    
    Vector3 queryCapsulePosition = data->capsulePositions[capsuleIndex];
    float queryCapsuleHalfLength = data->capsuleHalfLengths[capsuleIndex];
    float queryCapsuleRadius = data->capsuleRadii[capsuleIndex];
    Quaternion queryCapsuleRotation = data->capsuleRotations[capsuleIndex];
    Vector3 queryCapsuleStart = CapsuleStart(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleEnd = CapsuleEnd(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleVector = CapsuleVector(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);

    data->shadowCapsuleCount = 0;

    for (int i = 0; i < data->capsuleCount; i++)
    {
        if (i == capsuleIndex) { continue; }
        
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        
        // Find the closest point between the capsule and the ray cast from the center of the casting capsule
        float midRayTime = NearestPointOnLineSegment(
            capsulePosition,
            lightRay,
            queryCapsulePosition);
        
        Vector3 capsuleMid = Vector3Add(capsulePosition, Vector3Scale(lightRay, midRayTime));
        float maxRatio = 4.0f;
        
        // Check if this is greater than maxRatio away
        if (Vector3Distance(queryCapsulePosition, capsuleMid) - queryCapsuleHalfLength - queryCapsuleRadius > capsuleHalfLength + maxRatio * capsuleRadius)
        {
            continue;
        }
        
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        
        // Find the nearest point between the capsule and the swept line of the casting capsule
        float queryCapsuleTime;
        Vector3 nearestRayPoint;
        NearestPointBetweenLineSegmentAndSweptLine(
            &queryCapsuleTime,
            &nearestRayPoint,
            queryCapsuleStart,
            queryCapsuleEnd,
            capsuleStart,
            capsuleEnd,
            lightRay);
        
        Vector3 queryCapsulePoint = Vector3Add(queryCapsuleStart, Vector3Scale(queryCapsuleVector, queryCapsuleTime));
        
        // If this distance is greater than maxRatio away then skip
        if (Vector3Distance(queryCapsulePoint, nearestRayPoint) - queryCapsuleRadius > capsuleHalfLength + maxRatio * capsuleRadius)
        {
            continue;
        }
        
        Vector3 surfaceNormal = Vector3Normalize(Vector3Subtract(nearestRayPoint, queryCapsulePoint));
        Vector3 surfacePoint = Vector3Add(queryCapsulePoint, Vector3Scale(surfaceNormal, queryCapsuleRadius));

        // Find actual occlusion amount
        float capsuleOcclusion = Vector3Distance(queryCapsulePoint, nearestRayPoint) <= queryCapsuleRadius + capsuleRadius ? 0.0f :
            CapsuleDirectionalOcclusion(surfacePoint, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle);
        
        if (capsuleOcclusion < 0.99f)
        {
            data->shadowCapsuleSort[data->shadowCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion };
            data->shadowCapsuleCount++;
        }
    }

    qsort(data->shadowCapsuleSort, data->shadowCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);

    for (int i = 0; i < data->shadowCapsuleCount; i++)
    {
        int j = data->shadowCapsuleSort[i].index;
        data->shadowCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

// Resize so that we have enough capsules in the buffers for the given set of characters
static inline void CapsuleDataUpdateForCharacters(CapsuleData* capsuleData, CharacterData* characterData)
{
    int totalJointCount = 0;
    for (int i = 0; i < characterData->count; i++)
    {
        totalJointCount += characterData->xformData[i].jointCount;
    }

    CapsuleDataResize(capsuleData, totalJointCount);
}

//----------------------------------------------------------------------------------
// Shaders
//----------------------------------------------------------------------------------

#define AO_CAPSULES_MAX 32
#define SHADOW_CAPSULES_MAX 64


#define GLSL_DEFINE_VALUE(X) #X
#define GLSL_DEFINE(X) "#define " #X " " GLSL_DEFINE_VALUE(X) " \n"

#if defined(PLATFORM_WEB)
#define GLSL_VERSION "#version 300 es\n"
#define GLSL_PRECISION "precision highp float;\nprecision mediump int;\n"
#else
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_PRECISION ""
#endif

#define GLSL_STRINGIFY_INNER(X) #X
#define GLSL_STRINGIFY(X) GLSL_STRINGIFY_INNER(X)

#define GLSL_HEADER \
  GLSL_VERSION \
  GLSL_DEFINE(AO_RATIO_MAX) \
  GLSL_DEFINE(AO_CAPSULES_MAX) \
  GLSL_DEFINE(SHADOW_CAPSULES_MAX) \
  GLSL_DEFINE(PI)

#define GLSL_SHADER(X) \
  GLSL_HEADER \
  GLSL_STRINGIFY(X)

#define GLSL_SHADER_WITH_PRECISION(X) \
  GLSL_HEADER \
  GLSL_PRECISION \
  GLSL_STRINGIFY(X)

// Vertex Shader
static const char* shaderVS = GLSL_SHADER(

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform int isCapsule;
uniform vec3 capsulePosition;
uniform vec4 capsuleRotation;
uniform float capsuleHalfLength;
uniform float capsuleRadius;

uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 matView;
uniform mat4 matProjection;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

vec3 Rotate(in vec4 q, vec3 v)
{
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Stretch capsule according to capsule half length
vec3 CapsuleStretch(vec3 pos, float hlength, float radius)
{
    vec3 scaled = pos * radius;
    scaled.x = scaled.x > 0.0 ? scaled.x + hlength : scaled.x - hlength;
    return scaled;
}

void main()
{
    fragTexCoord = vertexTexCoord;

    if (isCapsule == 1)
    {
        fragPosition = Rotate(capsuleRotation,
            CapsuleStretch(vertexPosition,
            capsuleHalfLength, capsuleRadius)) + capsulePosition;

        fragNormal = Rotate(capsuleRotation, vertexNormal);
    }
    else
    {
        fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
        fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));
    }

    gl_Position = matProjection * matView * vec4(fragPosition, 1.0);
}

);

// Fragment Shader
static const char* shaderFS = GLSL_SHADER_WITH_PRECISION(

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform vec3 objectColor;
uniform float objectSpecularity;
uniform float objectGlossiness;
uniform float objectOpacity;

uniform sampler2D texture0;
uniform int useTexture;

uniform int isCapsule;
uniform vec3 capsulePosition;
uniform vec4 capsuleRotation;
uniform float capsuleHalfLength;
uniform float capsuleRadius;
uniform vec3 capsuleStart;
uniform vec3 capsuleVector;

uniform int shadowCapsuleCount;
uniform vec3 shadowCapsuleStarts[SHADOW_CAPSULES_MAX];
uniform vec3 shadowCapsuleVectors[SHADOW_CAPSULES_MAX];
uniform float shadowCapsuleRadii[SHADOW_CAPSULES_MAX];
uniform sampler2D shadowLookupTable;
uniform vec2 shadowLookupResolution;

uniform int aoCapsuleCount;
uniform vec3 aoCapsuleStarts[AO_CAPSULES_MAX];
uniform vec3 aoCapsuleVectors[AO_CAPSULES_MAX];
uniform float aoCapsuleRadii[AO_CAPSULES_MAX];
uniform sampler2D aoLookupTable;
uniform vec2 aoLookupResolution;

uniform vec3 cameraPosition;

uniform float sunStrength;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform float skyStrength;
uniform vec3 skyColor;
uniform float ambientStrength;
uniform float groundStrength;
uniform float exposure;

out vec4 finalColor;

vec3 ToGamma(in vec3 col)
{
    return vec3(pow(col.x, 2.2), pow(col.y, 2.2), pow(col.z, 2.2));
}

vec3 FromGamma(in vec3 col)
{
    return vec3(pow(col.x, 1.0/2.2), pow(col.y, 1.0/2.2), pow(col.z, 1.0/2.2));
}

float Saturate(in float x)
{
    return clamp(x, 0.0, 1.0);
}

float Square(in float x)
{
    return x * x;
}

float FastAcos(in float x)
{
    float y = abs(x);
    float p = -0.1565827 * y + 1.570796;
    p *= sqrt(max(1.0 - y, 0.0));
    return x >= 0.0 ? p : PI - p;
}

float FastPositiveAcos(in float x)
{
    float p = -0.1565827 * x + 1.570796;
    return p * sqrt(max(1.0 - x, 0.0));
}

vec3 Rotate(in vec4 q, vec3 v)
{
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec3 Unrotate(in vec4 q, vec3 v)
{
    return Rotate(vec4(-q.x, -q.y, -q.z, q.w), v);
}

float Checker(in vec2 uv)
{
    vec4 uvDDXY = vec4(dFdx(uv), dFdy(uv));
    vec2 w = vec2(length(uvDDXY.xz), length(uvDDXY.yw));
    vec2 i = 2.0*(abs(fract((uv-0.5*w)*0.5)-0.5)-
                  abs(fract((uv+0.5*w)*0.5)-0.5))/w;
    return 0.5 - 0.5*i.x*i.y;
}

float Grid(in vec2 uv, in float lineWidth)
{
    vec4 uvDDXY = vec4(dFdx(uv), dFdy(uv));
    vec2 uvDeriv = vec2(length(uvDDXY.xz), length(uvDDXY.yw));
    float targetWidth = lineWidth > 0.5 ? 1.0 - lineWidth : lineWidth;
    vec2 drawWidth = clamp(
        vec2(targetWidth, targetWidth), uvDeriv, vec2(0.5, 0.5));
    vec2 lineAA = uvDeriv * 1.5;
    vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);
    gridUV = lineWidth > 0.5 ? gridUV : 1.0 - gridUV;
    vec2 g2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);
    g2 *= clamp(targetWidth / drawWidth, 0.0, 1.0);
    g2 = mix(g2, vec2(targetWidth, targetWidth),
        clamp(uvDeriv * 2.0 - 1.0, 0.0, 1.0));
    g2 = lineWidth > 0.5 ? 1.0 - g2 : g2;
    return mix(g2.x, 1.0, g2.y);
}

float SphereOcclusion(in vec3 pos, in vec3 nor, in vec3 sph, in float rad)
{
    vec3 di = sph - pos;
    float l = length(di);
    float nlAngle = FastAcos(dot(nor, di / l));
    float h  = l < rad ? 1.0 : l / rad;
    vec2 uvs = vec2(nlAngle / PI, (h - 1.0) / (AO_RATIO_MAX - 1.0));
    uvs = uvs * (aoLookupResolution - 1.0) / aoLookupResolution + 0.5 / aoLookupResolution;
    return texture(aoLookupTable, uvs).r;
}

float SphereDirectionalOcclusion(
    in vec3 pos, 
    in vec3 sphere, 
    in float radius,
    in vec3 coneDir)
{
    vec3 occluder = sphere - pos;
    float occluderLen2 = dot(occluder, occluder);
    vec3 occluderDir = occluder * inversesqrt(occluderLen2);
    float phi = FastAcos(dot(occluderDir, -coneDir));
    float theta = FastPositiveAcos(sqrt(occluderLen2 / (Square(radius) + occluderLen2)));

    vec2 uvs = vec2(phi / PI, theta / (PI / 2.0));
    uvs = uvs * (shadowLookupResolution - 1.0) / shadowLookupResolution + 0.5 / shadowLookupResolution;
    return texture(shadowLookupTable, uvs).r;
}

float CapsuleOcclusion(
    in vec3 pos, 
    in vec3 nor,
    in vec3 capStart, 
    in vec3 capVec, 
    in float radius)
{
    vec3 ba = capVec;
    vec3 pa = pos - capStart;
    float l = dot(ba, ba);
    float t = abs(l) < 1e-8f ? 0.0 : Saturate(dot(pa, ba) / l);
    return SphereOcclusion(pos, nor, capStart + t * ba, radius);
}

float CapsuleDirectionalOcclusion(
    in vec3 pos, in vec3 capStart, in vec3 capVec,
    in float capRadius, in vec3 coneDir)
{
    vec3 ba = capVec;
    vec3 pa = capStart - pos;
    vec3 cba = dot(-coneDir, ba) * -coneDir - ba;
    float t = Saturate(dot(pa, cba) / dot(cba, cba));

    return SphereDirectionalOcclusion(pos, capStart + t * ba, capRadius, coneDir);
}

vec2 CapsuleUVs(
    in vec3 pos, in vec3 capPos,
    in vec4 capRot, in float capHalfLength,
    in float capRadius, in vec2 scale)
{
    vec3 loc = Unrotate(capRot, pos - capPos);

    vec2 limit = vec2(
        2.0 * capHalfLength + 2.0 * capRadius,
        PI * capRadius);

    vec2 repeat = max(round(scale * limit), 1.0);

    return (repeat / limit) * vec2(loc.x, capRadius * atan(loc.z, loc.y));
}

vec3 CapsuleNormal(
    in vec3 pos, in vec3 capStart,
    in vec3 capVec)
{
    vec3 ba = capVec;
    vec3 pa = pos - capStart;
    float h = Saturate(dot(pa, ba) / dot(ba, ba));
    return normalize(pa - h*ba);
}

void main()
{
    vec3 pos = fragPosition;
    vec3 nor = fragNormal;
    vec2 uvs = fragTexCoord;

    // Recompute uvs and normals if capsule

    if (isCapsule == 1)
    {
        uvs = CapsuleUVs(
            pos,
            capsulePosition,
            capsuleRotation,
            capsuleHalfLength,
            capsuleRadius,
            vec2(4.0, 4.0));
        
        nor = CapsuleNormal(pos, capsuleStart, capsuleVector);
    }

    // Compute sun shadow amount

    float sunShadow = 1.0;
    for (int i = 0; i < shadowCapsuleCount; i++)
    {
        sunShadow = min(sunShadow, CapsuleDirectionalOcclusion(
            pos,
            shadowCapsuleStarts[i],
            shadowCapsuleVectors[i],
            shadowCapsuleRadii[i],
            sunDir));
    }
    
    // Compute ambient shadow amount

    float ambShadow = 1.0;
    for (int i = 0; i < aoCapsuleCount; i++)
    {
        ambShadow = min(ambShadow, CapsuleOcclusion(
            pos, nor,
            aoCapsuleStarts[i],
            aoCapsuleVectors[i],
            aoCapsuleRadii[i]));
    }

    // Compute albedo: sample texture if available, otherwise use grid/checker pattern

    vec3 texColor = texture(texture0, uvs).rgb;

    float gridFine = Grid(20.0 * uvs, 0.025);
    float gridCoarse = Grid(2.0 * uvs, 0.02);
    float check = Checker(2.0 * uvs);

    vec3 proceduralColor = FromGamma(objectColor) * mix(mix(mix(0.9, 0.95, check), 0.85, gridFine), 1.0, gridCoarse);
    vec3 albedo = mix(proceduralColor, FromGamma(objectColor) * texColor, float(useTexture));
    float specularity = objectSpecularity * mix(mix(0.0, 0.75, check), 1.0, gridCoarse);
    
    // Compute lighting
    
    vec3 eyeDir = normalize(pos - cameraPosition);

    vec3 lightSunColor = FromGamma(sunColor);
    vec3 lightSunHalf = normalize(sunDir + eyeDir);

    vec3 lightSkyColor = FromGamma(skyColor);
    vec3 skyDir = vec3(0.0, -1.0, 0.0);
    vec3 lightSkyHalf = normalize(skyDir + eyeDir);

    float sunFactorDiff = max(dot(nor, -sunDir), 0.0);
    float sunFactorSpec = specularity *
        ((objectGlossiness+2.0) / (8.0 * PI)) *
        pow(max(dot(nor, lightSunHalf), 0.0), objectGlossiness);

    float skyFactorDiff = max(dot(nor, -skyDir), 0.0);
    float skyFactorSpec = specularity *
        ((objectGlossiness+2.0) / (8.0 * PI)) *
        pow(max(dot(nor, lightSkyHalf), 0.0), objectGlossiness);

    float groundFactorDiff = max(dot(nor, skyDir), 0.0);
    
    // Combine
    
    vec3 ambient = ambShadow * ambientStrength * lightSkyColor * albedo;

    vec3 diffuse = sunShadow * sunStrength * lightSunColor * albedo * sunFactorDiff +
        groundStrength * lightSkyColor * albedo * groundFactorDiff +
        skyStrength * lightSkyColor * albedo * skyFactorDiff;

    float specular = sunShadow * sunStrength * sunFactorSpec + skyStrength * skyFactorSpec;

    vec3 final = diffuse + ambient + specular;

    finalColor = vec4(ToGamma(exposure * final), objectOpacity);
}

);

// Structure containing all the uniform indices for the shader
typedef struct
{
    int isCapsule;
    int capsulePosition;
    int capsuleRotation;
    int capsuleHalfLength;
    int capsuleRadius;
    int capsuleStart;
    int capsuleVector;

    int shadowCapsuleCount;
    int shadowCapsuleStarts;
    int shadowCapsuleVectors;
    int shadowCapsuleRadii;
    int shadowLookupTable;
    int shadowLookupResolution;

    int aoCapsuleCount;
    int aoCapsuleStarts;
    int aoCapsuleVectors;
    int aoCapsuleRadii;
    int aoLookupTable;
    int aoLookupResolution;

    int cameraPosition;

    int objectColor;
    int objectSpecularity;
    int objectGlossiness;
    int objectOpacity;
    int useTexture;

    int sunStrength;
    int sunDir;
    int sunColor;
    int skyStrength;
    int skyColor;
    int ambientStrength;
    int groundStrength;

    int exposure;

} ShaderUniforms;

// Lookup all shader uniform indices
static void ShaderUniformsInit(ShaderUniforms* uniforms, Shader shader)
{
    uniforms->isCapsule = GetShaderLocation(shader, "isCapsule");
    uniforms->capsulePosition =  GetShaderLocation(shader, "capsulePosition");
    uniforms->capsuleRotation =  GetShaderLocation(shader, "capsuleRotation");
    uniforms->capsuleHalfLength =  GetShaderLocation(shader, "capsuleHalfLength");
    uniforms->capsuleRadius =  GetShaderLocation(shader, "capsuleRadius");
    uniforms->capsuleStart =  GetShaderLocation(shader, "capsuleStart");
    uniforms->capsuleVector =  GetShaderLocation(shader, "capsuleVector");

    uniforms->shadowCapsuleCount = GetShaderLocation(shader, "shadowCapsuleCount");
    uniforms->shadowCapsuleStarts =  GetShaderLocation(shader, "shadowCapsuleStarts");
    uniforms->shadowCapsuleVectors =  GetShaderLocation(shader, "shadowCapsuleVectors");
    uniforms->shadowCapsuleRadii =  GetShaderLocation(shader, "shadowCapsuleRadii");
    uniforms->shadowLookupTable =  GetShaderLocation(shader, "shadowLookupTable");
    uniforms->shadowLookupResolution =  GetShaderLocation(shader, "shadowLookupResolution");

    uniforms->aoCapsuleCount = GetShaderLocation(shader, "aoCapsuleCount");
    uniforms->aoCapsuleStarts =  GetShaderLocation(shader, "aoCapsuleStarts");
    uniforms->aoCapsuleVectors =  GetShaderLocation(shader, "aoCapsuleVectors");
    uniforms->aoCapsuleRadii =  GetShaderLocation(shader, "aoCapsuleRadii");
    uniforms->aoLookupTable =  GetShaderLocation(shader, "aoLookupTable");
    uniforms->aoLookupResolution =  GetShaderLocation(shader, "aoLookupResolution");

    uniforms->cameraPosition = GetShaderLocation(shader, "cameraPosition");

    uniforms->objectColor = GetShaderLocation(shader, "objectColor");
    uniforms->objectSpecularity = GetShaderLocation(shader, "objectSpecularity");
    uniforms->objectGlossiness = GetShaderLocation(shader, "objectGlossiness");
    uniforms->objectOpacity = GetShaderLocation(shader, "objectOpacity");
    uniforms->useTexture = GetShaderLocation(shader, "useTexture");

    uniforms->sunStrength = GetShaderLocation(shader, "sunStrength");
    uniforms->sunDir = GetShaderLocation(shader, "sunDir");
    uniforms->sunColor = GetShaderLocation(shader, "sunColor");
    uniforms->skyStrength = GetShaderLocation(shader, "skyStrength");
    uniforms->skyColor = GetShaderLocation(shader, "skyColor");
    uniforms->ambientStrength = GetShaderLocation(shader, "ambientStrength");
    uniforms->groundStrength = GetShaderLocation(shader, "groundStrength");

    uniforms->exposure = GetShaderLocation(shader, "exposure");
}

//----------------------------------------------------------------------------------
// Models
//----------------------------------------------------------------------------------

// Embedded Capsule OBJ file
static const char* capsuleOBJ = "\
v 0.82165808 -0.82165808 -1.0579772e-18\nv 0.82165808 -0.58100000 0.58100000\n\
v 0.82165808 8.7595780e-17 0.82165808\nv 0.82165808 0.58100000 0.58100000\n\
v 0.82165808 0.82165808 9.9566116e-17\nv 0.82165808 0.58100000 -0.58100000\n\
v 0.82165808 2.8884397e-16 -0.82165808\nv 0.82165808 -0.58100000 -0.58100000\n\
v -0.82165808 -0.82165808 -1.0579772e-18\nv -0.82165808 -0.58100000 0.58100000\n\
v -0.82165808 -1.3028313e-17 0.82165808\nv -0.82165808 0.58100000 0.58100000\n\
v -0.82165808 0.82165808 9.9566116e-17\nv -0.82165808 0.58100000 -0.58100000\n\
v -0.82165808 1.8821987e-16 -0.82165808\nv -0.82165808 -0.58100000 -0.58100000\n\
v 1.16200000 1.5874776e-16 -1.0579772e-18\nv -1.16200000 1.6443801e-17 -1.0579772e-18\n\
v -9.1030792e-3 -1.15822938 -1.0579772e-18\nv 9.1030792e-3 -1.15822938 -1.0579772e-18\n\
v 9.1030792e-3 -0.81899185 0.81899185\nv -9.1030792e-3 -0.81899185 0.81899185\n\
v 9.1030792e-3 1.7232088e-17 1.15822938\nv -9.1030792e-3 1.6117282e-17 1.15822938\n\
v 9.1030792e-3 0.81899185 0.81899185\nv -9.1030792e-3 0.81899185 0.81899185\n\
v 9.1030792e-3 1.15822938 1.4078421e-16\nv -9.1030792e-3 1.15822938 1.4078421e-16\n\
v 9.1030792e-3 0.81899185 -0.81899185\nv -9.1030792e-3 0.81899185 -0.81899185\n\
v 9.1030792e-3 3.0091647e-16 -1.15822938\nv -9.1030792e-3 2.9980166e-16 -1.15822938\n\
v 9.1030792e-3 -0.81899185 -0.81899185\nv -9.1030792e-3 -0.81899185 -0.81899185\n\
vn 0.71524683 -0.69887193 -2.5012597e-16\nvn 0.61185516 -0.55930013 0.55930013\n\
vn 0.71524683 0.0000000e+0 0.69887193\nvn 0.61185516 0.55930013 0.55930013\n\
vn 0.71524683 0.69887193 1.5632873e-17\nvn 0.61185516 0.55930013 -0.55930013\n\
vn 0.71524683 6.2531494e-17 -0.69887193\nvn 0.61185516 -0.55930013 -0.55930013\n\
vn -0.71524683 -0.69887193 -2.5012597e-16\nvn -0.61185516 -0.55930013 0.55930013\n\
vn -0.71524683 0.0000000e+0 0.69887193\nvn -0.61185516 0.55930013 0.55930013\n\
vn -0.71524683 0.69887193 4.6898620e-17\nvn -0.61185516 0.55930013 -0.55930013\n\
vn -0.71524683 4.6898620e-17 -0.69887193\nvn -0.61185516 -0.55930013 -0.55930013\n\
vn 1.00000000 1.5208752e-17 -2.6615316e-17\nvn -1.00000000 -1.5208752e-17 2.2813128e-17\n\
vn -0.19614758 -0.98057439 -2.2848712e-16\nvn 0.26047011 -0.96548191 -2.4273177e-16\n\
vn 0.13072302 -0.70103905 0.70103905\nvn -0.19614758 -0.69337080 0.69337080\n\
vn 0.22349711 5.9825845e-2 0.97286685\nvn -0.22349711 -5.9825845e-2 0.97286685\n\
vn 0.15641931 0.75510180 0.63667438\nvn -0.15641931 0.63667438 0.75510180\n\
vn 0.22349711 0.97286685 -5.9825845e-2\nvn -0.22349711 0.97286685 5.9825845e-2\n\
vn 0.15641931 0.63667438 -0.75510180\nvn -0.15641931 0.75510180 -0.63667438\n\
vn 0.22349711 -5.9825845e-2 -0.97286685\nvn -0.22349711 5.9825845e-2 -0.97286685\n\
vn 0.15641931 -0.75510180 -0.63667438\nvn -0.15641931 -0.63667438 -0.75510180\n\
f 1//1 17//17 2//2\nf 1//1 20//20 8//8\nf 2//2 17//17 3//3\nf 2//2 20//20 1//1\n\
f 2//2 23//23 21//21\nf 3//3 17//17 4//4\nf 3//3 23//23 2//2\nf 4//4 17//17 5//5\n\
f 4//4 23//23 3//3\nf 4//4 27//27 25//25\nf 5//5 17//17 6//6\nf 5//5 27//27 4//4\n\
f 6//6 17//17 7//7\nf 6//6 27//27 5//5\nf 6//6 31//31 29//29\nf 7//7 17//17 8//8\n\
f 7//7 31//31 6//6\nf 8//8 17//17 1//1\nf 8//8 20//20 33//33\nf 8//8 31//31 7//7\n\
f 9//9 18//18 16//16\nf 9//9 19//19 10//10\nf 10//10 18//18 9//9\nf 10//10 19//19 22//22\n\
f 10//10 24//24 11//11\nf 11//11 18//18 10//10\nf 11//11 24//24 12//12\nf 12//12 18//18 11//11\n\
f 12//12 24//24 26//26\nf 12//12 28//28 13//13\nf 13//13 18//18 12//12\nf 13//13 28//28 14//14\n\
f 14//14 18//18 13//13\nf 14//14 28//28 30//30\nf 14//14 32//32 15//15\nf 15//15 18//18 14//14\n\
f 15//15 32//32 16//16\nf 16//16 18//18 15//15\nf 16//16 19//19 9//9\nf 16//16 32//32 34//34\n\
f 19//19 33//33 20//20\nf 20//20 21//21 19//19\nf 21//21 20//20 2//2\nf 21//21 24//24 22//22\n\
f 22//22 19//19 21//21\nf 22//22 24//24 10//10\nf 23//23 26//26 24//24\nf 24//24 21//21 23//23\n\
f 25//25 23//23 4//4\nf 25//25 28//28 26//26\nf 26//26 23//23 25//25\nf 26//26 28//28 12//12\n\
f 27//27 30//30 28//28\nf 28//28 25//25 27//27\nf 29//29 27//27 6//6\nf 29//29 32//32 30//30\n\
f 30//30 27//27 29//29\nf 30//30 32//32 14//14\nf 31//31 34//34 32//32\nf 32//32 29//29 31//31\n\
f 33//33 19//19 34//34\nf 33//33 31//31 8//8\nf 34//34 19//19 16//16\nf 34//34 31//31 33//33";

#undef TINYOBJ_LOADER_C_IMPLEMENTATION
#include "external/tinyobj_loader_c.h"

// Extra function for loading OBJ from memory
static Model LoadOBJFromMemory(const char *fileText)
{
    Model model = { 0 };

    tinyobj_attrib_t attrib = { 0 };
    tinyobj_shape_t *meshes = NULL;
    unsigned int meshCount = 0;

    tinyobj_material_t *materials = NULL;
    unsigned int materialCount = 0;

    if (fileText != NULL)
    {
        unsigned int dataSize = (unsigned int)strlen(fileText);

        unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
        tinyobj_parse_obj(&attrib, &meshes, &meshCount, &materials, &materialCount, fileText, dataSize, flags);
        
        model.meshCount = 1;
        model.meshes = (Mesh *)RL_CALLOC(model.meshCount, sizeof(Mesh));
        model.meshMaterial = (int *)RL_CALLOC(model.meshCount, sizeof(int));

        // Count the faces for each material
        int *matFaces = (int *)RL_CALLOC(model.meshCount, sizeof(int));
        matFaces[0] = attrib.num_faces;

        //--------------------------------------
        // Create the material meshes

        // Running counts/indexes for each material mesh as we are
        // building them at the same time
        int *vCount = (int *)RL_CALLOC(model.meshCount, sizeof(int));
        int *vtCount = (int *)RL_CALLOC(model.meshCount, sizeof(int));
        int *vnCount = (int *)RL_CALLOC(model.meshCount, sizeof(int));
        int *faceCount = (int *)RL_CALLOC(model.meshCount, sizeof(int));

        // Allocate space for each of the material meshes
        for (int mi = 0; mi < model.meshCount; mi++)
        {
            model.meshes[mi].vertexCount = matFaces[mi]*3;
            model.meshes[mi].triangleCount = matFaces[mi];
            model.meshes[mi].vertices = (float *)RL_CALLOC(model.meshes[mi].vertexCount*3, sizeof(float));
            model.meshes[mi].texcoords = (float *)RL_CALLOC(model.meshes[mi].vertexCount*2, sizeof(float));
            model.meshes[mi].normals = (float *)RL_CALLOC(model.meshes[mi].vertexCount*3, sizeof(float));
            model.meshMaterial[mi] = mi;
        }

        // Scan through the combined sub meshes and pick out each material mesh
        for (unsigned int af = 0; af < attrib.num_faces; af++)
        {
            int mm = attrib.material_ids[af];   // mesh material for this face
            if (mm == -1) { mm = 0; }           // no material object..

            // Get indices for the face
            tinyobj_vertex_index_t idx0 = attrib.faces[3*af + 0];
            tinyobj_vertex_index_t idx1 = attrib.faces[3*af + 1];
            tinyobj_vertex_index_t idx2 = attrib.faces[3*af + 2];

            // Fill vertices buffer (float) using vertex index of the face
            for (int v = 0; v < 3; v++) { model.meshes[mm].vertices[vCount[mm] + v] = attrib.vertices[idx0.v_idx*3 + v]; } vCount[mm] +=3;
            for (int v = 0; v < 3; v++) { model.meshes[mm].vertices[vCount[mm] + v] = attrib.vertices[idx1.v_idx*3 + v]; } vCount[mm] +=3;
            for (int v = 0; v < 3; v++) { model.meshes[mm].vertices[vCount[mm] + v] = attrib.vertices[idx2.v_idx*3 + v]; } vCount[mm] +=3;

            if (attrib.num_texcoords > 0)
            {
                // Fill texcoords buffer (float) using vertex index of the face
                // NOTE: Y-coordinate must be flipped upside-down to account for
                // raylib's upside down textures...
                model.meshes[mm].texcoords[vtCount[mm] + 0] = attrib.texcoords[idx0.vt_idx*2 + 0];
                model.meshes[mm].texcoords[vtCount[mm] + 1] = 1.0f - attrib.texcoords[idx0.vt_idx*2 + 1]; vtCount[mm] += 2;
                model.meshes[mm].texcoords[vtCount[mm] + 0] = attrib.texcoords[idx1.vt_idx*2 + 0];
                model.meshes[mm].texcoords[vtCount[mm] + 1] = 1.0f - attrib.texcoords[idx1.vt_idx*2 + 1]; vtCount[mm] += 2;
                model.meshes[mm].texcoords[vtCount[mm] + 0] = attrib.texcoords[idx2.vt_idx*2 + 0];
                model.meshes[mm].texcoords[vtCount[mm] + 1] = 1.0f - attrib.texcoords[idx2.vt_idx*2 + 1]; vtCount[mm] += 2;
            }

            if (attrib.num_normals > 0)
            {
                // Fill normals buffer (float) using vertex index of the face
                for (int v = 0; v < 3; v++) { model.meshes[mm].normals[vnCount[mm] + v] = attrib.normals[idx0.vn_idx*3 + v]; } vnCount[mm] +=3;
                for (int v = 0; v < 3; v++) { model.meshes[mm].normals[vnCount[mm] + v] = attrib.normals[idx1.vn_idx*3 + v]; } vnCount[mm] +=3;
                for (int v = 0; v < 3; v++) { model.meshes[mm].normals[vnCount[mm] + v] = attrib.normals[idx2.vn_idx*3 + v]; } vnCount[mm] +=3;
            }
        }

        model.materialCount = 1;
        model.materials = (Material *)RL_CALLOC(model.materialCount, sizeof(Material));
        model.materials[0] = LoadMaterialDefault();

        tinyobj_attrib_free(&attrib);
        tinyobj_shapes_free(meshes, meshCount);
        tinyobj_materials_free(materials, materialCount);

        RL_FREE(matFaces);
        RL_FREE(vCount);
        RL_FREE(vtCount);
        RL_FREE(vnCount);
        RL_FREE(faceCount);
    }

    // Make sure model transform is set to identity matrix!
    model.transform = MatrixIdentity();

    // Upload vertex data to GPU (static mesh)
    for (int i = 0; i < model.meshCount; i++) { UploadMesh(&model.meshes[i], false); }

    return model;
}

//----------------------------------------------------------------------------------
// Render Settings
//----------------------------------------------------------------------------------

typedef struct {

    Color backgroundColor;

    float sunLightConeAngle;
    float sunLightStrength;
    float sunAzimuth;
    float sunAltitude;
    Color sunColor;

    float skyLightStrength;
    Color skyColor;

    float groundLightStrength;
    float ambientLightStrength;

    float exposure;

    bool drawOrigin;
    bool drawGrid;
    bool drawChecker;
    bool drawMeshes;
    bool drawCapsules;
    bool drawWireframes;
    bool drawSkeleton;
    bool drawTransforms;
    bool drawAO;
    bool drawShadows;
    bool drawEndSites;
    bool drawFPS;
    bool drawTexture;
    bool drawUI;

} RenderSettings;

void RenderSettingsInit(RenderSettings* settings, int argc, char** argv)
{
    settings->backgroundColor = ArgColor(argc, argv, "backgroundColor", WHITE);

    settings->sunLightConeAngle = ArgFloat(argc, argv, "sunLightConeAngle", 0.2f);
    settings->sunLightStrength = ArgFloat(argc, argv, "sunLightStrength", 0.25f);
    settings->sunAzimuth = ArgFloat(argc, argv, "sunAzimuth", PI / 4.0f);
    settings->sunAltitude = ArgFloat(argc, argv, "sunAltitude", 0.8f);
    settings->sunColor = ArgColor(argc, argv, "sunColor", (Color){ 253, 255, 232 });

    settings->skyLightStrength = ArgFloat(argc, argv, "skyLightStrength", 0.15f);
    settings->skyColor = ArgColor(argc, argv, "skyColor", (Color){ 174, 183, 190 });

    settings->groundLightStrength = ArgFloat(argc, argv, "groundLightStrength", 0.1f);
    settings->ambientLightStrength = ArgFloat(argc, argv, "ambientLightStrength", 1.0f);

    settings->exposure = ArgFloat(argc, argv, "exposure", 0.9f);

    settings->drawOrigin = ArgBool(argc, argv, "drawOrigin", true);
    settings->drawGrid = ArgBool(argc, argv, "drawGrid", false);
    settings->drawChecker = ArgBool(argc, argv, "drawChecker", true);
    settings->drawMeshes = ArgBool(argc, argv, "drawMeshes", false);
    settings->drawCapsules = ArgBool(argc, argv, "drawCapsules", true);
    settings->drawWireframes = ArgBool(argc, argv, "drawWireframes", false);
    settings->drawSkeleton = ArgBool(argc, argv, "drawSkeleton", true);
    settings->drawTransforms = ArgBool(argc, argv, "drawTransforms", false);
    settings->drawAO = ArgBool(argc, argv, "drawAO", true);
    settings->drawShadows = ArgBool(argc, argv, "drawShadows", true);
    settings->drawEndSites = ArgBool(argc, argv, "drawEndSites", true);
    settings->drawFPS = ArgBool(argc, argv, "drawFPS", false);
    settings->drawTexture = ArgBool(argc, argv, "drawTexture", true);
    settings->drawUI = ArgBool(argc, argv, "drawUI", true);
}

//--------------------------------------
// Scrubber
//--------------------------------------

typedef struct {

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

static inline void ScrubberSettingsInit(ScrubberSettings* settings, int argc, char** argv)
{
    settings->playing = ArgBool(argc, argv, "playing", true);
    settings->looping = ArgBool(argc, argv, "looping", false);
    settings->inplace = ArgBool(argc, argv, "inplace", false);
    settings->playTime = ArgFloat(argc, argv, "playTime", 0.0f);
    settings->playSpeed = ArgFloat(argc, argv, "playSpeed", 1.0f);
    settings->frameSnap = ArgBool(argc, argv, "frameSnap", true);
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

static inline int ScrubberGetFrameCount(CharacterData* characterData, int index)
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

static inline float ScrubberGetFrameTime(CharacterData* characterData, int index)
{
    if (characterData->isGLB[index])
        return GLBDataGetSourceFrameTime(&characterData->glbData[index], characterData->glbData[index].activeAnim);
    return characterData->bvhData[index].frameTime;
}

static inline void ScrubberSettingsRecomputeLimits(ScrubberSettings* settings, CharacterData* characterData)
{
    settings->frameLimit = 0;
    settings->timeLimit = 0.0f;
    for (int i = 0; i < characterData->count; i++)
    {
        int frameCount = ScrubberGetFrameCount(characterData, i);
        float frameTime = ScrubberGetFrameTime(characterData, i);
        settings->frameLimit = MaxInt(settings->frameLimit, frameCount - 1);
        settings->timeLimit = Max(settings->timeLimit, (frameCount - 1) * frameTime);
    }
}

static inline void ScrubberSettingsInitMaxs(ScrubberSettings* settings, CharacterData* characterData)
{
    if (characterData->count == 0) { return; }

    int frameCount = ScrubberGetFrameCount(characterData, characterData->active);
    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);

    settings->frameMax = frameCount - 1;
    settings->frameMaxSelect = settings->frameMax;
    settings->timeMax = settings->frameMax * frameTime;

    settings->frameMin = 0;
    settings->frameMinSelect = settings->frameMin;
    settings->timeMin = 0.0f;
}

static inline void ScrubberSettingsClamp(ScrubberSettings* settings, CharacterData* characterData)
{
    if (characterData->count == 0) { return; }

    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);

    settings->frameMax = ClampInt(settings->frameMax, 0, settings->frameLimit);
    settings->frameMaxSelect = settings->frameMax;
    settings->timeMax = settings->frameMax * frameTime;

    settings->frameMin = ClampInt(settings->frameMin, 0, settings->frameMax);
    settings->frameMinSelect = settings->frameMin;
    settings->timeMin = settings->frameMin * frameTime;

    settings->playTime = Clamp(settings->playTime, settings->timeMin, settings->timeMax);
}

//----------------------------------------------------------------------------------
// Drawing
//----------------------------------------------------------------------------------

static inline void DrawTransform(const Vector3 position, const Quaternion rotation, const float size)
{
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ size, 0.0, 0.0 }, rotation)), RED);
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ 0.0, size, 0.0 }, rotation)), GREEN);
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ 0.0, 0.0, size }, rotation)), BLUE);
}

static inline void DrawSkeleton(TransformData* xformData, bool drawEndSites, Color color, Color endSiteColor)
{
    for (int i = 0; i < xformData->jointCount; i++)
    {
        if (!xformData->endSite[i])
        {
            DrawSphereWires(
                xformData->globalPositions[i],
                0.01f,
                4,
                6,
                color);
        }
        else if (drawEndSites)
        {
            DrawCubeWiresV(
                xformData->globalPositions[i],
                (Vector3){ 0.02f, 0.02f, 0.02f },
                endSiteColor);
        }

        if (xformData->parents[i] != -1)
        {
            if (!xformData->endSite[i])
            {
                DrawLine3D(
                    xformData->globalPositions[i],
                    xformData->globalPositions[xformData->parents[i]],
                    color);
            }
            else if (drawEndSites)
            {
                DrawLine3D(
                    xformData->globalPositions[i],
                    xformData->globalPositions[xformData->parents[i]],
                    endSiteColor);
            }
        }
    }
}

static inline void DrawTransforms(TransformData* xformData)
{
    for (int i = 0; i < xformData->jointCount; i++)
    {
        if (!xformData->endSite[i])
        {
            DrawTransform(
                xformData->globalPositions[i],
                xformData->globalRotations[i],
                0.1f);
        }
    }
}

static inline void DrawWireFrames(CapsuleData* capsuleData, Color color)
{
    for (int i = 0; i < capsuleData->capsuleCount; i++)
    {
        Vector3 capsuleStart = CapsuleStart(capsuleData->capsulePositions[i], capsuleData->capsuleRotations[i], capsuleData->capsuleHalfLengths[i]);
        Vector3 capsuleEnd = CapsuleEnd(capsuleData->capsulePositions[i], capsuleData->capsuleRotations[i], capsuleData->capsuleHalfLengths[i]);
        float capsuleRadius = capsuleData->capsuleRadii[i];

        DrawSphereWires(capsuleStart, capsuleRadius, 4, 6, color);
        DrawSphereWires(capsuleEnd, capsuleRadius, 4, 6, color);
        DrawCylinderWiresEx(capsuleStart, capsuleEnd, capsuleRadius, capsuleRadius, 6, color);
    }
}

//----------------------------------------------------------------------------------
// GUI
//----------------------------------------------------------------------------------

static inline void GuiOrbitCamera(OrbitCamera* camera, CharacterData* characterData, int argc, char** argv)
{
    GuiGroupBox((Rectangle){ 20, 10, 190, 260 }, "Camera");

    GuiLabel((Rectangle){ 30, 20, 150, 20 }, "Ctrl + Left Click - Rotate");
    GuiLabel((Rectangle){ 30, 40, 150, 20 }, "Ctrl + Right Click - Pan");
    GuiLabel((Rectangle){ 30, 60, 150, 20 }, "Mouse Scroll - Zoom");
    GuiLabel((Rectangle){ 30, 80, 150, 20 }, TextFormat("Target: [% 5.3f % 5.3f % 5.3f]", camera->cam3d.target.x, camera->cam3d.target.y, camera->cam3d.target.z));
    GuiLabel((Rectangle){ 30, 100, 150, 20 }, TextFormat("Offset: [% 5.3f % 5.3f % 5.3f]", camera->offset.x, camera->offset.y, camera->offset.z));
    GuiLabel((Rectangle){ 30, 120, 150, 20 }, TextFormat("Azimuth: %5.3f", camera->azimuth));
    GuiLabel((Rectangle){ 30, 140, 150, 20 }, TextFormat("Altitude: %5.3f", camera->altitude));
    GuiLabel((Rectangle){ 30, 160, 150, 20 }, TextFormat("Distance: %5.3f", camera->distance));
    
    if (GuiButton((Rectangle){ 30, 180, 100, 20 }, "Reset"))
    {
        camera->azimuth = ArgFloat(argc, argv, "cameraAzimuth", 0.0f);
        camera->altitude = ArgFloat(argc, argv, "cameraAltitude", 0.4f);
        camera->distance = ArgFloat(argc, argv, "cameraDistance", 4.0f);
        camera->offset = ArgVector3(argc, argv, "cameraOffset", Vector3Zero());
        camera->track = ArgBool(argc, argv, "cameraTrack", true);
        camera->trackBone = ArgInt(argc, argv, "cameraTrackBone", 0);
    }

    if (characterData->count > 0)
    {
        GuiToggle((Rectangle){ 30, 210, 100, 20 }, "Track", &camera->track);
        GuiComboBox((Rectangle){ 30, 240, 150, 20 }, characterData->jointNamesCombo[characterData->active], &camera->trackBone);
    }
}

static inline void GuiRenderSettings(RenderSettings* settings, CapsuleData* capsuleData, int screenWidth, int screenHeight)
{
    GuiGroupBox((Rectangle){ screenWidth - 260, 10, 240, 470 }, "Rendering");

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 20, 100, 20 },
        "Exposure",
        TextFormat("%5.2f", settings->exposure),
        &settings->exposure,
        0.0f, 3.0f);

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 50, 100, 20 },
        "Sun Light",
        TextFormat("%5.2f", settings->sunLightStrength),
        &settings->sunLightStrength,
        0.0f, 1.0f);

    if (GuiSliderBar(
        (Rectangle){ screenWidth - 160, 80, 100, 20 },
        "Sun Softness",
        TextFormat("%5.2f", settings->sunLightConeAngle),
        &settings->sunLightConeAngle,
        0.02f, PI / 4.0f))
    {
        CapsuleDataUpdateShadowLookupTable(capsuleData, settings->sunLightConeAngle);
    }

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 110, 100, 20 },
        "Sky Light",
        TextFormat("%5.2f", settings->skyLightStrength),
        &settings->skyLightStrength,
        0.0f, 1.0f);

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 140, 100, 20 },
        "Ambient Light",
        TextFormat("%5.2f", settings->ambientLightStrength),
        &settings->ambientLightStrength,
        0.0f, 2.0f);

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 170, 100, 20 },
        "Ground Light",
        TextFormat("%5.2f", settings->groundLightStrength),
        &settings->groundLightStrength,
        0.0f, 0.5f);

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 200, 100, 20 },
        "Sun Azimuth",
        TextFormat("%5.2f", settings->sunAzimuth),
        &settings->sunAzimuth,
        -PI, PI);

    GuiSliderBar(
        (Rectangle){ screenWidth - 160, 230, 100, 20 },
        "Sun Altitude",
        TextFormat("%5.2f", settings->sunAltitude),
        &settings->sunAltitude,
        0.0f, 0.49f * PI);

    GuiCheckBox((Rectangle){ screenWidth - 250, 260, 20, 20 }, "Draw Origin", &settings->drawOrigin);
    GuiCheckBox((Rectangle){ screenWidth - 130, 260, 20, 20 }, "Draw Grid", &settings->drawGrid);
    GuiCheckBox((Rectangle){ screenWidth - 250, 290, 20, 20 }, "Draw Checker", &settings->drawChecker);
    if (GuiCheckBox((Rectangle){ screenWidth - 130, 290, 20, 20 }, "Draw Meshes", &settings->drawMeshes) && settings->drawMeshes)
    {
        settings->drawCapsules = false;
    }
    GuiCheckBox((Rectangle){ screenWidth - 250, 320, 20, 20 }, "Draw Capsules", &settings->drawCapsules);
    GuiCheckBox((Rectangle){ screenWidth - 130, 320, 20, 20 }, "Draw Wireframes", &settings->drawWireframes);
    GuiCheckBox((Rectangle){ screenWidth - 250, 350, 20, 20 }, "Draw Skeleton", &settings->drawSkeleton);
    GuiCheckBox((Rectangle){ screenWidth - 130, 350, 20, 20 }, "Draw Transforms", &settings->drawTransforms);
    GuiCheckBox((Rectangle){ screenWidth - 250, 380, 20, 20 }, "Draw AO", &settings->drawAO);
    GuiCheckBox((Rectangle){ screenWidth - 130, 380, 20, 20 }, "Draw Shadows", &settings->drawShadows);
    GuiCheckBox((Rectangle){ screenWidth - 250, 410, 20, 20 }, "Draw End Sites", &settings->drawEndSites);
    GuiCheckBox((Rectangle){ screenWidth - 130, 410, 20, 20 }, "Draw FPS", &settings->drawFPS);
    GuiCheckBox((Rectangle){ screenWidth - 250, 440, 20, 20 }, "Draw Texture", &settings->drawTexture);
    GuiLabel((Rectangle){ screenWidth - 130, 440, 100, 20 }, "H Key - Hide UI");
}

static inline void GuiCharacterData(
    CharacterData* characterData,
    GuiWindowFileDialogState* fileDialogState,
    ScrubberSettings* scrubberSettings,
    char* errMsg,
    int argc,
    char** argv)
{
    int offsetHeight = 280;
  
    GuiGroupBox((Rectangle){ 20, offsetHeight, 190, (CHARACTERS_MAX - 1) * 30 + 180 }, "Characters");

#if !defined(PLATFORM_WEB)
    if (GuiButton((Rectangle){ 30, offsetHeight + 10, 110, 20 }, "Open"))
    {
        fileDialogState->windowActive = true;
    }
#endif

    if (GuiButton((Rectangle){ 150, offsetHeight + 10, 50, 20 }, "Clear"))
    {
        characterData->count = 0;
        errMsg[0] = '\0';
        ScrubberSettingsInit(scrubberSettings, argc, argv);
        SetWindowTitle("BVHView");
   }

    for (int i = 0; i < characterData->count; i++)
    {
        char bvhNameShort[20];
        bvhNameShort[0] = '\0';
        if (strlen(characterData->names[i]) + 1 <= 18)
        {
            strcat(bvhNameShort, characterData->names[i]);
        }
        else
        {
            memcpy(bvhNameShort, characterData->names[i], 14);
            memcpy(bvhNameShort + 14, "...", 4);
        }

        bool bvhSelected = i == characterData->active;
        GuiToggle((Rectangle){ 30, offsetHeight + 40 + i * 30, 120, 20 }, bvhNameShort, &bvhSelected);

        // Show GLB/BVH type indicator
        DrawText(characterData->isGLB[i] ? "GLB" : "BVH", 155, offsetHeight + 43 + i * 30, 10, GRAY);

        if (bvhSelected && (characterData->active != i))
        {
            characterData->active = i;
            ScrubberSettingsClamp(scrubberSettings, characterData);
            
            char windowTitle[528];
            snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", characterData->filePaths[characterData->active]);
            SetWindowTitle(windowTitle);
        }

        DrawRectangleRec((Rectangle){ 180, offsetHeight + 40 + i * 30, 20, 20 }, characterData->colors[i]);
        DrawRectangleLinesEx((Rectangle){ 180, offsetHeight + 40 + i * 30, 20, 20 }, 1, GRAY);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            Vector2 mousePosition = GetMousePosition();
            if (mousePosition.x > 180 && mousePosition.x < 200 &&
                mousePosition.y > offsetHeight + 40 + i * 30 && mousePosition.y < offsetHeight + 40 + i * 30 + 20)
            {
                characterData->colorPickerActive = !characterData->colorPickerActive;
            }
        }
    }
    
    if (characterData->count > 0)
    {
        int active = characterData->active;

        // GLB animation selector (only show if active character is GLB with multiple animations)
        if (characterData->isGLB[active] && characterData->glbData[active].animCount > 1)
        {
            // Build animation names combo string
            static char animCombo[256];
            animCombo[0] = '\0';
            for (int a = 0; a < characterData->glbData[active].animCount; a++)
            {
                if (a > 0) strcat(animCombo, ";");
                if (strlen(characterData->glbData[active].animations[a].name) > 0)
                    strcat(animCombo, characterData->glbData[active].animations[a].name);
                else
                    strcat(animCombo, TextFormat("Anim %d", a));
            }

            int prevAnim = characterData->glbData[active].activeAnim;
            GuiComboBox((Rectangle){ 30, offsetHeight + 60 + (CHARACTERS_MAX - 1) * 30, 150, 20 }, animCombo, &characterData->glbData[active].activeAnim);

            if (characterData->glbData[active].activeAnim != prevAnim)
            {
                // Animation changed - reset scrubber
                ScrubberSettingsRecomputeLimits(scrubberSettings, characterData);
                ScrubberSettingsInitMaxs(scrubberSettings, characterData);
            }
        }

        // Scale controls (offset depends on whether animation selector is shown)
        int scaleY = offsetHeight + 60 + (CHARACTERS_MAX - 1) * 30;
        if (characterData->isGLB[active] && characterData->glbData[active].animCount > 1)
            scaleY += 30;

        bool scaleM = characterData->scales[active] == 1.0f;
        GuiToggle((Rectangle){ 30, scaleY, 30, 20 }, "m", &scaleM);
        if (scaleM) { characterData->scales[active] = 1.0f; }

        bool scaleCM = characterData->scales[active] == 0.01f;
        GuiToggle((Rectangle){ 65, scaleY, 30, 20 }, "cm", &scaleCM);
        if (scaleCM) { characterData->scales[active] = 0.01f; }

        bool scaleInches = characterData->scales[active] == 0.0254f;
        GuiToggle((Rectangle){ 100, scaleY, 30, 20 }, "inch", &scaleInches);
        if (scaleInches) { characterData->scales[active] = 0.0254f; }

        bool scaleFeet = characterData->scales[active] == 0.3048f;
        GuiToggle((Rectangle){ 135, scaleY, 30, 20 }, "feet", &scaleFeet);
        if (scaleFeet) { characterData->scales[active] = 0.3048f; }

        bool scaleAuto = characterData->scales[active] == characterData->autoScales[active];
        GuiToggle((Rectangle){ 170, scaleY, 30, 20 }, "auto", &scaleAuto);
        if (scaleAuto) { characterData->scales[active] = characterData->autoScales[active]; }

        GuiSliderBar(
            (Rectangle){ 70, scaleY + 30, 100, 20 },
            "Radius",
            TextFormat("%5.2f", characterData->radii[active]),
            &characterData->radii[active],
            0.01f, 0.1f);

        GuiSliderBar(
            (Rectangle){ 70, scaleY + 60, 100, 20 },
            "Opacity",
            TextFormat("%5.2f", characterData->opacities[active]),
            &characterData->opacities[active],
            0.0f, 1.0f);
    }
}

static inline void GuiScrubberSettings(
    ScrubberSettings* settings,
    CharacterData* characterData,
    int screenWidth,
    int screenHeight)
{
    if (characterData->count == 0) { return; }

    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);

    GuiGroupBox((Rectangle){ screenWidth / 2 - 600, screenHeight - 100, 1200, 90 }, "Scrubber");

    GuiLabel((Rectangle){ screenWidth / 2 - 480, screenHeight - 80, 150, 20 }, TextFormat("Frame Time: %f", frameTime));
    GuiCheckBox((Rectangle){ screenWidth / 2 - 350, screenHeight - 80, 20, 20 }, "Snap to Frame", &settings->frameSnap);
    GuiComboBox((Rectangle){ screenWidth / 2 - 240, screenHeight - 80, 100, 20 }, "Nearest;Linear;Cubic", &settings->sampleMode);

    GuiToggle((Rectangle){ screenWidth / 2 - 130, screenHeight - 80, 50, 20 }, "Inplace", &settings->inplace);
    GuiToggle((Rectangle){ screenWidth / 2 - 70, screenHeight - 80, 50, 20 }, "Loop", &settings->looping);
    GuiToggle((Rectangle){ screenWidth / 2 - 10, screenHeight - 80, 50, 20 }, "Play", &settings->playing);

    bool speed01x = settings->playSpeed == 0.1f;
    GuiToggle((Rectangle){ screenWidth / 2 + 50, screenHeight - 80, 30, 20 }, "0.1x", &speed01x); if (speed01x) { settings->playSpeed = 0.1f; }
    bool speed05x = settings->playSpeed == 0.5f;
    GuiToggle((Rectangle){ screenWidth / 2 + 90, screenHeight - 80, 30, 20 }, "0.5x", &speed05x); if (speed05x) { settings->playSpeed = 0.5f; }
    bool speed1x = settings->playSpeed == 1.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 130, screenHeight - 80, 30, 20 }, "1x", &speed1x); if (speed1x) { settings->playSpeed = 1.0f; }
    bool speed2x = settings->playSpeed == 2.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 170, screenHeight - 80, 30, 20 }, "2x", &speed2x); if (speed2x) { settings->playSpeed = 2.0f; }
    bool speed4x = settings->playSpeed == 4.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 210, screenHeight - 80, 30, 20 }, "4x", &speed4x); if (speed4x) { settings->playSpeed = 4.0f; }
    GuiSliderBar((Rectangle){ screenWidth / 2 + 250, screenHeight - 80, 70, 20 }, "", TextFormat("%5.2fx", settings->playSpeed), &settings->playSpeed, 0.0f, 4.0f);

    int frame = ClampInt((int)(settings->playTime / frameTime + 0.5f), settings->frameMin, settings->frameMax);

    if (GuiValueBox(
        (Rectangle){ screenWidth / 2 - 540, screenHeight - 80, 50, 20 },
        "Min   ", &settings->frameMinSelect, 0, settings->frameLimit, settings->frameMinEdit))
    {
        settings->frameMinEdit = !settings->frameMinEdit;
        if (!settings->frameMinEdit)
        {
            settings->frameMin = settings->frameMinSelect;
            ScrubberSettingsClamp(settings, characterData);
        }
    }

    if (GuiValueBox(
        (Rectangle){ screenWidth / 2 + 470, screenHeight - 80, 50, 20 },
        "Max   ", &settings->frameMaxSelect, 0, settings->frameLimit, settings->frameMaxEdit))
    {
        settings->frameMaxEdit = !settings->frameMaxEdit;

        if (!settings->frameMaxEdit)
        {
            settings->frameMax = settings->frameMaxSelect;
            ScrubberSettingsClamp(settings, characterData);
        }
    }

    GuiLabel(
        (Rectangle){ screenWidth / 2 + 530, screenHeight - 80, 100, 20 },
        TextFormat("of %i", settings->frameLimit));

    float frameFloatPrev = settings->frameSnap ? (float)frame : settings->playTime / frameTime;
    float frameFloat = frameFloatPrev;

    GuiSliderBar(
        (Rectangle){ screenWidth / 2 - 540, screenHeight - 50, 1080, 20 },
        TextFormat("%5.2f", settings->playTime),
        TextFormat("%i", frame),
        &frameFloat,
        (float)settings->frameMin, (float)settings->frameMax);

    if (frameFloat != frameFloatPrev)
    {
        if (settings->frameSnap)
        {
            frame = ClampInt((int)(frameFloat + 0.5f), settings->frameMin, settings->frameMax);
            settings->playTime = Clamp(frame * frameTime, settings->timeMin, settings->timeMax);
        }
        else
        {
            settings->playTime = Clamp(frameFloat * frameTime, settings->timeMin, settings->timeMax);
        }
    }
}

//----------------------------------------------------------------------------------
// Application
//----------------------------------------------------------------------------------

// Structure containing all of the application state which we can then pass to the Update function
typedef struct {

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

} ApplicationState;

// Update function - what is called to "tick" the application.
static void ApplicationUpdate(void* voidApplicationState)
{
    ApplicationState* app = voidApplicationState;

    // Process File Dialog

    if (app->fileDialogState.SelectFilePressed)
    {
        if (IsFileExtension(app->fileDialogState.fileNameText, ".bvh") ||
            IsFileExtension(app->fileDialogState.fileNameText, ".glb") ||
            IsFileExtension(app->fileDialogState.fileNameText, ".gltf"))
        {
            char fileNameToLoad[2048];
            snprintf(fileNameToLoad, sizeof(fileNameToLoad), "%s/%s", app->fileDialogState.dirPathText, app->fileDialogState.fileNameText);

            if (CharacterDataLoadFromFile(&app->characterData, fileNameToLoad, app->errMsg, 512))
            {
                app->characterData.active = app->characterData.count - 1;

                // Auto-toggle render mode if a skinned mesh was loaded
                if (app->characterData.hasSkinnedMesh)
                {
                    app->renderSettings.drawMeshes = true;
                    app->renderSettings.drawCapsules = false;
                }

                CapsuleDataUpdateForCharacters(&app->capsuleData, &app->characterData);
                ScrubberSettingsRecomputeLimits(&app->scrubberSettings, &app->characterData);
                ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);

                char windowTitle[528];
                snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app->characterData.filePaths[app->characterData.active]);
                SetWindowTitle(windowTitle);
            }
        }
        else
        {
            snprintf(app->errMsg, 512, "Error: File '%.*s' is not a supported animation file (.bvh, .glb, .gltf).", 400, app->fileDialogState.fileNameText);
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
            {
                app->characterData.active = app->characterData.count - 1;
            }
        }

        UnloadDroppedFiles(droppedFiles);

        if (app->characterData.count > prevBvhCount)
        {
            // Auto-toggle render mode if a skinned mesh was loaded
            if (app->characterData.hasSkinnedMesh)
            {
                app->renderSettings.drawMeshes = true;
                app->renderSettings.drawCapsules = false;
            }

            CapsuleDataUpdateForCharacters(&app->capsuleData, &app->characterData);
            ScrubberSettingsRecomputeLimits(&app->scrubberSettings, &app->characterData);
            ScrubberSettingsInitMaxs(&app->scrubberSettings, &app->characterData);

            char windowTitle[528];
            snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app->characterData.filePaths[app->characterData.active]);
            SetWindowTitle(windowTitle);
        }
    }

    // Process Key Presses

    if (IsKeyPressed(KEY_H) && !app->fileDialogState.windowActive)
    {
        app->renderSettings.drawUI = !app->renderSettings.drawUI;
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
            {
                app->scrubberSettings.playTime =
                    fmodf(app->scrubberSettings.playTime - app->scrubberSettings.timeMin, loopSpan) +
                    app->scrubberSettings.timeMin;
            }
            else
            {
                app->scrubberSettings.playTime = app->scrubberSettings.timeMax;
            }
        }
    }

    // Sample Animation Data

    for (int i = 0; i < app->characterData.count; i++)
    {
        if (app->characterData.isGLB[i])
        {
            // GLB animation: sample directly from animation time
            TransformDataSampleFrameGLB(
                &app->characterData.xformData[i],
                &app->characterData.glbData[i],
                app->scrubberSettings.playTime,
                app->characterData.scales[i]);
        }
        else if (app->scrubberSettings.sampleMode == 0)
        {
            TransformDataSampleFrameNearest(
                &app->characterData.xformData[i],
                &app->characterData.bvhData[i],
                app->scrubberSettings.playTime,
                app->characterData.scales[i]);
        }
        else if (app->scrubberSettings.sampleMode == 1)
        {
            TransformDataSampleFrameLinear(
                &app->characterData.xformData[i],
                &app->characterData.xformTmp0[i],
                &app->characterData.xformTmp1[i],
                &app->characterData.bvhData[i],
                app->scrubberSettings.playTime,
                app->characterData.scales[i]);
        }
        else
        {
            TransformDataSampleFrameCubic(
                &app->characterData.xformData[i],
                &app->characterData.xformTmp0[i],
                &app->characterData.xformTmp1[i],
                &app->characterData.xformTmp2[i],
                &app->characterData.xformTmp3[i],
                &app->characterData.bvhData[i],
                app->scrubberSettings.playTime,
                app->characterData.scales[i]);
        }

        if (app->scrubberSettings.inplace)
        {
            // Remove Translation on ground Plane
          
            app->characterData.xformData[i].localPositions[0].x = 0.0f;
            app->characterData.xformData[i].localPositions[0].z = 0.0f;
            
            // Attempt to extract rotation around vertical axis (this does not work 
            // for all animations but is pretty effective for almost all of them)
            
            Quaternion verticalRotation = QuaternionInvert(QuaternionNormalize((Quaternion){
                0.0f,
                app->characterData.xformData[i].localRotations[0].y,
                0.0f,
                app->characterData.xformData[i].localRotations[0].w,
            }));
            
            // Remove rotation around vertical axis
            
            app->characterData.xformData[i].localRotations[0] = QuaternionMultiply(
                verticalRotation, 
                app->characterData.xformData[i].localRotations[0]);
        }

        TransformDataForwardKinematics(&app->characterData.xformData[i]);
    }

    // Update Camera

    Vector3 cameraTarget = (Vector3){ 0.0f, 1.0f, 0.0f };

    if (app->characterData.count > 0 &&
        app->camera.track &&
        app->camera.trackBone < app->characterData.xformData[app->characterData.active].jointCount)
    {
        cameraTarget = app->characterData.xformData[app->characterData.active].globalPositions[app->camera.trackBone];
    }

    if (!app->fileDialogState.windowActive)
    {
        bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool middleDown = IsMouseButtonDown(2);
        OrbitCameraUpdate(
            &app->camera,
            cameraTarget,
            (middleDown && !shiftHeld) ? GetMouseDelta().x : 0.0f,
            (middleDown && !shiftHeld) ? GetMouseDelta().y : 0.0f,
            (middleDown && shiftHeld) ? -GetMouseDelta().x : 0.0f,
            (middleDown && shiftHeld) ? -GetMouseDelta().y : 0.0f,
            GetMouseWheelMove(),
            GetFrameTime());
    }

    // Create Capsules

    CapsuleDataReset(&app->capsuleData);
    for (int i = 0; i < app->characterData.count; i++)
    {
        CapsuleDataAppendFromTransformData(
            &app->capsuleData,
            &app->characterData.xformData[i],
            app->characterData.radii[i],
            app->characterData.colors[i],
            app->characterData.opacities[i],
            !app->renderSettings.drawEndSites);
    }

    PROFILE_END(Update);

    // Rendering

    Frustum frustum = FrustumFromCameraMatrices(
        GetCameraProjectionMatrix(&app->camera.cam3d, app->screenHeight / app->screenWidth),
        GetCameraViewMatrix(&app->camera.cam3d));

    BeginDrawing();

    PROFILE_BEGIN(Rendering);

    ClearBackground(app->renderSettings.backgroundColor);

    BeginMode3D(app->camera.cam3d);

    // Set shader uniforms that don't change based on the object being drawn

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
        Vector3 groundColor = { 0.75f, 0.75f, 0.75f };

        SetShaderValue(app->shader, app->uniforms.isCapsule, &groundIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.useTexture, &groundUseTexture, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.objectColor, &groundColor, SHADER_UNIFORM_VEC3);

        // Draw ground in a grid of 10x10, 2 meter wide segments.
        
        for (int i = 0; i < 11; i++)
        {
            for (int j = 0; j < 11; j++)
            {
                // Check if we can cull ground segment

                Vector3 groundSegmentPosition =
                {
                    (((float)i / 10) - 0.5f) * 20.0f,
                    0.0f,
                    (((float)j / 10) - 0.5f) * 20.0f,
                };                
                
                if (!FrustumContainsSphere(frustum, groundSegmentPosition, sqrtf(2.0f)))
                {
                    continue;
                }

                PROFILE_BEGIN(RenderingGroundSegment);
                
                // Gather all capsules casting AO on this ground segment
                
                PROFILE_BEGIN(RenderingGroundSegmentAO);
                
                app->capsuleData.aoCapsuleCount = 0;
                if (app->renderSettings.drawCapsules && app->renderSettings.drawAO)
                {  
                    CapsuleDataUpdateAOCapsulesForGroundSegment(&app->capsuleData, groundSegmentPosition);
                }
                int aoCapsuleCount = MinInt(app->capsuleData.aoCapsuleCount, AO_CAPSULES_MAX);

                PROFILE_END(RenderingGroundSegmentAO);
                
                SetShaderValue(app->shader, app->uniforms.aoCapsuleCount, &aoCapsuleCount, SHADER_UNIFORM_INT);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleStarts, app->capsuleData.aoCapsuleStarts, SHADER_UNIFORM_VEC3, aoCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleVectors, app->capsuleData.aoCapsuleVectors, SHADER_UNIFORM_VEC3, aoCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.aoCapsuleRadii, app->capsuleData.aoCapsuleRadii, SHADER_UNIFORM_FLOAT, aoCapsuleCount);
                
                // Gather all capsules casting shadows on this ground segment
                
                PROFILE_BEGIN(RenderingGroundSegmentShadow);

                app->capsuleData.shadowCapsuleCount = 0;
                if (app->renderSettings.drawCapsules && app->renderSettings.drawShadows)
                {
                    CapsuleDataUpdateShadowCapsulesForGroundSegment(&app->capsuleData, groundSegmentPosition, sunLightDir, app->renderSettings.sunLightConeAngle);
                }
                int shadowCapsuleCount = MinInt(app->capsuleData.shadowCapsuleCount, SHADOW_CAPSULES_MAX);

                PROFILE_END(RenderingGroundSegmentShadow);
                
                SetShaderValue(app->shader, app->uniforms.shadowCapsuleCount, &shadowCapsuleCount, SHADER_UNIFORM_INT);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleStarts, app->capsuleData.shadowCapsuleStarts, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleVectors, app->capsuleData.shadowCapsuleVectors, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
                SetShaderValueV(app->shader, app->uniforms.shadowCapsuleRadii, app->capsuleData.shadowCapsuleRadii, SHADER_UNIFORM_FLOAT, shadowCapsuleCount);
                
                // Draw

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
            if (!app->characterData.isGLB[i]) continue;

            GLBData* glb = &app->characterData.glbData[i];
            if (glb->model.meshCount == 0) continue;

            Vector3 meshColor = {
                app->characterData.colors[i].r / 255.0f,
                app->characterData.colors[i].g / 255.0f,
                app->characterData.colors[i].b / 255.0f
            };
            float meshOpacity = app->characterData.opacities[i];

            Model drawModel = glb->model;

            for (int materialIndex = 0; materialIndex < drawModel.materialCount; materialIndex++)
            {
                drawModel.materials[materialIndex].shader = app->shader;
            }

            drawModel.transform = GLBDataGetModelTransform(glb, app->characterData.scales[i], app->scrubberSettings.inplace);

            SetShaderValue(app->shader, app->uniforms.objectColor, &meshColor, SHADER_UNIFORM_VEC3);
            SetShaderValue(app->shader, app->uniforms.objectOpacity, &meshOpacity, SHADER_UNIFORM_FLOAT);

            // Detect if any material has a real albedo texture (id > 0, not the default 1x1 white pixel)
            int modelHasTexture = 0;
            if (app->renderSettings.drawTexture)
            {
                unsigned int defaultTexId = rlGetTextureIdDefault();
                for (int materialIndex = 0; materialIndex < drawModel.materialCount; materialIndex++)
                {
                    Texture2D tex = drawModel.materials[materialIndex].maps[MATERIAL_MAP_ALBEDO].texture;
                    if (tex.id > 0 && tex.id != defaultTexId && (tex.width > 1 || tex.height > 1))
                    {
                        modelHasTexture = 1;
                        break;
                    }
                }
            }
            SetShaderValue(app->shader, app->uniforms.useTexture, &modelHasTexture, SHADER_UNIFORM_INT);

            if (meshOpacity < 1.0f)
            {
                rlDrawRenderBatchActive();
                rlDisableDepthMask();
            }

            DrawModel(drawModel, Vector3Zero(), 1.0f, WHITE);

            if (meshOpacity < 1.0f)
            {
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
            }
        }
    }

    // Draw Capsules

    PROFILE_BEGIN(RenderingCapsules);

    if (app->renderSettings.drawCapsules)
    {
        // Depth sort back to front for transparency

        for (int i = 0; i < app->capsuleData.capsuleCount; i++)
        {
            app->capsuleData.capsuleSort[i].index = i;
            app->capsuleData.capsuleSort[i].value = Vector3Distance(app->camera.cam3d.position, app->capsuleData.capsulePositions[i]);
        }

        qsort(app->capsuleData.capsuleSort, app->capsuleData.capsuleCount, sizeof(CapsuleSort), CapsuleSortCompareLess);

        // Render

        int capsuleIsCapsule = 1;
        int capsuleUseTexture = 0;
        SetShaderValue(app->shader, app->uniforms.isCapsule, &capsuleIsCapsule, SHADER_UNIFORM_INT);
        SetShaderValue(app->shader, app->uniforms.useTexture, &capsuleUseTexture, SHADER_UNIFORM_INT);

        for (int i = 0; i < app->capsuleData.capsuleCount; i++)
        {
            int j = app->capsuleData.capsuleSort[i].index;
            
            // Check if we can cull capsule
            
            Vector3 capsulePosition = app->capsuleData.capsulePositions[j];
            float capsuleHalfLength = app->capsuleData.capsuleHalfLengths[j];
            float capsuleRadius = app->capsuleData.capsuleRadii[j];

            if (!FrustumContainsSphere(frustum, capsulePosition, capsuleHalfLength + capsuleRadius))
            {
                continue;
            }
            
            PROFILE_BEGIN(RenderingCapsulesCapsule);
            
            // If capsule is semi-transparent disable depth mask
            
            if (app->capsuleData.capsuleOpacities[j] < 1.0f)
            {
                rlDrawRenderBatchActive();
                rlDisableDepthMask();
            }
            
            // Set shader properties
            
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
            
            // Find all capsules casting AO on this capsule

            PROFILE_BEGIN(RenderingCapsulesCapsuleAO);

            app->capsuleData.aoCapsuleCount = 0;
            if (app->renderSettings.drawAO)
            {
                CapsuleDataUpdateAOCapsulesForCapsule(&app->capsuleData, j);
            }
            int aoCapsuleCount = MinInt(app->capsuleData.aoCapsuleCount, AO_CAPSULES_MAX);
            
            PROFILE_END(RenderingCapsulesCapsuleAO);

            SetShaderValue(app->shader, app->uniforms.aoCapsuleCount, &aoCapsuleCount, SHADER_UNIFORM_INT);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleStarts, app->capsuleData.aoCapsuleStarts, SHADER_UNIFORM_VEC3, aoCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleVectors, app->capsuleData.aoCapsuleVectors, SHADER_UNIFORM_VEC3, aoCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.aoCapsuleRadii, app->capsuleData.aoCapsuleRadii, SHADER_UNIFORM_FLOAT, aoCapsuleCount);

            // Find all capsules casting shadows on this capsule

            PROFILE_BEGIN(RenderingCapsulesCapsuleShadow);

            app->capsuleData.shadowCapsuleCount = 0;
            if (app->renderSettings.drawShadows)
            {
                CapsuleDataUpdateShadowCapsulesForCapsule(&app->capsuleData, j, sunLightDir, app->renderSettings.sunLightConeAngle);
            }
            int shadowCapsuleCount = MinInt(app->capsuleData.shadowCapsuleCount, SHADOW_CAPSULES_MAX);

            PROFILE_END(RenderingCapsulesCapsuleShadow);

            SetShaderValue(app->shader, app->uniforms.shadowCapsuleCount, &shadowCapsuleCount, SHADER_UNIFORM_INT);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleStarts, app->capsuleData.shadowCapsuleStarts, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleVectors, app->capsuleData.shadowCapsuleVectors, SHADER_UNIFORM_VEC3, shadowCapsuleCount);
            SetShaderValueV(app->shader, app->uniforms.shadowCapsuleRadii, app->capsuleData.shadowCapsuleRadii, SHADER_UNIFORM_FLOAT, shadowCapsuleCount);

            // Draw

            DrawModel(app->capsuleModel, Vector3Zero(), 1.0f, WHITE);
            
            // Reset depth mask if rendered semi-transparent
            
            if (app->capsuleData.capsuleOpacities[j] < 1.0f)
            {
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
            }

            PROFILE_END(RenderingCapsulesCapsule);
        }
    }

    PROFILE_END(RenderingCapsules);

    // Grid

    if (app->renderSettings.drawGrid)
    {
        DrawGrid(20, 1.0f);
    }

    // Origin

    if (app->renderSettings.drawOrigin)
    {
        DrawTransform(
            (Vector3){ 0.0f, 0.01f, 0.0f },
            QuaternionIdentity(),
            1.0f);
    }

    // Disable Depth Test

    rlDrawRenderBatchActive();
    rlDisableDepthTest();

    // Draw Capsule Wireframes

    if (app->renderSettings.drawWireframes)
    {
        DrawWireFrames(&app->capsuleData, DARKGRAY);
    }

    // Draw Bones

    if (app->renderSettings.drawSkeleton)
    {
        for (int i = 0; i < app->characterData.count; i++)
        {
            DrawSkeleton(
                &app->characterData.xformData[i],
                app->renderSettings.drawEndSites,
                DARKGRAY,
                GRAY);
        }
    }

    // Draw Joint Transforms

    if (app->renderSettings.drawTransforms)
    {
        for (int i = 0; i < app->characterData.count; i++)
        {
            DrawTransforms(&app->characterData.xformData[i]);
        }
    }

    // Re-Enable Depth Test

    rlDrawRenderBatchActive();
    rlEnableDepthTest();

    // Rendering Done

    EndMode3D();

    PROFILE_END(Rendering);

    // Draw UI

    PROFILE_BEGIN(Gui);

    if (app->renderSettings.drawUI)
    {
        if (app->fileDialogState.windowActive) { GuiLock(); }

        // Error Message

        DrawText(app->errMsg, 250, 20, 15, RED);

        if (app->characterData.count == 0)
        {
            DrawText("Drag and Drop .bvh / .glb / .gltf files to open them.",
              app->screenWidth / 2 - 370, app->screenHeight / 2 - 15, 30, DARKGRAY);
        }

        // Render Settings

        GuiRenderSettings(&app->renderSettings, &app->capsuleData, app->screenWidth, app->screenHeight);

        // FPS

        if (app->renderSettings.drawFPS)
        {
            DrawFPS(230, 10);
        }

        // Camera Settings

        GuiOrbitCamera(&app->camera, &app->characterData, app->argc, app->argv);

        // Characters

        GuiCharacterData(&app->characterData, &app->fileDialogState, &app->scrubberSettings, app->errMsg, app->argc, app->argv);

        // Color Picker

        if (app->characterData.colorPickerActive)
        {
            GuiGroupBox((Rectangle){ app->screenWidth - 180, 450, 160, 140 }, "Color Picker");
            GuiColorPicker((Rectangle){ app->screenWidth - 165, 465, 110, 110 }, NULL, &app->characterData.colors[app->characterData.active]);
        }

        // Scrubber

        GuiScrubberSettings(&app->scrubberSettings, &app->characterData, app->screenWidth, app->screenHeight);

        // File Dialog

        if (app->fileDialogState.windowActive) { GuiUnlock(); }
        
        GuiWindowFileDialog(&app->fileDialogState);
    }

    PROFILE_END(Gui);

#if defined(ENABLE_PROFILE) && defined(_WIN32)

    // Display Profile Records

    PROFILE_TICKERS_UPDATE();
    
    for (int i = 0; i < globalProfileRecords.num; i++)
    {
        GuiLabel((Rectangle){ 260, 10 + (float)i * 20, 200, 20 }, globalProfileRecords.records[i]->name);
        GuiLabel((Rectangle){ 450, 10 + (float)i * 20, 100, 20 }, TextFormat("%6.1f us", globalProfileTickers.times[i]));
        GuiLabel((Rectangle){ 550, 10 + (float)i * 20, 100, 20 }, TextFormat("%i calls", globalProfileTickers.samples[i]));
    }
#endif

    // Done

    EndDrawing();
}

//----------------------------------------------------------------------------------
// Main
//----------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    PROFILE_INIT();
    PROFILE_TICKERS_INIT();
    
    // Init Application State
    
    ApplicationState app;
    app.argc = argc;
    app.argv = argv;    
    app.screenWidth = ArgInt(argc, argv, "screenWidth", 1920);
    app.screenHeight = ArgInt(argc, argv, "screenHeight", 1080);
    
    // Init Window

    SetConfigFlags(FLAG_VSYNC_HINT);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(app.screenWidth, app.screenHeight, "BVHView");
    SetTargetFPS(0);  // 0 = unlimited, VSYNC will sync with monitor refresh rate

    // Camera

    OrbitCameraInit(&app.camera, argc, argv);

    // Shader

    app.shader = LoadShaderFromMemory(shaderVS, shaderFS);
    ShaderUniformsInit(&app.uniforms, app.shader);

    // Models

    app.groundPlaneMesh = GenMeshPlane(2.0f, 2.0f, 1, 1);
    app.groundPlaneModel = LoadModelFromMesh(app.groundPlaneMesh);
    app.groundPlaneModel.materials[0].shader = app.shader;

    app.capsuleModel = LoadOBJFromMemory(capsuleOBJ);
    app.capsuleModel.materials[0].shader = app.shader;

    // Character Data

    CharacterDataInit(&app.characterData, argc, argv);

    // Capsule Data

    CapsuleDataInit(&app.capsuleData);

    // Scrubber Settings

    ScrubberSettingsInit(&app.scrubberSettings, argc, argv);

    // Render Settings

    RenderSettingsInit(&app.renderSettings, argc, argv);
    CapsuleDataUpdateShadowLookupTable(&app.capsuleData, app.renderSettings.sunLightConeAngle);

    // File Dialog

    app.fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());

    // Reset Error Message
    
    app.errMsg[0] = '\0';

    // Load any files given as command line arguments

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-') { continue; }

        CharacterDataLoadFromFile(&app.characterData, argv[i], app.errMsg, 512);
    }

    // If any characters loaded, update capsules and scrubber

    if (app.characterData.count > 0)
    {
        app.characterData.active = app.characterData.count - 1;

        CapsuleDataUpdateForCharacters(&app.capsuleData, &app.characterData);
        ScrubberSettingsRecomputeLimits(&app.scrubberSettings, &app.characterData);
        ScrubberSettingsInitMaxs(&app.scrubberSettings, &app.characterData);
        
        char windowTitle[528];
        snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", app.characterData.filePaths[app.characterData.active]);
        SetWindowTitle(windowTitle);
    }

    // Game Loop

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop_arg(ApplicationUpdate, &app, 0, 1);
#else
    while (!WindowShouldClose())
    {
        ApplicationUpdate(&app);
    }
#endif

    // Unload and finish

    CapsuleDataFree(&app.capsuleData);
    CharacterDataFree(&app.characterData);

    UnloadModel(app.capsuleModel);
    UnloadModel(app.groundPlaneModel);
    UnloadShader(app.shader);

    CloseWindow();

    return 0;
}



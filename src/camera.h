#ifndef CAMERA_H
#define CAMERA_H

#include "raylib.h"

// Default orbit target position (world origin)
#define CAMERA_DEFAULT_TARGET ((Vector3){ 0.0f, 0.5f, 0.0f })

typedef struct OrbitCamera {
    Camera3D cam3d;
    float azimuth;
    float altitude;
    float distance;
    Vector3 offset;
    bool track;
    int trackBone;
    bool showSkeletonPanel;
    int selectedBone;
} OrbitCamera;

void OrbitCameraInit(OrbitCamera* camera, int argc, char** argv);
void OrbitCameraUpdate(OrbitCamera* camera, Vector3 target, float azimuthDelta, float altitudeDelta, float offsetDeltaX, float offsetDeltaY, float mouseWheel, float dt);

#endif // CAMERA_H

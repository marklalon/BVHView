/*******************************************************************************************
*
*    render_settings.h - RenderSettings struct and Init declaration
*
*******************************************************************************************/

#ifndef RENDER_SETTINGS_H
#define RENDER_SETTINGS_H

#include <stdbool.h>
#include "raylib.h"

#define DEFAULT_BACKGROUND_COLOR (Color){ 180, 180, 180, 255 }

typedef struct RenderSettings {
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
    bool enableLighting;
    bool drawPBR;
    bool drawFPS;
    bool drawTexture;
    bool drawBindPose;
    bool drawUI;
} RenderSettings;

void RenderSettingsInit(RenderSettings* settings, int argc, char** argv);

#endif // RENDER_SETTINGS_H

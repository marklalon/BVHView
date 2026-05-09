/*******************************************************************************************
*
*    render_settings.h - RenderSettings struct and Init declaration
*
*******************************************************************************************/

#ifndef RENDER_SETTINGS_H
#define RENDER_SETTINGS_H

#include <stdbool.h>
#include "raylib.h"

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
    bool drawShadows;
    bool drawEndSites;
    bool drawFPS;
    bool drawTexture;
    bool drawUI;
} RenderSettings;

void RenderSettingsInit(RenderSettings* settings, int argc, char** argv);

#endif // RENDER_SETTINGS_H

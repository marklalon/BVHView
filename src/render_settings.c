/*******************************************************************************************
*
*    render_settings.c - RenderSettingsInit implementation
*
*******************************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include "render_settings.h"
#include "argparse.h"

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

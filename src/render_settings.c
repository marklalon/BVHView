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
    settings->backgroundColor = ArgColor(argc, argv, "backgroundColor", DEFAULT_BACKGROUND_COLOR);
    settings->lightConeAngle = ArgFloat(argc, argv, "lightConeAngle", 0.2f);
    settings->lightAzimuth = ArgFloat(argc, argv, "lightAzimuth", PI / 4.0f);
    settings->lightAltitude = ArgFloat(argc, argv, "lightAltitude", 0.8f);
    settings->sunStrength = ArgFloat(argc, argv, "sunStrength", 0.25f);
    settings->ambientStrength = ArgFloat(argc, argv, "ambientStrength", 1.0f);
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
    settings->enableLighting = ArgBool(argc, argv, "enableLighting", true);
    settings->drawPBR = ArgBool(argc, argv, "drawPBR", false);
    settings->drawFPS = ArgBool(argc, argv, "drawFPS", false);
    settings->drawTexture = ArgBool(argc, argv, "drawTexture", true);
    settings->drawBindPose = ArgBool(argc, argv, "drawBindPose", false);
    settings->drawUI = ArgBool(argc, argv, "drawUI", true);
}

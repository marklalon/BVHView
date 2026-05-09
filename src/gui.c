/*******************************************************************************************
*
*    gui.c - GUI function implementations
*
*******************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "raygui.h"
#include "gui.h"
#include "app.h"
#include "character_data.h"
#include "scrubber.h"
#include "render_settings.h"
#include "capsule_data.h"
#include "math_utils.h"
#include "argparse.h"

#define RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT 24
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "../examples/custom_file_dialog/gui_window_file_dialog.h"

void GuiOrbitCamera(OrbitCamera* camera, CharacterData* characterData, int argc, char** argv)
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
        bool skeletonToggle = camera->showSkeletonPanel;
        GuiToggle((Rectangle){ 30, 240, 100, 20 }, "Skeleton", &skeletonToggle);
        int ci = characterData->active;
        if (ci >= 0 && ci < characterData->count)
        {
            int jointCount = characterData->xformData[ci].jointCount;
            char jointText[32];
            snprintf(jointText, sizeof(jointText), "%d", jointCount);
            DrawText(jointText, 135, 245, 10, GRAY);
        }
        if (skeletonToggle != camera->showSkeletonPanel)
        {
            camera->showSkeletonPanel = skeletonToggle;
            if (!skeletonToggle) camera->selectedBone = -1;
        }
    }
}

int GetJointDepth(int jointIndex, const int* parents)
{
    int depth = 0;
    int p = parents[jointIndex];
    while (p != -1) { depth++; p = parents[p]; }
    return depth;
}

void GuiSkeletonPanel(OrbitCamera* camera, CharacterData* characterData, int screenWidth, int screenHeight)
{
    int ci = characterData->active;
    if (ci < 0 || ci >= characterData->count) return;
    TransformData* xform = &characterData->xformData[ci];
    int jointCount = xform->jointCount;
    const int* boneParents = xform->parents;
    static const char* s_names[1024];
    const char** names = s_names;
    if (characterData->isGLB[ci])
    {
        GLBData* glb = &characterData->glbData[ci];
        for (int i = 0; i < jointCount && i < 1024; i++)
        {
            int origIdx = glb->topoOrder[i];
            names[i] = glb->model.skeleton.bones[origIdx].name;
        }
    }
    else
    {
        for (int i = 0; i < jointCount && i < 1024; i++)
            names[i] = characterData->bvhData[ci].joints[i].name;
    }
    int panelWidth = 260;
    int panelHeight = 600;
    int panelX = 220;
    int panelY = 10;
    GuiGroupBox((Rectangle){ panelX, panelY, panelWidth, panelHeight }, "Skeleton");
    int itemHeight = 22;
    float borderWidth = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    float scrollBarWidth = (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    int scrollAreaHeight = panelHeight - 12;
    int contentHeight = jointCount * itemHeight;
    Rectangle scrollBounds = { (float)panelX + 8, (float)panelY + 8, (float)panelWidth - 16, (float)scrollAreaHeight };
    Rectangle content = { 0, 0, scrollBounds.width - 2.0f*borderWidth - scrollBarWidth, (float)contentHeight };
    static Vector2 scroll = { 0, 0 };
    Rectangle view = { 0 };
    GuiScrollPanel(scrollBounds, NULL, content, &scroll, &view);
    int visibleStart = MaxInt(0, (int)(-scroll.y / itemHeight));
    int visibleCount = (int)(view.height / itemHeight) + 2;
    if (visibleStart >= jointCount) visibleStart = jointCount - 1;
    if (visibleStart < 0) visibleStart = 0;
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = visibleStart; i < jointCount && i < visibleStart + visibleCount; i++)
    {
        int depth = GetJointDepth(i, boneParents);
        int indent = depth * 2;
        static char displayName[256];
        int pos = 0;
        for (int s = 0; s < indent && pos < 250; s++) displayName[pos++] = ' ';
        const char* nm = names[i];
        int nameLen = (int)strlen(nm);
        int maxNameLen = 255 - pos;
        if (nameLen > maxNameLen) nameLen = maxNameLen;
        memcpy(displayName + pos, nm, nameLen);
        pos += nameLen;
        displayName[pos] = '\0';
        Rectangle itemRec = { view.x, view.y + scroll.y + (float)(i * itemHeight), view.width, (float)itemHeight - 2 };
        if (i == camera->selectedBone)
        {
            DrawRectangleRec(itemRec, LIGHTGRAY);
            DrawRectangleLines((int)itemRec.x, (int)itemRec.y, (int)itemRec.width, (int)itemRec.height, DARKGRAY);
        }
        if (GuiLabelButton(itemRec, displayName))
        {
            camera->selectedBone = i;
            if (camera->track) camera->trackBone = i;
        }
    }
    EndScissorMode();
}

void GuiRenderSettings(RenderSettings* settings, CapsuleData* capsuleData, int screenWidth, int screenHeight)
{
    GuiGroupBox((Rectangle){ screenWidth - 260, 10, 240, 470 }, "Rendering");
    GuiSliderBar((Rectangle){ screenWidth - 160, 20, 100, 20 }, "Exposure", TextFormat("%5.2f", settings->exposure), &settings->exposure, 0.0f, 3.0f);
    GuiSliderBar((Rectangle){ screenWidth - 160, 50, 100, 20 }, "Sun Light", TextFormat("%5.2f", settings->sunLightStrength), &settings->sunLightStrength, 0.0f, 1.0f);
    if (GuiSliderBar((Rectangle){ screenWidth - 160, 80, 100, 20 }, "Sun Softness", TextFormat("%5.2f", settings->sunLightConeAngle), &settings->sunLightConeAngle, 0.02f, PI / 4.0f))
        CapsuleDataUpdateShadowLookupTable(capsuleData, settings->sunLightConeAngle);
    GuiSliderBar((Rectangle){ screenWidth - 160, 110, 100, 20 }, "Sky Light", TextFormat("%5.2f", settings->skyLightStrength), &settings->skyLightStrength, 0.0f, 1.0f);
    GuiSliderBar((Rectangle){ screenWidth - 160, 140, 100, 20 }, "Ambient Light", TextFormat("%5.2f", settings->ambientLightStrength), &settings->ambientLightStrength, 0.0f, 2.0f);
    GuiSliderBar((Rectangle){ screenWidth - 160, 170, 100, 20 }, "Ground Light", TextFormat("%5.2f", settings->groundLightStrength), &settings->groundLightStrength, 0.0f, 0.5f);
    GuiSliderBar((Rectangle){ screenWidth - 160, 200, 100, 20 }, "Sun Azimuth", TextFormat("%5.2f", settings->sunAzimuth), &settings->sunAzimuth, -PI, PI);
    GuiSliderBar((Rectangle){ screenWidth - 160, 230, 100, 20 }, "Sun Altitude", TextFormat("%5.2f", settings->sunAltitude), &settings->sunAltitude, 0.0f, 0.49f * PI);
    GuiCheckBox((Rectangle){ screenWidth - 250, 260, 20, 20 }, "Draw Origin", &settings->drawOrigin);
    GuiCheckBox((Rectangle){ screenWidth - 130, 260, 20, 20 }, "Draw Grid", &settings->drawGrid);
    GuiCheckBox((Rectangle){ screenWidth - 250, 290, 20, 20 }, "Draw Checker", &settings->drawChecker);
    if (GuiCheckBox((Rectangle){ screenWidth - 130, 290, 20, 20 }, "Draw Meshes", &settings->drawMeshes) && settings->drawMeshes)
        settings->drawCapsules = false;
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

void GuiCharacterData(CharacterData* characterData, GuiWindowFileDialogState* fileDialogState, ScrubberSettings* scrubberSettings, char* errMsg, int argc, char** argv)
{
    int offsetHeight = 280;
    GuiGroupBox((Rectangle){ 20, offsetHeight, 190, (CHARACTERS_MAX - 1) * 30 + 180 }, "Characters");
#if !defined(PLATFORM_WEB)
    if (GuiButton((Rectangle){ 30, offsetHeight + 10, 110, 20 }, "Open"))
        fileDialogState->windowActive = true;
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
            strcat(bvhNameShort, characterData->names[i]);
        else { memcpy(bvhNameShort, characterData->names[i], 14); memcpy(bvhNameShort + 14, "...", 4); }
        bool bvhSelected = i == characterData->active;
        GuiToggle((Rectangle){ 30, offsetHeight + 40 + i * 30, 120, 20 }, bvhNameShort, &bvhSelected);
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
            if (mousePosition.x > 180 && mousePosition.x < 200 && mousePosition.y > offsetHeight + 40 + i * 30 && mousePosition.y < offsetHeight + 40 + i * 30 + 20)
                characterData->colorPickerActive = !characterData->colorPickerActive;
        }
    }
    if (characterData->count > 0)
    {
        int active = characterData->active;
        if (characterData->isGLB[active] && characterData->glbData[active].animCount > 1)
        {
            static char animCombo[256];
            animCombo[0] = '\0';
            for (int a = 0; a < characterData->glbData[active].animCount; a++)
            {
                if (a > 0) strcat(animCombo, ";");
                if (strlen(characterData->glbData[active].animations[a].name) > 0)
                    strcat(animCombo, characterData->glbData[active].animations[a].name);
                else strcat(animCombo, TextFormat("Anim %d", a));
            }
            int prevAnim = characterData->glbData[active].activeAnim;
            GuiComboBox((Rectangle){ 30, offsetHeight + 60 + (CHARACTERS_MAX - 1) * 30, 150, 20 }, animCombo, &characterData->glbData[active].activeAnim);
            if (characterData->glbData[active].activeAnim != prevAnim)
            {
                ScrubberSettingsRecomputeLimits(scrubberSettings, characterData);
                ScrubberSettingsInitMaxs(scrubberSettings, characterData);
            }
        }
        int scaleY = offsetHeight + 60 + (CHARACTERS_MAX - 1) * 30;
        if (characterData->isGLB[active] && characterData->glbData[active].animCount > 1) scaleY += 30;
        bool scaleM = characterData->scales[active] == 1.0f;
        GuiToggle((Rectangle){ 30, scaleY, 30, 20 }, "m", &scaleM);
        if (scaleM) characterData->scales[active] = 1.0f;
        bool scaleCM = characterData->scales[active] == 0.01f;
        GuiToggle((Rectangle){ 65, scaleY, 30, 20 }, "cm", &scaleCM);
        if (scaleCM) characterData->scales[active] = 0.01f;
        bool scaleInches = characterData->scales[active] == 0.0254f;
        GuiToggle((Rectangle){ 100, scaleY, 30, 20 }, "inch", &scaleInches);
        if (scaleInches) characterData->scales[active] = 0.0254f;
        bool scaleFeet = characterData->scales[active] == 0.3048f;
        GuiToggle((Rectangle){ 135, scaleY, 30, 20 }, "feet", &scaleFeet);
        if (scaleFeet) characterData->scales[active] = 0.3048f;
        bool scaleAuto = characterData->scales[active] == characterData->autoScales[active];
        GuiToggle((Rectangle){ 170, scaleY, 30, 20 }, "auto", &scaleAuto);
        if (scaleAuto) characterData->scales[active] = characterData->autoScales[active];
        GuiSliderBar((Rectangle){ 70, scaleY + 30, 100, 20 }, "Radius", TextFormat("%5.2f", characterData->radii[active]), &characterData->radii[active], 0.01f, 0.1f);
        GuiSliderBar((Rectangle){ 70, scaleY + 60, 100, 20 }, "Opacity", TextFormat("%5.2f", characterData->opacities[active]), &characterData->opacities[active], 0.0f, 1.0f);
    }
}

void GuiScrubberSettings(ScrubberSettings* settings, CharacterData* characterData, int screenWidth, int screenHeight)
{
    if (characterData->count == 0) return;
    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);
    GuiGroupBox((Rectangle){ screenWidth / 2 - 600, screenHeight - 100, 1200, 90 }, "Scrubber");
    GuiLabel((Rectangle){ screenWidth / 2 - 480, screenHeight - 80, 150, 20 }, TextFormat("Frame Time: %f", frameTime));
    GuiCheckBox((Rectangle){ screenWidth / 2 - 350, screenHeight - 80, 20, 20 }, "Snap to Frame", &settings->frameSnap);
    GuiComboBox((Rectangle){ screenWidth / 2 - 240, screenHeight - 80, 100, 20 }, "Nearest;Linear;Cubic", &settings->sampleMode);
    GuiToggle((Rectangle){ screenWidth / 2 - 130, screenHeight - 80, 50, 20 }, "Inplace", &settings->inplace);
    GuiToggle((Rectangle){ screenWidth / 2 - 70, screenHeight - 80, 50, 20 }, "Loop", &settings->looping);
    GuiToggle((Rectangle){ screenWidth / 2 - 10, screenHeight - 80, 50, 20 }, "Play", &settings->playing);
    bool speed01x = settings->playSpeed == 0.1f;
    GuiToggle((Rectangle){ screenWidth / 2 + 50, screenHeight - 80, 30, 20 }, "0.1x", &speed01x); if (speed01x) settings->playSpeed = 0.1f;
    bool speed05x = settings->playSpeed == 0.5f;
    GuiToggle((Rectangle){ screenWidth / 2 + 90, screenHeight - 80, 30, 20 }, "0.5x", &speed05x); if (speed05x) settings->playSpeed = 0.5f;
    bool speed1x = settings->playSpeed == 1.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 130, screenHeight - 80, 30, 20 }, "1x", &speed1x); if (speed1x) settings->playSpeed = 1.0f;
    bool speed2x = settings->playSpeed == 2.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 170, screenHeight - 80, 30, 20 }, "2x", &speed2x); if (speed2x) settings->playSpeed = 2.0f;
    bool speed4x = settings->playSpeed == 4.0f;
    GuiToggle((Rectangle){ screenWidth / 2 + 210, screenHeight - 80, 30, 20 }, "4x", &speed4x); if (speed4x) settings->playSpeed = 4.0f;
    GuiSliderBar((Rectangle){ screenWidth / 2 + 250, screenHeight - 80, 70, 20 }, "", TextFormat("%5.2fx", settings->playSpeed), &settings->playSpeed, 0.0f, 4.0f);
    int frame = ClampInt((int)(settings->playTime / frameTime + 0.5f), settings->frameMin, settings->frameMax);
    if (GuiValueBox((Rectangle){ screenWidth / 2 - 540, screenHeight - 80, 50, 20 }, "Min   ", &settings->frameMinSelect, 0, settings->frameLimit, settings->frameMinEdit))
    {
        settings->frameMinEdit = !settings->frameMinEdit;
        if (!settings->frameMinEdit) { settings->frameMin = settings->frameMinSelect; ScrubberSettingsClamp(settings, characterData); }
    }
    if (GuiValueBox((Rectangle){ screenWidth / 2 + 470, screenHeight - 80, 50, 20 }, "Max   ", &settings->frameMaxSelect, 0, settings->frameLimit, settings->frameMaxEdit))
    {
        settings->frameMaxEdit = !settings->frameMaxEdit;
        if (!settings->frameMaxEdit) { settings->frameMax = settings->frameMaxSelect; ScrubberSettingsClamp(settings, characterData); }
    }
    GuiLabel((Rectangle){ screenWidth / 2 + 530, screenHeight - 80, 100, 20 }, TextFormat("of %i", settings->frameLimit));
    float frameFloatPrev = settings->frameSnap ? (float)frame : settings->playTime / frameTime;
    float frameFloat = frameFloatPrev;
    GuiSliderBar((Rectangle){ screenWidth / 2 - 540, screenHeight - 50, 1080, 20 }, TextFormat("%5.2f", settings->playTime), TextFormat("%i", frame), &frameFloat, (float)settings->frameMin, (float)settings->frameMax);
    if (frameFloat != frameFloatPrev)
    {
        if (settings->frameSnap)
        {
            frame = ClampInt((int)(frameFloat + 0.5f), settings->frameMin, settings->frameMax);
            settings->playTime = Clamp(frame * frameTime, settings->timeMin, settings->timeMax);
        }
        else settings->playTime = Clamp(frameFloat * frameTime, settings->timeMin, settings->timeMax);
    }
}

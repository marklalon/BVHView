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

#define GUI_GROUPBOX_TEXT_PADDING 4
#define GUI_GROUPBOX_TEXT_MARGIN 12

void GuiInitDarkMode(void)
{
    unsigned int cBg = ColorToInt((Color){ 59, 60, 63, 255 });       // #3B3C3F
    unsigned int cBorder = ColorToInt((Color){ 102, 102, 105, 255 }); // #666669
    unsigned int cText = ColorToInt((Color){ 224, 224, 224, 255 });  // #E0E0E0
    unsigned int cBase = ColorToInt((Color){ 69, 70, 73, 255 });     // #454649
    unsigned int cInner = ColorToInt((Color){ 49, 50, 53, 255 });    // #313235
    unsigned int cActive = ColorToInt((Color){ 58, 134, 255, 255 }); // #3A86FF
    unsigned int cFocus = ColorToInt((Color){ 0, 122, 204, 255 });   // #007ACC

    // DEFAULT (extended properties)
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, (int)cBg);

    // BUTTON (base properties for all states)
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(BUTTON, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(BUTTON, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(BUTTON, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(BUTTON, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(BUTTON, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(BUTTON, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(BUTTON, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(BUTTON, BORDER_WIDTH, 1);

    // TOGGLE
    GuiSetStyle(TOGGLE, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(TOGGLE, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(TOGGLE, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(TOGGLE, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(TOGGLE, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(TOGGLE, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(TOGGLE, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(TOGGLE, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(TOGGLE, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(TOGGLE, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(TOGGLE, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(TOGGLE, BORDER_WIDTH, 1);

    // SLIDER (also used for SLIDERBAR)
    GuiSetStyle(SLIDER, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(SLIDER, BASE_COLOR_NORMAL, (int)cInner);
    GuiSetStyle(SLIDER, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(SLIDER, BORDER_COLOR_FOCUSED, (int)ColorToInt((Color){ 74, 159, 255, 255 })); // #4A9FFF
    GuiSetStyle(SLIDER, BASE_COLOR_FOCUSED, (int)cInner);
    GuiSetStyle(SLIDER, TEXT_COLOR_FOCUSED, (int)ColorToInt((Color){ 74, 159, 255, 255 })); // #4A9FFF
    GuiSetStyle(SLIDER, BORDER_COLOR_PRESSED, (int)ColorToInt((Color){ 74, 159, 255, 255 })); // #4A9FFF
    GuiSetStyle(SLIDER, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(SLIDER, TEXT_COLOR_PRESSED, (int)ColorToInt((Color){ 74, 159, 255, 255 })); // #4A9FFF
    GuiSetStyle(SLIDER, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(SLIDER, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(SLIDER, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(SLIDER, BORDER_WIDTH, 1);

    // CHECKBOX
    GuiSetStyle(CHECKBOX, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(CHECKBOX, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(CHECKBOX, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(CHECKBOX, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(CHECKBOX, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(CHECKBOX, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(CHECKBOX, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(CHECKBOX, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(CHECKBOX, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(CHECKBOX, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(CHECKBOX, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(CHECKBOX, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(CHECKBOX, BORDER_WIDTH, 1);
    GuiSetStyle(CHECKBOX, CHECK_PADDING, 4);

    // COMBOBOX
    GuiSetStyle(COMBOBOX, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(COMBOBOX, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(COMBOBOX, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(COMBOBOX, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(COMBOBOX, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(COMBOBOX, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(COMBOBOX, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(COMBOBOX, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(COMBOBOX, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(COMBOBOX, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(COMBOBOX, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(COMBOBOX, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(COMBOBOX, BORDER_WIDTH, 1);
    GuiSetStyle(COMBOBOX, TEXT_PADDING, 4);

    // VALUEBOX
    GuiSetStyle(VALUEBOX, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(VALUEBOX, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(VALUEBOX, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(VALUEBOX, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(VALUEBOX, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(VALUEBOX, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(VALUEBOX, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(VALUEBOX, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(VALUEBOX, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(VALUEBOX, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(VALUEBOX, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(VALUEBOX, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(VALUEBOX, BORDER_WIDTH, 1);
    GuiSetStyle(VALUEBOX, TEXT_PADDING, 4);

    // LABEL (also used for LABELBUTTON)
    GuiSetStyle(LABEL, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(LABEL, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(LABEL, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(LABEL, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(LABEL, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(LABEL, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(LABEL, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(LABEL, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(LABEL, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(LABEL, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(LABEL, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(LABEL, BORDER_WIDTH, 1);

    // SCROLLBAR (used by ScrollPanel)
    GuiSetStyle(SCROLLBAR, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(SCROLLBAR, BASE_COLOR_NORMAL, (int)cInner);
    GuiSetStyle(SCROLLBAR, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(SCROLLBAR, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(SCROLLBAR, BASE_COLOR_FOCUSED, (int)cInner);
    GuiSetStyle(SCROLLBAR, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(SCROLLBAR, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(SCROLLBAR, BASE_COLOR_PRESSED, (int)cInner);
    GuiSetStyle(SCROLLBAR, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(SCROLLBAR, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(SCROLLBAR, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(SCROLLBAR, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(SCROLLBAR, BORDER_WIDTH, 1);

    // LISTVIEW (used by ScrollPanel content area)
    GuiSetStyle(LISTVIEW, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(LISTVIEW, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(LISTVIEW, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(LISTVIEW, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(LISTVIEW, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(LISTVIEW, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(LISTVIEW, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(LISTVIEW, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(LISTVIEW, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(LISTVIEW, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(LISTVIEW, BORDER_WIDTH, 1);
    GuiSetStyle(LISTVIEW, BACKGROUND_COLOR, (int)cBg);

    // COLORPICKER
    GuiSetStyle(COLORPICKER, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(COLORPICKER, BASE_COLOR_NORMAL, (int)cBg);
    GuiSetStyle(COLORPICKER, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(COLORPICKER, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(COLORPICKER, BASE_COLOR_FOCUSED, (int)cBg);
    GuiSetStyle(COLORPICKER, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(COLORPICKER, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(COLORPICKER, BASE_COLOR_PRESSED, (int)cBg);
    GuiSetStyle(COLORPICKER, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(COLORPICKER, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(COLORPICKER, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(COLORPICKER, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(COLORPICKER, BORDER_WIDTH, 1);

    // STATUSBAR (used for WINDOWBOX)
    GuiSetStyle(STATUSBAR, BORDER_COLOR_NORMAL, (int)cBorder);
    GuiSetStyle(STATUSBAR, BASE_COLOR_NORMAL, (int)cBase);
    GuiSetStyle(STATUSBAR, TEXT_COLOR_NORMAL, (int)cText);
    GuiSetStyle(STATUSBAR, BORDER_COLOR_FOCUSED, (int)cFocus);
    GuiSetStyle(STATUSBAR, BASE_COLOR_FOCUSED, (int)cBase);
    GuiSetStyle(STATUSBAR, TEXT_COLOR_FOCUSED, (int)cText);
    GuiSetStyle(STATUSBAR, BORDER_COLOR_PRESSED, (int)cFocus);
    GuiSetStyle(STATUSBAR, BASE_COLOR_PRESSED, (int)cActive);
    GuiSetStyle(STATUSBAR, TEXT_COLOR_PRESSED, (int)cText);
    GuiSetStyle(STATUSBAR, BORDER_COLOR_DISABLED, (int)cBorder);
    GuiSetStyle(STATUSBAR, BASE_COLOR_DISABLED, (int)cInner);
    GuiSetStyle(STATUSBAR, TEXT_COLOR_DISABLED, (int)cText);
    GuiSetStyle(STATUSBAR, BORDER_WIDTH, 1);
    GuiSetStyle(STATUSBAR, BACKGROUND_COLOR, (int)cBg);
}

void GuiCustomGroupBox(Rectangle bounds, const char* text)
{
    Color borderColor = { 102, 102, 105, 255 };   // #666669

    DrawRectangleRec(bounds, (Color){ 59, 60, 63, 255 });  // #3B3C3F
    DrawRectangleLinesEx(bounds, 1.0f, borderColor);

    if ((text != NULL) && (text[0] != '\0'))
    {
        Rectangle textBounds = {
            bounds.x + GUI_GROUPBOX_TEXT_MARGIN,
            bounds.y + GUI_GROUPBOX_TEXT_PADDING,
            bounds.width - (float)(GUI_GROUPBOX_TEXT_MARGIN * 2),
            (float)GuiGetStyle(DEFAULT, TEXT_SIZE)
        };
        Vector2 textPosition = { textBounds.x, textBounds.y };

        DrawTextEx(GuiGetFont(), text, textPosition, (float)GuiGetStyle(DEFAULT, TEXT_SIZE), (float)GuiGetStyle(DEFAULT, TEXT_SPACING), (Color){ 224, 224, 224, 255 });
    }
}

void GuiOrbitCamera(OrbitCamera* camera, CharacterData* characterData, int argc, char** argv)
{
    GuiCustomGroupBox((Rectangle){ 20, 10, 190, 240 }, "Camera");
    GuiLabel((Rectangle){ 30, 30, 150, 20 }, TextFormat("Target: [% 5.3f % 5.3f % 5.3f]", camera->cam3d.target.x, camera->cam3d.target.y, camera->cam3d.target.z));
    GuiLabel((Rectangle){ 30, 50, 150, 20 }, TextFormat("Offset: [% 5.3f % 5.3f % 5.3f]", camera->offset.x, camera->offset.y, camera->offset.z));
    GuiLabel((Rectangle){ 30, 70, 150, 20 }, TextFormat("Azimuth: %5.3f", camera->azimuth));
    GuiLabel((Rectangle){ 30, 90, 150, 20 }, TextFormat("Altitude: %5.3f", camera->altitude));
    GuiLabel((Rectangle){ 30, 110, 150, 20 }, TextFormat("Distance: %5.3f", camera->distance));
    if (GuiButton((Rectangle){ 30, 130, 100, 20 }, "Reset"))
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
        GuiToggle((Rectangle){ 30, 160, 100, 20 }, "Track", &camera->track);
        bool skeletonToggle = camera->showSkeletonPanel;
        GuiToggle((Rectangle){ 30, 190, 100, 20 }, "Skeleton", &skeletonToggle);
        int ci = characterData->active;
        if (ci >= 0 && ci < characterData->count)
        {
            int jointCount = characterData->xformData[ci].jointCount;
            int sel = camera->selectedBone;
            if (sel < 0) sel = 0;
            if (sel >= jointCount) sel = jointCount - 1;
            char jointText[32];
            snprintf(jointText, sizeof(jointText), "%d/%d", sel + 1, jointCount);
            DrawText(jointText, 135, 195, 10, (Color){ 153, 153, 153, 255 });
        }
        if (ci >= 0 && ci < characterData->count && camera->selectedBone >= 0 && camera->selectedBone < characterData->xformData[ci].jointCount)
        {
            const char* boneName = characterData->isGLB[ci]
                ? characterData->glbData[ci].model.skeleton.bones[characterData->glbData[ci].topoOrder[camera->selectedBone]].name
                : characterData->bvhData[ci].joints[camera->selectedBone].name;
            DrawText(boneName, 50, 220, 10, GOLD);
        }
        if (skeletonToggle != camera->showSkeletonPanel)
        {
            camera->showSkeletonPanel = skeletonToggle;
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
    int panelHeight = 620;
    int panelX = 220;
    int panelY = 10;
    GuiCustomGroupBox((Rectangle){ panelX, panelY, panelWidth, panelHeight }, "Skeleton");
    int itemHeight = 22;
    float borderWidth = (float)GuiGetStyle(DEFAULT, BORDER_WIDTH);
    float scrollBarWidth = (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    int scrollAreaHeight = panelHeight - 26;
    int contentHeight = jointCount * itemHeight;
    Rectangle scrollBounds = { (float)panelX + 8, (float)panelY + 18, (float)panelWidth - 16, (float)scrollAreaHeight };
    Rectangle content = { 0, 0, scrollBounds.width - 2.0f*borderWidth - scrollBarWidth, (float)contentHeight };
    static Vector2 scroll = { 0, 0 };
    Rectangle view = { 0 };
    GuiScrollPanel(scrollBounds, NULL, content, &scroll, &view);
    int visibleStart = MaxInt(0, (int)(-scroll.y / itemHeight));
    int visibleCount = (int)(view.height / itemHeight) + 2;
    if (visibleStart >= jointCount) visibleStart = jointCount - 1;
    if (visibleStart < 0) visibleStart = 0;
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    Vector2 mouse = GetMousePosition();
    int hoveredIndex = -1;
    Rectangle hoveredItemRec = { 0, 0, 0, 0 };
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
        bool isHovered = CheckCollisionPointRec(mouse, itemRec);
        if (isHovered)
        {
            hoveredIndex = i;
            hoveredItemRec = itemRec;
        }
        if (i == camera->selectedBone)
        {
            DrawRectangleRec(itemRec, (Color){ 69, 70, 73, 255 });
            DrawRectangleLines((int)itemRec.x, (int)itemRec.y, (int)itemRec.width, (int)itemRec.height, (Color){ 102, 102, 105, 255 });
        }
        else if (isHovered)
        {
            DrawRectangleLines((int)itemRec.x, (int)itemRec.y, (int)itemRec.width, (int)itemRec.height, (Color){ 140, 140, 143, 255 });
        }
        if (GuiLabelButton(itemRec, displayName))
        {
            camera->selectedBone = i;
            if (camera->track) camera->trackBone = i;
        }
    }
    EndScissorMode();

    // Tooltip: show full bone path on hover (drawn outside scissor so it's not clipped)
    if (hoveredIndex >= 0)
    {
        static char pathBuf[512];
        pathBuf[0] = '\0';
        int pathLen = 0;

        // Collect indices from leaf to root
        int tempPath[64];
        int depth = 0;
        int idx = hoveredIndex;
        while (depth < 64)
        {
            tempPath[depth++] = idx;
            int parent = boneParents[idx];
            if (parent < 0) break;
            idx = parent;
        }

        // Build path string from root to leaf
        for (int p = depth - 1; p >= 0; p--)
        {
            if (p < depth - 1 && pathLen < 509)
                pathBuf[pathLen++] = '/';
            const char* nm = names[tempPath[p]];
            int nLen = (int)strlen(nm);
            if (pathLen + nLen >= 510) nLen = 510 - pathLen;
            memcpy(pathBuf + pathLen, nm, nLen);
            pathLen += nLen;
        }
        pathBuf[pathLen] = '\0';

        int textWidth = MeasureText(pathBuf, 10);
        float tooltipX = hoveredItemRec.x + hoveredItemRec.width + 8;
        float tooltipY = hoveredItemRec.y;
        int tooltipW = textWidth + 10;

        // Keep tooltip within screen bounds
        if (tooltipX + tooltipW > (float)screenWidth)
            tooltipX = hoveredItemRec.x - (float)(textWidth + 10);
        if (tooltipY < 0) tooltipY = 0;
        if (tooltipY + 24 > (float)screenHeight) tooltipY = (float)screenHeight - 24;

        Rectangle tooltipRect = { tooltipX, tooltipY, (float)tooltipW, 22 };
        DrawRectangleRec(tooltipRect, (Color){ 40, 40, 40, 230 });
        DrawRectangleLinesEx(tooltipRect, 1, (Color){ 140, 140, 143, 255 });
        DrawText(pathBuf, (int)tooltipRect.x + 5, (int)tooltipRect.y + 6, 10, LIGHTGRAY);
    }
}

void GuiRenderSettings(RenderSettings* settings, CapsuleData* capsuleData, int screenWidth, int screenHeight)
{
    GuiCustomGroupBox((Rectangle){ screenWidth - 270, 10, 250, 480 }, "Rendering");
    GuiSliderBar((Rectangle){ screenWidth - 170, 30, 100, 20 }, "Exposure", TextFormat("%5.2f", settings->exposure), &settings->exposure, 0.0f, 3.0f);
    GuiSliderBar((Rectangle){ screenWidth - 170, 60, 100, 20 }, "Sun Light", TextFormat("%5.2f", settings->sunLightStrength), &settings->sunLightStrength, 0.0f, 1.0f);
    if (GuiSliderBar((Rectangle){ screenWidth - 170, 90, 100, 20 }, "Sun Softness", TextFormat("%5.2f", settings->sunLightConeAngle), &settings->sunLightConeAngle, 0.02f, PI / 4.0f))
        CapsuleDataUpdateShadowLookupTable(capsuleData, settings->sunLightConeAngle);
    GuiSliderBar((Rectangle){ screenWidth - 170, 120, 100, 20 }, "Sky Light", TextFormat("%5.2f", settings->skyLightStrength), &settings->skyLightStrength, 0.0f, 1.0f);
    GuiSliderBar((Rectangle){ screenWidth - 170, 150, 100, 20 }, "Ambient Light", TextFormat("%5.2f", settings->ambientLightStrength), &settings->ambientLightStrength, 0.0f, 2.0f);
    GuiSliderBar((Rectangle){ screenWidth - 170, 180, 100, 20 }, "Ground Light", TextFormat("%5.2f", settings->groundLightStrength), &settings->groundLightStrength, 0.0f, 0.5f);
    GuiSliderBar((Rectangle){ screenWidth - 170, 210, 100, 20 }, "Sun Azimuth", TextFormat("%5.2f", settings->sunAzimuth), &settings->sunAzimuth, -PI, PI);
    GuiSliderBar((Rectangle){ screenWidth - 170, 240, 100, 20 }, "Sun Altitude", TextFormat("%5.2f", settings->sunAltitude), &settings->sunAltitude, 0.0f, 0.49f * PI);
    GuiCheckBox((Rectangle){ screenWidth - 260, 270, 20, 20 }, "Draw Origin", &settings->drawOrigin);
    GuiCheckBox((Rectangle){ screenWidth - 140, 270, 20, 20 }, "Draw Grid", &settings->drawGrid);
    GuiCheckBox((Rectangle){ screenWidth - 260, 300, 20, 20 }, "Draw Checker", &settings->drawChecker);
    if (GuiCheckBox((Rectangle){ screenWidth - 140, 300, 20, 20 }, "Draw Meshes", &settings->drawMeshes) && settings->drawMeshes)
        settings->drawCapsules = false;
    GuiCheckBox((Rectangle){ screenWidth - 260, 330, 20, 20 }, "Draw Capsules", &settings->drawCapsules);
    GuiCheckBox((Rectangle){ screenWidth - 140, 330, 20, 20 }, "Draw Wireframes", &settings->drawWireframes);
    GuiCheckBox((Rectangle){ screenWidth - 260, 360, 20, 20 }, "Draw Skeleton", &settings->drawSkeleton);
    GuiCheckBox((Rectangle){ screenWidth - 140, 360, 20, 20 }, "Draw Transforms", &settings->drawTransforms);
    GuiCheckBox((Rectangle){ screenWidth - 260, 390, 20, 20 }, "Draw AO", &settings->drawAO);
    GuiCheckBox((Rectangle){ screenWidth - 140, 390, 20, 20 }, "Draw Shadows", &settings->drawShadows);
    GuiCheckBox((Rectangle){ screenWidth - 260, 420, 20, 20 }, "Draw End Sites", &settings->drawEndSites);
    GuiCheckBox((Rectangle){ screenWidth - 140, 420, 20, 20 }, "Draw FPS", &settings->drawFPS);
    GuiCheckBox((Rectangle){ screenWidth - 260, 450, 20, 20 }, "Draw Texture", &settings->drawTexture);
    GuiLabel((Rectangle){ screenWidth - 140, 450, 100, 20 }, "H Key - Hide UI");
}

void GuiCharacterData(CharacterData* characterData, GuiWindowFileDialogState* fileDialogState, ScrubberSettings* scrubberSettings, char* errMsg, int argc, char** argv)
{
    int offsetHeight = 260;
    GuiCustomGroupBox((Rectangle){ 20, offsetHeight, 190, (CHARACTERS_MAX - 1) * 30 + 190 }, "Characters");
#if !defined(PLATFORM_WEB)
    if (GuiButton((Rectangle){ 30, offsetHeight + 20, 110, 20 }, "Open"))
        fileDialogState->windowActive = true;
#endif
    if (GuiButton((Rectangle){ 150, offsetHeight + 20, 50, 20 }, "Clear"))
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
        Rectangle toggleRect = (Rectangle){ 30, offsetHeight + 50 + i * 30, 120, 20 };
        GuiToggle(toggleRect, bvhNameShort, &bvhSelected);
        // Tooltip: show filename on hover over the name button
        Vector2 mousePos = GetMousePosition();
        if (CheckCollisionPointRec(mousePos, toggleRect) && strlen(characterData->filePaths[i]) > strlen(characterData->names[i]))
        {
            const char* fileName = ExtractFilename(characterData->filePaths[i]);
            int textWidth = MeasureText(fileName, 10);
            Rectangle tooltipRect = (Rectangle){ toggleRect.x, toggleRect.y - 24, (float)textWidth + 10, 22 };
            DrawRectangleRec(tooltipRect, (Color){ 40, 40, 40, 230 });
            DrawRectangleLinesEx(tooltipRect, 1, (Color){ 140, 140, 143, 255 });
            DrawText(fileName, (int)tooltipRect.x + 5, (int)tooltipRect.y + 6, 10, LIGHTGRAY);
        }
        GuiCheckBox((Rectangle){ 155, offsetHeight + 50 + i * 30, 20, 20 }, "", &characterData->visible[i]);
        // If active character was just hidden, switch to another visible one
        if (!characterData->visible[i] && i == characterData->active)
        {
            for (int j = 0; j < characterData->count; j++)
            {
                if (characterData->visible[j])
                {
                    characterData->active = j;
                    ScrubberSettingsClamp(scrubberSettings, characterData);
                    break;
                }
            }
        }
        if (bvhSelected && (characterData->active != i))
        {
            characterData->active = i;
            ScrubberSettingsClamp(scrubberSettings, characterData);
            char windowTitle[528];
            snprintf(windowTitle, sizeof(windowTitle), "%s - BVHView", characterData->filePaths[characterData->active]);
            SetWindowTitle(windowTitle);
        }
        DrawRectangleRec((Rectangle){ 180, offsetHeight + 50 + i * 30, 20, 20 }, characterData->colors[i]);
        DrawRectangleLinesEx((Rectangle){ 180, offsetHeight + 50 + i * 30, 20, 20 }, 1, (Color){ 102, 102, 105, 255 });
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            Vector2 mousePosition = GetMousePosition();
            if (mousePosition.x > 180 && mousePosition.x < 200 && mousePosition.y > offsetHeight + 50 + i * 30 && mousePosition.y < offsetHeight + 50 + i * 30 + 20)
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
            GuiComboBox((Rectangle){ 30, offsetHeight + 70 + (CHARACTERS_MAX - 1) * 30, 150, 20 }, animCombo, &characterData->glbData[active].activeAnim);
            if (characterData->glbData[active].activeAnim != prevAnim)
            {
                ScrubberSettingsRecomputeLimits(scrubberSettings, characterData);
                ScrubberSettingsInitMaxs(scrubberSettings, characterData);
            }
        }
        int scaleY = offsetHeight + 70 + (CHARACTERS_MAX - 1) * 30;
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
        GuiSliderBar((Rectangle){ 70, scaleY + 40, 100, 20 }, "Radius", TextFormat("%5.2f", characterData->radii[active]), &characterData->radii[active], 0.01f, 0.1f);
        GuiSliderBar((Rectangle){ 70, scaleY + 70, 100, 20 }, "Opacity", TextFormat("%5.2f", characterData->opacities[active]), &characterData->opacities[active], 0.0f, 1.0f);
    }
}

void GuiScrubberSettings(ScrubberSettings* settings, CharacterData* characterData, int screenWidth, int screenHeight)
{
    if (characterData->count == 0) return;
    float frameTime = ScrubberGetFrameTime(characterData, characterData->active);
    GuiCustomGroupBox((Rectangle){ screenWidth / 2 - 600, screenHeight - 100, 1200, 90 }, "Scrubber");
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
    int activeFrameLimit = ScrubberGetFrameCount(characterData, characterData->active) - 1;
    if (GuiValueBox((Rectangle){ screenWidth / 2 - 540, screenHeight - 80, 50, 20 }, "Min   ", &settings->frameMinSelect, 0, activeFrameLimit, settings->frameMinEdit))
    {
        settings->frameMinEdit = !settings->frameMinEdit;
        if (!settings->frameMinEdit) { settings->frameMin = settings->frameMinSelect; ScrubberSettingsClamp(settings, characterData); }
    }
    if (GuiValueBox((Rectangle){ screenWidth / 2 + 470, screenHeight - 80, 50, 20 }, "Max   ", &settings->frameMaxSelect, 0, activeFrameLimit, settings->frameMaxEdit))
    {
        settings->frameMaxEdit = !settings->frameMaxEdit;
        if (!settings->frameMaxEdit) { settings->frameMax = settings->frameMaxSelect; ScrubberSettingsClamp(settings, characterData); }
    }
    GuiLabel((Rectangle){ screenWidth / 2 + 530, screenHeight - 80, 100, 20 }, TextFormat("of %i", activeFrameLimit));
    Rectangle sliderRect = { screenWidth / 2 - 540, screenHeight - 50, 1080, 20 };
    float frameFloatPrev = settings->frameSnap ? (float)frame : settings->playTime / frameTime;
    float frameFloat = frameFloatPrev;

    static bool scrubberDragging = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), sliderRect)) scrubberDragging = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) scrubberDragging = false;

    bool wrapped = false;
    float wrappedValue = frameFloat;
    if (scrubberDragging && settings->looping)
    {
        float mouseX = GetMousePosition().x;
        if (mouseX < sliderRect.x || mouseX > sliderRect.x + sliderRect.width)
        {
            float range = (float)settings->frameMax - (float)settings->frameMin;
            if (range > 0.0f)
            {
                float t = (mouseX - sliderRect.x) / sliderRect.width;
                float raw = t * range;
                float w = fmodf(fmodf(raw, range) + range, range);
                wrappedValue = (float)settings->frameMin + w;
                wrapped = true;
            }
        }
    }

    int displayFrame = wrapped ? ClampInt((int)(wrappedValue + 0.5f), settings->frameMin, settings->frameMax) : frame;
    float displayPlayTime = wrapped ? wrappedValue * frameTime : settings->playTime;

    GuiSliderBar(sliderRect, TextFormat("%5.2f", displayPlayTime), TextFormat("%i", displayFrame), &frameFloat, (float)settings->frameMin, (float)settings->frameMax);

    if (wrapped)
    {
        frameFloat = wrappedValue;
        int bw = GuiGetStyle(SLIDER, BORDER_WIDTH);
        int sp = GuiGetStyle(SLIDER, SLIDER_PADDING);
        Color baseColor = GetColor(GuiGetStyle(SLIDER, BASE_COLOR_NORMAL));
        Color fillColor = GetColor(GuiGetStyle(SLIDER, TEXT_COLOR_PRESSED));
        Rectangle inner = {
            sliderRect.x + bw,
            sliderRect.y + bw + sp,
            sliderRect.width - 2 * bw,
            sliderRect.height - 2 * bw - 2 * sp
        };
        DrawRectangleRec(inner, baseColor);
        float range = (float)settings->frameMax - (float)settings->frameMin;
        float frac = (wrappedValue - (float)settings->frameMin) / range;
        DrawRectangleRec((Rectangle){ inner.x, inner.y, frac * inner.width, inner.height }, fillColor);
    }

    if (frameFloat != frameFloatPrev || wrapped)
    {
        settings->playing = false;
        if (settings->frameSnap)
        {
            frame = ClampInt((int)(frameFloat + 0.5f), settings->frameMin, settings->frameMax);
            settings->playTime = Clamp(frame * frameTime, settings->timeMin, settings->timeMax);
        }
        else settings->playTime = Clamp(frameFloat * frameTime, settings->timeMin, settings->timeMax);
    }
}

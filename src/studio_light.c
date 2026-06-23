/*******************************************************************************************
*
*    studio_light.c - Upload the embedded prefiltered HDR cubemap
*
*******************************************************************************************/

#include "raylib.h"
#include "rlgl.h"
#include "studio_light.h"

TextureCubemap StudioLightLoad(void)
{
    TextureCubemap cubemap = { 0 };
    cubemap.id = rlLoadTextureCubemap(studio_prefiltered_cubemap,
        STUDIO_LIGHT_CUBEMAP_SIZE, PIXELFORMAT_UNCOMPRESSED_R16G16B16,
        STUDIO_LIGHT_MIP_COUNT);
    if (cubemap.id == 0) return cubemap;

    cubemap.width = STUDIO_LIGHT_CUBEMAP_SIZE;
    cubemap.height = STUDIO_LIGHT_CUBEMAP_SIZE;
    cubemap.mipmaps = STUDIO_LIGHT_MIP_COUNT;
    cubemap.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16;
    return cubemap;
}

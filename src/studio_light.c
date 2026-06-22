/*******************************************************************************************
*
*    studio_light.c - Decode and upload the embedded Blender studio HDRI
*
*******************************************************************************************/

#include "raylib.h"
#include "rlgl.h"
#include "studio_light.h"

// Raylib disables Radiance HDR decoding in its default build. Compile a small,
// translation-unit-local HDR-only stb_image decoder so this stays self-contained.
#define STB_IMAGE_STATIC
#define STBI_ONLY_HDR
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "external/stb_image.h"

extern const unsigned char blender_studio_hdr[];
extern const unsigned int blender_studio_hdr_len;

Texture2D StudioLightLoad(void)
{
    Texture2D texture = { 0 };
    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf_from_memory(blender_studio_hdr, (int)blender_studio_hdr_len,
        &width, &height, &channels, 3);
    if (pixels == 0) return texture;

    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32 };
    // Half float preserves the HDR range and has broader filtered-texture support
    // on WebGL 2 while using half the VRAM of the decoder's float32 output.
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R16G16B16);

    texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0) return texture;

    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
    rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    return texture;
}

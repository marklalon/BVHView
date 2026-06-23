/*******************************************************************************************
*
*    studio_light.h - Embedded Blender material-preview studio light (SH encoded)
*
*******************************************************************************************/

#ifndef STUDIO_LIGHT_H
#define STUDIO_LIGHT_H

// Order-2 real spherical-harmonic projection of the Blender forest HDRI,
// baked offline by tools/bake_studio_light.py. The runtime reconstructs both
// diffuse irradiance and a roughness-windowed specular approximation from these
// in the shader (see ShBasis/ShIrradiance/ShRadiance), so there is no texture to
// upload and no equirect pole distortion.
//
// Layout: studio_sh_count coefficients, 3 floats (R,G,B) each, linear radiance.
extern const int studio_sh_count;
extern const float studio_sh_coeffs[];

#endif // STUDIO_LIGHT_H

#version 460 core

layout(location = 0) in vec2 inPosEm;  // glyph quad in EM space
layout(location = 1) in vec2 inUV;     // atlas uv

out vec2 vUV;

uniform mat4 mvp;

// pixels -> model-units scale (set per-space)
//   world : (wppX, wppY)
//   screen: (1/screenScaleX, 1/screenScaleY)
uniform vec2 pxToModel;

// EM -> pixels scale (your “font size” in px for EM=1)
uniform float emScalePx;

uniform int isWorldSpace; // kept for parity/debug

void main()
{
    vec2 posPx    = inPosEm * emScalePx; // EM -> px
    vec2 posModel = posPx * pxToModel;   // px -> model units

    vUV = inUV;
    gl_Position = mvp * vec4(posModel, 0.0, 1.0);
}

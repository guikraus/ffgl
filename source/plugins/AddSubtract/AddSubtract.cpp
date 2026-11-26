#include <FFGLSDK.h>
#include <cmath>

using namespace ffglex;

enum ParamID : FFUInt32 {
    PT_SF = 0,
    PT_PX,
    PT_PY,
    PT_CX,
    PT_CY,
    PT_RC,
    PT_BW,
    PT_BC_R,
    PT_BC_G,
    PT_BC_B,
    PT_BC_A,
    PT_BAM,
    PT_BGL,
    PT_SS,
    PT_ZB,
    PT_RB,
    PT_BA,
    PT_BB,
    PT_BS,
    PT_BV,
    PT_COUNT
};

static CFFGLPluginInfo PluginInfo(
    PluginFactory< class OGKBlur >,
    "OGKB",
    "OGK Blur Ultimate",
    2, 1,
    1, 0,
    FF_EFFECT,
    "Background Blur Ultimate Auto-Aspect",
    "Edicao Ultimate Auto-Aspect"
);

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main() {
    gl_Position = vPosition;
    uv = vUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D inputTexture;
uniform vec2 resolution;
uniform float sF;
uniform float pX;
uniform float pY;
uniform float cX;
uniform float cY;
uniform float rC;
uniform float bW;
uniform vec4 bC;
uniform float bAm;
uniform float bGl;
uniform float sS;
uniform float zB;
uniform float rB;
uniform float bA;
uniform float bB;
uniform float bS;
uniform float bV;

in vec2 uv;
out vec4 fragColor;

float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {
    vec2 q = abs(centerPos) - size + vec2(radius, radius);
    return length(max(q, vec2(0.0, 0.0))) - radius;
}

vec2 clampUV(vec2 coord, float minX, float minY, float maxX, float maxY) {
    return vec2(clamp(coord.x, minX, maxX), clamp(coord.y, minY, maxY));
}

vec3 adjustSaturation(vec3 color, float sat) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luma, luma, luma), color, sat);
}

void main() {
    float screenAspect = resolution.x / max(resolution.y, 1.0);

    float minX = cX;
    float minY = cY;
    float maxX = 1.0 - cX;
    float maxY = 1.0 - cY;
    float actW = max(maxX - minX, 0.01);
    float actH = max(maxY - minY, 0.01);

    vec2 imgSize = vec2(textureSize(inputTexture, 0));
    float rawImgAspect = max(imgSize.x, 1.0) / max(imgSize.y, 1.0);
    float contentAspect = rawImgAspect * (actW / actH);

    float safeMinX = minX + 0.001;
    float safeMinY = minY + 0.001;
    float safeMaxX = maxX - 0.001;
    float safeMaxY = maxY - 0.001;

    vec2 bgSpace = vec2((uv.x - 0.5) * screenAspect, uv.y - 0.5);

    float cosR = cos(rB);
    float sinR = sin(rB);
    vec2 rotBg = vec2(
        bgSpace.x * cosR - bgSpace.y * sinR,
        bgSpace.x * sinR + bgSpace.y * cosR
    );

    vec2 fillScale = vec2(screenAspect, screenAspect / contentAspect);
    if (contentAspect > screenAspect) {
        fillScale = vec2(contentAspect, 1.0);
    }

    vec2 bgNorm = (rotBg / (fillScale * zB)) + vec2(0.5, 0.5);
    vec2 bgUV = vec2(minX + bgNorm.x * actW, minY + bgNorm.y * actH);

    vec2 baseCoord = clampUV(bgUV, safeMinX, safeMinY, safeMaxX, safeMaxY);
    vec4 bgColor = texture(inputTexture, baseCoord);
    float totalWeight = 1.0;

    float stepX = (bA / max(resolution.x, 1.0)) * actW;
    float stepY = (bA / max(resolution.y, 1.0)) * actH;

    for (int d = 0; d < 16; d++) {
        float angle = float(d) * 0.39269908;
        vec2 dir = vec2(cos(angle), sin(angle));
        for (int q = 1; q <= 4; q++) {
            float stepFrac = float(q) * 0.25;
            float weight = 1.0 - (stepFrac * 0.7);
            vec2 offset = vec2(dir.x * stepX * stepFrac, dir.y * stepY * stepFrac);
            vec2 sCoord = clampUV(bgUV + offset, safeMinX, safeMinY, safeMaxX, safeMaxY);
            bgColor += texture(inputTexture, sCoord) * weight;
            totalWeight += weight;
        }
    }
    bgColor /= totalWeight;

    vec3 finalBg = adjustSaturation(bgColor.rgb, bS) * bB;
    float distCenter = length(uv - vec2(0.5, 0.5)) * 1.4142;
    float vignette = 1.0 - smoothstep(0.4, 1.15, distCenter) * bV;
    finalBg *= vignette;

    vec2 fgBox = vec2(contentAspect, 1.0);
    if (contentAspect > screenAspect) {
        fgBox = vec2(screenAspect, screenAspect / contentAspect);
    }
    fgBox *= sF;

    vec2 p = vec2((uv.x - pX) * screenAspect, uv.y - pY);
    vec2 halfSize = fgBox * 0.5;

    float maxR = min(halfSize.x, halfSize.y);
    float r = min(rC * min(fgBox.x, fgBox.y), maxR);

    float dist = roundedBoxSDF(p, halfSize, r);

    vec2 fgNorm = (p / fgBox) + vec2(0.5, 0.5);
    vec2 fgUV = vec2(minX + fgNorm.x * actW, minY + fgNorm.y * actH);

    vec3 ambientSample = adjustSaturation(bgColor.rgb, 1.5) * 1.35;
    vec3 activeBorderColor = mix(bC.rgb, ambientSample, bAm);

    float shadowDist = roundedBoxSDF(p - vec2(0.0, -0.015), halfSize + vec2(bW, bW), r + bW);
    float shadowAlpha = (1.0 - smoothstep(-0.01, 0.08, shadowDist)) * sS;
    vec3 composed = mix(finalBg, vec3(0.0, 0.0, 0.0), shadowAlpha);

    if (bGl > 0.001) {
        float glowDist = max(dist - bW, 0.0);
        float glowIntensity = exp(-glowDist * 28.0) * bGl;
        composed += activeBorderColor * glowIntensity;
    }

    if (bW > 0.0001) {
        float outerMask = 1.0 - smoothstep(-0.003, 0.003, dist - bW);
        composed = mix(composed, activeBorderColor, outerMask * bC.a);
    }

    float innerMask = 1.0 - smoothstep(-0.0015, 0.0015, dist);
    vec4 fgColor = texture(inputTexture, clampUV(fgUV, safeMinX, safeMinY, safeMaxX, safeMaxY));
    float finalFgAlpha = innerMask * fgColor.a;
    composed = mix(composed, fgColor.rgb, finalFgAlpha);

    fragColor = vec4(composed, 1.0);
}
)";

class OGKBlur : public CFFGLPlugin {
public:
    OGKBlur() {
        SetMinInputs(1);
        SetMaxInputs(1);

        SetParamInfof(PT_SF,   "1. Tamanho Video",    FF_TYPE_STANDARD);
        SetParamInfof(PT_PX,   "2. Posicao X",        FF_TYPE_XPOS);
        SetParamInfof(PT_PY,   "3. Posicao Y",        FF_TYPE_YPOS);
        SetParamInfof(PT_CX,   "4. Crop Laterais",    FF_TYPE_STANDARD);
        SetParamInfof(PT_CY,   "5. Crop Topo/Base",   FF_TYPE_STANDARD);
        SetParamInfof(PT_RC,   "6. Cantos Redondos",  FF_TYPE_STANDARD);
        SetParamInfof(PT_BW,   "7. Espessura Borda",  FF_TYPE_STANDARD);
        SetParamInfof(PT_BC_R, "8. Borda Vermelho",   FF_TYPE_RED);
        SetParamInfof(PT_BC_G, "8. Borda Verde",      FF_TYPE_GREEN);
        SetParamInfof(PT_BC_B, "8. Borda Azul",       FF_TYPE_BLUE);
        SetParamInfof(PT_BC_A, "8. Borda Opacidade",  FF_TYPE_ALPHA);
        SetParamInfof(PT_BAM,  "9. Borda Ambient",    FF_TYPE_STANDARD);
        SetParamInfof(PT_BGL,  "10. Brilho Neon",     FF_TYPE_STANDARD);
        SetParamInfof(PT_SS,   "11. Sombra",          FF_TYPE_STANDARD);
        SetParamInfof(PT_ZB,   "12. Zoom Fundo",      FF_TYPE_STANDARD);
        SetParamInfof(PT_RB,   "13. Rotacao Fundo",   FF_TYPE_STANDARD);
        SetParamInfof(PT_BA,   "14. Intensidade Blur",FF_TYPE_STANDARD);
        SetParamInfof(PT_BB,   "15. Brilho Fundo",    FF_TYPE_STANDARD);
        SetParamInfof(PT_BS,   "16. Saturacao Fundo", FF_TYPE_STANDARD);
        SetParamInfof(PT_BV,   "17. Vinheta",         FF_TYPE_STANDARD);

        values[PT_SF]   = 0.90f;
        values[PT_PX]   = 0.50f;
        values[PT_PY]   = 0.50f;
        values[PT_CX]   = 0.00f;
        values[PT_CY]   = 0.00f;
        values[PT_RC]   = 0.035f;
        values[PT_BW]   = 0.006f;
        values[PT_BC_R] = 1.00f;
        values[PT_BC_G] = 1.00f;
        values[PT_BC_B] = 1.00f;
        values[PT_BC_A] = 1.00f;
        values[PT_BAM]  = 0.00f;
        values[PT_BGL]  = 0.25f;
        values[PT_SS]   = 0.55f;
        values[PT_ZB]   = 1.15f;
        values[PT_RB]   = 0.00f;
        values[PT_BA]   = 28.0f;
        values[PT_BB]   = 0.65f;
        values[PT_BS]   = 0.85f;
        values[PT_BV]   = 0.35f;
    }

    FFResult InitGL(const FFGLViewportStruct* vp) override {
        if (!shader.Compile(vertexShaderCode, fragmentShaderCode)) {
            DeInitGL();
            return FF_FAIL;
        }
        if (!quad.Initialise()) {
            DeInitGL();
            return FF_FAIL;
        }
        return CFFGLPlugin::InitGL(vp);
    }

    FFResult ProcessOpenGL(ProcessOpenGLStruct* pGL) override {
        if (pGL->numInputTextures < 1 || pGL->inputTextures == nullptr || pGL->inputTextures == nullptr)
            return FF_FAIL;

        ffglex::ScopedShaderBinding shaderBinding(shader.GetGLID());
        ffglex::ScopedSamplerActivation activateSampler(0);
        ffglex::Scoped2DTextureBinding textureBinding(pGL->inputTextures->Handle);

        shader.Set("inputTexture", 0);
        shader.Set("resolution", (float)currentViewport.width, (float)currentViewport.height);
        shader.Set("sF",  values[PT_SF]);
        shader.Set("pX",  values[PT_PX]);
        shader.Set("pY",  values[PT_PY]);
        shader.Set("cX",  values[PT_CX]);
        shader.Set("cY",  values[PT_CY]);
        shader.Set("rC",  values[PT_RC]);
        shader.Set("bW",  values[PT_BW]);
        shader.Set("bC",  values[PT_BC_R], values[PT_BC_G], values[PT_BC_B], values[PT_BC_A]);
        shader.Set("bAm", values[PT_BAM]);
        shader.Set("bGl", values[PT_BGL]);
        shader.Set("sS",  values[PT_SS]);
        shader.Set("zB",  values[PT_ZB]);
        shader.Set("rB",  values[PT_RB]);
        shader.Set("bA",  values[PT_BA]);
        shader.Set("bB",  values[PT_BB]);
        shader.Set("bS",  values[PT_BS]);
        shader.Set("bV",  values[PT_BV]);

        quad.Draw();
        return FF_SUCCESS;
    }

    FFResult SetFloatParameter(unsigned int dwIndex, float value) override {
        if (dwIndex < PT_COUNT) {
            values[dwIndex] = value;
            return FF_SUCCESS;
        }
        return FF_FAIL;
    }

    float GetFloatParameter(unsigned int dwIndex) override {
        if (dwIndex < PT_COUNT)
            return values[dwIndex];
        return 0.0f;
    }

    FFResult DeInitGL() override {
        shader.FreeGLResources();
        quad.Release();
        return CFFGLPlugin::DeInitGL();
    }

private:
    ffglex::FFGLShader shader;

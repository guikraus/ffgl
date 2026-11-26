#include <FFGLSDK.h>
#include <cmath>

using namespace ffglex;

static CFFGLPluginInfo PluginInfo(
    PluginFactory< class OGKBlur >,
    "OGKB",                                     // ID unico de 4 caracteres
    "OGK Blur Ultimate",                        // Nome no Resolume
    2, 1,                                       // Versao API FFGL
    1, 0,                                       // Versao do Plugin
    FF_EFFECT,                                  // Tipo: Efeito
    "Background Blur Ultimate Auto-Aspect",     // Descricao
    "Edicao Ultimate Auto-Aspect"               // Autor
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

        AddParam(ffglex::FFGLParamRange::Create("sF", 0.90f, { 0.1f, 1.3f }));
        AddParam(ffglex::FFGLParamRange::Create("pX", 0.50f, { 0.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("pY", 0.50f, { 0.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("cX", 0.00f, { 0.0f, 0.45f }));
        AddParam(ffglex::FFGLParamRange::Create("cY", 0.00f, { 0.0f, 0.45f }));
        AddParam(ffglex::FFGLParamRange::Create("rC", 0.035f, { 0.0f, 0.25f }));
        AddParam(ffglex::FFGLParamRange::Create("bW", 0.006f, { 0.0f, 0.05f }));
        AddParam(ffglex::FFGLParamColor::Create("bC", { 1.0f, 1.0f, 1.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("bAm", 0.00f, { 0.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("bGl", 0.25f, { 0.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("sS", 0.55f, { 0.0f, 1.0f }));
        AddParam(ffglex::FFGLParamRange::Create("zB", 1.15f, { 1.0f, 4.0f }));
        AddParam(ffglex::FFGLParamRange::Create("rB", 0.00f, { -3.14159f, 3.14159f }));
        AddParam(ffglex::FFGLParamRange::Create("bA", 28.0f, { 0.0f, 60.0f }));
        AddParam(ffglex::FFGLParamRange::Create("bB", 0.65f, { 0.0f, 1.5f }));
        AddParam(ffglex::FFGLParamRange::Create("bS", 0.85f, { 0.0f, 2.0f }));
        AddParam(ffglex::FFGLParamRange::Create("bV", 0.35f, { 0.0f, 1.0f }));
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
        if (pGL->numInputTextures < 1 || pGL->inputTextures[0] == nullptr)
            return FF_FAIL;

        FFGLTexCoords maxCoords = GetMaxGLTexCoords(*pGL->inputTextures[0]);
        ffglex::ScopedShaderBinding shaderBinding(shader.GetGLID());
        ffglex::ScopedSamplerActivation activateSampler(0);
        ffglex::Scoped2DTextureBinding textureBinding(pGL->inputTextures[0]->Handle);

        shader.Set("inputTexture", 0);
        shader.Set("resolution", (float)currentViewport.width, (float)currentViewport.height);
        SendParams(shader);
        quad.Draw();
        return FF_SUCCESS;
    }

    FFResult DeInitGL() override {
        shader.FreeGLResources();
        quad.Release();
        return CFFGLPlugin::DeInitGL();
    }

private:
    ffglex::FFGLShader shader;
    ffglex::FFGLScreenQuad quad;
};

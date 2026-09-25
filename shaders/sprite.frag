#version 450

layout(set = 0, binding = 1) uniform sampler2D spriteTexture;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 color;

void main()
{
	vec2 texturePixelSize = vec2(1.0) / vec2(textureSize(spriteTexture, 0));
	vec2 spriteScreenResolution = vec2(1.0) / fwidth(uv);
	vec2 uvPixelSrc = floor(uv / texturePixelSize + vec2(0.499));
	vec2 edge = uvPixelSrc * texturePixelSize * spriteScreenResolution;
	vec2 uvPixel = uv * spriteScreenResolution;
	vec2 uvFactor = clamp(uvPixel - edge + vec2(0.5), vec2(0.0), vec2(1.0));
	vec2 sampleUv = (mix(uvPixelSrc - vec2(1.0), uvPixelSrc, uvFactor) + vec2(0.5)) * texturePixelSize;
	color = texture(spriteTexture, sampleUv) * tint;
}

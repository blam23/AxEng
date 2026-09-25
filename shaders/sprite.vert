#version 450

struct SpriteGpuData
{
	vec2 pos;
	vec2 scale;
	vec4 region;
	vec4 tint;
	float z;
	float rotation;
	uint useRegion;
	uint padding;
};

layout(set = 0, binding = 0, std430) readonly buffer SpriteBuffer
{
	SpriteGpuData sprites[];
};
layout(set = 0, binding = 1) uniform sampler2D spriteTexture;
layout(set = 1, binding = 0) uniform Viewport
{
	vec4 dimensions;
};

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 tint;

const vec2 corners[6] = vec2[](
	vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
	vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main()
{
	SpriteGpuData sprite = sprites[gl_InstanceIndex];
	vec2 corner = corners[gl_VertexIndex];
	vec2 imageSize = vec2(textureSize(spriteTexture, 0));
	vec2 sourceOffset = sprite.useRegion != 0 ? sprite.region.xy : vec2(0.0);
	vec2 sourceSize = sprite.useRegion != 0 ? sprite.region.zw : imageSize;
	vec2 size = sourceSize * sprite.scale;
	vec2 local = (corner - vec2(0.5)) * size;
	float sine = sin(sprite.rotation);
	float cosine = cos(sprite.rotation);
	vec2 rotated = vec2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine);
	vec2 pixel = sprite.pos + size * 0.5 + rotated;
	vec2 ndc = pixel / dimensions.xy * 2.0 - 1.0;
	gl_Position = vec4(ndc, sprite.z, 1.0);
	uv = (sourceOffset + corner * sourceSize) / imageSize;
	tint = sprite.tint;
}

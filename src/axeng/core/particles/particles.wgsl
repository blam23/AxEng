struct Particle
{
	originVelocity: vec4<f32>,
	ageLifeRotationAngular: vec4<f32>,
	randomState: vec4<u32>,
};

struct Parameters
{
	cameraBounds: vec4<f32>,
	transform: vec4<f32>,
	time: vec4<f32>,
	lifeSpeed: vec4<f32>,
	directionSpreadRadiusSize: vec4<f32>,
	spawnArea: vec4<f32>,
	appearance: vec4<f32>,
	rotationRange: vec4<f32>,
	colorStart: vec4<f32>,
	colorEnd: vec4<f32>,
	gravity: vec4<f32>,
	misc: vec4<u32>,
};

struct DrawArguments
{
	vertexCount: u32,
	instanceCount: atomic<u32>,
	firstVertex: u32,
	firstInstance: u32,
};

@group(0) @binding(0) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(1) var<uniform> parameters: Parameters;
@group(0) @binding(2) var<storage, read_write> spawnCounter: atomic<u32>;
@group(0) @binding(3) var<storage, read_write> visibleIndices: array<u32>;
@group(0) @binding(4) var<storage, read_write> drawArguments: DrawArguments;

fn hash(value: u32) -> u32
{
	var v = value;
	v = (v ^ (v >> 16u)) * 0x7feb352du;
	v = (v ^ (v >> 15u)) * 0x846ca68bu;
	return v ^ (v >> 16u);
}

fn random01(seed: ptr<function, u32>) -> f32
{
	*seed = hash(*seed + 0x9e3779b9u);
	return f32(*seed) / 4294967295.0;
}

fn rotate(value: vec2<f32>, angle: f32) -> vec2<f32>
{
	let c = cos(angle);
	let s = sin(angle);
	return vec2<f32>(value.x * c - value.y * s, value.x * s + value.y * c);
}

fn particle_age(particle: Particle, currentTime: f32) -> f32
{
	let elapsed = currentTime - particle.ageLifeRotationAngular.x;
	return select(elapsed, elapsed + 4096.0, elapsed < 0.0);
}

fn particle_position(particle: Particle, age: f32, parameters: Parameters) -> vec2<f32>
{
	var localPosition = particle.originVelocity.xy + particle.originVelocity.zw * age
		+ parameters.gravity.xy * (0.5 * age * age);
	if ((parameters.misc.w & 2u) != 0u)
	{
		localPosition = parameters.transform.xy + rotate(localPosition, parameters.transform.z);
	}
	return localPosition;
}

@compute @workgroup_size(64)
fn update_particles(@builtin(global_invocation_id) id: vec3<u32>)
{
	let index = id.x;
	if (index >= parameters.misc.x)
	{
		return;
	}

	var particle = particles[index];
	if ((parameters.misc.w & 1u) != 0u)
	{
		particle.ageLifeRotationAngular.y = 0.0;
		particles[index] = particle;
	}

	var age = particle_age(particle, parameters.time.x);
	if (particle.ageLifeRotationAngular.y > 0.0 && age >= particle.ageLifeRotationAngular.y)
	{
		particle.ageLifeRotationAngular.y = 0.0;
		particles[index] = particle;
	}

	if (particle.ageLifeRotationAngular.y <= 0.0)
	{
		let ticket = atomicAdd(&spawnCounter, 1u);
		if (ticket >= u32(parameters.time.y))
		{
			return;
		}

		var seed = hash(parameters.misc.y ^ index ^ parameters.misc.z);
		let offsetAngle = random01(&seed) * 6.28318530718;
		let offsetRadius = sqrt(random01(&seed)) * parameters.directionSpreadRadiusSize.z;
		var spawnOffset = vec2<f32>(0.0);
		let spawnShape = (parameters.misc.w >> 2u) & 3u;
		if (spawnShape == 1u)
		{
			spawnOffset = vec2<f32>(cos(offsetAngle), sin(offsetAngle)) * offsetRadius;
		}
		else if (spawnShape == 2u)
		{
			spawnOffset = (vec2<f32>(random01(&seed), random01(&seed)) * 2.0 - 1.0)
				* parameters.spawnArea.xy * 0.5;
		}
		let localSpace = (parameters.misc.w & 2u) != 0u;
		let origin = select(parameters.transform.xy + rotate(spawnOffset, parameters.transform.z),
			spawnOffset, localSpace);
		let angle = parameters.directionSpreadRadiusSize.x
			+ (random01(&seed) * 2.0 - 1.0) * parameters.directionSpreadRadiusSize.y
			+ select(parameters.transform.z, 0.0, localSpace);
		let speed = mix(parameters.lifeSpeed.z, parameters.lifeSpeed.w, random01(&seed));
		let velocity = vec2<f32>(cos(angle), sin(angle)) * speed;
		let lifetime = max(mix(parameters.lifeSpeed.x, parameters.lifeSpeed.y, random01(&seed)), 0.001);
		let initialRotation = mix(parameters.rotationRange.x, parameters.rotationRange.y, random01(&seed));
		let angularVelocity = mix(parameters.rotationRange.z, parameters.rotationRange.w, random01(&seed));
		particle.originVelocity = vec4<f32>(origin, velocity);
		particle.ageLifeRotationAngular = vec4<f32>(parameters.time.x, lifetime, initialRotation, angularVelocity);
		particle.randomState = vec4<u32>(seed, 0u, 0u, 0u);
		age = 0.0;
	}
	else if (particle.ageLifeRotationAngular.y > 0.0)
	{
		age = particle_age(particle, parameters.time.x);
	}

	if (particle.ageLifeRotationAngular.y <= 0.0)
	{
		particles[index] = particle;
		return;
	}
	let position = particle_position(particle, age, parameters);
	let ageRatio = clamp(age / particle.ageLifeRotationAngular.y, 0.0, 1.0);
	let size = mix(parameters.directionSpreadRadiusSize.w, parameters.appearance.x, ageRatio);
	let halfSize = max(size * 0.5, 0.0);
	let angle = particle.ageLifeRotationAngular.z + particle.ageLifeRotationAngular.w * age
		+ select(0.0, parameters.transform.z, (parameters.misc.w & 2u) != 0u);
	let rotatedHalfExtent = halfSize * (abs(cos(angle)) + abs(sin(angle)))
		+ 0.5 / max(parameters.appearance.w, 0.001);
	let outside = position.x + rotatedHalfExtent < parameters.cameraBounds.x
		|| position.y + rotatedHalfExtent < parameters.cameraBounds.y
		|| position.x - rotatedHalfExtent > parameters.cameraBounds.z
		|| position.y - rotatedHalfExtent > parameters.cameraBounds.w;
	if (!outside)
	{
		let visibleIndex = atomicAdd(&drawArguments.instanceCount, 1u);
		if (visibleIndex < parameters.misc.x)
		{
			visibleIndices[visibleIndex] = index;
		}
	}
	particles[index] = particle;
}

struct RenderParticle
{
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
	@location(1) color: vec4<f32>,
};

@group(1) @binding(0) var<storage, read> renderParticles: array<Particle>;
@group(1) @binding(1) var<uniform> renderParameters: Parameters;
@group(1) @binding(2) var<storage, read> renderVisibleIndices: array<u32>;

@vertex
fn render_particle(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> RenderParticle
{
	let particleIndex = renderVisibleIndices[instanceIndex];
	let particle = renderParticles[particleIndex];
	let age = particle_age(particle, renderParameters.time.x);
	let ageRatio = clamp(age / particle.ageLifeRotationAngular.y, 0.0, 1.0);
	let size = mix(renderParameters.directionSpreadRadiusSize.w, renderParameters.appearance.x, ageRatio);
	let angle = particle.ageLifeRotationAngular.z + particle.ageLifeRotationAngular.w * age
		+ select(0.0, renderParameters.transform.z, (renderParameters.misc.w & 2u) != 0u);
	let local = array<vec2<f32>, 6>(
		vec2<f32>(-0.5, -0.5), vec2<f32>(0.5, -0.5), vec2<f32>(-0.5, 0.5),
		vec2<f32>(-0.5, 0.5), vec2<f32>(0.5, -0.5), vec2<f32>(0.5, 0.5)
	);
	let corner = local[vertexIndex];
	let worldPosition = particle_position(particle, age, renderParameters) + rotate(corner * size, angle);
	let cameraCenter = (renderParameters.cameraBounds.xy + renderParameters.cameraBounds.zw) * 0.5;
	let screenPosition = (worldPosition - cameraCenter) * renderParameters.appearance.w
		+ renderParameters.appearance.yz * 0.5;
	let ndc = vec2<f32>(screenPosition.x / renderParameters.appearance.y * 2.0 - 1.0,
		1.0 - screenPosition.y / renderParameters.appearance.z * 2.0);
	let depth = 0.5 + atan(renderParameters.transform.w) / 3.14159265359;
	var output: RenderParticle;
	output.position = vec4<f32>(ndc, depth, 1.0);
	output.uv = corner * 2.0;
	output.color = mix(renderParameters.colorStart, renderParameters.colorEnd, ageRatio);
	return output;
}

struct OitOutput
{
	@location(0) accumulation: vec4<f32>,
	@location(1) revealage: f32,
};

@fragment
fn render_particle_fragment(input: RenderParticle) -> OitOutput
{
	let distance = length(input.uv);
	let coverage = 1.0 - smoothstep(0.85, 1.0, distance);
	let alpha = clamp(input.color.a * coverage, 0.0, 1.0);
	if (alpha <= 0.0)
	{
		discard;
	}
	let weight = clamp(0.01 + 8.0 * input.position.z * input.position.z * input.position.z, 0.01, 8.0);
	var output: OitOutput;
	output.accumulation = vec4<f32>(input.color.rgb * alpha * weight, alpha * weight);
	output.revealage = alpha;
	return output;
}

#include "lua_noise_bindings.h"

#define STB_PERLIN_IMPLEMENTATION
#include "axeng/external/stb_perlin.h"

void ax::lua::bindings::setup_noise_bindings(sol::state& lua)
{
    auto noise{ lua.create_table() };

    noise["seed"] = 0xDEADBEEF;

    noise["perlin"] =
        [&lua] (float x, float y)
        {
            return stb_perlin_noise3_seed(x, y, 0, 0, 0, 0, lua["noise"]["seed"]);
        };

    noise["ridge"] =
        [] (float x, float y, float lacunarity = 2.0f, float gain = 0.5f, float offset = 1.0f, int octaves = 6)
        {
            return stb_perlin_ridge_noise3(x, y, 0, lacunarity, gain, offset, octaves);
        };

    noise["fbm"] =
        [] (float x, float y, float lacunarity = 2.0f, float gain = 0.5f, int octaves = 6)
        {
            return stb_perlin_fbm_noise3(x, y, 0, lacunarity, gain, octaves);
        };

    noise["turbulence"] =
        [] (float x, float y, float lacunarity = 2.0f, float gain = 0.5f, int octaves = 6)
        {
            return stb_perlin_turbulence_noise3(x, y, 0, lacunarity, gain, octaves);
        };

    lua["noise"] = noise;
}

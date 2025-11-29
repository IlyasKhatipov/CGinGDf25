#define SHADOW_DEPTH_BIAS 0.00005f

struct Light
{
    float4 position;
    float4 color;
};

cbuffer ConstantBuffer: register(b0)
{
    float4x4 mwpMatrix;
    float4x4 shadowMatrix;
    Light light;
}

Texture2D g_texture : register(t0);
Texture2D g_shadow_map : register(t1);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 world_position : POSITION;
    float3 normal : NORMAL;
};

PSInput VSMain(float4 position : POSITION, float4 normal: NORMAL, float4 ambient : COLOR0, float4 diffuse : COLOR1,  float4 emissive : COLOR2, float4 texcoords : TEXCOORD)
{
    PSInput result;
    result.position = mul(mwpMatrix, position);
    result.color = ambient;
    result.uv = texcoords.xy;
    result.world_position = position.xyz;
    result.normal = normal.xyz;
    return result;
}

PSInput VSShadowMap(float4 position : POSITION, float4 normal: NORMAL, float4 ambient : COLOR0, float4 diffuse : COLOR1,  float4 emissive : COLOR2, float4 texcoords : TEXCOORD)
{
    PSInput result;
    result.position = mul(shadowMatrix, position);
    result.color = ambient;
    result.uv = texcoords.xy;
    result.world_position = position.xyz;
    result.normal = normal.xyz;
    return result;
}

float4 GetLambertIntensity(PSInput input)
{
    float3 to_light = normalize(float3(1.f, 1.f, 1.f) - input.world_position);
    float distance = length(to_light);
    float attenuation = 1.f / (distance * distance + 1);
    return saturate(dot(input.normal, normalize(to_light))) * light.color * attenuation;
}

float CalcUnshadowedAmount(float3 world_pos)
{
    float4 light_space_position = float4(world_pos, 1.f);
    light_space_position = mul(shadowMatrix, light_space_position);
    light_space_position.xyz /= light_space_position.w;
    float2 tex_coords = 0.5f * light_space_position.xy + 0.5f;
    tex_coords.y = 1.f - tex_coords.y;

    float ls_depth = light_space_position.z - SHADOW_DEPTH_BIAS;
    return (g_shadow_map.Sample(g_sampler, tex_coords) >= ls_depth) ? 1 : 0.5;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color * CalcUnshadowedAmount(input.world_position) * (0.5f + 0.5f * GetLambertIntensity(input));
}

float4 PSMain_texture(PSInput input) : SV_TARGET
{
    return g_texture.Sample(g_sampler, input.uv) * CalcUnshadowedAmount(input.world_position) * (0.5f + 0.5f * GetLambertIntensity(input));
}
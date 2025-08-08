// Minimal Levels compute pass for D3D11

cbuffer cbLevels : register(b0) {
    float4 levelsParams0; // x=inBlack, y=inWhite, z=outBlack, w=outWhite
    float4 levelsParams1; // x=gamma, y/z/w unused
};

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(64, 1, 1)]
void mainCS(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID) {
    uint2 gxy = (uint2(LocalThreadId.x & 7u, (LocalThreadId.x >> 3) & 7u) + (uint2(WorkGroupId.x, WorkGroupId.y) << 4u));

    float inBlack = levelsParams0.x;
    float inWhite = levelsParams0.y;
    float outBlack = levelsParams0.z;
    float outWhite = levelsParams0.w;
    float gamma = max(levelsParams1.x, 0.001);

    float3 c = InputTexture.Load(int3(gxy, 0)).rgb;
    float3 v = saturate((c - inBlack) / max(inWhite - inBlack, 1e-6));
    v = pow(v, gamma);
    v = v * saturate(outWhite - outBlack) + outBlack;
    OutputTexture[gxy] = float4(v, 1);

    gxy.x += 8u;
    c = InputTexture.Load(int3(gxy, 0)).rgb;
    v = saturate((c - inBlack) / max(inWhite - inBlack, 1e-6));
    v = pow(v, gamma);
    v = v * saturate(outWhite - outBlack) + outBlack;
    OutputTexture[gxy] = float4(v, 1);

    gxy.y += 8u;
    c = InputTexture.Load(int3(gxy, 0)).rgb;
    v = saturate((c - inBlack) / max(inWhite - inBlack, 1e-6));
    v = pow(v, gamma);
    v = v * saturate(outWhite - outBlack) + outBlack;
    OutputTexture[gxy] = float4(v, 1);

    gxy.x -= 8u;
    c = InputTexture.Load(int3(gxy, 0)).rgb;
    v = saturate((c - inBlack) / max(inWhite - inBlack, 1e-6));
    v = pow(v, gamma);
    v = v * saturate(outWhite - outBlack) + outBlack;
    OutputTexture[gxy] = float4(v, 1);
}



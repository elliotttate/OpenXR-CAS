// Minimal CAS compute shader for D3D11

cbuffer cb : register(b0) {
    uint4 const0;
    uint4 const1;
};

cbuffer cbDebug : register(b1) {
    uint4 dbg0; // x=flags, y=offset.x, z=offset.y, w=extent.x
    uint4 dbg1; // x=extent.y
};

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

#define A_GPU 1
#define A_HLSL 1

#include "ffx_a.h"
#if 1
// Provide loader hookup expected by ffx_cas.h
AF3 CasLoad(ASU2 p) {
    return InputTexture.Load(int3(p, 0)).rgb;
}
void CasInput(inout AF1 r, inout AF1 g, inout AF1 b) {
}
#endif
#include "ffx_cas.h"

// Debug overlay disabled in production builds.
void DrawOverlay(uint flags, uint2 loc, uint2 pos, uint2 extent) { /* no-op */ }

[numthreads(64, 1, 1)]
void mainCS(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID) {
    AU2 gxyLocal = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
    AU2 offset = AU2(dbg0.y, dbg0.z);
    AU2 extent = AU2(dbg0.w, dbg1.x);
    AU2 gxy = gxyLocal + offset;

    uint flags = 0u; // force disabled

    AF3 c;
    bool sharpenOnly = true; // sharpen-only path

    // Note: CAS expects (0..1) strength internally; if user sets >1 we rely on the CPU side to produce const0/const1 that reflect that magnitude.
    CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
    OutputTexture[ASU2(gxy)] = AF4(c, 1);
    DrawOverlay(flags, gxyLocal, gxy, extent);
    gxy.x += 8u;

    CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
    OutputTexture[ASU2(gxy)] = AF4(c, 1);
    DrawOverlay(flags, gxyLocal + AU2(8u, 0u), gxy, extent);
    gxy.y += 8u;

    CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
    OutputTexture[ASU2(gxy)] = AF4(c, 1);
    DrawOverlay(flags, gxyLocal + AU2(8u, 8u), gxy, extent);
    gxy.x -= 8u;

    CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
    OutputTexture[ASU2(gxy)] = AF4(c, 1);
    DrawOverlay(flags, gxyLocal + AU2(0u, 8u), gxy, extent);
}



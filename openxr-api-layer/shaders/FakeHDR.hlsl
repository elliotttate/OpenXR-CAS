// Fake HDR compute pass for D3D11 (inspired by ReShade HDR.fx)

cbuffer cbFakeHDR : register(b0) {
    float HDRPower;
    float radius1;
    float radius2;
    float pad0;
    uint2 offset;   // sub-rect offset in pixels
    uint2 extent;   // sub-rect size in pixels (width, height)
}

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

float3 sampleAt(int2 p) { return InputTexture.Load(int3(p, 0)).rgb; }

bool inside(uint2 q, uint2 off, uint2 ext) {
    return (q.x >= off.x) && (q.y >= off.y) && (q.x < off.x + ext.x) && (q.y < off.y + ext.y);
}

float3 ringBlur(int2 p, int da, int db, int2 minXY, int2 maxXY) {
    float3 s = 0;
    s += sampleAt(int2(clampi(p.x + da, minXY.x, maxXY.x), clampi(p.y - da, minXY.y, maxXY.y)));
    s += sampleAt(int2(clampi(p.x - da, minXY.x, maxXY.x), clampi(p.y - da, minXY.y, maxXY.y)));
    s += sampleAt(int2(clampi(p.x + da, minXY.x, maxXY.x), clampi(p.y + da, minXY.y, maxXY.y)));
    s += sampleAt(int2(clampi(p.x - da, minXY.x, maxXY.x), clampi(p.y + da, minXY.y, maxXY.y)));
    s += sampleAt(int2(p.x, clampi(p.y - db, minXY.y, maxXY.y)));
    s += sampleAt(int2(p.x, clampi(p.y + db, minXY.y, maxXY.y)));
    s += sampleAt(int2(clampi(p.x - db, minXY.x, maxXY.x), p.y));
    s += sampleAt(int2(clampi(p.x + db, minXY.x, maxXY.x), p.y));
    return s * (1.0 / 8.0);
}

float3 processPixel(uint2 dst, int2 minXY, int2 maxXY, float r1, float r2, float hdrPower) {
    // Clamp sampling position for stable kernels near edges
    int2 p = int2(clampi((int)dst.x, minXY.x, maxXY.x), clampi((int)dst.y, minXY.y, maxXY.y));
    float3 color = sampleAt(p);

    int d1a = max(1, (int)round(1.5 * r1));
    int d1b = max(1, (int)round(2.5 * r1));
    int d2a = max(1, (int)round(1.5 * r2));
    int d2b = max(1, (int)round(2.5 * r2));

    float3 b1 = ringBlur(p, d1a, d1b, minXY, maxXY);
    float3 b2 = ringBlur(p, d2a, d2b, minXY, maxXY);

    // Strength decoupled from radius: derive from radius gap (tunable scale)
    float Strength = max(0.0, r2 - r1);
    float3 hdrDelta = (b2 - b1) * Strength;
    float3 hdr = color + hdrDelta;
    return pow(abs(hdr), abs(hdrPower)) + hdrDelta;
}

[numthreads(64, 1, 1)]
void mainCS(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID) {
    // Map 64 threads to 8x8 tiles
    uint2 base = uint2(LocalThreadId.x & 7u, (LocalThreadId.x >> 3) & 7u) + (uint2(WorkGroupId.xy) << 4u);

    int2 minXY = int2(offset.xy);
    int2 maxXY = int2(offset.xy + extent.xy - 1);

    // Sanitize radii and ensure r2 >= r1
    float r1 = max(0.0, radius1);
    float r2 = max(0.0, radius2);
    if (r2 < r1) { float t = r1; r1 = r2; r2 = t; }

    uint2 dsts[4] = {
        base + offset,
        base + offset + uint2(8, 0),
        base + offset + uint2(8, 8),
        base + offset + uint2(0, 8)
    };

    [unroll]
    for (int i = 0; i < 4; ++i) {
        uint2 dst = dsts[i];
        if (inside(dst, offset, extent)) {
            float3 outc = processPixel(dst, minXY, maxXY, r1, r2, HDRPower);
            OutputTexture[dst] = float4(saturate(outc), 1);
        }
    }
}



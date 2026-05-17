#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    float m;
    float n;
    float theta;
    float lineWidth;
    float time;
    float signal;
}
pc;

layout(set = 0, binding = 1) uniform PaletteUBO
{
    vec4 colors[8];
    int numStops;
    float _pad0;
    float _pad1;
    float _pad2;
}
palette;

const float PI = 3.14159265;

// exact roots of cos(b)*cosh(b)=1; k>=6 uses (k-0.5)*pi on the fly (indistinguishable in float32)
const float BETA[4] = float[](
    4.73004074,  // k=2
    7.85320462,  // k=3
    10.99560784, // k=4
    14.13716549  // k=5
);

// sigma=(cosh(b)-cos(b))/(sinh(b)-sin(b)); converges to 1.0 by k=5 in float32
const float SIGMA[3] = float[](
    0.98250222,  // k=2
    1.00077731,  // k=3
    0.99999645   // k=4
);

vec4 samplePalette(float t)
{
    t = clamp(t, 0.0, 1.0);
    int n = palette.numStops;
    if (n <= 1)
    {
        return palette.colors[0];
    }
    float scaled = t * float(n - 1);
    int lo = int(floor(scaled));
    int hi = lo + 1;
    if (hi >= n)
    {
        return palette.colors[n - 1];
    }
    float frac = scaled - float(lo);
    return mix(palette.colors[lo], palette.colors[hi], frac);
}

// stable form of cosh(bx)-s*sinh(bx): rewrite as ((1-s)*exp(bx)+(1+s)*exp(-bx))/2 to avoid catastrophic cancellation at high modes
float beamMode(int k, float xi)
{
    int idx = clamp(k - 2, 0, 13);
    float b = (idx < 4) ? BETA[idx] : (float(k) - 0.5) * PI;
    float s = (idx < 3) ? SIGMA[idx] : 1.0;
    float bx = b * xi;
    float ebx = exp(bx);
    float hyperbolic = ((1.0 - s) * ebx + (1.0 + s) / ebx) * 0.5;
    return hyperbolic + cos(bx) - s * sin(bx);
}

// normalize to amplitude 1 at xi=0 so adjacent modes stay at comparable scale
float beamModeNorm(int k, float xi)
{
    return beamMode(k, xi) * 0.5;
}

// linearly interpolate between floor(k) and ceil(k) to avoid sudden nodal-line jumps on mode transitions
float evalMode(float k_cont, float xi)
{
    float kf = clamp(k_cont, 2.0, 15.0);
    int klo = clamp(int(floor(kf)), 2, 14);
    int khi = klo + 1;
    float frac = kf - float(klo);
    return mix(beamModeNorm(klo, xi), beamModeNorm(khi, xi), frac);
}

void main()
{
    // abs(fragUV) folds both halves to xi in [0,1] with xi=0 at screen center, giving 4-fold symmetry
    float xi_x = abs(fragUV.x);
    float xi_y = abs(fragUV.y);

    float phiMx = evalMode(pc.m, xi_x);
    float phiMy = evalMode(pc.m, xi_y);
    float phiNx = evalMode(pc.n, xi_x);
    float phiNy = evalMode(pc.n, xi_y);

    // degenerate superposition: tensor product with angle theta blending (m,n) and (n,m)
    float u = cos(pc.theta) * phiMx * phiNy + sin(pc.theta) * phiNx * phiMy;

    float brightness = 1.0 - smoothstep(0.0, pc.lineWidth, abs(u));

    outColor = vec4(samplePalette(brightness).rgb, 1.0);
}

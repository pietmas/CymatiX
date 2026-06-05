#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inVel;
layout(location = 2) in float inAge;
layout(location = 3) in float inEnergy;

layout(location = 0) out float fragAge;
layout(location = 1) out float fragEnergy;

layout(push_constant) uniform PC
{
    int largePoints;
}
pc;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0);

    fragAge = inAge;
    fragEnergy = inEnergy;

    if (pc.largePoints == 1)
    {
        // size by energy; older particles shrink
        gl_PointSize = mix(4.0, 1.0, inAge) * (1.0 + inEnergy * 3.0);
    }
    else
    {
        // largePoints feature unsupported, must stay at 1.0
        gl_PointSize = 1.0;
    }
}

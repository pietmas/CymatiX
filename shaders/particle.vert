#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inVel;
layout(location = 2) in float inAge;
layout(location = 3) in float inEnergy;
layout(location = 4) in float inFreq;

layout(location = 0) out float fragAge;
layout(location = 1) out float fragEnergy;
layout(location = 2) out float fragFreq;

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
    fragFreq = inFreq;

    if (pc.largePoints == 1)
    {
        // size by energy; older particles shrink. louder => fatter (punch).
        // gentle freq term so treble reads a bit bigger than bass, proportional to freq
        float trebleSize = 1.0 + inFreq * 0.8;
        gl_PointSize = mix(5.0, 1.0, inAge) * (1.0 + inEnergy * 5.0) * trebleSize;
    }
    else
    {
        // largePoints feature unsupported, must stay at 1.0
        gl_PointSize = 1.0;
    }
}

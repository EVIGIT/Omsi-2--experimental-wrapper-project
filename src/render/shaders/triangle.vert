#version 450

// The first shader of the project. It is not used by anything yet; it exists so that the
// build-time SPIR-V compilation is exercised from the first day. A shader that does not
// compile fails the build instead of the first frame.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColour;
layout(location = 0) out vec3 outColour;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    outColour = inColour;
}
// AI ALERT
#version 430

struct GPURay {
    dvec2 pos;
    dvec2 vel;
    vec2 cartPos;
    int dead;
    int pad1; int pad2; int pad3; int pad4; int pad5;
};

layout(std430, binding = 0) buffer RayBuffer {
    GPURay rays[];
};

uniform double spaceScale;
uniform dvec2 spaceOffset;

void main() {
    if (rays[gl_VertexID].dead == 1) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0); // off screen
        return;
    }
    vec2 cart = rays[gl_VertexID].cartPos;  // already computed by compute shader
    gl_Position = vec4(
        float(double(cart.x) * spaceScale + spaceOffset.x),
        float(double(cart.y) * spaceScale + spaceOffset.y),
        0.0, 1.0
    );
    gl_PointSize = 2.0;
}
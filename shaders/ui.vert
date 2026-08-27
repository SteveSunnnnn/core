#version 460
layout(location=0) in vec2 pos;layout(location=1) in vec2 uvIn;layout(location=2) in vec4 colorIn;layout(location=0) out vec2 uv;layout(location=1) out vec4 color;layout(push_constant) uniform Push{vec2 invViewport;}pc;void main(){uv=uvIn;color=colorIn;vec2 p=pos*pc.invViewport*2.0-1.0;gl_Position=vec4(p.x,-p.y,0,1);}

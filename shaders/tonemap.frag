#version 460
layout(set=0,binding=0) uniform sampler2D hdrColor;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Push{int srgb_output;}pc;
vec3 aces_tonemap(vec3 x){
    const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e),vec3(0.0),vec3(1.0));
}
void main(){
    vec3 hdr=texture(hdrColor,uv).rgb;
    vec3 mapped=aces_tonemap(hdr);
    if(pc.srgb_output==0){mapped=pow(mapped,vec3(1.0/2.2));}
    outColor=vec4(mapped,1.0);
}

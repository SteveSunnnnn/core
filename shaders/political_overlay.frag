#version 460
layout(location=0) in vec2 uv;
layout(location=1) in vec4 vview;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Push{vec4 color;}pc;

// Procedural political layer: muted country tints with dark borders over a
// continental land mask. Purely synthetic — a stand-in until a real world
// pack supplies province colors through the production map pipeline.
// Coordinates are screen-normalized so continents and borders stay readable
// at any zoom level.

float hash12(vec2 p){
    vec3 p3=fract(vec3(p.xyx)*0.1031);
    p3+=dot(p3,p3.yzx+33.33);
    return fract((p3.x+p3.y)*p3.z);
}
float vnoise(vec2 p){
    vec2 i=floor(p),f=fract(p);
    vec2 u=f*f*(3.0-2.0*f);
    return mix(mix(hash12(i),hash12(i+vec2(1,0)),u.x),
               mix(hash12(i+vec2(0,1)),hash12(i+vec2(1,1)),u.x),u.y);
}

vec3 palette(float id){
    // Muted nineteenth-century map tints.
    vec3 c0=vec3(0.55,0.30,0.30); // dusty red
    vec3 c1=vec3(0.52,0.48,0.26); // olive
    vec3 c2=vec3(0.30,0.38,0.55); // steel blue
    vec3 c3=vec3(0.45,0.32,0.52); // plum
    vec3 c4=vec3(0.28,0.47,0.42); // teal
    vec3 c5=vec3(0.62,0.52,0.30); // ochre
    vec3 c6=vec3(0.42,0.42,0.40); // gray
    vec3 c7=vec3(0.55,0.40,0.24); // tan
    int k=int(mod(id*8.0,8.0));
    if(k==0)return c0;if(k==1)return c1;if(k==2)return c2;if(k==3)return c3;
    if(k==4)return c4;if(k==5)return c5;if(k==6)return c6;return c7;
}

void main(){
    // Screen-normalized world coordinate: feature size stays readable at any
    // zoom, while panning still traverses the same synthetic continent.
    float s=clamp(vview.z*2.0,0.10,1.0);
    vec2 w=uv*(3.0/s);

    float cont=vnoise(w*0.9+7.7)*0.72+vnoise(w*2.6+3.3)*0.28;
    float land=smoothstep(0.40,0.50,cont);

    // One warped-cell evaluation; borders come from the distance to the cell
    // edge in warped space instead of extra region lookups.
    vec2 q=w*1.4+vec2(vnoise(w*0.6+11.7),vnoise(w*0.75+47.3))*0.55;
    float id=hash12(floor(q));
    vec2 f=fract(q);
    float edge=min(min(f.x,1.0-f.x),min(f.y,1.0-f.y));
    float border=(1.0-smoothstep(0.0,0.06,edge))*land;

    vec3 tint=palette(id);
    tint*=0.92+0.16*vnoise(w*6.0+5.0);

    float alpha=land*0.46+border*0.40;
    vec3 col=mix(tint,vec3(0.10,0.08,0.07),clamp(border,0.0,1.0)*0.85);
    outColor=vec4(col,alpha);
}

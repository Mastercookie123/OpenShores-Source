// Subtle, derivative-filtered surface detail; no change to palette or alpha.
#ifdef CHARACTER_SURFACE_ENABLED
uniform sampler2D surfaceDetail;
uniform bool surfaceDetailEnabled;
uniform int surfaceKind;

vec3 characterBodyNormal(vec3 N, vec3 P, vec2 uv, float clothes)
{
    if(!surfaceDetailEnabled) return N;
    vec3 T = dFdx(P) * dFdy(uv).y - dFdy(P) * dFdx(uv).y;
    float determinant=dFdx(uv).x*dFdy(uv).y-dFdx(uv).y*dFdy(uv).x;
    if(abs(determinant)<1.e-12) return N;
    T*=sign(determinant);
    if(dot(T,T)<1.e-12) return N;
    T=T-N*dot(N,T);
    if(dot(T,T)<1.e-12) return N;
    T=normalize(T);
    vec3 B=normalize(cross(N,T))*sign(determinant);
    vec2 q=uv*4.;
    vec2 stepUV=1./vec2(textureSize(surfaceDetail,0));
    float dx=texture(surfaceDetail,q+vec2(stepUV.x,0)).r-texture(surfaceDetail,q-vec2(stepUV.x,0)).r;
    float dy=texture(surfaceDetail,q+vec2(0,stepUV.y)).r-texture(surfaceDetail,q-vec2(0,stepUV.y)).r;
    float fade=1.-smoothstep(2.,8.,max(length(dFdx(q/stepUV)),length(dFdy(q/stepUV))));
    float skin=(surfaceKind==2 || surfaceKind==7)?1.:.25;
    float clothFade=1.-smoothstep(1.,3.,max(fwidth(uv.x*1800.),fwidth(uv.y*1800.)));
    vec2 weave=vec2(sin(uv.x*1800.),sin(uv.y*1800.))*.018*clothFade;
    vec2 slope=mix(vec2(dx,dy)*skin*.12*fade,weave,clothes);
    return normalize(N-T*slope.x-B*slope.y);
}

float characterMaterialGloss(float original, float clothes)
{
    if(!surfaceDetailEnabled) return original;
    bool wet=surfaceKind>=5;
    float skinGloss=wet?.75:.35;
    return original*mix(skinGloss,.7,clothes);
}

vec3 characterSurfaceAlbedo(vec3 original,sampler2D palette,vec4 offset,float topFace)
{
    if(!surfaceDetailEnabled || (surfaceKind!=2 && surfaceKind!=7)) return original;
    vec3 top=textureLod(palette,vec2(.14,.98)+offset.xy,3.).rgb;
    vec3 bottom=textureLod(palette,vec2(.14,.98)+offset.zw,3.).rgb;
    vec3 base=mix(bottom,top,topFace);
    float background=1.-smoothstep(.07,.18,length(original-base));
    return mix(original,base,.45*background);
}

vec3 characterSkinDetail(vec3 albedo, vec2 uv)
{
    vec2 p = uv * 600.;
    float fade = 1. - smoothstep(.5, 1.5, max(fwidth(p.x), fwidth(p.y)));
    float pores = sin(p.x * 1.7 + sin(p.y * 1.3)) * sin(p.y * 1.9);
    float detail=surfaceDetailEnabled?texture(surfaceDetail,uv*4.).r-.5:0.;
    return albedo * (1. + .015 * pores * fade + .035*detail);
}

vec3 characterHairNormal(vec3 N, vec3 P, vec2 uv)
{
    vec3 tangent = dFdx(P) * dFdy(uv).y - dFdy(P) * dFdx(uv).y;
    if(dot(tangent, tangent) < 1.e-12) return N;
    tangent = normalize(tangent);
    float phase = uv.x * 900. + sin(uv.y * 13.) * .4;
    float fade = 1. - smoothstep(.3, 1., fwidth(phase));
    return normalize(N + tangent * (.08 * sin(phase) * fade));
}

float characterHairDetail(vec2 uv)
{
    float phase = uv.x * 900. + sin(uv.y * 13.) * .4;
    float fade = 1. - smoothstep(.3, 1., fwidth(phase));
    return 1. - .12 * (.5 + .5 * sin(phase)) * fade;
}
#endif

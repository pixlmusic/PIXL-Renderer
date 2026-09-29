// PIXL Renderer - profile-aware contained-liquid optics.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#ifndef PIXL_CONTAINED_LIQUIDS_INCLUDED
#define PIXL_CONTAINED_LIQUIDS_INCLUDED
#include "Common/BRDF.hlsli"
// Independent PIXL implementation. Native camera-relative positions, Skyrim +Z up.
namespace ContainedLiquids
{
    struct DrawData {
        float4 center, axisX, axisY, axisZ, plane, optics, crop, dynamics;
        float4 profile0, profile1, profile2, profile3, detail, appearance;
        float4 opticalColor, liquidColor;
    };
    StructuredBuffer<DrawData> LiquidDraw : register(t120);
    Texture2D<float4> SceneCrop : register(t121);

    float3 SampleCrop(float2 pixel, DrawData d)
    {
        float2 p=clamp(pixel-d.crop.xy-0.5f,0.0f.xx,d.crop.zw-2.0f);
        int2 q=int2(floor(p)); float2 f=frac(p);
        return lerp(lerp(SceneCrop.Load(int3(q,0)).rgb,SceneCrop.Load(int3(q+int2(1,0),0)).rgb,f.x),
            lerp(SceneCrop.Load(int3(q+int2(0,1),0)).rgb,SceneCrop.Load(int3(q+1,0)).rgb,f.x),f.y);
    }
    void Miss(DrawData d) { if (d.dynamics.z>0.5f) discard; }

    float Hash11(float value)
    {
        return frac(sin(value * 127.1f + 311.7f) * 43758.5453f);
    }

    float ProfileRadius(float z, DrawData d)
    {
        float coordinate=saturate((z+1.0f)*0.5f)*15.0f;
        int lower=(int)floor(coordinate);
        int upper=min(lower+1,15);
        float radii[16]={d.profile0.x,d.profile0.y,d.profile0.z,d.profile0.w,
            d.profile1.x,d.profile1.y,d.profile1.z,d.profile1.w,
            d.profile2.x,d.profile2.y,d.profile2.z,d.profile2.w,
            d.profile3.x,d.profile3.y,d.profile3.z,d.profile3.w};
        return lerp(radii[lower],radii[upper],frac(coordinate));
    }

    float ProfileRadiusAtIndex(int index,DrawData d)
    {
        index=clamp(index,0,15);
        float4 values=d.profile3;
        if (index<4) values=d.profile0;
        else if (index<8) values=d.profile1;
        else if (index<12) values=d.profile2;
        return values[index&3];
    }

    bool InsideProfile(float3 samplePosition, DrawData d)
    {
        if (abs(samplePosition.z)>1.0f) return false;
        float radius=max(ProfileRadius(samplePosition.z,d),0.02f);
        return dot(samplePosition.xy,samplePosition.xy)<radius*radius;
    }

    float3 ProfileNormal(float3 samplePosition, DrawData d)
    {
        float radius=max(length(samplePosition.xy),1e-4f);
        float dz=0.025f;
        float slope=(ProfileRadius(clamp(samplePosition.z+dz,-1.0f,1.0f),d)-
            ProfileRadius(clamp(samplePosition.z-dz,-1.0f,1.0f),d))/(2.0f*dz);
        return normalize(float3(samplePosition.xy/radius,-slope));
    }

    void AddProfileInterval(float lower,float upper,inout bool found,
        inout float profileEntry,inout float profileExit)
    {
        if (upper<=lower) return;
        found=true;
        profileEntry=min(profileEntry,lower);
        profileExit=max(profileExit,upper);
    }

    void SolveProfileQuadratic(float segmentEntry,float segmentExit,
        float a,float b,float c,inout bool found,
        inout float profileEntry,inout float profileExit)
    {
        const float epsilon=1.0e-7f;
        if (abs(a)<epsilon) {
            if (abs(b)<epsilon) {
                if (c<=0.0f) AddProfileInterval(segmentEntry,segmentExit,found,profileEntry,profileExit);
                return;
            }
            float root=-c/b;
            if (b>0.0f) AddProfileInterval(segmentEntry,min(segmentExit,root),found,profileEntry,profileExit);
            else AddProfileInterval(max(segmentEntry,root),segmentExit,found,profileEntry,profileExit);
            return;
        }
        float discriminant=b*b-4.0f*a*c;
        if (discriminant<0.0f) {
            if (a<0.0f) AddProfileInterval(segmentEntry,segmentExit,found,profileEntry,profileExit);
            return;
        }
        float root=sqrt(max(discriminant,0.0f));
        float first=(-b-root)/(2.0f*a);
        float second=(-b+root)/(2.0f*a);
        if (first>second) {float temporary=first;first=second;second=temporary;}
        if (a>0.0f) {
            AddProfileInterval(max(segmentEntry,first),min(segmentExit,second),found,profileEntry,profileExit);
        } else {
            AddProfileInterval(segmentEntry,min(segmentExit,first),found,profileEntry,profileExit);
            AddProfileInterval(max(segmentEntry,second),segmentExit,found,profileEntry,profileExit);
        }
    }

    bool IntersectProfile(float3 origin,float3 direction,float broadEntry,float broadExit,
        DrawData d,out float profileEntry,out float profileExit)
    {
        profileEntry=broadExit;
        profileExit=broadEntry;
        bool found=false;
        [unroll] for (int segment=0;segment<15;++segment) {
            float z0=-1.0f+2.0f*segment/15.0f;
            float z1=-1.0f+2.0f*(segment+1)/15.0f;
            float segmentEntry=broadEntry;
            float segmentExit=broadExit;
            if (abs(direction.z)>1.0e-7f) {
                float tz0=(z0-origin.z)/direction.z;
                float tz1=(z1-origin.z)/direction.z;
                segmentEntry=max(segmentEntry,min(tz0,tz1));
                segmentExit=min(segmentExit,max(tz0,tz1));
            } else if (origin.z<z0 || origin.z>z1) {
                continue;
            }
            if (segmentExit<=segmentEntry) continue;
            float r0=ProfileRadiusAtIndex(segment,d);
            float r1=ProfileRadiusAtIndex(segment+1,d);
            float slope=(r1-r0)/(z1-z0);
            float radiusBase=r0+slope*(origin.z-z0);
            float radiusRate=slope*direction.z;
            float qa=dot(direction.xy,direction.xy)-radiusRate*radiusRate;
            float qb=2.0f*(dot(origin.xy,direction.xy)-radiusBase*radiusRate);
            float qc=dot(origin.xy,origin.xy)-radiusBase*radiusBase;
            SolveProfileQuadratic(segmentEntry,segmentExit,qa,qb,qc,
                found,profileEntry,profileExit);
        }
        return found && profileExit>profileEntry;
    }
    void Composite(float3 position, float2 pixel, float3 ambient, float3 lightColor,
        float3 lightDirection, inout float3 bottle, inout float alpha)
    {
        DrawData d=LiquidDraw[0]; // Unbound slot returns zero: exact no-op fallback.
        if (d.center.w<0.5f) return;
        // The fitted profile deliberately excludes the neck. Discard replay
        // fragments beyond its local height so the original cork/label remains.
        float shellHeight=dot(position-d.center.xyz,d.axisZ.xyz)/max(d.axisZ.w,0.01f);
        if (abs(shellHeight)>1.08f) { Miss(d); return; }
        float2 cropPixel=pixel-d.crop.xy;
        if (any(cropPixel<2)||any(cropPixel>d.crop.zw-3)) { Miss(d); return; }
        float rayLength=length(position);
        if (rayLength<1e-4f) { Miss(d); return; }
        float3 ray=position/rayLength;
        float3 origin=-d.center.xyz;
        float3 o=float3(dot(origin,d.axisX.xyz)/d.axisX.w,dot(origin,d.axisY.xyz)/d.axisY.w,dot(origin,d.axisZ.xyz)/d.axisZ.w);
        float3 v=float3(dot(ray,d.axisX.xyz)/d.axisX.w,dot(ray,d.axisY.xyz)/d.axisY.w,dot(ray,d.axisZ.xyz)/d.axisZ.w);
        float a=dot(v,v);
        if (a<1e-8f) { Miss(d); return; }
        // Quadratic in closest-approach form avoids subtracting two huge
        // nearly equal discriminant terms for a small distant bottle.
        float mid=-dot(o,v)/a;
        float3 closest=o+v*mid;
        float disc=1-dot(closest,closest);
        if (disc<=0) { Miss(d); return; }
        float halfInterval=sqrt(disc/a);
        float entry=max(0,mid-halfInterval), exit=mid+halfInterval;
        if (exit<=entry || !isfinite(exit)) { Miss(d); return; }
        // The bottle mesh itself supplies raster coverage. Inside that coverage,
        // intersect a simplified eight-slice profile generated from the source
        // mesh rather than shading a circular ellipsoid through the glass wall.
        float profileEntry,profileExit;
        if (!IntersectProfile(o,v,entry,exit,d,profileEntry,profileExit)) { Miss(d); return; }
        entry=profileEntry;exit=profileExit;
        if (d.optics.z==1) {bottle=float3(0,0.8f,0.9f);alpha=1;return;}
        float denom=dot(ray,d.plane.xyz);
        float height=dot(origin,d.plane.xyz)-d.plane.w;
        bool surface=false;
        float surfaceT=0;
        if (abs(denom)<1e-5f) { if (height>0) { Miss(d); return; } }
        else {
            surfaceT=-height/denom;
            surface=surfaceT>entry && surfaceT<exit;
            if (denom<0) entry=max(entry,surfaceT); else exit=min(exit,surfaceT);
        }
        float distance=exit-entry;
        if (distance<=1e-4f || !isfinite(distance)) { Miss(d); return; }
        float thickness=min(distance/max(d.optics.w,0.01f),20.0f);
        float3 n=d.plane.xyz;
        float3 entryLocal=o+v*entry;
        float3 profileNormalLocal=ProfileNormal(entryLocal,d);
        float3 profileNormal=normalize(
            d.axisX.xyz*profileNormalLocal.x+
            d.axisY.xyz*profileNormalLocal.y+
            d.axisZ.xyz*profileNormalLocal.z);
        float meniscus=0.0f;
        if (surface) {
            float3 p=origin+ray*surfaceT;
            float2 waves=float2(cos(dot(p.xy,float2(1.7f,0.8f))+d.dynamics.x*5.2f),
                cos(dot(p.xy,float2(-0.6f,2.1f))-d.dynamics.x*6.7f));
            n=normalize(n+float3(waves*d.dynamics.y*0.014f,0));
            float3 surfaceLocal=o+v*surfaceT;
            float wallRadius=max(ProfileRadius(surfaceLocal.z,d),0.02f);
            meniscus=smoothstep(0.72f,0.98f,length(surfaceLocal.xy)/wallRadius);
            n=normalize(lerp(n,profileNormal,meniscus*0.22f));
        }
        if (d.optics.z==2) {bottle=surface?float3(0.1f,1,0.1f):float3(0.1f,0.05f,0.1f);alpha=1;return;}
        if (d.optics.z==3) {bottle=(1-exp(-thickness*0.25f)).xxx;alpha=1;return;}
        if (d.optics.z==4) {bottle=n*0.5f+0.5f;alpha=1;return;}
        // Reverse optical transport through air -> glass -> liquid. The final
        // screen-space displacement is bounded, but its direction and strength
        // come from Snell-style refraction rather than an arbitrary normal shift.
        float edge=saturate(thickness*0.6f);
        float3 opticalNormal=surface?normalize(lerp(profileNormal,n,0.65f)):profileNormal;
        if (dot(opticalNormal,ray)>0.0f) opticalNormal=-opticalNormal;
        float glassIOR=max(d.detail.w,1.01f);
        float liquidIOR=max(d.liquidColor.w,1.01f);
        float3 glassDirection=refract(ray,opticalNormal,1.0f/glassIOR);
        if (dot(glassDirection,glassDirection)<1.0e-6f)
            glassDirection=reflect(ray,opticalNormal);
        float3 liquidDirection=refract(glassDirection,opticalNormal,glassIOR/liquidIOR);
        if (dot(liquidDirection,liquidDirection)<1.0e-6f)
            liquidDirection=reflect(glassDirection,opticalNormal);
        float3 viewRay=mul((float3x3)FrameBuffer::CameraView,ray);
        float3 viewRefracted=mul((float3x3)FrameBuffer::CameraView,normalize(liquidDirection));
        float2 incidentSlope=viewRay.xy/max(abs(viewRay.z),0.25f);
        float2 refractedSlope=viewRefracted.xy/max(abs(viewRefracted.z),0.25f);
        float2 offset=clamp((refractedSlope-incidentSlope)*d.optics.y*(1.2f+thickness*0.32f),
            -3.0f.xx,3.0f.xx)*edge;
        // Preserve the original material response before the replay replaces
        // the volume. The first draw owns opaque ink, labels, cork and glass.
        float3 originalBottle=max(bottle,0);
        float3 background=max(SampleCrop(pixel+offset,d),0);
        float3 absorption=max(d.opticalColor.rgb,0.001f.xxx);
        float3 liquidTint=max(d.liquidColor.rgb,0.0f.xxx);
        float3 transmission=exp(-absorption*d.optics.x*thickness);
        float3 illumination=max(ambient,0)+max(lightColor,0)*saturate(dot(n,lightDirection));
        float3 liquid=background*transmission+liquidTint*(1-transmission)*illumination;
        if (surface) {
            const float f0=(liquidIOR-1)*(liquidIOR-1)/((liquidIOR+1)*(liquidIOR+1));
            float nv=saturate(abs(dot(n,-ray)));
            float fresnel=BRDF::F_Schlick(f0.xxx,nv).x;
            float3 h=lightDirection-ray;
            h*=rsqrt(max(dot(h,h),1e-6f));
            const float surfaceRoughness=0.22f;
            float nl=saturate(dot(n,lightDirection));
            float spec=BRDF::D_GGX(surfaceRoughness,saturate(dot(n,h)))*
                BRDF::Vis_SmithJoint(surfaceRoughness,nv,nl)*nl;
            float3 directF=BRDF::F_Schlick(f0.xxx,saturate(dot(-ray,h)));
            liquid=liquid*(1-fresnel)+fresnel*max(ambient,0)+max(lightColor,0)*directF*spec;
            // Contact-edge curvature carries a restrained dark/tinted meniscus
            // without introducing a screen-space outline around the bottle.
            liquid=lerp(liquid,liquid*(0.82f+liquidTint*0.18f),meniscus*0.42f);
        }
        // Inner-glass reflection responds to scene light on the complete liquid
        // body, with finite energy and no additional scene texture reads.
        float profileNV=saturate(abs(dot(profileNormal,-ray)));
        float glassF0=(glassIOR-1.0f)*(glassIOR-1.0f)/((glassIOR+1.0f)*(glassIOR+1.0f));
        float profileFresnel=BRDF::F_Schlick(glassF0.xxx,profileNV).x*max(d.detail.y,0.0f);
        float profileNL=saturate(dot(profileNormal,lightDirection));
        liquid=lerp(liquid,liquid+max(ambient,0)*0.35f+max(lightColor,0)*profileNL,
            saturate(profileFresnel*0.38f));
        // Light travelling from liquid toward air can exceed the critical angle.
        // Reinforce reflection only in that bounded grazing region.
        float criticalCos=sqrt(saturate(1.0f-1.0f/(liquidIOR*liquidIOR)));
        float tir=1.0f-smoothstep(max(criticalCos-0.10f,0.0f),min(criticalCos+0.03f,1.0f),profileNV);
        liquid+=tir*max(d.detail.y,0.0f)*(max(ambient,0.0f)*0.12f+originalBottle*0.06f);

        // Cheap single-scatter approximation: light arriving from behind the
        // bottle carries a warm tint toward the viewer through thicker fluid.
        // It is deliberately bounded to avoid turning ordinary potions into
        // bloom sources in dark interiors.
        float backLit=saturate(-dot(profileNormal,lightDirection));
        float forwardPhase=pow(saturate(dot(ray,lightDirection)),4.0f);
        float scatterControl=max(d.appearance.x,0.0f);
        float anisotropy=0.35f;
        float phaseDenominator=max(pow(1.0f+anisotropy*anisotropy-
            2.0f*anisotropy*saturate(dot(ray,lightDirection)),1.5f),0.05f);
        float phase=(1.0f-anisotropy*anisotropy)/phaseDenominator;
        float scatterAmount=scatterControl*backLit*lerp(forwardPhase,phase*0.22f,0.55f)*
            (1.0f-exp(-thickness*0.55f));
        liquid+=max(lightColor,0)*liquidTint*scatterAmount*0.85f;
        liquid+=max(ambient,0)*liquidTint*scatterAmount*0.10f;

        // Reuse the bottle's existing lighting evaluation as an entry source:
        // the lit outer wall seeds a soft internal lobe on the same side of the
        // volume. This is a cheap stand-in for refracted caustics and keeps the
        // liquid visually tied to Skyrim's actual light direction.
        float lightReceiver=pow(saturate(dot(profileNormal,lightDirection)),1.65f);
        float viewerReceiver=pow(saturate(dot(-ray,reflect(-lightDirection,profileNormal))),3.0f);
        float entryLobe=lightReceiver*lerp(0.38f,1.0f,viewerReceiver)*(1.0f-exp(-thickness*0.32f));
        liquid+=(max(lightColor,0)+max(ambient,0)*0.18f)*liquidTint*
            entryLobe*(0.20f+0.55f*min(scatterControl,2.0f));

        // A few deterministic bubbles live in bottle-local space. They rise
        // slowly but retain an object-stable layout and contribute only a small
        // reflective rim, avoiding noisy per-frame sparkle.
        float bubbleMask=0.0f;
        [unroll] for (int bubbleIndex=0;bubbleIndex<4;++bubbleIndex) {
            float key=d.detail.z+bubbleIndex*1.731f;
            float3 bubbleCenter=float3(
                (Hash11(key)-0.5f)*0.85f,
                (Hash11(key+2.7f)-0.5f)*0.85f,
                frac(Hash11(key+5.1f)+d.dynamics.x*(0.018f+0.006f*bubbleIndex))*1.55f-0.78f);
            float bubbleRadius=lerp(0.025f,0.065f,Hash11(key+8.4f));
            float3 bubbleOrigin=o-bubbleCenter;
            float bubbleA=dot(v,v);
            float bubbleB=dot(bubbleOrigin,v);
            float bubbleDiscriminant=bubbleB*bubbleB-bubbleA*
                (dot(bubbleOrigin,bubbleOrigin)-bubbleRadius*bubbleRadius);
            if (bubbleDiscriminant>0.0f) {
                float bubbleT=(-bubbleB-sqrt(bubbleDiscriminant))/max(bubbleA,1.0e-7f);
                if (bubbleT>=entry && bubbleT<=exit) {
                    float3 bubbleNormal=normalize(bubbleOrigin+v*bubbleT);
                    float bubbleRim=pow(1.0f-saturate(abs(dot(bubbleNormal,normalize(v)))),2.5f);
                    bubbleMask=max(bubbleMask,0.35f+0.65f*bubbleRim);
                }
            }
        }
        bubbleMask*=saturate(d.detail.x)*saturate(thickness*0.8f);
        liquid+=bubbleMask*(max(lightColor,0)*0.32f+max(ambient,0)*0.12f);
        // Magical liquids can illuminate their own volume. Emission scales with
        // thickness so the silhouette stays dark and the core carries the glow.
        liquid+=liquidTint*max(d.appearance.y,0.0f)*(1.0f-exp(-thickness*0.42f));

        // Keep the authored bottle response as a stable outer coating. The old
        // screen-space difference mask changed with reflections and could erase
        // the liquid at particular view angles. A bounded, view-independent
        // retention instead makes the volume read behind the glass while the
        // fitted profile and original bottle raster own all hard boundaries.
        float coverage=smoothstep(0.01f,0.18f,thickness)*d.dynamics.w;
        // Keep the authored glass as the dominant outer coat. The liquid and
        // refraction sit behind it; replacing most of this response makes
        // wine/beer bottles read like opaque tinted plastic.
        float outerRetention=lerp(0.74f,0.92f,saturate(d.appearance.z));
        float3 containedLayer=lerp(liquid+originalBottle*0.06f,originalBottle,outerRetention);
        bottle=lerp(originalBottle,containedLayer,coverage);
        alpha=lerp(alpha,1.0f,coverage);
    }
}
#endif

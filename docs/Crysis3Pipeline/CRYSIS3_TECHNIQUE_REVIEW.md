# Crysis 3 Rendering Technique Review

This document separates techniques documented by Crytek for Crysis 3 from later or broader CryEngine features, then records the design lesson that is applicable to PIXL Renderer. It is not a proposal to transplant CryEngine code or APIs.

Primary references:

- [The Rendering Technologies of Crysis 3 — Crytek, GDC 2013](https://www.slideshare.net/slideshow/rendering-technologies-from-crysis-3-gdc-2013/25052434)
- [The Art and Technology Behind Crysis 3 — Crytek, FMX 2013](https://www.yumpu.com/en/document/view/51949136/the-art-and-technology-behind-crysis-3-pdf-crytek)
- [Crytek: Crysis 3-era CRYENGINE 3 technology trailer](https://www.crytek.com/news/feast-your-eyes-on-a-new-cryengine-3-tech-trailer)
- [CryEngine 3 water-volume caustics](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048636)
- [CryEngine 3 light emitters](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1605701)
- [CryEngine 3 cloud rendering](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/21892261)
- [CryEngine 3 shadow caching](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/19380385)
- [CryEngine 3 glass shader](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048626)

## Evidence table

| Technique | Evidence status | Crytek implementation lesson | PIXL interpretation |
| --- | --- | --- | --- |
| SSDO and bent normals | Confirmed Crysis 3 | Screen-space directional occlusion produced an average unoccluded direction and was used beyond a scalar corner-darkening term. | Reuse PIXL HybridGI horizon masks to produce one canonical directional-visibility resource for indirect diffuse, sky, probe and reflection visibility. |
| Real-time area lights | Confirmed Crysis 3 feature; shape details are broader CryEngine documentation | Finite emitter size changes highlight shape and shadow penumbra. | Begin with bounded effective-radius emitters; prototype LTC rectangle/disc/tube evaluation only after PIXL light-data ABI and inference are proven. |
| Particle lighting | Confirmed Crysis 3 feature; detailed receive/cast capabilities span broader CryEngine versions | Particles belong to the lighting and atmosphere, rather than being unlit emissive overlays. | Preserve PIXL's existing lit Effect/Particle paths and stabilize representative particle emitters before adding new shadow-casting paths. |
| Dynamic water caustics | Confirmed Crysis 3 / CryEngine 3 | Caustics were derived from the actual water surface, including interaction ripples, and could project above the nominal water plane. | Feed a low-resolution live water-normal/focus source into PIXL's existing physical caustic receiver. |
| Volumetric fog shadows | Confirmed Crysis 3 | Shadow visibility was integrated along the view ray using interleaved half-resolution samples and bilateral reconstruction. | Preserve PIXL's more modern froxel/history architecture and validate its directional/local/cloud shadow inputs rather than replacing it. |
| Massive grass simulation | Confirmed Crysis 3 | Constraint chains were grouped into approximately 16 m patches, driven by a coarse 4x4x4 force field and distance/time-sliced. | Treat this as a future FoliageDynamics 2.0 architecture. A shared deterministic wind contract is the safe first foundation; constraint simulation is a separate prototype. |
| Thin G-buffer | Confirmed Crysis 3 | A compact hybrid deferred scene description reduced bandwidth while keeping complex skin/hair paths specialized. | PIXL already owns a thin auxiliary scene buffer. Inventory and reuse spare semantic channels before allocating another MRT. |
| Local IBL and temporal SSR | Confirmed Crysis 3 | Bounded box-corrected HDR probes provided fallback around a screen-space reflection path with history. | PIXL already has a screen/world-cache/probe/sky hierarchy; bounded parallax-correct local probes remain the principal gap. |
| Screen-space SSS | Confirmed Crysis 3 | Screen-space skin diffusion reused deferred lighting data while complex materials remained hybrid-forward. | PIXL Tissue Diffusion already follows this principle. Generic material transmission is a later shared framework, not a reason to duplicate skin SSS. |
| Silhouette POM | Confirmed Crysis 3 | A conservative prism and pixel raymarch could reject pixels at the displaced silhouette, but geometry-shader cost and unchanged main depth were limitations. | Pursue adaptive near-field relief as a controlled research path, not the original geometry-shader implementation. |
| Cloud shadows | Confirmed Crysis 3, texture-based | A moving projected shadow texture accompanied the visible cloud hemisphere but was not a true cloud-cast solution. | Drive future cloud shadowing from the same optical-depth state used to render PIXL clouds/atmosphere. |
| Shadow caching/time slicing | Broader CryEngine 3 documentation | Distant sun cascades and local-light shadows could update at coarser intervals. | Use dirty-state and motion-aware cadence only after measuring Skyrim's actual shadow ownership and update hooks. |
| Advanced glass | Broader CryEngine 3.4 | Dedicated glass handled tint, dirt/cloudiness, depth fog, blur and specialized glass types. | WindowLife already implements much of the optical stack; improve ordering and stability rather than adding a second glass shader. |
| HDR flares | Confirmed Crysis 3, artist-authored rather than physically based | Crytek separated flare authoring from ordinary bloom and explicitly warned against excessive flare. | Keep PIXL bloom restrained; future glare/diffraction/ghosts must be energy- and occlusion-driven. |
| Deferred decals | Confirmed Crysis 3 use with dynamic-geometry limitations | Deferred detail could carry material response instead of only color. | Extend PIXL's existing actor surface-event framework only when real producers exist; a persistent world material-state atlas is post-release research. |
| Material discipline | Confirmed Crysis 3 guidance, not a full modern metal/roughness pipeline | Calibrated Fresnel/specular/gloss values and material test scenes mattered as much as individual effects. | Continue conservative legacy-material normalization in MaterialForge and add confidence/debug tooling before any global retune. |

## Main conclusion

Crysis 3's transferable advantage is system coherence. PIXL already contains many of the corresponding mechanisms. The release-safe path is to formalize shared data contracts and fix gaps in their consumers, not to add similarly named duplicate modules.

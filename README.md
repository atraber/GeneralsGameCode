# Generals Game Code - D3D9 & Graphical Experiments

This repository is dedicated to experimenting with a **Direct3D 9 port** and modern graphical improvements for *Command & Conquer: Generals* and its expansion *Zero Hour*. 

It is built entirely on top of the codebase from [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode), which modernizes the original engine to build with Visual Studio 2022 and modern C++20.

> [!WARNING]
> The `main` branch does not contain a stable, production-ready build. Instead, it hosts experimental ideas, graphics API porting work, and visual enhancements that are actively being developed and tested.

---

## Experimental Features & Implementations

The branches in this repository contain various progressive implementations aiming to modernize the game's original **W3D renderer** and introduce modern rendering techniques:

*   **Direct3D 9 Port:** Porting the core W3D rendering engine and device wrapper from Direct3D 8 to Direct3D 9. This forms the foundation for modern GPU feature support and shader APIs.
*   **Rendering Quality Settings:** Native integration of advanced filtering settings (such as Anisotropic Filtering up to 16x) configurable directly through `options.ini` and in-game menus.
*   **Programmable HLSL Shaders:** Replacing the legacy fixed-function pipeline with vertex and pixel HLSL shaders for rendering terrain and 3D unit meshes.
*   **Physically Based Shading (PBR) & Dynamic Reflections:** Adding an opt-in PBR metallic-roughness BRDF path for unit models utilizing ORM textures (Ambient Occlusion, Roughness, Metallic, Height) alongside dynamic environment maps generated in real-time for reflections.
*   **Screen-Space Bloom:** A post-process bloom pipeline using multiple passes (bright-pass filter, separable Gaussian blur, and additive blending) built on a custom render-to-texture framework that maintains compatibility with MSAA.
*   **Directional Shadow Mapping (WIP):** Active development and experimentation to implement real-time directional shadow mapping.
*   **Developer and Debugging Utilities:** Custom development features such as a free-roaming fly camera and environment debug overlay tools to assist in editing and fine-tuning visual effects.

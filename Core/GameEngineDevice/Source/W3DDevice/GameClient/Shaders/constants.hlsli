// Constants that more than one shader has to agree on.
//
// Nothing lives here just because it is a nice round number, or because two shaders happen
// to use the same value today. Everything here is a value where two shaders disagreeing is
// a visible bug: a unit lit differently from the ground it stands on, a road that cloud
// shade tints differently from the terrain either side of it. Those are the values worth
// paying an include for. A constant only one shader uses belongs in that shader, next to
// the code that reads it.
//
// The build already knows about this file. cmake/shaders.cmake globs *.hlsli and makes
// every shader depend on all of them, so changing a value here rebuilds everything that
// could be affected -- which is the other half of the point. Before this file, raising the
// cast-shadow floor means editing one file rather than five and getting all five right.
//
#ifndef RTS_SHADER_CONSTANTS_HLSLI
#define RTS_SHADER_CONSTANTS_HLSLI


// ---------------------------------------------------------------------------------------
// Cast shadows
// ---------------------------------------------------------------------------------------

// How much light a fully shadowed surface keeps. Every shader that receives the shadow map
// darkens toward this floor rather than to black -- terrain, road, and units all have their
// lighting baked into the colour they start from, so there is no direct term left to remove
// on its own. Sharing it is what makes a unit and its own cast shadow on the ground sit at
// the same brightness, which is the whole reason it is here rather than in five shaders.
//
// **This constant is not the fraction that reaches the screen.** Measured on china.rep
// frame 3600 against a build with the floor forced to 1.0, a fully shadowed ground pixel
// displayed at 0.209 of its own unshadowed value while this said 0.35, and at 0.410 while
// it said 0.55. Both fit displayed = SHADOW_MIN^1.49 exactly, and the same exponent came
// back off self-shadowed mesh faces, so it is downstream of every shader here -- the tone
// map, which decodes this gamma-encoded buffer, compresses and re-encodes. Anything picked
// as "how much light should a shadow keep" has to be raised by that power to survive to
// the frame.
//
// 0.51 displays as about 0.49 under the shoulder curve, which is identity below its 0.80
// knee and so leaves the shadowed range alone. It was 0.35, which displayed as 0.21 and is
// why a physically defensible number still read as a hole in the ground: shaded faces lost
// their material, and the stock diffuse maps already have occlusion painted into them, so
// the dynamic term was shading art that was shaded once already. Measured shadowed/lit on
// open ground either side of one cast shadow: 0.44 -> 0.57 on china.rep (Twilight Flame),
// 0.49 -> 0.64 on shadow_frustum2.rep (barren badlands, bright daylight). Vanilla sat at
// 0.74, so shadows here are still deeper than the game shipped with -- which is the point,
// they just are not holes any more.
static const float SHADOW_MIN = 0.51;


// ---------------------------------------------------------------------------------------
// Cloud shadow
// ---------------------------------------------------------------------------------------

// Two layers, each projected over thousands of world units and drifting at its own rate,
// so the field does not read as one texture sliding rigidly across the map. The engine
// feeds both layers' drift in *world* units; dividing by these periods rather than
// scrolling in UV means one wind speed means the same thing at either scale.
//
// The vertex shaders that compute the cloud UV (terrain_vs, road_vs) and the pixel shaders
// that compute it themselves (the unit shaders, which project straight down from the
// pixel's ground position) have to use the same pair, or a unit drifts out of step with
// the cloud crossing the ground under it.
static const float CLOUD_PERIOD_A = 1800.0;
static const float CLOUD_PERIOD_B = 2900.0;

// Colour of ground under full cloud: how the shade is *coloured*, not how deep it is.
// Depth is CloudShadowStrength's job, and the two multiply, so this constant should only
// ever have to answer "what colour is skylight".
//
// It used to answer "how dark is a cloud" as well, at 0.60/0.66/0.79. Against the 0.8
// strength that is a 0.68/0.73/0.83 multiply: a third of the luminance, but 40% of the
// red against 21% of the blue. That much hue rotation stops reading as shade and starts
// reading as the wrong colour -- measured against vanilla on a pixel-aligned frame, a
// China power plant under cloud lost 22% of its luminance and 23% of its saturation and
// came out uniformly cold grey with no sunlit face, while the ground beside it was
// brighter than vanilla's.
//
// At 0.78/0.82/0.90 full coverage multiplies by 0.824/0.856/0.920 -- 15% off the
// luminance, and R:B narrowed from 0.82 to 0.90. Some spread is kept deliberately:
// sky-lit shade genuinely is blue, and a perfectly neutral grey reads as a dirty lens.
static const float3 CLOUD_SHADE_TINT = float3(0.78, 0.82, 0.90);


// ---------------------------------------------------------------------------------------
// Map coordinates
// ---------------------------------------------------------------------------------------

// World XY -> the static noise-detail overlay's coordinate, matching the camera-space
// projection the fixed-function pass used (its matrix cancels the view, so it comes to
// the same thing). 1 / (63 * MAP_XY_FACTOR / 2), MAP_XY_FACTOR = 10  ->  1/315.
//
// Terrain and road are drawn by different shaders over the same ground, so they have to
// stretch identically or the overlay steps at the edge of every road.
static const float STRETCH_FACTOR = 1.0 / 315.0;


// ---------------------------------------------------------------------------------------
// Luminance
// ---------------------------------------------------------------------------------------

// Rec. 601 luma weights. Shared by the bloom bright pass and the debug views that have to
// show what that pass selected: a debug view answering "which pixels bloom" with different
// weights from the pass itself is not a debug view, it is a second opinion.
static const float3 LUMA = float3(0.299, 0.587, 0.114);


#endif  // RTS_SHADER_CONSTANTS_HLSLI

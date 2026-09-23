// Shared CPU/HLSL Ground Response constants. Keep this preprocessor-only so the
// gameplay resistance path and TerrainSurface shader cannot quietly diverge.
// Runtime identifiers and logical-field dimensions also live here.  They are
// consumed by C++ and HLSL, so a cached incompatible terrain shader fails closed
// instead of interpreting a stale b13 payload or a differently sized clipmap.
#define PIXL_GR_RUNTIME_MAGIC 0x47523330u
#define PIXL_GR_RUNTIME_VERSION 0x00030200u
#define PIXL_GR_SESSION_HISTORY_FORMAT_VERSION 1u
#define PIXL_GR_SURFACE_TEXTURE_SIZE 1024u
#define PIXL_GR_SURFACE_WORLD_SIZE 4096.0f
#define PIXL_GR_SURFACE_TILE_SIZE 32u
#define PIXL_GR_SNOW_FINE_VARIATION 0.10f
#define PIXL_GR_SNOW_POCKET_STRENGTH 0.22f
#define PIXL_GR_SNOW_FINE_SCALE 180.0f
#define PIXL_GR_SNOW_POCKET_SCALE 620.0f
#define PIXL_GR_SNOW_MOUND_STRENGTH 0.32f
#define PIXL_GR_SNOW_MOUND_SCALE 300.0f
#define PIXL_GR_SNOW_MOUND_DETAIL_SCALE 170.0f
#define PIXL_GR_SNOW_MAX_PRISTINE_HEIGHT 72.0f
#define PIXL_GR_SNOW_DEEP_DRIFT_SCALE 560.0f
#define PIXL_GR_SNOW_DEEP_DRIFT_DETAIL_SCALE 290.0f
#define PIXL_GR_SNOW_DEEP_DRIFT_BLUR_RADIUS 120.0f
#define PIXL_GR_SNOW_MOUND_BLUR_RADIUS 58.0f
#define PIXL_GR_SNOW_WIND_X 0.8192319f
#define PIXL_GR_SNOW_WIND_Y 0.5734623f
#define PIXL_GR_MUD_FINE_VARIATION 0.08f
#define PIXL_GR_MUD_POCKET_STRENGTH 0.16f
#define PIXL_GR_MUD_FINE_SCALE 200.0f
#define PIXL_GR_MUD_POCKET_SCALE 480.0f
#define PIXL_GR_MUD_WETNESS_LOWER_BAND 0.22f
#define PIXL_GR_MUD_WETNESS_UPPER_BAND 0.18f
#define PIXL_GR_SHORELINE_BELOW_WATER_FADE 2.0f
#define PIXL_GR_SHORELINE_ABOVE_WATER_FULL 5.0f
#define PIXL_GR_SHORELINE_ABOVE_WATER_FADE 64.0f

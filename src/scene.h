#ifndef SCENE_H
#define SCENE_H

#include <shader-works/renderer.h>
#include <shader-works/primitives.h>
#include <shader-works/maths.h>

// Master seed for all procedural generation
#define WORLD_SEED 2
#define CHUNK_SIZE 64
#define HALF_CHUNK_SIZE CHUNK_SIZE / 2
#define MAX_CHUNK_DISTANCE 2

typedef struct {
  float x, z;
  model_t ground_plane;
  model_t *trees;
  usize num_trees;
} chunk_t;

// Implementation found in proc_gen.c
extern float map_range(float value, float old_min, float old_max, float new_min, float new_max);

extern float lerp(float a, float b, float t);
extern float hash2(int x, int y, int seed);

extern float noise2D(float x, float y, int seed);
extern float terrainHeight(float x, float y, int seed);
extern float get_interpolated_terrain_height(float x, float z);

// Implementation found in scene.c
void generate_chunk(chunk_t *chunk, float x, float z);
void render_chunk(renderer_t *state, chunk_t *chunk, transform_t *camera, light_t *lights, const usize num_lights);
bool should_chunk_unload(transform_t *player, chunk_t *chunk);
void unload_chunk(chunk_t *chunk);
  
#endif // SCENE_H
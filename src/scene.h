#ifndef SCENE_H
#define SCENE_H

#include <shader-works/primitives.h>
#include <shader-works/maths.h>

// Master seed for all procedural generation
#define WORLD_SEED 420 * 69

float map_range(float value, float old_min, float old_max, float new_min, float new_max);

float lerp(float a, float b, float t);
float smoothstep(float t);
float hash2(int x, int y, int seed);

float noise2D(float x, float y, int seed);
float terrainHeight(float x, float y, int seed);

int generate_tree(model_t *model, float base_radius, float base_angle, float3 base_position, float branch_chance, usize max_branches, usize level, usize num_level);
int generate_tree_cylinder(model_t* model, float bottom_radius, float top_radius, float height, float3 bottom_center, float3 top_center, usize segments, float bottom_angle_offset, float top_angle_offset);

#endif // SCENE_H
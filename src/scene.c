#include "scene.h"

#include <math.h>
#include <stdlib.h>

#include <shader-works/renderer.h>
#include <shader-works/maths.h>

extern fragment_shader_t ground_frag;
extern fragment_shader_t tree_frag;

extern int generate_tree(model_t *, float, float, float3, float, usize, const usize, const usize, const usize); // in proc_gen.c
extern void generate_ground_plane(model_t *, float2, float2, float3);                                           // in proc_gen.c

float get_interpolated_terrain_height(float x, float z) {
  float grid_size = 1.0f; // Sample every 1 unit
  
  // Find which grid cell we're in
  float grid_x = x / grid_size;
  float grid_z = z / grid_size;
  
  // Get integer grid coordinates
  int gx = (int)floorf(grid_x);
  int gz = (int)floorf(grid_z);
  
  // Get fractional parts for interpolation
  float fx = grid_x - (float)gx;
  float fz = grid_z - (float)gz;
  
  // Sample terrain height at the four corners of the grid cell
  float corner_x0 = (float)gx * grid_size;
  float corner_x1 = (float)(gx + 1) * grid_size;
  float corner_z0 = (float)gz * grid_size;
  float corner_z1 = (float)(gz + 1) * grid_size;
  
  float h00 = terrainHeight(corner_x0, corner_z0, WORLD_SEED);
  float h10 = terrainHeight(corner_x1, corner_z0, WORLD_SEED);
  float h01 = terrainHeight(corner_x0, corner_z1, WORLD_SEED);
  float h11 = terrainHeight(corner_x1, corner_z1, WORLD_SEED);
  
  // Bilinear interpolation
  float h0 = lerp(h00, h10, fx);
  float h1 = lerp(h01, h11, fx);
  return lerp(h0, h1, fz);
}

void generate_chunk(chunk_t *chunk, float x, float z) {
  if (chunk == NULL) return;

  chunk->x = x;
  chunk->z = z;

  generate_ground_plane(&chunk->ground_plane, make_float2(CHUNK_SIZE, CHUNK_SIZE), make_float2(1.0f, 1.0f), make_float3(x, 0, z));
  chunk->ground_plane.frag_shader = &ground_frag;
  
  chunk->num_trees = map_range(hash2(x, z, WORLD_SEED), -1.0f, 1.0f, 0, 7);
  chunk->trees = calloc(chunk->num_trees, sizeof(model_t));

  for (usize i = 0; i < chunk->num_trees; ++i) {
    // Random position within terrain bounds
    float tree_x = map_range(hash2(i, i % 3, WORLD_SEED), -1.0f, 1.0f, -CHUNK_SIZE, CHUNK_SIZE);
    float tree_z = map_range(hash2(i % 3, i, WORLD_SEED), -1.0f, 1.0f, -CHUNK_SIZE, CHUNK_SIZE);
    float tree_y = terrainHeight(x, z, WORLD_SEED) - 0.5f;

    float3 tree_pos = make_float3(tree_x, tree_y, tree_z);

    // Bigger trees with more branches
    float base_radius = map_range(hash2(tree_pos.x, tree_pos.z, WORLD_SEED), -1.0f, 1.0f, 0.4f, 0.55f);
    float base_angle = map_range(hash2(tree_pos.x, tree_pos.z, WORLD_SEED), -1.0f, 1.0f, 0.0f, 2.0f * PI);
    float branch_chance = map_range(hash2(tree_pos.x, tree_pos.z, WORLD_SEED), -1.0f, 1.0f, 0.8, 0.9);
    usize max_branches = map_range(hash2(tree_pos.x, tree_pos.z, WORLD_SEED), -1.0f, 1.0f, 5.f, 10.f);
    usize num_levels = map_range(hash2(tree_pos.x, tree_pos.z, WORLD_SEED), -1.0f, 1.0f, 5.f, 7.f);

    chunk->trees[i].frag_shader = &tree_frag;

    generate_tree(&(chunk->trees[i]), base_radius, base_angle, tree_pos, branch_chance, 0, max_branches, num_levels, 6);
  }
}

void render_chunk(renderer_t *state, chunk_t *chunk, transform_t *camera, light_t *lights, const usize num_lights) {
  render_model(state, camera, &chunk->ground_plane, lights, num_lights);

  for (usize i = 0; i < chunk->num_trees; ++i) {
      // Only render tree_trunk if it has valid vertex data
    if (chunk->trees[i].vertex_data != NULL && chunk->trees[i].num_vertices > 0) {
      render_model(state, camera, &chunk->trees[i], lights, num_lights);
    }
  }
}

bool should_chunk_unload(transform_t *player, chunk_t *chunk) {
  float distance = float2_magnitude(float2_sub(make_float2(player->position.x, player->position.z), make_float2(chunk->x, chunk->z)));

  if (fabsf(distance) > CHUNK_SIZE * MAX_CHUNK_DISTANCE)
    return true;
  
  return false;
}

void unload_chunk(chunk_t *chunk) {
  if (!chunk) return;

  delete_model(&chunk->ground_plane);
  
  for (usize i = 0; i < chunk->num_trees; ++i) {
    delete_model(&chunk->trees[i]);
  }

  chunk = NULL;
}

#include "scene.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <shader-works/renderer.h>
#include <shader-works/maths.h>

extern fragment_shader_t ground_frag;
extern fragment_shader_t tree_frag;

extern int generate_tree(model_t *, float, float, float3, float, usize, const usize, const usize, const usize); // in proc_gen.c
extern void generate_ground_plane(model_t *, float2, float2, float3);                                           // in proc_gen.c

static void generate_chunk(chunk_t *chunk, int chunk_x, int chunk_z) {
  if (chunk == NULL) return;
  
  chunk->x = chunk_x;
  chunk->z = chunk_z;
  
  // Initialize ground plane model to zero state
  chunk->ground_plane = (model_t){0};
  
  float world_x = chunk_x * CHUNK_SIZE;
  float world_z = chunk_z * CHUNK_SIZE;
  
  // Position chunk to ensure vertex grid alignment at boundaries
  // Use corner-based positioning to ensure vertices align perfectly
  float corner_x = world_x;
  float corner_z = world_z;
  
  generate_ground_plane(&chunk->ground_plane, make_float2(CHUNK_SIZE, CHUNK_SIZE), make_float2(1.0f, 1.0f), make_float3(corner_x + HALF_CHUNK_SIZE, 0, corner_z + HALF_CHUNK_SIZE));
  chunk->ground_plane.frag_shader = &ground_frag;
  
  // Ensure ground plane was generated successfully
  if (chunk->ground_plane.vertex_data == NULL || chunk->ground_plane.num_vertices == 0) {
    // Re-try ground plane generation or handle error
    generate_ground_plane(&chunk->ground_plane, make_float2(CHUNK_SIZE, CHUNK_SIZE), make_float2(1.0f, 1.0f), make_float3(corner_x + HALF_CHUNK_SIZE, 0, corner_z + HALF_CHUNK_SIZE));
  }
  
  chunk->num_trees = map_range(hash2(world_x, world_z, WORLD_SEED), -1.0f, 1.0f, 0, 7);
  chunk->trees = calloc(chunk->num_trees, sizeof(model_t));
  
  for (usize i = 0; i < chunk->num_trees; ++i) {
    // Random position within terrain bounds
    float tree_x = map_range(hash2(i, i % 3, WORLD_SEED), -1.0f, 1.0f, world_x - (HALF_CHUNK_SIZE - 2), world_x + (HALF_CHUNK_SIZE - 2));
    float tree_z = map_range(hash2(i % 3, i, WORLD_SEED), -1.0f, 1.0f, world_z - (HALF_CHUNK_SIZE - 2), world_z + (HALF_CHUNK_SIZE - 2));
    float tree_y = terrainHeight(world_x, world_z, WORLD_SEED) - 0.5f;
    
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

static usize render_chunk(renderer_t *state, chunk_t *chunk, transform_t *camera, light_t *lights, const usize num_lights) {
  usize triangles_rendered = 0;
  
  // Only render ground plane if it has valid vertex data
  if (chunk->ground_plane.vertex_data != NULL && chunk->ground_plane.num_vertices > 0) {
    triangles_rendered += render_model(state, camera, &chunk->ground_plane, lights, num_lights);
  }
  
  for (usize i = 0; i < chunk->num_trees; ++i) {
    // Only render tree_trunk if it has valid vertex data
    if (chunk->trees[i].vertex_data != NULL && chunk->trees[i].num_vertices > 0) {
      triangles_rendered += render_model(state, camera, &chunk->trees[i], lights, num_lights);
    }
  }
  
  return triangles_rendered;
}

void init_scene(scene_t *scene, usize max_loaded_chunks) {
  if (!scene) return;
  
  scene->controller = (fps_controller_t){
    .move_speed = 8.0f,
    .mouse_sensitivity = 0.0015f,
    .min_pitch = -PI/3,
    .max_pitch = PI/3,
    .ground_height = 2.0f,
    .camera_height_offset = 6.0f,
    .last_frame_time = SDL_GetPerformanceCounter()
  };
  
  scene->camera_pos = (transform_t){ 0 };
  
  init_chunk_map(&scene->chunk_map, CHUNK_MAP_NUM_BUCKETS);
}

bool cull_chunk(chunk_t *chunk, void *param, usize num_params) {
  if (!chunk || !param) return true;
  
  transform_t *player = (transform_t*)param;
  
  // Calculate player's chunk coordinates (handle negative coordinates properly)
  int player_chunk_x = (int)floorf(player->position.x / CHUNK_SIZE);
  int player_chunk_z = (int)floorf(player->position.z / CHUNK_SIZE);
  
  // Check if chunk is within the 2x3 grid pattern
  int dx = chunk->x - player_chunk_x;
  int dz = chunk->z - player_chunk_z;
  
  float3 right, up, fwd;
  transform_get_inverse_basis_vectors(player, &right, &up, &fwd);
  
  // Check if chunk matches the xxx/xPx pattern
  float chunk_offsets[][2] = {
    {0, 0}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}
  };
  
  int num_chunks = sizeof(chunk_offsets) / sizeof(chunk_offsets[0]);
  
  for (int i = 0; i < num_chunks; i++) {
    float rel_x = chunk_offsets[i][0];
    float rel_z = chunk_offsets[i][1];
    int world_dx = (int)roundf(rel_x * right.x + rel_z * fwd.x);
    int world_dz = (int)roundf(rel_x * right.z + rel_z * fwd.z);
    
    if (dx == world_dx && dz == world_dz) {
      return false; // chunk is within the pattern
    }
  }
  
  return true; // cull chunk outside the 2x3 grid
}

void update_loaded_chunks(scene_t *scene) {
  remove_chunk_if(&scene->chunk_map, cull_chunk, &scene->camera_pos, 1);
  
  // Calculate player's chunk coordinates (handle negative coordinates properly)
  int player_chunk_x = (int)floorf(scene->camera_pos.position.x / CHUNK_SIZE);
  int player_chunk_z = (int)floorf(scene->camera_pos.position.z / CHUNK_SIZE);
  
  // Generate 3x3 grid around player
  static int debug_frame = 0;
  for (int dx = -1; dx <= 1; dx++) {
    for (int dz = -1; dz <= 1; dz++) {
      int chunk_x = player_chunk_x + dx;
      int chunk_z = player_chunk_z + dz;
      
      // Check if chunk is already loaded
      if (!is_chunk_loaded(&scene->chunk_map, chunk_x, chunk_z)) {
        chunk_t new_chunk = {0};
        generate_chunk(&new_chunk, chunk_x, chunk_z);
        insert_chunk(&scene->chunk_map, &new_chunk);
        if (debug_frame % 60 == 0) {
          printf("  Loading chunk (%d,%d)\n", chunk_x, chunk_z);
        }
      } else if (debug_frame % 60 == 0) {
        printf("  Chunk (%d,%d) already loaded\n", chunk_x, chunk_z);
      }
    }
    
    ++debug_frame;
  }
}

// Helper structure for sorting chunks by distance
typedef struct {
  chunk_t *chunk;
  float distance;
} chunk_distance_t;

// Comparison function for sorting chunks by distance (closest first)
static int compare_chunks_by_distance(const void *a, const void *b) {
  const chunk_distance_t *chunk_a = (const chunk_distance_t*)a;
  const chunk_distance_t *chunk_b = (const chunk_distance_t*)b;
  
  if (chunk_a->distance < chunk_b->distance) return -1;
  if (chunk_a->distance > chunk_b->distance) return 1;
  return 0;
}

usize render_loaded_chunks(renderer_t *state, scene_t *scene, light_t *lights, const usize num_lights) {
  chunk_t **chunks = calloc(MAX_CHUNKS, sizeof(chunk_t*));
  usize chunk_count = 0;
  usize total_triangles_rendered = 0;
  
  // Get all loaded chunks from the hash map
  get_all_chunks(&scene->chunk_map, chunks, &chunk_count);
  
  if (chunk_count == 0) {
    free(chunks);
    return 0;
  }
  
  // Create array for sorting chunks by distance
  chunk_distance_t *sorted_chunks = calloc(chunk_count, sizeof(chunk_distance_t));
  
  float2 camera_pos = make_float2(scene->camera_pos.position.x, scene->camera_pos.position.z);
  
  // Calculate distances and populate sorting array
  for (usize i = 0; i < chunk_count; i++) {
    if (chunks[i]) {
      float2 chunk_center = make_float2(
        chunks[i]->x * CHUNK_SIZE + HALF_CHUNK_SIZE,
        chunks[i]->z * CHUNK_SIZE + HALF_CHUNK_SIZE
      );
      
      float distance = float2_magnitude(float2_sub(camera_pos, chunk_center));
      
      sorted_chunks[i].chunk = chunks[i];
      sorted_chunks[i].distance = distance;
    }
  }
  
  // Sort chunks front-to-back for early Z-rejection
  qsort(sorted_chunks, chunk_count, sizeof(chunk_distance_t), compare_chunks_by_distance);
  
  // Render chunks in sorted order (closest first)
  for (usize i = 0; i < chunk_count; i++) {
    if (sorted_chunks[i].chunk) {
      total_triangles_rendered += render_chunk(state, sorted_chunks[i].chunk, &scene->camera_pos, lights, num_lights);
    }
  }
  
  // Clean up allocated buffers
  free(chunks);
  free(sorted_chunks);
  
  return total_triangles_rendered;
}
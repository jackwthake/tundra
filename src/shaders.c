#include <shader-works/shaders.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include "scene.h"
#include "chunk_map.h"

u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
  return SDL_MapRGBA(format, NULL, r, g, b, 255);
}

void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
  SDL_GetRGB(color, format, NULL, r, g, b);
}

u32 tree_frag_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)args; (void)argc;
  
  // white/black noise pattern based on world position
  float check_size = 0.02f;
  float x = floorf(ctx->world_pos.x / check_size);
  float z = floorf(ctx->world_pos.y / check_size);

  float intensity = map_range(noise2D(x, z, WORLD_SEED), -1.0f, 1.0f, 0.55f, 1.0f);

  u8 r = (u8)(110.f * intensity);
  u8 g = (u8)(90.f * intensity);
  u8 b = (u8)(40.f * intensity);
  return default_lighting_frag_shader.func(rgb_to_u32(r, g, b), ctx, args, argc);
}

// Check if a point is in shadow from any tree
static bool point_in_tree_shadow(float3 world_pos, scene_t *scene) {
  if (!scene) return false;

  // Check current and neighboring chunks for tree shadows
  for (int dx = -1; dx <= 1; dx++) {
    for (int dz = -1; dz <= 1; dz++) {
      int chunk_x = (int)floorf(world_pos.x / CHUNK_SIZE) + dx;
      int chunk_z = (int)floorf(world_pos.z / CHUNK_SIZE) + dz;

      chunk_map_node_t *node = chunk_lookup((chunk_map_t*)&scene->chunk_map, chunk_x, chunk_z);
      if (!node || !node->loaded) continue;

      chunk_t *chunk = &node->chunk;

      // Check all trees in this chunk
      for (usize i = 0; i < chunk->num_trees; i++) {
        if (chunk->trees[i].num_vertices == 0) continue;

        // Recalculate tree position using same method as generation
        float world_x = chunk_x * CHUNK_SIZE;
        float world_z = chunk_z * CHUNK_SIZE;
        float tree_x = map_range(hash2(i, i % 3, WORLD_SEED), -1.0f, 1.0f, world_x - (HALF_CHUNK_SIZE - 2), world_x + (HALF_CHUNK_SIZE - 2));
        float tree_z = map_range(hash2(i % 3, i, WORLD_SEED), -1.0f, 1.0f, world_z - (HALF_CHUNK_SIZE - 2), world_z + (HALF_CHUNK_SIZE - 2));

        // Simple circular shadow check
        float tree_dx = world_pos.x - tree_x;
        float tree_dz = world_pos.z - tree_z;
        float dist_sq = tree_dx * tree_dx + tree_dz * tree_dz;

        if (dist_sq < 3.24f) { // 1.8 * 1.8 = 3.24
          return true;
        }
      }
    }
  }

  return false;
}

// Shadow-enabled ground shader
u32 ground_shadow_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input;

  // Generate normal ground color
  float check_size = 0.05f;
  float x = floorf(ctx->world_pos.x / check_size);
  float z = floorf(ctx->world_pos.z / check_size);

  float intensity = map_range(noise2D(x, z, WORLD_SEED), -1.0f, 1.0f, 0.85f, 1.0f);

  u8 r = (u8)(255.f * intensity);
  u8 g = (u8)(255.f * intensity);
  u8 b = (u8)(255.f * intensity);

  u32 base_color = rgb_to_u32(r, g, b);
  u32 lit_color = default_lighting_frag_shader.func(base_color, ctx, NULL, 0);

  // Apply tree shadows if scene data is available
  if (args && argc > 0) {
    scene_t *scene = (scene_t*)args;

    if (point_in_tree_shadow(ctx->world_pos, scene)) {
      // Darken the pixel by 50%
      u8 shadow_r, shadow_g, shadow_b;
      u32_to_rgb(lit_color, &shadow_r, &shadow_g, &shadow_b);
      shadow_r = (u8)(shadow_r * 0.5f);
      shadow_g = (u8)(shadow_g * 0.5f);
      shadow_b = (u8)(shadow_b * 0.5f);
      return rgb_to_u32(shadow_r, shadow_g, shadow_b);
    }
  }

  return lit_color;
}

// Global scene pointer for shadow calculations
static scene_t *g_scene_for_shadows = NULL;

fragment_shader_t ground_shadow_frag = { .func = ground_shadow_func, .argv = NULL, .argc = 0, .valid = true };
fragment_shader_t tree_frag = { .func = tree_frag_func, .argv = NULL, .argc = 0, .valid = true};

// Function to set the scene data for shadow calculations
void set_shadow_scene(scene_t *scene) {
  g_scene_for_shadows = scene;
  // Update the shader arguments to point to the scene
  ground_shadow_frag.argv = g_scene_for_shadows;
  ground_shadow_frag.argc = sizeof(scene_t);
}

// Snow particle 3D world-space effect
void apply_snow_effect(renderer_t *renderer, float time, int width, int height, transform_t *camera) {
  if (!renderer || !renderer->framebuffer || !camera) return;

  const int num_particles = 500; // Number of snow particles
  const float particle_size = 2.0f; // Size of each snow particle in pixels
  const float snow_area = renderer->max_depth * 0.8f; // Area around camera to generate snow

  // Get camera position
  float3 cam_pos = camera->position;

  for (int i = 0; i < num_particles; i++) {
    // Use hash functions for deterministic particle positions relative to camera
    float hash_x = hash2(i, 12345, WORLD_SEED);
    float hash_z = hash2(i * 7, 54321, WORLD_SEED);
    float hash_y_offset = hash2(i * 13, 98765, WORLD_SEED);
    float hash_speed = hash2(i * 19, 11111, WORLD_SEED);

    // Generate world position around camera
    float world_x = cam_pos.x + map_range(hash_x, -1.0f, 1.0f, -snow_area, snow_area);
    float world_z = cam_pos.z + map_range(hash_z, -1.0f, 1.0f, -snow_area, snow_area);

    // Falling animation with different speeds (downward)
    float fall_speed = map_range(hash_speed, -1.0f, 1.0f, 5.0f, 12.0f);
    float y_offset = map_range(hash_y_offset, -1.0f, 1.0f, 0.0f, 40.0f);

    // Particles fall from above camera to below camera, then wrap
    float total_fall_height = 80.0f; // Total height range
    float fall_progress = fmodf(time * fall_speed + y_offset, total_fall_height);
    float world_y = cam_pos.y - 40.0f + fall_progress;

    // Skip if particle is too far below camera (shouldn't happen with wrap-around)
    if (world_y < cam_pos.y - 60.0f) continue;

    // Create 3D position
    float3 world_pos = make_float3(world_x, world_y, world_z);

    // Project to screen space using renderer's camera matrices
    float3 relative_pos = float3_sub(world_pos, cam_pos);

    // Transform relative to camera space
    float3 cam_right = renderer->cam_right;
    float3 cam_up = renderer->cam_up;
    float3 cam_forward = renderer->cam_forward;

    float cam_x = float3_dot(relative_pos, cam_right);
    float cam_y = float3_dot(relative_pos, cam_up);
    float cam_z = float3_dot(relative_pos, cam_forward);

    // Skip if behind camera
    if (cam_z <= 0.1f) continue;

    // Project to screen coordinates
    float screen_x = (cam_x * renderer->projection_scale / cam_z) + (float)width * 0.5f;
    float screen_y = (cam_y * renderer->projection_scale / cam_z) + (float)height * 0.5f;

    // Skip if outside screen bounds
    if (screen_x < 0 || screen_x >= width || screen_y < 0 || screen_y >= height) continue;

    // Calculate depth for depth testing
    float depth = cam_z;

    // Calculate particle position
    int px = (int)screen_x;
    int py = (int)screen_y;

    // Draw particle as a small square
    for (int dy = 0; dy < (int)particle_size; dy++) {
      for (int dx = 0; dx < (int)particle_size; dx++) {
        int x = px + dx;
        int y = py + dy;

        // Bounds check
        if (x >= 0 && x < width && y >= 0 && y < height) {
          int index = y * width + x;

          // Only draw if particle is in front of existing geometry
          if (renderer->depthbuffer[index] > depth) {
            // White snow particle
            renderer->framebuffer[index] = rgb_to_u32(255, 255, 255);
            renderer->depthbuffer[index] = depth;
          }
        }
      }
    }
  }
}
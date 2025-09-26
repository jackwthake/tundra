#include <shader-works/shaders.h>

#include <math.h>
#include "scene.h"

u32 ground_shader_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)args; (void)argc;
  
  // white/black noise pattern based on world position
  float check_size = 0.05f;
  float x = floorf(ctx->world_pos.x / check_size);
  float z = floorf(ctx->world_pos.z / check_size);
  
  float intensity = map_range(noise2D(x, z, WORLD_SEED), -1.0f, 1.0f, 0.85f, 1.0f);
  
  u8 r = (u8)(255.f * intensity);
  u8 g = (u8)(255.f * intensity);
  u8 b = (u8)(255.f * intensity);
  return default_lighting_frag_shader.func(rgb_to_u32(r, g, b), ctx, args, argc);
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

fragment_shader_t ground_frag = { .func = ground_shader_func, .argv = NULL, .argc = 0, .valid = true };
fragment_shader_t tree_frag = { .func = tree_frag_func, .argv = NULL, .argc = 0, .valid = true};
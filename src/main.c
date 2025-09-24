#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <shader-works/renderer.h>
#include <shader-works/primitives.h>
#include <shader-works/shaders.h>
#include <shader-works/maths.h>

#include <SDL3/SDL.h>

#include "scene.h"

#define WIN_WIDTH 400
#define WIN_HEIGHT 250
#define WIN_SCALE 4
#define WIN_TITLE "Tundra"
#define MAX_DEPTH 15
#define NUM_TREES 5

typedef struct {
  float move_speed;
  float mouse_sensitivity;
  float min_pitch, max_pitch;
  float ground_height;
  float camera_height_offset;
  float delta_time;
  uint64_t last_frame_time;
} fps_controller_t;

u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
  return SDL_MapRGBA(format, NULL, r, g, b, 255);
}

void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
  SDL_GetRGB(color, format, NULL, r, g, b);
}

u32 brown_shader_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)args; (void)argc;
  
  // white/black noise pattern based on world position
  float check_size = 0.5f;
  float x = floorf(ctx->world_pos.x / check_size);
  float z = floorf(ctx->world_pos.y / check_size);

  float intensity = map_range(noise2D(x, z, WORLD_SEED), -1.0f, 1.0f, 0.85f, 1.0f);

  u8 r = (u8)(130.f * intensity);
  u8 g = (u8)(70.f * intensity);
  u8 b = (u8)(20.f * intensity);
  return default_lighting_frag_shader.func(rgb_to_u32(r, g, b), ctx, args, argc);
}

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

void update_timing(fps_controller_t *controller) {
  uint64_t current_time = SDL_GetPerformanceCounter();
  controller->delta_time = (float)(current_time - controller->last_frame_time) / (float)SDL_GetPerformanceFrequency();
  controller->last_frame_time = current_time;
  if (controller->delta_time > 0.1f) controller->delta_time = 0.1f;
}

// Something strange goes on here
float get_interpolated_terrain_height(const model_t *ground, float x, float z) {
  // Get terrain bounds and segment size from the ground model
  float2 size = {64, 64}; // Match the generate_ground call
  float2 segment_size = {4, 4}; // Match the generate_ground call

  // Calculate grid dimensions
  int grid_width = (int)(size.x / segment_size.x) + 1;
  int grid_height = (int)(size.y / segment_size.y) + 1;

  // Convert world position to grid coordinates
  float grid_x = (x + size.x/2) / segment_size.x;
  float grid_z = (z + size.y/2) / segment_size.y;

  // Get integer grid coordinates
  int gx = (int)floorf(grid_x);
  int gz = (int)floorf(grid_z);

  // Clamp to valid range
  if (gx < 0 || gx >= grid_width-1 || gz < 0 || gz >= grid_height-1) {
    return terrainHeight(x, z, WORLD_SEED); // Fallback for out of bounds
  }

  // Get fractional parts for interpolation
  float fx = grid_x - (float)gx;
  float fz = grid_z - (float)gz;

  // Get heights at the four corners
  float h00 = ground->vertex_data[gz * grid_width + gx].position.y;
  float h10 = ground->vertex_data[gz * grid_width + (gx + 1)].position.y;
  float h01 = ground->vertex_data[(gz + 1) * grid_width + gx].position.y;
  float h11 = ground->vertex_data[(gz + 1) * grid_width + (gx + 1)].position.y;

  // Bilinear interpolation
  float h0 = lerp(h00, h10, fx);
  float h1 = lerp(h01, h11, fx);
  return lerp(h0, h1, fz);
}

void generate_ground(model_t *model, float2 size, float2 segment_size, float3 position) {
  generate_plane(model, size, segment_size, position);
  model->transform = (transform_t){0};

  for (usize i = 0; i < model->num_vertices; ++i) {
    float3 *v = &model->vertex_data[i].position;
    v->y = terrainHeight(v->x, v->z, WORLD_SEED);
  }

  // Recalculate face normals after terrain height modification
  for (usize i = 0; i < model->num_faces; ++i) {
    usize base_idx = i * 3;
    float3 v0 = model->vertex_data[base_idx].position;
    float3 v1 = model->vertex_data[base_idx + 1].position;
    float3 v2 = model->vertex_data[base_idx + 2].position;

    float3 edge1 = float3_sub(v1, v0);
    float3 edge2 = float3_sub(v2, v0);

    model->face_normals[i] = float3_normalize(float3_cross(edge2, edge1));
  }

  // Recalculate vertex normals after terrain height modification
  for (usize i = 0; i < model->num_vertices; ++i) {
    model->vertex_data[i].normal = model->face_normals[i / 3];
  }
}

void handle_input(fps_controller_t *controller, transform_t *camera, bool *mouse_captured, const model_t *ground) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  
  // Mouse input
  if (*mouse_captured) {
    float mx, my;
    SDL_GetRelativeMouseState(&mx, &my);
    camera->yaw += mx * controller->mouse_sensitivity;
    camera->pitch -= my * controller->mouse_sensitivity;
    if (camera->pitch < controller->min_pitch) camera->pitch = controller->min_pitch;
    if (camera->pitch > controller->max_pitch) camera->pitch = controller->max_pitch;
  }
  
  // Keyboard movement
  float cy = cosf(camera->yaw), sy = sinf(camera->yaw);
  float cp = cosf(camera->pitch), sp = sinf(camera->pitch);
  float3 right = make_float3(cy, 0, -sy);
  float3 forward = make_float3(-sy * cp, sp, -cy * cp);
  float3 movement = make_float3(0, 0, 0);
  float speed = controller->move_speed * controller->delta_time;
  
  if (keys[SDL_SCANCODE_W]) movement = float3_add(movement, float3_scale(forward, speed));
  if (keys[SDL_SCANCODE_S]) movement = float3_add(movement, float3_scale(forward, -speed));
  if (keys[SDL_SCANCODE_A]) movement = float3_add(movement, float3_scale(right, speed));
  if (keys[SDL_SCANCODE_D]) movement = float3_add(movement, float3_scale(right, -speed));
  
  camera->position = float3_add(camera->position, movement);
  float new_ground_height = get_interpolated_terrain_height(ground, camera->position.x, camera->position.z);

  // Safety check: if terrain height calculation fails or gives extreme values, use previous height
  if (!isfinite(new_ground_height) || fabsf(new_ground_height - controller->ground_height) > 20.0f) {
    new_ground_height = controller->ground_height; // Keep previous height
  }

  controller->ground_height = new_ground_height;

  // Use the camera height offset
  float target_y = controller->ground_height + controller->camera_height_offset;

  // Prevent sudden drops - limit how much the camera can drop per frame
  if (target_y < camera->position.y - 10.0f) {
    target_y = camera->position.y - 10.0f; // Max drop of 10 units per frame
  }

  camera->position.y = target_y;
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  SDL_Init(SDL_INIT_VIDEO);
  srand(WORLD_SEED);
  
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_CreateWindowAndRenderer(WIN_TITLE, WIN_WIDTH * WIN_SCALE, WIN_HEIGHT * WIN_SCALE, 0, &window, &renderer);
  
  SDL_Texture *framebuffer_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WIN_WIDTH, WIN_HEIGHT);
  SDL_SetTextureScaleMode(framebuffer_tex, SDL_SCALEMODE_NEAREST);
  
  u32 framebuffer[WIN_WIDTH * WIN_HEIGHT];
  f32 depthbuffer[WIN_WIDTH * WIN_HEIGHT];
  
  fps_controller_t controller = {
    .move_speed = 8.0f,
    .mouse_sensitivity = 0.0015f,
    .min_pitch = -PI/3,
    .max_pitch = PI/3,
    .ground_height = 2.0f,
    .camera_height_offset = 6.0f,
    .last_frame_time = SDL_GetPerformanceCounter()
  };
  
  transform_t camera = { .position = make_float3(0, 5, 0) };
  bool mouse_captured = false;
  bool running = true;
  
  SDL_SetWindowRelativeMouseMode(window, false);
  
  renderer_t renderer_state = {0};
  init_renderer(&renderer_state, WIN_WIDTH, WIN_HEIGHT, 0, 0, framebuffer, depthbuffer, MAX_DEPTH);
  
  fragment_shader_t brown_shader = make_fragment_shader(brown_shader_func, NULL, 0);
  fragment_shader_t soft_green_shader = make_fragment_shader(ground_shader_func, NULL, 0);
  
  model_t ground = {0}, tree_trunk = {0};
  generate_ground(&ground, make_float2(64, 64), make_float2(4, 4), make_float3(0, 0, 0));
  
  ground.frag_shader = &soft_green_shader;
  ground.vertex_shader = tree_trunk.vertex_shader = &default_vertex_shader;
  ground.use_textures = tree_trunk.use_textures = false;

  model_t trees[NUM_TREES] = { 0 };

  // Initialize trees with random parameters
  for (int i = 0; i < NUM_TREES; i++) {
    // Random position within terrain bounds
    float x = ((float)rand() / RAND_MAX) * 60.0f - 30.0f; // -30 to 30
    float z = ((float)rand() / RAND_MAX) * 60.0f - 30.0f; // -30 to 30
    float y = get_interpolated_terrain_height(&ground, x, z);

    float3 tree_pos = make_float3(x, y, z);

    // Bigger trees with more branches
    float base_radius = 0.25f + ((float)rand() / RAND_MAX) * 0.4f;  // 0.25 to 0.65 (bigger)
    float base_angle = ((float)rand() / RAND_MAX) * 2.0f * PI;      // 0 to 2π
    float branch_chance = 0.7f + ((float)rand() / RAND_MAX) * 0.2f; // 0.7 to 0.9 (more branches)
    usize max_branches = 5 + (rand() % 4);                          // 5 to 8 (more branches)
    usize num_levels = 5 + (rand() % 3);                           // 5 to 7 (more levels)

    // Initialize the model properly
    trees[i].vertex_data = NULL;
    trees[i].face_normals = NULL;
    trees[i].num_vertices = 0;
    trees[i].num_faces = 0;
    trees[i].transform = (transform_t){0};

    int result = generate_tree(&trees[i], base_radius, base_angle, tree_pos, branch_chance, max_branches, 0, num_levels);
    if (result < 0) {
      printf("Failed to generate tree %d at position (%.2f, %.2f, %.2f)\n", i, x, y, z);
    }

    trees[i].transform.position.y -= 0.5f;

    trees[i].frag_shader = &brown_shader;
    trees[i].vertex_shader = &default_vertex_shader;
    trees[i].use_textures = false;
  }

  float terrain_height = get_interpolated_terrain_height(&ground, camera.position.x, camera.position.z);
  controller.ground_height = terrain_height;
  float terrain_clearance = fmaxf(3.0f, fabsf(terrain_height) * 0.1f + 2.0f);
  camera.position.y = terrain_height + terrain_clearance;
  
  light_t sun = {
    .is_directional = true,
    .direction = make_float3(-1, -1, -1),
    .color = rgb_to_u32(255, 225, 255)
  };
  
  while (running) {
    update_timing(&controller);
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        mouse_captured = !mouse_captured;
        SDL_SetWindowRelativeMouseMode(window, mouse_captured);
      }
    }
    
    handle_input(&controller, &camera, &mouse_captured, &ground);
    update_camera(&renderer_state, &camera);
    
    for(int i = 0; i < WIN_WIDTH * WIN_HEIGHT; ++i) {
      framebuffer[i] = rgb_to_u32(100, 120, 255);
      depthbuffer[i] = FLT_MAX;
    }
    
    render_model(&renderer_state, &camera, &ground, &sun, 1);

    // Only render tree_trunk if it has valid vertex data
    if (tree_trunk.vertex_data != NULL && tree_trunk.num_vertices > 0) {
      render_model(&renderer_state, &camera, &tree_trunk, &sun, 1);
    }

    // Render all trees (only if they have valid vertex data)
    for (int i = 0; i < NUM_TREES; i++) {
      if (trees[i].vertex_data != NULL && trees[i].num_vertices > 0) {
        render_model(&renderer_state, &camera, &trees[i], &sun, 1);
      }
    }

    apply_fog_to_screen(&renderer_state, 15.f, 30.f, 100, 120, 255);

    SDL_UpdateTexture(framebuffer_tex, NULL, framebuffer, WIN_WIDTH * sizeof(u32));
    SDL_RenderTexture(renderer, framebuffer_tex, NULL, NULL);
    SDL_RenderPresent(renderer);
  }
  
  delete_model(&ground);
  delete_model(&tree_trunk);

  // Clean up all trees
  for (int i = 0; i < NUM_TREES; i++) {
    delete_model(&trees[i]);
  }
  SDL_DestroyTexture(framebuffer_tex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

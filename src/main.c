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
#define MAX_DEPTH 90
#define NUM_TREES 15

void update_timing(fps_controller_t *controller) {
  uint64_t current_time = SDL_GetPerformanceCounter();
  
  controller->delta_time = (float)(current_time - controller->last_frame_time) / (float)SDL_GetPerformanceFrequency();
  controller->last_frame_time = current_time;
  
  if (controller->delta_time > 0.1f) controller->delta_time = 0.1f;
}

void handle_input(fps_controller_t *controller, transform_t *camera, bool *mouse_captured, bool overhead) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  
  // Mouse input
  if (*mouse_captured && !overhead) {
    float mx, my;
    SDL_GetRelativeMouseState(&mx, &my);
    
    camera->yaw += mx * controller->mouse_sensitivity;
    camera->pitch -= my * controller->mouse_sensitivity;
    
    if (camera->pitch < controller->min_pitch) camera->pitch = controller->min_pitch;
    if (camera->pitch > controller->max_pitch) camera->pitch = controller->max_pitch;
  }

  // Keyboard movement
  float3 right, up, forward;
  transform_get_basis_vectors(camera, &right, &up, &forward);

  float3 movement = make_float3(0, 0, 0);
  float speed = controller->move_speed * controller->delta_time;
  
  if (keys[SDL_SCANCODE_W] && !overhead) movement = float3_add(movement, float3_scale(forward, -speed));
  if (keys[SDL_SCANCODE_S] && !overhead) movement = float3_add(movement, float3_scale(forward, speed));
  if (keys[SDL_SCANCODE_W] && overhead) movement = float3_add(movement, float3_scale(up, -speed));
  if (keys[SDL_SCANCODE_S] && overhead) movement = float3_add(movement, float3_scale(up, speed));
  if (keys[SDL_SCANCODE_A]) movement = float3_add(movement, float3_scale(right, speed));
  if (keys[SDL_SCANCODE_D]) movement = float3_add(movement, float3_scale(right, -speed));
  
  camera->position = float3_add(camera->position, movement);
  float new_ground_height = get_interpolated_terrain_height(camera->position.x, camera->position.z);
  
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

  if (!overhead) {
    camera->position.y = target_y;
  }
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
  
  bool mouse_captured = false;
  bool running = true;
  
  // Fixed timestep variables
  const float TICK_RATE = 20.0f; // 20 TPS
  const float TICK_INTERVAL = 1.0f / TICK_RATE;
  float accumulator = 0.0f;
  uint64_t last_time = SDL_GetPerformanceCounter();
  
  // Performance counters
  uint64_t fps_counter = 0;
  uint64_t tps_counter = 0;
  uint64_t triangle_counter = 0;
  uint64_t last_counter_time = SDL_GetPerformanceCounter();
  
  SDL_SetWindowRelativeMouseMode(window, false);
  
  renderer_t renderer_state = {0};
  init_renderer(&renderer_state, WIN_WIDTH, WIN_HEIGHT, 0, 0, framebuffer, depthbuffer, MAX_DEPTH);
  
  scene_t scene = {0};
  init_scene(&scene, MAX_CHUNKS);
  
  float terrain_height = get_interpolated_terrain_height(scene.camera_pos.position.x, scene.camera_pos.position.z);
  scene.controller.ground_height = terrain_height;
  float terrain_clearance = fmaxf(3.0f, fabsf(terrain_height) * 0.1f + 2.0f);
  scene.camera_pos.position.y = terrain_height + terrain_clearance;
  
  light_t sun = {
    .is_directional = true,
    .direction = make_float3(1, -1, 1),
    .color = rgb_to_u32(255, 225, 255)
  };
  
  bool overhead_mode = false;
  while (running) {
    uint64_t current_time = SDL_GetPerformanceCounter();
    float frame_time = (float)(current_time - last_time) / (float)SDL_GetPerformanceFrequency();
    last_time = current_time;
    
    // Cap frame time to prevent spiral of death
    if (frame_time > 0.1f) frame_time = 0.1f;
    
    accumulator += frame_time;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
          mouse_captured = !mouse_captured;
          SDL_SetWindowRelativeMouseMode(window, mouse_captured);
        }
        
        if (event.key.key == SDLK_1) {
          overhead_mode = !overhead_mode;
        }
        
        if (event.key.key == SDLK_2) {
          renderer_state.wireframe_mode = !renderer_state.wireframe_mode;
        }
      }
    }
    
    // Fixed timestep game updates
    while (accumulator >= TICK_INTERVAL) {
      scene.controller.delta_time = TICK_INTERVAL;
      handle_input(&scene.controller, &scene.camera_pos, &mouse_captured, overhead_mode);
      update_loaded_chunks(&scene);
      
      accumulator -= TICK_INTERVAL;
      tps_counter++;
    }
    
    if (overhead_mode) {
      scene.camera_pos.position.y = 200;
      scene.camera_pos.pitch = -PI / 2;
      renderer_state.max_depth = 250;
    }
    
    update_camera(&renderer_state, &scene.camera_pos);
    
    for(int i = 0; i < WIN_WIDTH * WIN_HEIGHT; ++i) {
      framebuffer[i] = rgb_to_u32(100, 120, 255);
      depthbuffer[i] = FLT_MAX;
    }
    
    // Render at unlimited FPS
    usize triangles_rendered = render_loaded_chunks(&renderer_state, &scene, &sun, 1);
    
    if (!overhead_mode)
      apply_fog_to_screen(&renderer_state, 20.f, 30.f, 100, 120, 255);
    else {
      model_t cube = { 0 };
      float3 pos = make_float3(scene.camera_pos.position.x, scene.controller.ground_height + 3, scene.camera_pos.position.z);
      generate_cube(&cube, pos, (float3){ 2, 1, 2 });

      render_model(&renderer_state, &scene.camera_pos, &cube, &sun, 1);
    }
    
    SDL_UpdateTexture(framebuffer_tex, NULL, framebuffer, WIN_WIDTH * sizeof(u32));
    SDL_RenderTexture(renderer, framebuffer_tex, NULL, NULL);
    SDL_RenderPresent(renderer);
    
    fps_counter++;
    triangle_counter += triangles_rendered;
    
    // Print TPS and FPS every second
    uint64_t counter_time = SDL_GetPerformanceCounter();
    if ((float)(counter_time - last_counter_time) / (float)SDL_GetPerformanceFrequency() >= 1.0f) {
      // Calculate chunk count
      chunk_t **chunks = calloc(MAX_CHUNKS, sizeof(chunk_t*));
      usize chunk_count = 0;
      get_all_chunks(&scene.chunk_map, chunks, &chunk_count);
      free(chunks);
      
      uint64_t avg_triangles_per_frame = fps_counter > 0 ? triangle_counter / fps_counter : 0;
      printf("TPS: %lu, FPS: %lu, Triangles/frame: %lu, Chunks: %zu\n",
        tps_counter, fps_counter, avg_triangles_per_frame, chunk_count);
        tps_counter = 0;
        fps_counter = 0;
        triangle_counter = 0;
        last_counter_time = counter_time;
      }
    }
    
    free_chunk_map(&scene.chunk_map);
    
    SDL_DestroyTexture(framebuffer_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
  }
  
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

void update_timing(fps_controller_t *controller) {
  uint64_t current_time = SDL_GetPerformanceCounter();
  controller->delta_time = (float)(current_time - controller->last_frame_time) / (float)SDL_GetPerformanceFrequency();
  controller->last_frame_time = current_time;
  if (controller->delta_time > 0.1f) controller->delta_time = 0.1f;
}

void handle_input(fps_controller_t *controller, transform_t *camera, bool *mouse_captured) {
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
    
  chunk_t chunk = { 0 };
  generate_chunk(&chunk, 0.f, 0.f);
    
  float terrain_height = get_interpolated_terrain_height(camera.position.x, camera.position.z);
  controller.ground_height = terrain_height;
  float terrain_clearance = fmaxf(3.0f, fabsf(terrain_height) * 0.1f + 2.0f);
  camera.position.y = terrain_height + terrain_clearance;
  
  light_t sun = {
    .is_directional = true,
    .direction = make_float3(1, -1, 1),
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
    
    handle_input(&controller, &camera, &mouse_captured);
    update_camera(&renderer_state, &camera);
    
    for(int i = 0; i < WIN_WIDTH * WIN_HEIGHT; ++i) {
      framebuffer[i] = rgb_to_u32(100, 120, 255);
      depthbuffer[i] = FLT_MAX;
    }
    
    if (chunk.ground_plane.vertex_data != NULL) {
      render_chunk(&renderer_state, &chunk, &camera, &sun, 1);
      
      if(should_chunk_unload(&camera, &chunk)) {
        unload_chunk(&chunk);
      }
    }


    // apply_fog_to_screen(&renderer_state, 15.f, 30.f, 100, 120, 255);
    
    SDL_UpdateTexture(framebuffer_tex, NULL, framebuffer, WIN_WIDTH * sizeof(u32));
    SDL_RenderTexture(renderer, framebuffer_tex, NULL, NULL);
    SDL_RenderPresent(renderer);
  }
  
  unload_chunk(&chunk);
  
  SDL_DestroyTexture(framebuffer_tex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

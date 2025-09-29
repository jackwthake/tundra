#include <shader-works/renderer.h>

#include <assert.h>
#include <float.h> // FLT_MAX
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "scene.h"

#define WIN_WIDTH 200
#define WIN_HEIGHT 125
#define WIN_SCALE 8
#define WIN_TITLE "Tundra"
#define MAX_DEPTH 40

typedef enum {
  WIREFRAME,
  OVERHEAD,
  NORMAL
} render_mode;

struct performance_counter {
  uint64_t fps_counter;
  uint64_t tps_counter;
  uint64_t triangle_counter;
  uint64_t last_counter_time;
};

struct state_t {
  u32 framebuffer[WIN_WIDTH * WIN_HEIGHT];
  f32 depth_buffer[WIN_WIDTH * WIN_HEIGHT];
  render_mode mode;

  transform_t camera;
  fps_controller_t controller;

  renderer_t renderer;

  SDL_Window *sdl_window;
  SDL_Renderer *sdl_renderer;
  SDL_Texture *sdl_framebuffer_tex;

  struct performance_counter stats;

  bool mouse_captured, running;
};

static const float TICK_RATE = 20.0f; // 20 TPS
static const float TICK_INTERVAL = 1.0f / TICK_RATE;

static void SDL_library_init(SDL_Window **window, SDL_Renderer **renderer, SDL_Texture **frame_buf) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_CreateWindowAndRenderer(WIN_TITLE, WIN_WIDTH * WIN_SCALE, WIN_HEIGHT * WIN_SCALE, 0, window, renderer);

  *frame_buf = SDL_CreateTexture(*renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WIN_WIDTH, WIN_HEIGHT);
  SDL_SetTextureScaleMode(*frame_buf, SDL_SCALEMODE_NEAREST);

  SDL_SetWindowRelativeMouseMode(*window, false);
}

static void init_performance_counter(struct performance_counter *stats) {
  stats->fps_counter = 0;
  stats->tps_counter = 0;
  stats->triangle_counter = 0;
  stats->last_counter_time = SDL_GetPerformanceCounter();
}

static u32 get_sun_color(float time_elapsed) {
  const float CYCLE_DURATION = 120.0f; // 2 minutes total
  const float PHASE_DURATION = 30.0f;  // 30 seconds per phase

  float cycle_time = fmodf(time_elapsed, CYCLE_DURATION);
  float phase = cycle_time / PHASE_DURATION;

  u8 r, g, b;

  if (phase < 1.0f) { // Dawn: deep blue to white
    float t = phase;
    r = (u8)(30 + t * 170);   // 30 -> 200
    g = (u8)(50 + t * 110);   // 50 -> 160
    b = (u8)(120 + t * 40);   // 120 -> 160
  } else if (phase < 2.0f) { // Noon: white to light pink
    float t = phase - 1.0f;
    r = (u8)(200 + t * 55);   // 200 -> 255
    g = (u8)(160 - t * 60);   // 160 -> 100
    b = (u8)(160 - t * 10);   // 160 -> 150
  } else if (phase < 3.0f) { // Dusk: light pink to dark blue
    float t = phase - 2.0f;
    r = (u8)(255 - t * 235);  // 255 -> 20
    g = (u8)(100 - t * 80);   // 100 -> 20
    b = (u8)(150 - t * 50);   // 150 -> 100
  } else { // Midnight: dark blue to deep blue
    float t = phase - 3.0f;
    r = (u8)(20 + t * 10);    // 20 -> 30
    g = (u8)(20 + t * 30);    // 20 -> 50
    b = (u8)(100 + t * 20);   // 100 -> 120
  }

  return rgb_to_u32(r, g, b);
}

static void get_fog_color(float time_elapsed, u8 *r, u8 *g, u8 *b) {
  const float CYCLE_DURATION = 120.0f; // 2 minutes total
  const float PHASE_DURATION = 30.0f;  // 30 seconds per phase

  float cycle_time = fmodf(time_elapsed, CYCLE_DURATION);
  float phase = cycle_time / PHASE_DURATION;

  if (phase < 1.0f) { // Dawn: rich deep blue to almost white with sky blue
    float t = phase;
    *r = (u8)(20 + t * 220);   // 20 -> 240
    *g = (u8)(30 + t * 215);   // 30 -> 245
    *b = (u8)(80 + t * 170);   // 80 -> 250
  } else if (phase < 2.0f) { // Noon: almost white to pastel orange/red
    float t = phase - 1.0f;
    *r = (u8)(240 + t * 15);   // 240 -> 255
    *g = (u8)(245 - t * 95);   // 245 -> 150
    *b = (u8)(250 - t * 170);  // 250 -> 80
  } else if (phase < 3.0f) { // Dusk: pastel orange/red to black
    float t = phase - 2.0f;
    *r = (u8)(255 - t * 255);  // 255 -> 0
    *g = (u8)(150 - t * 150);  // 150 -> 0
    *b = (u8)(80 - t * 80);    // 80 -> 0
  } else { // Midnight: black to rich deep blue
    float t = phase - 3.0f;
    *r = (u8)(0 + t * 20);     // 0 -> 20
    *g = (u8)(0 + t * 30);     // 0 -> 30
    *b = (u8)(0 + t * 80);     // 0 -> 80
  }
}

void update_state(struct state_t *state) {
  assert(state != NULL);

  const bool *keys = SDL_GetKeyboardState(NULL);

  float3 movement = make_float3(0, 0, 0);
  float speed = state->controller.move_speed * state->controller.delta_time;

  float3 right, up, forward;
  transform_get_basis_vectors(&state->camera, &right, &up, &forward);

  float target_y = state->controller.ground_height + state->controller.camera_height_offset;

  if (state->mode == NORMAL || state->mode == WIREFRAME) {
    if (keys[SDL_SCANCODE_W]) movement = float3_add(movement, float3_scale(forward, -speed));
    if (keys[SDL_SCANCODE_S]) movement = float3_add(movement, float3_scale(forward, speed));
    if (keys[SDL_SCANCODE_A]) movement = float3_add(movement, float3_scale(right, speed));
    if (keys[SDL_SCANCODE_D]) movement = float3_add(movement, float3_scale(right, -speed));

    state->camera.position.y = target_y;
    state->renderer.max_depth = MAX_DEPTH;

    if (state->mouse_captured) {
      float mx, my;
      SDL_GetRelativeMouseState(&mx, &my);

      state->camera.yaw += mx * state->controller.mouse_sensitivity;
      state->camera.pitch -= my * state->controller.mouse_sensitivity;

      if (state->camera.pitch < state->controller.min_pitch) state->camera.pitch = state->controller.min_pitch;
      if (state->camera.pitch > state->controller.max_pitch) state->camera.pitch = state->controller.max_pitch;
    }
  } else if (state->mode == OVERHEAD) {
    // In overhead mode, use world axes instead of camera axes
    float3 world_forward = make_float3(0, 0, -1); // Forward is negative Z (up on screen)
    float3 world_right = make_float3(1, 0, 0);    // Right is positive X

    if (keys[SDL_SCANCODE_W]) movement = float3_add(movement, float3_scale(world_forward, speed));
    if (keys[SDL_SCANCODE_S]) movement = float3_add(movement, float3_scale(world_forward, -speed));
    if (keys[SDL_SCANCODE_A]) movement = float3_add(movement, float3_scale(world_right, speed));
    if (keys[SDL_SCANCODE_D]) movement = float3_add(movement, float3_scale(world_right, -speed));

    state->camera.position.y = target_y + 200.f;
    state->camera.pitch = -PI / 2;
    state->renderer.max_depth = 250;

    // Release mouse in overhead mode
    if (state->mouse_captured) {
      state->mouse_captured = false;
      SDL_SetWindowRelativeMouseMode(state->sdl_window, false);
    }
  }

  if (state->mode == WIREFRAME) {
    state->renderer.wireframe_mode = true;
  } else {
    state->renderer.wireframe_mode = false;
  }
    
  state->camera.position = float3_add(state->camera.position, movement);
  float new_ground_height = get_interpolated_terrain_height(state->camera.position.x, state->camera.position.z);

  state->controller.ground_height = new_ground_height;
  update_camera(&state->renderer, &state->camera);
}

int main(int argc, char const *argv[]) {
  (void)argc; (void)argv;
  // Initialize basic game state and window
  struct state_t state = { 0 };

  SDL_library_init(&state.sdl_window, &state.sdl_renderer, &state.sdl_framebuffer_tex);

  state.running = true;
  state.mouse_captured = false;
  state.mode = NORMAL;

  init_performance_counter(&state.stats);

  // Initialize shader-works renderer and scene
  init_renderer(&state.renderer, WIN_WIDTH, WIN_HEIGHT, 0, 0, state.framebuffer, state.depth_buffer, MAX_DEPTH);
  
  scene_t scene = {0};
  init_scene(&scene, MAX_CHUNKS);

  // Initialize camera position
  float terrain_height = get_interpolated_terrain_height(scene.camera_pos.position.x, scene.camera_pos.position.z);
  state.controller.ground_height = terrain_height;
  float terrain_clearance = fmaxf(3.0f, fabsf(terrain_height) * 0.1f + 2.0f);
  scene.camera_pos.position.y = terrain_height + terrain_clearance;
  state.camera = scene.camera_pos;

  // Initialize controller settings
  state.controller.move_speed = 15.0f;
  state.controller.mouse_sensitivity = 0.002f;
  state.controller.min_pitch = -PI/2 + 0.1f;
  state.controller.max_pitch = PI/2 - 0.1f;
  state.controller.camera_height_offset = terrain_clearance;

  light_t sun = {
    .is_directional = true,
    .direction = make_float3(1, -1, 1),
    .color = rgb_to_u32(200, 160, 160)
  };

  float accumulator = 0.0f;
  float total_time = 0.0f;
  uint64_t last_time = SDL_GetPerformanceCounter();

  // main game loop
  while (state.running) {
    uint64_t current_time = SDL_GetPerformanceCounter();
    float frame_time = (float)(current_time - last_time) / (float)SDL_GetPerformanceFrequency();
    last_time = current_time;

    // Cap frame time to prevent spiral of death
    if (frame_time > 0.1f) frame_time = 0.1f;

    accumulator += frame_time;
    total_time += frame_time;

    // Update sun color based on day/night cycle
    sun.color = get_sun_color(total_time);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) state.running = false;
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
          state.running = false;
        } 
        
        if (event.key.key == SDLK_SPACE) {
          state.mouse_captured = !state.mouse_captured;
          SDL_SetWindowRelativeMouseMode(state.sdl_window, state.mouse_captured);
        } else if (event.key.key == SDLK_1)
          state.mode = NORMAL;
        else if (event.key.key == SDLK_2)
          state.mode = OVERHEAD;
        else if (event.key.key == SDLK_3)
          state.mode = WIREFRAME;
      }
    }

    // Fixed timestep game updates
    while (accumulator >= TICK_INTERVAL) {
      state.controller.delta_time = TICK_INTERVAL;
      update_state(&state);
      scene.controller = state.controller;
      scene.camera_pos = state.camera;

      update_loaded_chunks(&scene);

      accumulator -= TICK_INTERVAL;
      state.stats.tps_counter++;
    }

    u8 bg_r, bg_g, bg_b;
    get_fog_color(total_time, &bg_r, &bg_g, &bg_b);
    u32 background_color = rgb_to_u32(bg_r, bg_g, bg_b);

    for(int i = 0; i < WIN_WIDTH * WIN_HEIGHT; ++i) {
      state.framebuffer[i] = background_color;
      state.depth_buffer[i] = FLT_MAX;
    }

     // Render at unlimited FPS
    usize triangles_rendered = render_loaded_chunks(&state.renderer, &scene, &sun, 1);

    // Update and render falling particles
    update_quads(scene.camera_pos.position, &scene.camera_pos);
    usize quad_triangles = render_quads(&state.renderer, &scene.camera_pos, &sun, 1);
    triangles_rendered += quad_triangles;

    if (state.mode != OVERHEAD) {
      u8 fog_r, fog_g, fog_b;
      get_fog_color(total_time, &fog_r, &fog_g, &fog_b);
      apply_fog_to_screen(&state.renderer, state.renderer.max_depth / 2.f, state.renderer.max_depth - 1.0f, fog_r, fog_g, fog_b);
    } else {
      model_t cube = { 0 };
      float3 pos = make_float3(scene.camera_pos.position.x, scene.controller.ground_height + 3, scene.camera_pos.position.z);
      generate_cube(&cube, pos, (float3){ 2, 1, 2 });

      render_model(&state.renderer, &scene.camera_pos, &cube, &sun, 1);
    }

    SDL_UpdateTexture(state.sdl_framebuffer_tex, NULL, state.framebuffer, WIN_WIDTH * sizeof(u32));
    SDL_RenderTexture(state.sdl_renderer, state.sdl_framebuffer_tex, NULL, NULL);
    SDL_RenderPresent(state.sdl_renderer);
    
    state.stats.fps_counter++;
    state.stats.triangle_counter += triangles_rendered;

    // Print TPS and FPS every second
    uint64_t counter_time = SDL_GetPerformanceCounter();
    if ((float)(counter_time - state.stats.last_counter_time) / (float)SDL_GetPerformanceFrequency() >= 1.0f) {
      // Calculate chunk count
      chunk_t **chunks = calloc(MAX_CHUNKS, sizeof(chunk_t*));
      usize chunk_count = 0;
      get_all_chunks(&scene.chunk_map, chunks, &chunk_count);
      free(chunks);
      
      uint64_t avg_triangles_per_frame = state.stats.fps_counter > 0 ? state.stats.triangle_counter / state.stats.fps_counter : 0;
      printf("TPS: %lu, FPS: %lu, Triangles/frame: %lu, Chunks: %zu, Player: (%.1f, %.1f, %.1f)\n",
              state.stats.tps_counter, state.stats.fps_counter, avg_triangles_per_frame, chunk_count,
              scene.camera_pos.position.x, scene.camera_pos.position.y, scene.camera_pos.position.z);
      state.stats.tps_counter = 0;
      state.stats.fps_counter = 0;
      state.stats.triangle_counter = 0;
      state.stats.last_counter_time = counter_time;
    }
  }

  free_chunk_map(&scene.chunk_map);
  
  SDL_DestroyTexture(state.sdl_framebuffer_tex);
  SDL_DestroyRenderer(state.sdl_renderer);
  SDL_DestroyWindow(state.sdl_window);
  SDL_Quit();

  return 0;
}

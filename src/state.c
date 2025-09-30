#include "state.h"

#include <stdlib.h>

void init_state_machine(state_machine_t *sm, unsigned default_state, unsigned num_states) {
  if (!sm) return;

  sm->default_state = default_state;
  sm->current_state = default_state;

  sm->states = calloc(num_states, sizeof(state_interface_t));
  sm->num_states = num_states;

  sm->game_state = NULL;
  sm->game_state_size = 0;
}

int start_state_machine(state_machine_t *sm) {
  if (!sm) return -1;

  if ((unsigned)sm->current_state < sm->num_states && sm->states[sm->current_state].enter) {
    sm->states[sm->current_state].enter(sm->game_state, sm->game_state_size);

    return 1;
  }

  return -1;
}

void free_state_machine(state_machine_t *sm) {
  if (!sm) return;

  free(sm->states);
  sm->states = NULL;
  sm->num_states = 0;

  sm->game_state = NULL;
  sm->game_state_size = 0;
}

int set_state_interface(state_machine_t *sm, unsigned state, state_interface_t *interface) {
  if (!sm || !interface) return -1;
  if (state >= sm->num_states) return -1;

  sm->states[state] = *interface;

  return 1;
}

int update_internal_state(state_machine_t *sm, void *state, size_t size) {
  if (!sm || !state) return -1;

  sm->game_state = state;
  sm->game_state_size = size;
  return 1;
}

int change_state(state_machine_t *sm, unsigned new_state) {
  if (!sm) return -1;
  if (new_state >= sm->num_states) return -1;

  if (sm->states[sm->current_state].exit)
    sm->states[sm->current_state].exit(sm->game_state, sm->game_state_size);

  sm->current_state = new_state;

  if (sm->states[sm->current_state].enter)
    sm->states[sm->current_state].enter(sm->game_state, sm->game_state_size);

  return 1;
}

int get_state(state_machine_t *sm) {
  if (!sm) return -1;

  return (int)sm->current_state;
}

void tick_state(state_machine_t *sm, float dt) {
  if (!sm) return;

  if (sm->states[sm->current_state].tick)
    sm->states[sm->current_state].tick(sm->game_state, sm->game_state_size, dt);
}

int render_state(state_machine_t *sm) {
  if (!sm) return 0;

  if (sm->states[sm->current_state].render)
    return sm->states[sm->current_state].render(sm->game_state, sm->game_state_size);

  return 0;
}

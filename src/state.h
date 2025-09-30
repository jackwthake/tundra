#ifndef __STATE_H__
#define __STATE_H__

#include <stddef.h>

typedef struct {
  void (*enter)(void *, size_t);
  void (*tick)(void *, size_t, float dt);
  int (*render)(void *, size_t);
  void (*exit)(void *, size_t);
} state_interface_t;

typedef struct {
  state_interface_t *states;
  unsigned num_states;

  void *game_state;
  size_t game_state_size;

  int default_state, current_state;
} state_machine_t;

void init_state_machine(state_machine_t *sm, unsigned default_state, unsigned num_states);
int start_state_machine(state_machine_t *sm);
void free_state_machine(state_machine_t *sm);

int set_state_interface(state_machine_t *sm, unsigned state, state_interface_t *interface);
int change_state(state_machine_t *sm, unsigned new_state);
int update_internal_state(state_machine_t *sm, void *state, size_t size);
int get_state(state_machine_t *sm);

void tick_state(state_machine_t *sm, float dt);
int render_state(state_machine_t *sm);

#endif
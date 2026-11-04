#pragma once
int satchel_input_enter(void);
void satchel_input_leave(int *entered);
void satchel_input_stop(void);
int satchel_input_join(unsigned milliseconds);
void satchel_input_reset(void);
#define SATCHEL_INPUT_SCOPE() \
    int satchel_input_scope __attribute__((cleanup(satchel_input_leave))) = satchel_input_enter(); \
    if (!satchel_input_scope) return

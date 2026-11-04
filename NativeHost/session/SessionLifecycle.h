#pragma once
// Main queue owner. Menu, application activity and interruption are independent.
typedef struct {
    void (*pause)(int);
    int (*audio)(int, int);
    void (*flush)(void);
    void (*pressure)(void);
} SatchelLifecycleHooks;
void satchel_lifecycle_begin(SatchelLifecycleHooks hooks);
void satchel_lifecycle_end(void);
void satchel_lifecycle_focus(int event);
void satchel_lifecycle_menu(int paused);
void satchel_lifecycle_interruption(int began, int should_resume);
void satchel_lifecycle_route(void);
void satchel_lifecycle_retry_audio(void);
void satchel_lifecycle_pressure(void);

#pragma once
#include <stdatomic.h>
#include <stdint.h>
typedef struct { _Atomic int ready, release; uint64_t args[4], number; } media_fixture;
void media_movie_start(int cycle);
void media_movie_release(void);
void media_movie_verify(void);
void media_audio_start(void);
void media_audio_verify(void);
void media_audren_start(void);
void media_audren_verify(void);
void media_helper_start(void);
void media_helper_verify(void);
void *media_probe_symbol(const char *);
void media_select_title(int);
void media_start_guest(void (*)(void *), void *);
void media_await(_Atomic int *);

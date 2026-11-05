#pragma once
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

// This boundary owns guest pthreads only. A successful join is not a graphics,
// media or service shutdown certificate. One coordinator owns begin/reset/end.
int satchel_session_begin(void);
int satchel_session_active(void);
int satchel_session_stopping(void);
int satchel_session_quiescent(void);
int satchel_session_end(void);
int satchel_session_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
void satchel_session_run_guest(void (*)(void *), void *);
// Scoped guest callback, returning 0 on session cancellation, 1 on completion.
// The callback must be uninstrumented and own no native resources requiring
// cleanup. Its caller retains its native frames and must handle cancellation.
// Cancelled guest state is terminal, never resume or retry its SDK operations.
int satchel_session_call_guest(void (*)(void *), void *);
void satchel_session_request_stop(void);
void satchel_session_pause(int on);
int satchel_session_join(unsigned timeout_ms);
int satchel_session_guest_range(const void *, size_t);
void satchel_session_own_mapping(void *, size_t);
unsigned satchel_session_live_threads(void);
unsigned satchel_session_live_mappings(void);

// Called from SVC dispatch while its C frame is instrumented. A scoped guest
// callback is also a cancellation point, returning to its native cleanup.
int satchel_session_root_call(void);

// Engine overlays. Only call reset functions while the boundary is quiescent
// and all other users of that subsystem have been stopped by its owner.
void satchel_kernel_request_stop(void);
void satchel_kernel_gate_enter(int root);
void satchel_kernel_thread_exit(void);
void satchel_clock_request_stop(void);
int satchel_clock_session_reset(void);
int satchel_kernel_session_reset(void);
int satchel_fs_session_reset(void);
int satchel_profile_session_reset(void);
void satchel_movie_request_stop(void);
int satchel_movie_session_reset(void);
void satchel_audren_request_stop(void);
int satchel_audren_session_reset(void);
int satchel_guest_helpers_session_reset(void);
void satchel_audio_request_stop(void);
int satchel_audio_session_reset(void);
void satchel_hid_request_stop(void);
int satchel_hid_join(unsigned timeout_ms);
int satchel_hid_session_reset(void);
int satchel_touch_session_reset(void);
void satchel_memory_request_stop(void);
int satchel_memory_join(unsigned timeout_ms);
int satchel_memory_session_reset(void);
void satchel_mtl_request_stop(void);
int satchel_mtl_producers_join(unsigned timeout_ms);
int satchel_loader_session_reset(void);
int satchel_gate_session_reset(void);
void satchel_host_session_load(int argc, char **argv);
void *satchel_host_session_enter(void *unused);

int satchel_mtl_session_prepare(void);
int satchel_mtl_session_retire(void);

void satchel_objmap_put_owned(const void *, void *, void (*)(void *));
void satchel_sdk16_request_stop(void);
int satchel_sdk16_join(unsigned);
void satchel_oe_request_stop(void);
int satchel_oe_join(unsigned);
int satchel_os_resources_session_reset(void);
int satchel_sdk16_session_reset(void);
int satchel_oe_session_reset(void);
int satchel_sf_session_reset(void);
int satchel_misc_session_reset(void);
int satchel_net_session_reset(void);
int satchel_friends_session_reset(void);
int satchel_offline_session_reset(void);
int satchel_swkbd_session_reset(void);
void satchel_web_request_stop(void);
int satchel_web_session_reset(void);
int satchel_jit_session_reset(void);
int satchel_codec_video_session_reset(void);

int satchel_nvn_session_reset(void);
int satchel_dirty_session_reset(void);

int satchel_fpslock_session_reset(void);
int satchel_uemalloc_session_reset(void);

int satchel_host_session_prepare(void);
int satchel_host_session_stop(void);

int satchel_bootstrap_session_reset(void);
int satchel_cheats_session_reset(void);

void satchel_diagnostics_request_stop(void);
int satchel_diagnostics_join(unsigned);
int satchel_diagnostics_session_reset(void);

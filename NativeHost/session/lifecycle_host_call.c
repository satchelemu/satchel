#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
_Atomic int lifecycle_inside_host, lifecycle_host_returned;
void lifecycle_host_call(void) {
    pthread_mutex_lock(&fixture_lock);
    atomic_store(&lifecycle_inside_host, 1);
    usleep(50000);
    atomic_store(&lifecycle_host_returned, 1);
    pthread_mutex_unlock(&fixture_lock);
}
int lifecycle_host_lock_available(void) {
    int result = pthread_mutex_trylock(&fixture_lock);
    if (!result) pthread_mutex_unlock(&fixture_lock);
    return !result;
}

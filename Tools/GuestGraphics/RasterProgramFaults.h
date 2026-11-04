#include <stddef.h>
#include "GuestPools.h"
void *raster_test_malloc(size_t);
void *raster_test_calloc(size_t,size_t);
SatchelGuestPoolStatus raster_test_release(SatchelGuestPools *,uint64_t);
SatchelGuestPoolStatus raster_test_binding(SatchelGuestPools *,SatchelCPUProbe *,uint64_t,uint64_t,uint64_t,SatchelGuestPoolBinding *);

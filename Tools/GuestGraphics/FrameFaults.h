#include "VulkanRasterBatch.h"
#include <stddef.h>
void *frame_test_allocate(size_t);
SatchelVulkanStatus frame_test_read(uint64_t,uint64_t,uint64_t,void*,size_t);
SatchelVulkanStatus frame_test_destroy(uint64_t,uint64_t);
SatchelVulkanStatus frame_test_submit(uint64_t,uint64_t,const SatchelVulkanRasterBatch*,uint64_t*,SatchelVulkanCancelled,void*);

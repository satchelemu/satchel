#include "GuestTextureFrames.h"
#include "VulkanBuffer.h"
#include "VulkanRasterBatch.h"
SatchelVulkanStatus tf_frame_read(uint64_t,uint64_t,uint64_t,void*,size_t);
SatchelVulkanStatus tf_frame_destroy(uint64_t,uint64_t);
SatchelVulkanStatus tf_frame_submit(uint64_t,uint64_t,const SatchelVulkanRasterBatch*,uint64_t*,SatchelVulkanCancelled,void*);

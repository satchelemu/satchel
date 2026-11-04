#include "GuestCompute.h"
#include "VulkanScalar.h"
SatchelCPUWriteResult nvn_test_writev(SatchelCPUProbe *,const SatchelCPUProbeWrite *,size_t);
SatchelVulkanStatus nvn_test_submit(uint64_t,uint64_t,const SatchelVulkanScalarBindings *,uint64_t *,SatchelVulkanCancelled,void *);
SatchelVulkanStatus nvn_test_destroy(uint64_t,uint64_t);

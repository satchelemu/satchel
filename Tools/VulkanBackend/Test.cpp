#include "VulkanBackend.h"
#include <cstdio>
#include <cstdlib>
static int cancel(void *) {return 1;}
struct Counter {unsigned calls=0,target=0;};
static int cancel_after(void *p) {auto &c=*static_cast<Counter *>(p);return ++c.calls>=c.target;}
static int reenter(void *p) {SatchelVulkanReport inner={};*static_cast<int *>(p)=satchel_vulkan_compute_diagnostic(&inner,nullptr,nullptr);return 1;}
static unsigned checks;
static void check(bool condition,const char *name) {++checks;if (!condition) {std::fprintf(stderr,"Vulkan test failed: %s\n",name);std::exit(1);}}
int main() {
    check(satchel_vulkan_compute_diagnostic(nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID,"null report");
    SatchelVulkanReport r={};
    check(satchel_vulkan_compute_diagnostic(&r,cancel,nullptr)==SATCHEL_VULKAN_CANCELLED && !r.command_completed,"pre-cancel");
    int nested=-1;
    check(satchel_vulkan_compute_diagnostic(&r,reenter,&nested)==SATCHEL_VULKAN_CANCELLED && nested==SATCHEL_VULKAN_BUSY,"reentrant guard");
    for (unsigned target=2;target<=5;++target) {
        Counter counter={0,target};
        check(satchel_vulkan_compute_diagnostic(&r,cancel_after,&counter)==SATCHEL_VULKAN_CANCELLED &&
            !r.command_completed && !r.resources_retained,"cancel before submit with clean resource lifetime");
    }
    for (unsigned run=0;run<2;++run) {
        const auto status=satchel_vulkan_compute_diagnostic(&r,nullptr,nullptr);
        std::printf("Vulkan original fixture: status=%u stage=%u VkResult=%d api=%u portability=%u completed=%u value=%u guards=%u retained=%u\n",
            r.status,r.stage,r.vk_result,r.device_api_version,r.portability_subset_enabled,r.command_completed,r.value_matched,r.guard_words_matched,r.resources_retained);
        check(status==SATCHEL_VULKAN_OK,"native compute dispatch");
        check(r.command_completed && r.value_matched && r.guard_words_matched && r.portability_subset_enabled && !r.resources_retained,"result and clean completion");
    }
    std::printf("Vulkan wrapper: %u checks passed; original SPIR-V dispatched through MoltenVK/Metal, not a game shader.\n",checks);
}

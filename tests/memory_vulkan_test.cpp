#include "src/vulkan_trace.cpp"
#include <cassert>
static VkDevice device = reinterpret_cast<VkDevice>(1);
static VkQueue queue = reinterpret_cast<VkQueue>(2);
static VkImage image_handle = reinterpret_cast<VkImage>(3);
static VkImageCreateInfo info{};
static VkAllocationCallbacks allocator{};
static unsigned created = 0, destroyed = 0, waited = 0;
static VkResult VKAPI_CALL create_image(VkDevice d, const VkImageCreateInfo *i,
                                        const VkAllocationCallbacks *a, VkImage *image)
{
    assert(d == device && i == &info && a == &allocator);
    if (++created == 1)
    {
        *image = image_handle;
        return VK_SUCCESS;
    }
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}
static void VKAPI_CALL destroy_image(VkDevice d, VkImage i, const VkAllocationCallbacks *a)
{
    assert(d == device && (i == image_handle || !i) && a == &allocator);
    ++destroyed;
}
static VkResult VKAPI_CALL queue_idle(VkQueue q)
{
    assert(q == queue);
    ++waited;
    return VK_ERROR_DEVICE_LOST;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    ps5::memory::init(argv[1], "dispatch-test");
    PFN_vkVoidFunction fn = reinterpret_cast<PFN_vkVoidFunction>(create_image);
    ps5_vulkan_trace_symbol("vkCreateImage", &fn);
    ps5_vulkan_trace_symbol("vkCreateImage", &fn);
    VkImage image{};
    assert(reinterpret_cast<PFN_vkCreateImage>(fn)(device, &info, &allocator, &image) ==
           VK_SUCCESS);
    assert(image == image_handle);
    assert(reinterpret_cast<PFN_vkCreateImage>(fn)(device, &info, &allocator, &image) ==
           VK_ERROR_OUT_OF_HOST_MEMORY);
    fn = reinterpret_cast<PFN_vkVoidFunction>(destroy_image);
    ps5_vulkan_trace_symbol("vkDestroyImage", &fn);
    ps5_vulkan_trace_symbol("vkDestroyImage", &fn);
    reinterpret_cast<PFN_vkDestroyImage>(fn)(device, image, &allocator);
    reinterpret_cast<PFN_vkDestroyImage>(fn)(device, VK_NULL_HANDLE, &allocator);
    fn = reinterpret_cast<PFN_vkVoidFunction>(queue_idle);
    ps5_vulkan_trace_symbol("vkQueueWaitIdle", &fn);
    ps5_vulkan_trace_symbol("vkQueueWaitIdle", &fn);
    assert(reinterpret_cast<PFN_vkQueueWaitIdle>(fn)(queue) == VK_ERROR_DEVICE_LOST);
    assert(created == 2 && destroyed == 2 && waited == 1);
    ps5::memory::finish();
}

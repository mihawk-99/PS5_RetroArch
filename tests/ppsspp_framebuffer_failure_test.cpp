#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#define _dbg_assert_(x) assert(x)
#define _assert_(x) assert(x)
using VmaAllocator = void *;
using VmaAllocation = void *;
struct VmaAllocationCreateInfo
{
    int usage;
};
struct VmaAllocationInfo
{
};
constexpr int VMA_MEMORY_USAGE_GPU_ONLY = 1;
static unsigned call, fail_at, barriers, image_calls, view_calls;
static std::set<VkImage> images;
static std::set<VkImageView> views;
static VkResult failure = VK_ERROR_OUT_OF_DEVICE_MEMORY;
VkResult vmaCreateImage(VmaAllocator, const VkImageCreateInfo *, const VmaAllocationCreateInfo *,
                        VkImage *image, VmaAllocation *allocation, VmaAllocationInfo *)
{
    ++image_calls;
    if (++call == fail_at)
    {
        *image = reinterpret_cast<VkImage>(uintptr_t(999));
        *allocation = reinterpret_cast<void *>(uintptr_t(999));
        return failure;
    }
    *image = reinterpret_cast<VkImage>(uintptr_t(call));
    *allocation = reinterpret_cast<void *>(uintptr_t(call));
    assert(images.insert(*image).second);
    return VK_SUCCESS;
}
void vmaDestroyImage(VmaAllocator, VkImage image, VmaAllocation allocation)
{
    assert(allocation && images.erase(image) == 1);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice, const VkImageViewCreateInfo *info,
                                                 const VkAllocationCallbacks *, VkImageView *view)
{
    ++view_calls;
    assert(images.count(info->image)); // Never accept a null or failed image.
    if (++call == fail_at)
    {
        *view = reinterpret_cast<VkImageView>(uintptr_t(999));
        return failure;
    }
    *view = reinterpret_cast<VkImageView>(uintptr_t(call));
    assert(views.insert(*view).second);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView view,
                                              const VkAllocationCallbacks *)
{
    assert(views.erase(view) == 1);
}
struct DeleteList
{
    void QueueDeleteImageView(VkImageView &v)
    {
        vkDestroyImageView({}, v, {});
        v = {};
    }
    void QueueDeleteImageAllocation(VkImage &v, VmaAllocation &a)
    {
        vmaDestroyImage({}, v, a);
        v = {};
        a = {};
    }
    void QueueDeleteFramebuffer(VkFramebuffer &)
    {
        assert(false);
    }
};
struct VulkanContext
{
    DeleteList list;
    struct Info
    {
        VkFormat preferredDepthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    } info;
    Info &GetDeviceInfo()
    {
        return info;
    }
    VkDevice GetDevice()
    {
        return {};
    }
    VmaAllocator Allocator()
    {
        return {};
    }
    DeleteList &Delete()
    {
        return list;
    }
    bool DebugLayerEnabled()
    {
        return true;
    }
    template <class T> void SetDebugName(T handle, VkObjectType, const char *)
    {
        assert(handle);
    }
};
struct VulkanBarrierBatch
{
    template <class... Args> void TransitionImage(VkImage image, Args...)
    {
        assert(images.count(image));
        ++barriers;
    }
};
struct VKRImage
{
    VkImage image{};
    VkImageView rtView{}, texAllLayersView{}, texLayerViews[2]{};
    VmaAllocation alloc{};
    VkFormat format{};
    VkSampleCountFlagBits sampleCount{};
    VkImageLayout layout{};
    int numLayers{};
    std::string tag;
    void Delete(VulkanContext *);
};
struct VKRFramebuffer
{
    VulkanContext *vulkan_;
    int width, height, numLayers;
    VkSampleCountFlagBits sampleCount{};
    VKRImage color{}, depth{}, msaaColor{}, msaaDepth{};
    VkFramebuffer framebuf[9]{};
    VkResult result_ = VK_NOT_READY;
    VKRFramebuffer(VulkanContext *, VulkanBarrierBatch *, int, int, int, int, bool, const char *);
    ~VKRFramebuffer();
    bool Valid() const
    {
        return result_ == VK_SUCCESS;
    }
    void UpdateTag(const char *)
    {
    }
    static VkResult CreateImage(VulkanContext *, VKRImage &, int, int, int, VkSampleCountFlagBits,
                                VkFormat, VkImageLayout, bool, const char *);
};
// The runner inserts the actual patched core's methods here.
#include "framebuffer_methods.inc"
int main()
{
    VulkanContext vk;
    VulkanBarrierBatch batch;
    unsigned failures = 0;
    for (int layers : {1, 2})
        for (int msaa : {0, 3})
        {
            const unsigned attachments = msaa ? 4 : 2;
            const unsigned creates = attachments * (3 + layers);
            for (VkResult error : {VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY})
            {
                failure = error;
                for (unsigned fail = 1; fail <= creates; ++fail)
                {
                    call = barriers = image_calls = view_calls = 0;
                    fail_at = fail;
                    {
                        VKRFramebuffer fb(&vk, &batch, 4800, 2720, layers, msaa, true, "failure");
                        assert(!fb.Valid() && fb.result_ == error && call == fail);
                        assert(barriers == 0);
                    }
                    assert(images.empty() && views.empty());
                    ++failures;
                }
            }
            call = barriers = image_calls = view_calls = 0;
            fail_at = 0;
            {
                VKRFramebuffer fb(&vk, &batch, 4800, 2720, layers, msaa, true, "success");
                assert(fb.Valid() && barriers == attachments && image_calls == attachments);
                assert(view_calls == attachments * (2 + layers));
            }
            assert(images.empty() && views.empty());
        }
    printf("PPSSPP framebuffer: all %u injected image/view failures clean up without publishing "
           "barriers; mono/stereo and MSAA on/off success passes\n",
           failures);
}

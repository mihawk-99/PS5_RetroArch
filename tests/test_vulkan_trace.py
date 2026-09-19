"""The diagnostics must preserve Vulkan dispatch and failed results."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class VulkanTrace(unittest.TestCase):
    def test_dispatch_preserves_arguments_failures_and_reloads(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        harness = r'''
#include "src/vulkan_trace.cpp"
#include <cassert>
static VkCommandBuffer expected_command = reinterpret_cast<VkCommandBuffer>(1);
static VkQueue expected_queue = reinterpret_cast<VkQueue>(2);
static VkFence expected_fence = reinterpret_cast<VkFence>(3);
static VkSubmitInfo submit_info = {};
static VkPresentInfoKHR present_info = {};
static unsigned end_calls, submit_calls, present_calls;
static VkResult VKAPI_CALL fake_end(VkCommandBuffer command)
{
    assert(command == expected_command);
    ++end_calls;
    return static_cast<VkResult>(-13);
}
static VkResult VKAPI_CALL fake_submit(VkQueue queue, uint32_t count,
                                       const VkSubmitInfo *submits, VkFence fence)
{
    assert(queue == expected_queue && count == 1);
    assert(submits == &submit_info && fence == expected_fence);
    ++submit_calls;
    return VK_ERROR_DEVICE_LOST;
}
static VkResult VKAPI_CALL fake_present(VkQueue queue, const VkPresentInfoKHR *present)
{
    assert(queue == expected_queue && present == &present_info);
    present->pResults[0] = VK_SUBOPTIMAL_KHR;
    ++present_calls;
    return VK_SUCCESS;
}
int main()
{
    PFN_vkVoidFunction fn = nullptr;
    ps5_vulkan_trace_symbol("vkEndCommandBuffer", &fn);
    assert(fn == nullptr);
    fn = reinterpret_cast<PFN_vkVoidFunction>(fake_end);
    ps5_vulkan_trace_symbol("unrelated", &fn);
    assert(fn == reinterpret_cast<PFN_vkVoidFunction>(fake_end));
    ps5_vulkan_trace_symbol("vkEndCommandBuffer", &fn);
    ps5_vulkan_trace_symbol("vkEndCommandBuffer", &fn);
    assert(reinterpret_cast<PFN_vkEndCommandBuffer>(fn)(expected_command) == static_cast<VkResult>(-13));
    fn = reinterpret_cast<PFN_vkVoidFunction>(fake_end);
    ps5_vulkan_trace_symbol("vkEndCommandBuffer", &fn);
    assert(reinterpret_cast<PFN_vkEndCommandBuffer>(fn)(expected_command) == static_cast<VkResult>(-13));
    fn = reinterpret_cast<PFN_vkVoidFunction>(fake_submit);
    ps5_vulkan_trace_symbol("vkQueueSubmit", &fn);
    assert(reinterpret_cast<PFN_vkQueueSubmit>(fn)(expected_queue, 1, &submit_info,
                                                   expected_fence) == VK_ERROR_DEVICE_LOST);
    VkResult image_result = VK_SUCCESS;
    present_info.swapchainCount = 1;
    present_info.pResults = &image_result;
    fn = reinterpret_cast<PFN_vkVoidFunction>(fake_present);
    ps5_vulkan_trace_symbol("vkQueuePresentKHR", &fn);
    assert(reinterpret_cast<PFN_vkQueuePresentKHR>(fn)(expected_queue, &present_info) == VK_SUCCESS);
    assert(image_result == VK_SUBOPTIMAL_KHR);
    assert(end_calls == 2 && submit_calls == 1 && present_calls == 1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "trace-test.cpp"
            binary = Path(directory) / "trace-test"
            source.write_text(harness)
            subprocess.run([compiler, "-std=c++20", "-I", str(ROOT), "-I",
                            str(ROOT / "vendor/retroarch"), str(source), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            run = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        self.assertIn("vkEndCommandBuffer calls=2 failures=2 result=-13", run.stderr)
        self.assertIn("vkQueueSubmit calls=1 failures=1 result=-4", run.stderr)
        self.assertIn("vkQueuePresentKHR calls=1 failures=0 result=0", run.stderr)
        self.assertIn("swapchain image calls=1 failures=1 result=1000001003", run.stderr)

    def test_timing_partitions_windows_and_excludes_warmup(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        harness = r'''
#include <cstdint>
static uint64_t tick = 1;
static uint64_t fake_now() { return tick; }
#define PS5_VULKAN_PROFILE_NOW fake_now
#include "src/vulkan_trace.cpp"
#include <cassert>
static VkResult VKAPI_CALL fake_submit(VkQueue, uint32_t count,
                                       const VkSubmitInfo *, VkFence)
{
    assert(count == 1);
    tick += 700000000;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_end(VkCommandBuffer)
{
    tick += 10000000;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_present(VkQueue, const VkPresentInfoKHR *)
{
    tick += 50000000;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_wait(VkDevice, uint32_t count,
    const VkFence *, VkBool32 all, uint64_t timeout)
{
    assert(count == 2 && all == VK_TRUE && timeout == 123);
    tick += 20000000;
    return VK_TIMEOUT;
}
static VkResult VKAPI_CALL fake_acquire(VkDevice, VkSwapchainKHR,
    uint64_t timeout, VkSemaphore, VkFence, uint32_t *index)
{
    assert(timeout == 456);
    *index = 7;
    tick += 30000000;
    return VK_SUBOPTIMAL_KHR;
}
int main()
{
    end_command_buffer = fake_end;
    queue_submit = fake_submit;
    queue_present = fake_present;
    PFN_vkVoidFunction fn = reinterpret_cast<PFN_vkVoidFunction>(fake_wait);
    ps5_vulkan_trace_symbol("vkWaitForFences", &fn);
    ps5_vulkan_trace_symbol("vkWaitForFences", &fn);
    auto wait = reinterpret_cast<PFN_vkWaitForFences>(fn);
    fn = reinterpret_cast<PFN_vkVoidFunction>(fake_acquire);
    ps5_vulkan_trace_symbol("vkAcquireNextImageKHR", &fn);
    ps5_vulkan_trace_symbol("vkAcquireNextImageKHR", &fn);
    auto acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(fn);
    VkPresentInfoKHR info{};
    // Four expensive warmup frames must not pollute either measured window.
    for (unsigned i = 0; i < 16; ++i)
    {
        tick += 40000000;
        const auto upload_start = ps5_vulkan_profile_texture_begin();
        tick += 50000000;
        ps5_vulkan_profile_texture_end(upload_start);
        ps5_vulkan_profile_begin();
        tick += i < 4 ? 2000000000 : 100000000;
        traced_end(VK_NULL_HANDLE);
        traced_submit(VK_NULL_HANDLE, 1, nullptr, VK_NULL_HANDLE);
        traced_present(VK_NULL_HANDLE, &info);
        assert(wait(VK_NULL_HANDLE, 2, nullptr, VK_TRUE, 123) == VK_TIMEOUT);
        uint32_t index = 0;
        assert(acquire(VK_NULL_HANDLE, VK_NULL_HANDLE, 456, VK_NULL_HANDLE,
                       VK_NULL_HANDLE, &index) == VK_SUBOPTIMAL_KHR && index == 7);
        ps5_vulkan_profile_end();
        ps5_vulkan_profile_end(); // A duplicate end must not add a frame.
    }
    assert(profile.frames == 2); // Two complete five-frame windows, two pending.
    assert(profile.samples[Interval].sum == 2000000000);
    // An incomplete callback must not attribute its gap to the next completed one.
    ps5_vulkan_profile_begin();
    tick += 10000000000ULL;
    ps5_vulkan_profile_begin();
    tick += 1000000;
    ps5_vulkan_profile_end();
    assert(profile.frames == 2);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "timing-test.cpp"
            binary = Path(directory) / "timing-test"
            source.write_text(harness)
            subprocess.run([compiler, "-std=c++20", "-I", str(ROOT), "-I",
                            str(ROOT / "vendor/retroarch"), str(source), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            run = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        lines = [line for line in run.stderr.splitlines() if line.startswith("gpu timing:")]
        self.assertEqual(len(lines), 2)
        for line in lines:
            self.assertIn("frames=5 seconds=5.000 fps=1.000", line)
            self.assertIn("interval=1000.000/1000.000 outside=40.000/40.000 texture=50.000/50.000", line)
            self.assertIn("prepare=100.000/100.000 end=10.000/10.000", line)
            self.assertIn("submit=700.000/700.000 present=50.000/50.000 wait=50.000/50.000", line)

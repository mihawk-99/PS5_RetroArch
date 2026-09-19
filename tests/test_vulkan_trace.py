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

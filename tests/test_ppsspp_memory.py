"""Execute the port's actual framebuffer creation with failures at every GPU call."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class PPSSPPMemory(unittest.TestCase):
    def test_framebuffer_failure_cleanup(self):
        # Reconstruct the patched file from the pinned input, never trust a dev checkout.
        source = subprocess.check_output([
            'git', '-C', str(ROOT / '.deps/ppsspp-src'), 'show',
            'fa50bb1976065c4f8b1b47af227d367fe9771555:Common/GPU/Vulkan/VulkanFramebuffer.cpp'
        ], text=True)
        patch = (ROOT / 'patches/ppsspp/ps5-port.patch').read_text()
        start = patch.index('diff --git a/Common/GPU/Vulkan/VulkanFramebuffer.cpp')
        end = patch.find('\ndiff --git ', start + 1)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            path = work / 'Common/GPU/Vulkan/VulkanFramebuffer.cpp'
            path.parent.mkdir(parents=True)
            path.write_text(source)
            subprocess.run(['git', 'apply', '--unsafe-paths', '--directory=' + directory, '-'],
                           input=patch[start:end if end >= 0 else None] + '\n', text=True, check=True)
            source = path.read_text()
            parts = []
            for first, last in [
                ('VkSampleCountFlagBits MultiSampleLevelToFlagBits', 'void VKRFramebuffer::UpdateTag'),
                ('VKRFramebuffer::~VKRFramebuffer()', 'static VkAttachmentLoadOp'),
            ]:
                parts.append(source[source.index(first):source.index(last)])
            (work / 'framebuffer_methods.inc').write_text('\n'.join(parts))
            binary = work / 'framebuffer-test'
            subprocess.run(['c++', '-std=c++17', '-I' + directory,
                            str(ROOT / 'tests/ppsspp_framebuffer_failure_test.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_shared_pool_pressure(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'memory-test'
            (Path(directory) / 'ps5platform').symlink_to(
                ROOT / '.deps/native/ps5-payload-sdk/target/include/ps5platform')
            subprocess.run(['c++', '-std=c++17', '-pthread',
                            '-I' + str(ROOT / 'src'),
                            '-I' + directory,
                            str(ROOT / 'tests/memory_status_test.cpp'),
                            str(ROOT / 'src/memory_status.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

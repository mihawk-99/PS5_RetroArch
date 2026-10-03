#include <assert.h>
#include <stdint.h>
#include "softfilter.h"
int main(void)
{
    const struct softfilter_implementation *f = softfilter_get_implementation(0);
    for (unsigned fmt = SOFTFILTER_FMT_RGB565; fmt <= SOFTFILTER_FMT_XRGB8888; fmt *= 2)
    {
        assert(f->query_input_formats() & fmt);
        void *p = f->create(0, fmt, fmt, 2, 2, 1, 0, 0);
        assert(p);
        unsigned w = 0, h = 0;
        f->query_output_size(p, &w, &h, 2, 2);
        assert(w == 4 && h == 4);
        uint32_t in32[4] = {0x123456, 0xabcdef, 0x654321, 0xfedcba}, out32[16] = {0};
        uint16_t in16[4] = {0x1234, 0xabcd, 0x6543, 0xfedc}, out16[16] = {0};
        struct softfilter_work_packet packet;
        f->get_work_packets(p, &packet, fmt == 1 ? (void *)out16 : (void *)out32, fmt == 1 ? 8 : 16,
                            fmt == 1 ? (void *)in16 : (void *)in32, 2, 2, fmt == 1 ? 4 : 8);
        packet.work(p, packet.thread_data);
        for (unsigned y = 0; y < 4; y++)
            for (unsigned x = 0; x < 4; x++)
            {
                if (fmt == 1)
                    assert(out16[y * 4 + x] == in16[(y / 2) * 2 + x / 2]);
                else
                    assert(out32[y * 4 + x] == in32[(y / 2) * 2 + x / 2]);
            }
        f->destroy(p);
    }
    return 0;
}

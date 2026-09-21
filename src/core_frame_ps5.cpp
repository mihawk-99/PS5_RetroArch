/* The padded source quad a software core's frame is sampled through.
 *
 * Nothing here touches channels, and that is a measured property of the pair on
 * either side rather than an omission. A software core hands libretro
 * little-endian XRGB8888, and ../PS5_Vulkan samples VK_FORMAT_B8G8R8A8_UNORM in
 * that format's own B, G, R, A byte order - its 8_8_8_8_UNORM descriptor word
 * with the ZYXW selectors (driver/ps5vk_image.c), which its own v0-formats
 * battery proves on the console. The two layouts are the same four bytes, so the
 * frontend's upload is a row copy and this file holds only the geometry a
 * 256-byte-row image needs when its sampled width is wider than the core's.
 */

/* Two triangle-list quads, with UVs restricted to the logical source image.
 * The caller uses a buffer belonging to the current, fence-retired sync slot. */
void ps5_core_source_quad(float *vertices, unsigned width, unsigned physical_width)
{
    const float right = physical_width > width ? float(width) / physical_width : 1.0f;
    const float quad[] = {
        -1, -1, 0, 0, -1, 1, 0, 1, 1, -1, right, 0, 1, -1, right, 0, -1, 1, 0, 1, 1, 1, right, 1,
        0,  0,  0, 0, 0,  1, 0, 1, 1, 0,  right, 0, 1, 0,  right, 0, 0,  1, 0, 1, 1, 1, right, 1,
    };
    for (unsigned i = 0; i < 48; ++i)
        vertices[i] = quad[i];
}

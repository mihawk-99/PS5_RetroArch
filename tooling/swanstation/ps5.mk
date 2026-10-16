# Included after upstream Makefile.libretro. Keep its source selection and Vulkan
# backend; use the title's shared-core loader rather than a statically linked app.
# The xxhash and POSIX feature adjustments follow SwanStationPS5 (AngelXex), GPL-3.0.
OBJECTS := $(filter-out %xxh_x86dispatch.o,$(OBJECTS))
src/core/texture_replacements.o: CXXFLAGS += -DXXH_X86DISPATCH_H_13563687684
dep/libretro-common/rthreads/retro_eventcount.o dep/libretro-common/rthreads/rthreads.o: CFLAGS += -D_POSIX_C_SOURCE=200112L

.PHONY: ps5-core
ps5-core: $(OBJECTS) $(PS5_CORE_RUNTIME)
	$(LD) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

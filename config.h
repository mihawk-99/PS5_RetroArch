#ifndef QB_CONFIG_H__
#define QB_CONFIG_H__

#define PACKAGE_NAME "retroarch"
#define HAVE_7ZIP 1
#define HAVE_ACCESSIBILITY 1
/* #undef HAVE_AL */
/* #undef HAVE_ALSA */
/* #undef HAVE_ANGLE */
/* #undef HAVE_AUDIOIO */
#define HAVE_AUDIOMIXER 1
/* #undef HAVE_BLISSBOX */
/* #undef HAVE_BLUETOOTH */
/* #undef HAVE_BSV_MOVIE */
/* #undef HAVE_BUILTINBEARSSL */
/* #undef HAVE_BUILTINFLAC */
/* #undef HAVE_BUILTINGLSLANG */
/* #undef HAVE_BUILTINMBEDTLS */
#define HAVE_BUILTINZLIB 1
#define HAVE_C99 1
/* #undef HAVE_CACA */
#define HAVE_CC 1
#define HAVE_CC_RESAMPLER 1
/* #undef HAVE_CDROM */
/* #undef HAVE_CG */
#ifndef CXX_BUILD
#define HAVE_CHD 1
#endif
#define HAVE_CHEATS 1
/* #undef HAVE_CHECK */
/* #undef HAVE_CHEEVOS */
#define HAVE_CHEEVOS_RVZ 1
#define HAVE_COMMAND 1
#define HAVE_CONFIGFILE 1
/* #undef HAVE_COREAUDIO3 */
#define HAVE_CORE_INFO_CACHE 1
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_CRTSWITCHRES 1
#endif
#define HAVE_CXX 1
#define HAVE_CXX11 1
#define HAVE_CXX17 1
/* #undef HAVE_D3D8 */
/* #undef HAVE_D3D9 */
/* #undef HAVE_D3DX8 */
/* #undef HAVE_D3DX9 */
/* #undef HAVE_DBUS */
/* #undef HAVE_DEBUG */
/* #undef HAVE_DINPUT */
/* #undef HAVE_DISCORD */
/* #undef HAVE_DISPMANX */
/* #undef HAVE_DRMINGW */
#define HAVE_DR_MP3 1
/* #undef HAVE_DSOUND */
/* #undef HAVE_DSP_FILTER */
#define HAVE_DYLIB 1
#define HAVE_DYNAMIC 1
/* #undef HAVE_DYNAMIC_EGL */
#define HAVE_EGL 1
/* #undef HAVE_EXYNOS */
/* #undef HAVE_FFMPEG */
/* #undef HAVE_FLAC */
/* #undef HAVE_FLOATHARD */
/* #undef HAVE_FLOATSOFTFP */
#define HAVE_FONTCONFIG 1
/* #undef HAVE_FREETYPE */
/* #undef HAVE_GAME_AI */
/* #undef HAVE_GDI */
#define HAVE_GETOPT_LONG 1
/* #undef HAVE_GFX_WIDGETS */
/* #undef HAVE_GLSL */
/* #undef HAVE_GLSLANG */
#define HAVE_GLSLANG_GENERICCODEGEN 1
/* #undef HAVE_GLSLANG_HLSL */
#define HAVE_GLSLANG_MACHINEINDEPENDENT 1
/* #undef HAVE_GLSLANG_OGLCOMPILER */
#define HAVE_GLSLANG_OSDEPENDENT 1
#define HAVE_GLSLANG_SPIRV 1
#define HAVE_GLSLANG_SPIRV_TOOLS 1
#define HAVE_GLSLANG_SPIRV_TOOLS_OPT 1
/* #undef HAVE_HID */
/* #undef HAVE_HLSL */
#define HAVE_IBXM 1
#define HAVE_IFINFO 1
#define HAVE_IMAGEVIEWER 1
/* #undef HAVE_JACK */
/* #undef HAVE_KMS */
#define HAVE_LANGEXTRA 1
/* #undef HAVE_LIBCHECK */
/* #undef HAVE_LIBRETRODB */
/* #undef HAVE_LIBSHAKE */
/* #undef HAVE_LIBUSB */
/* #undef HAVE_LUA */
/* #undef HAVE_MALI_FBDEV */
/* #undef HAVE_MATERIALUI */
/* #undef HAVE_MEMFD_CREATE */
#define HAVE_MENU 1
/* #undef HAVE_METAL */
/* #undef HAVE_MICROPHONE */
/* #undef HAVE_MIST */
#define HAVE_MMAP 1
/* #undef HAVE_MOC */
/* #undef HAVE_MPV */
#define HAVE_NEAREST_RESAMPLER 1
/* #undef HAVE_NEON */
#define HAVE_NETPLAYDISCOVERY 1
#define HAVE_NETPLAYDISCOVERY 1
/* #undef HAVE_NETWORK_CMD */
/* #undef HAVE_NETWORKGAMEPAD */
/* #undef HAVE_NETWORKING */
/* #undef HAVE_NETWORK_VIDEO */
#define HAVE_NO_ALSA 1
#define HAVE_NO_ANGLE 1
#define HAVE_NO_BLISSBOX 1
#define HAVE_NO_BLUETOOTH 1
#define HAVE_NO_BSV_MOVIE 1
#define HAVE_NO_BUILTINBEARSSL 1
#define HAVE_NO_BUILTINFLAC 1
#define HAVE_NO_BUILTINGLSLANG 1
#define HAVE_NO_BUILTINMBEDTLS 1
#define HAVE_NO_CACA 1
#define HAVE_NO_CDROM 1
#define HAVE_NO_CG 1
#define HAVE_NO_CHEEVOS 1
#define HAVE_NO_DISCORD 1
#define HAVE_NO_DSP_FILTER 1
#define HAVE_NO_FFMPEG 1
#define HAVE_NO_FLAC 1
#define HAVE_NO_FREETYPE 1
#define HAVE_NO_GDI 1
#define HAVE_NO_GFX_WIDGETS 1
#define HAVE_NO_JACK 1
#define HAVE_NO_KMS 1
#define HAVE_NO_LIBRETRODB 1
#define HAVE_NO_MATERIALUI 1
#define HAVE_NO_MICROPHONE 1
#define HAVE_NO_NETWORKING 1
#define HAVE_NO_NVDA 1
#define HAVE_NO_OPENGL 1
#define HAVE_NO_OPENGL1 1
#define HAVE_NO_OPENGL_CORE 1
#define HAVE_NO_OSS 1
#define HAVE_NO_OZONE 1
#define HAVE_NO_PIPEWIRE 1
#define HAVE_NO_PULSE 1
#define HAVE_NO_QT 1
#define HAVE_NO_SAPI 1
#define HAVE_NO_SDL 1
#define HAVE_NO_SDL2 1
#define HAVE_NO_SIXEL 1
#define HAVE_NO_SSL 1
#define HAVE_NOUNUSED 1
#define HAVE_NOUNUSED_VARIABLE 1
#define HAVE_NO_UPDATE_CORE_INFO 1
#define HAVE_NO_UPDATE_CORES 1
#define HAVE_NO_VIDEO_FILTER 1
#define HAVE_NO_VULKAN 1
#define HAVE_NO_WAYLAND 1
#define HAVE_NO_WINRAWINPUT 1
#define HAVE_NO_X11 1
#define HAVE_NO_XDELTA 1
#define HAVE_NO_XMB 1
/* #undef HAVE_NVDA */
/* #undef HAVE_ODROIDGO2 */
/* #undef HAVE_OMAP */
#define HAVE_ONLINE_UPDATER 1
/* #undef HAVE_OPENDINGUX_FBDEV */
/* #undef HAVE_OPENGL */
/* #undef HAVE_OPENGL1 */
/* #undef HAVE_OPENGL_CORE */
/* #undef HAVE_OPENGLES */
/* #undef HAVE_OPENGLES3 */
/* #undef HAVE_OPENGLES3_1 */
/* #undef HAVE_OPENGLES3_2 */
/* #undef HAVE_OSMESA */
/* #undef HAVE_OSS */
#define HAVE_OVERLAY 1
/* #undef HAVE_OZONE */
/* #undef HAVE_PARPORT */
#define HAVE_PATCH 1
/* #undef HAVE_PIPEWIRE */
#define HAVE_PIPEWIRE_STABLE 1
/* #undef HAVE_PLAIN_DRM */
/* #undef HAVE_PRESERVE_DYLIB */
/* #undef HAVE_PULSE */
/* #undef HAVE_QT */
#define HAVE_RBMP 1
#define HAVE_REWIND 1
#define HAVE_RGUI 1
#define HAVE_RJPEG 1
/* #undef HAVE_ROAR */
#define HAVE_RPILED 1
#define HAVE_RPNG 1
/* #undef HAVE_RSOUND */
#define HAVE_RTGA 1
#define HAVE_RUNAHEAD 1
#define HAVE_RWAV 1
/* #undef HAVE_SAPI */
#define HAVE_SCREENSHOTS 1
/* #undef HAVE_SDL */
/* #undef HAVE_SDL2 */
/* #undef HAVE_SDL_DINGUX */
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_SHADERPIPELINE 1
#endif
/* #undef HAVE_SIXEL */
/* #undef HAVE_SLANG */
/* #undef HAVE_SPIRV_CROSS */
/* #undef HAVE_SR2 */
#define HAVE_SSA 1
/* #undef HAVE_SSE */
/* #undef HAVE_SSL */
#define HAVE_STB_FONT 1
#define HAVE_STB_IMAGE 1
#define HAVE_STB_VORBIS 1
#define HAVE_STDIN_CMD 1
/* #undef HAVE_STEAM */
#define HAVE_STRCASESTR 1
/* #undef HAVE_SUNXI */
#define HAVE_SYSTEMD 1
/* #undef HAVE_SYSTEMMBEDCRYPTO */
/* #undef HAVE_SYSTEMMBEDTLS */
/* #undef HAVE_SYSTEMMBEDX509 */
#define HAVE_TEST_DRIVERS 1
#define HAVE_THREADS 1
#define HAVE_THREAD_STORAGE 1
#define HAVE_TINYALSA 1
/* #undef HAVE_TRANSLATE */
#define HAVE_UDEV 1
#define HAVE_UPDATE_ASSETS 1
/* #undef HAVE_UPDATE_CORE_INFO */
/* #undef HAVE_UPDATE_CORES */
#define HAVE_V4L2 1
/* #undef HAVE_VC_TEST */
/* #undef HAVE_VG */
/* #undef HAVE_VIDEOCORE */
/* #undef HAVE_VIDEO_FILTER */
#define HAVE_VIDEOPROCESSOR 1
/* #undef HAVE_VIVANTE_FBDEV */
/* #undef HAVE_VULKAN */
#define HAVE_VULKAN_DISPLAY 1
/* #undef HAVE_WAYLAND */
#define HAVE_WAYLAND_CURSOR 1
/* #undef HAVE_WAYLAND_PROTOS */
#define HAVE_WAYLAND_SCANNER 1
/* #undef HAVE_WIFI */
/* #undef HAVE_WINRAWINPUT */
/* #undef HAVE_X11 */
/* #undef HAVE_XDELTA */
/* #undef HAVE_XINERAMA */
/* #undef HAVE_XINPUT */
#define HAVE_XKBCOMMON 1
/* #undef HAVE_XMB */
/* #undef HAVE_XRANDR */
/* #undef HAVE_XSHM */
/* #undef HAVE_XVIDEO */
#define HAVE_ZLIB 1
#define HAVE_ZSTD 1
#endif

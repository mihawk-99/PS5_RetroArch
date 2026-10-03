/* Plain-language guidance. Core choices/categories come from the installed core,
 * never inferred from a saved value. Global keys match RetroArch configuration.c. */
'use strict';
const globalGuide = {};
function guide(category, key, label, description, control = 'toggle', choices = null) {
  globalGuide[key] = { category, label, description, control, choices };
}
const globalCategories = [
  ['video', 'Video', 'Shape the picture on your TV.'], ['audio', 'Audio', 'Volume and sound playback.'],
  ['input', 'Input', 'Controller feel and response.'], ['saving', 'Saving', 'Game saves, save states and resuming play.'],
  ['system', 'System', 'Playback speed and configuration behavior.'], ['interface', 'Interface', 'The RetroArch menu on your console.']
].map(([key, label, description]) => ({ key, label, description }));
guide('video', 'video_smooth', 'Smooth image scaling', 'Soften pixels when the picture is enlarged. Turn off for crisp pixel art.');
guide('video', 'video_vsync', 'Vertical sync', 'Match frame presentation to the display refresh to reduce tearing.');
guide('video', 'video_scale_integer', 'Whole-number scaling', 'Enlarge each original pixel evenly. This keeps pixel art sharp but may leave borders.');
guide('video', 'video_force_aspect', 'Keep the original shape', 'Keep the chosen aspect ratio instead of stretching the picture to fill the screen.');
guide('video', 'video_crop_overscan', 'Crop overscan', 'Hide unused edges of the picture when the core supports it.');
guide('video', 'video_shader_enable', 'Video shaders', 'Apply the selected shader to the game picture. Availability depends on the video driver.');
guide('audio', 'audio_enable', 'Game audio', 'Play audio produced by the game.');
guide('audio', 'audio_mute_enable', 'Mute audio', 'Silence game audio without changing the saved volume.');
guide('audio', 'audio_volume', 'Audio volume', 'Adjust game audio in decibels. 0 dB is the original level; positive values amplify it and may distort.', 'volume');
guide('audio', 'audio_sync', 'Synchronize audio', 'Keep sound playback in step with the game to reduce crackling and timing drift.');
guide('audio', 'audio_fastforward_mute', 'Mute during fast-forward', 'Silence game audio while fast-forward is active.');
guide('audio', 'audio_rewind_mute', 'Mute during rewind', 'Silence game audio while rewinding.');
guide('audio', 'audio_latency', 'Audio buffer', 'A smaller buffer can reduce sound delay; a larger buffer can help with crackling.', 'select', ['32','64','96','128','192','256'].map(v => [v, v + ' ms']));
guide('input', 'input_rumble_gain', 'Rumble strength', 'Set how strongly the controller vibrates when a game sends rumble feedback.', 'rumble');
guide('input', 'input_autodetect_enable', 'Automatic controller setup', 'Use matching controller profiles to assign buttons automatically.');
guide('input', 'input_remap_binds_enable', 'Use saved button remaps', 'Apply saved core and game button mappings when loading content.');
guide('input', 'input_all_users_control_menu', 'All controllers can use the menu', 'Let any connected player navigate the RetroArch menu.');
guide('input', 'input_menu_swap_ok_cancel_buttons', 'Swap confirm and cancel', 'Exchange the confirm and cancel buttons in the RetroArch menu.');
guide('input', 'input_menu_swap_scroll_buttons', 'Swap menu scroll buttons', 'Exchange the shoulder-button actions used to move through menu lists.');
guide('saving', 'savestate_auto_save', 'Save state when closing a game', 'Create an automatic save state when content closes. Support depends on the core.');
guide('saving', 'savestate_auto_load', 'Resume from an automatic state', 'Load the automatic save state when opening a game, if one exists.');
guide('saving', 'savestate_auto_index', 'Use a new slot for each save state', 'Advance the save-state slot when saving so earlier states are kept.');
guide('saving', 'savestate_file_compression', 'Compress save states', 'Use less disk space for save states. Compression can add time when saving.');
guide('saving', 'save_file_compression', 'Compress game saves', 'Compress supported save files to reduce disk usage. These may need decompression for other emulators.');
guide('saving', 'block_sram_overwrite', 'Protect game saves when loading a state', 'Keep current save RAM instead of replacing it with the copy stored in a save state.');
guide('saving', 'sort_savefiles_enable', 'Separate saves by core', 'Store each core’s game saves in its own subfolder.');
guide('saving', 'sort_savestates_enable', 'Separate states by core', 'Store each core’s save states in its own subfolder.');
guide('saving', 'sort_savefiles_by_content_enable', 'Separate saves by content folder', 'Organize game saves using the folder that contains the game.');
guide('saving', 'sort_savestates_by_content_enable', 'Separate states by content folder', 'Organize save states using the folder that contains the game.');
guide('saving', 'autosave_interval', 'Save RAM backup interval', 'Periodically write in-game save RAM to disk. This is separate from emulator save states.', 'select', [['0','Off'],['10','Every 10 seconds'],['30','Every 30 seconds'],['60','Every minute'],['300','Every 5 minutes']] );
guide('system', 'fastforward_ratio', 'Fast-forward speed', 'Limit how fast games run while fast-forwarding. Actual speed depends on the game and core.', 'select', [['0','Unlimited'],['1.5','1.5×'],['2','2×'],['3','3×'],['4','4×'],['5','5×'],['10','10×']] );
guide('system', 'slowmotion_ratio', 'Slow-motion speed', 'Choose how much to slow gameplay when slow motion is active.', 'select', [['2','Half speed'],['3','One-third speed'],['4','Quarter speed'],['5','One-fifth speed']] );
guide('system', 'pause_nonactive', 'Pause when inactive', 'Pause emulation when RetroArch loses focus, where supported by the platform.');
guide('system', 'auto_overrides_enable', 'Load core and game preferences', 'Automatically apply saved RetroArch overrides when opening content. Enable this to use the core overrides edited here.');
guide('system', 'game_specific_options', 'Use game-specific core options', 'Allow game and content-folder option files to take priority over the core’s shared options.');
guide('system', 'global_core_options', 'Share one core-options file', 'Store options in one global file. Turn off to use separate core profiles such as those edited by this WebUI.');
guide('system', 'config_save_on_exit', 'Save console menu changes on exit', 'Write changes made in RetroArch to its configuration when it closes.');
guide('system', 'rewind_enable', 'Rewind', 'Keep recent game states so you can rewind. This uses additional memory and processing time, and requires core support.');
guide('interface', 'menu_driver', 'Console menu style', 'Choose the menu you see on your PS5. This does not change the WebUI theme.', 'select', [['xmb','XMB — horizontal menu'],['rgui','RGUI — compact text menu']] );
guide('interface', 'menu_show_advanced_settings', 'Advanced console settings', 'Show additional technical settings in the console menu. The WebUI’s Advanced mode is separate.');
guide('interface', 'menu_pause_libretro', 'Pause when the menu opens', 'Pause the game while browsing the RetroArch menu.');
guide('interface', 'menu_enable_widgets', 'On-screen notifications', 'Use graphical notifications for supported messages such as saves and achievements.');
guide('interface', 'video_font_enable', 'On-screen messages', 'Display RetroArch status messages over the game picture.');
guide('interface', 'fps_show', 'Show frame rate', 'Display the current frame-rate counter while playing.');
guide('interface', 'framecount_show', 'Show frame count', 'Display the number of frames rendered since the game started.');
guide('interface', 'memory_show', 'Show memory usage', 'Display memory usage information reported by the platform.');
guide('interface', 'menu_show_load_content', 'Show Load Content', 'Keep the Load Content entry visible in the console menu.');
guide('interface', 'menu_show_load_core', 'Show Load Core', 'Keep the Load Core entry visible in the console menu.');
guide('interface', 'history_list_enable', 'Remember recently played games', 'Keep a history of opened content so it is easier to return to a game.');

// Supplements the concise/missing help in the installed PPSSPP definitions.
// Choices always remain those registered by that exact core build.
const pspHelp = {
  cpu_core: 'Choose how PSP CPU instructions are emulated. JIT usually runs faster; interpreters can help diagnose compatibility problems.',
  fast_memory: 'Skip some memory checks to speed up emulation. This can reduce stability in some games.',
  ignore_bad_memory_access: 'Continue past invalid memory accesses instead of stopping emulation. This can hide game or emulation errors.',
  io_timing_method: 'Choose how disc-read timing is simulated. Simulating UMD delays more closely follows the original PSP disc drive.',
  force_lag_sync: 'Synchronize emulation more closely to real time to reduce input delay, at a possible performance cost.',
  locked_cpu_speed: 'Override the emulated PSP clock speed. Higher values may help some games but require more processing power. Disabled lets the game choose.',
  memstick_inserted: 'Make the emulated Memory Stick available to games for saving and loading.',
  cache_iso: 'Keep the entire disc image in memory to reduce disc reads. Large games use substantially more memory.',
  cheats: 'Allow PPSSPP to use its installed cheat files for the game.',
  language: 'Choose the language reported by the PSP to games that support it. Automatic follows RetroArch’s language.',
  psp_model: 'Choose the PSP model to emulate. The PSP-2000/3000 model has more emulated memory than the PSP-1000.',
  backend: 'Choose the graphics renderer. Automatic follows RetroArch’s video driver. Available choices do not guarantee support on every platform.',
  software_rendering: 'Render graphics on the CPU for compatibility. This is much slower than hardware rendering.',
  internal_resolution: 'Render 3D graphics at a higher resolution for a sharper picture. Higher values use more GPU memory. This PS5 build uses 6× as its balanced default; high resolution combined with MSAA can exhaust memory.',
  mulitsample_level: 'Smooth jagged polygon edges using multiple samples per pixel. Higher levels use more GPU memory. The PS5 default is 8× with 6× rendering resolution.',
  cropto16x9: 'Remove one line from the top and bottom of the PSP picture for an exact 16:9 shape.',
  frameskip: 'Skip drawing some frames to reduce graphics work. Movement will look less smooth.',
  frameskiptype: 'Choose whether frame skipping is measured as a frame count or a percentage of the frame rate.',
  auto_frameskip: 'Automatically skip frames when emulation cannot keep up with full speed.',
  frame_duplication: 'Repeat frames to produce a 60 Hz output for games that draw fewer frames per second.',
  detect_vsync_swap_interval: 'Tell RetroArch when the game changes its frame rate so presentation can adapt.',
  inflight_frames: 'Choose how many graphics command buffers can wait ahead. Less buffering can reduce input delay but may reduce performance.',
  button_preference: 'Choose whether Cross or Circle confirms actions in PSP system dialogs and supported games.',
  analog_is_circular: 'Compensate for differences between circular and square analog-stick movement ranges.',
  analog_deadzone: 'Ignore small stick movements near the center. Increase this if the character moves when you release the stick.',
  analog_sensitivity: 'Scale analog-stick movement after RetroArch processes input. Larger values reach full movement with less stick travel.',
  skip_buffer_effects: 'Skip buffered effects to reduce graphics work. Some games lose effects or show a blank picture.',
  disable_range_culling: 'Disable a geometry-culling check. This may help games with missing geometry but can affect rendering.',
  skip_gpu_readbacks: 'Skip copying graphics data back to the CPU. This can improve speed but break effects or game logic that needs those copies.',
  lazy_texture_caching: 'Reuse textures more aggressively to reduce processing. Some games may show incorrect text or textures.',
  spline_quality: 'Choose the detail of curved surfaces in games that use splines or Bezier curves.',
  lower_resolution_for_effects: 'Reduce the resolution of selected effects to avoid rendering artifacts. More aggressive settings may reduce detail.',
  gpu_hardware_transform: 'Use the GPU to transform game geometry instead of doing that work on the CPU.',
  software_skinning: 'Combine animated character geometry on the CPU. This can reduce drawing work in many games.',
  hardware_tesselation: 'Use the GPU to generate curved surfaces. Compatibility depends on the renderer and game.',
  texture_scaling_type: 'Choose the algorithm used to enlarge textures when texture upscaling is enabled.',
  texture_scaling_level: 'Enlarge game textures for more detail. Higher levels add CPU work and can cause stutter.',
  texture_deposterize: 'Reduce color banding in upscaled textures.',
  texture_shader: 'Use a Vulkan texture shader to enlarge textures. This overrides the texture upscaling algorithm.',
  texture_anisotropic_filtering: 'Keep textures sharper when viewed at an angle, such as roads stretching into the distance.',
  texture_filtering: 'Choose how texture pixels blend. Nearest keeps hard edges; Linear smooths them; Auto follows the game.',
  smart_2d_texture_filtering: 'Avoid filtering selected 2D textures to reduce unwanted seams and other artifacts.',
  texture_replacement: 'Load installed replacement texture packs for games that have them.',
  enable_wlan: 'Enable emulated PSP networking. This experimental feature may affect game compatibility.',
  wlan_channel: 'Choose the emulated PSP wireless channel. Use the same channel as other players when required.',
  enable_builtin_pro_ad_hoc_server: 'Run PPSSPP’s built-in ad hoc server for compatible multiplayer connections.',
  change_pro_ad_hoc_server_address: 'Choose the multiplayer server. Select IP address to use the individual address digits below.',
  enable_upnp: 'Ask a compatible router to create port mappings for multiplayer connections.',
  upnp_use_original_port: 'Use original PSP port numbers for UPnP mappings, for compatibility with real PSP systems.',
  port_offset: 'Shift multiplayer port numbers by this amount. Zero uses original PSP ports.',
  minimum_timeout: 'Set a minimum network timeout in milliseconds. Zero leaves the game’s timeout unchanged.',
  forced_first_connect: 'Force the initial multiplayer connection to complete sooner. This is a compatibility workaround.'
};
function pspCoreHelp(key) {
  if (key.startsWith('ppsspp_change_mac_address')) return 'Set this hexadecimal digit of the emulated PSP’s network address. Each multiplayer device needs a unique address.';
  if (key.startsWith('ppsspp_pro_ad_hoc_server_address')) return 'Set this digit of the multiplayer server’s IP address. Used only when the server selector is set to IP address.';
  return key.startsWith('ppsspp_') ? pspHelp[key.slice(7)] : '';
}

// Help for options whose core table supplies a label but no explanation.
const otherCoreHelp = {
  pcsx2_bios: 'Name of the PS2 BIOS to use. Available BIOS choices are detected when the core starts; leave empty to use its automatic selection.',
  pcsx2_renderer: 'Choose how PS2 graphics are drawn. Auto lets the core select its renderer; a different backend must be supported by this PS5 build.',
  pcsx2_upscale_multiplier: 'Render 3D graphics at a higher resolution for a sharper image. Higher values use more memory and can slow games down. Restart required.',
  pcsx2_texture_filtering: 'Choose whether textures keep sharp pixel edges or use smoother bilinear filtering. The PS2 option follows the game’s own requests.',
  pcsx2_trilinear_filtering: 'Smooth transitions between texture detail levels. Automatic follows the game; forcing it can change the original appearance.',
  pcsx2_anisotropic_filtering: 'Keep textures clearer when viewed at an angle, such as roads and floors. Higher levels require more graphics work.',
  pcsx2_dithering: 'Reproduce the PS2’s pattern of pixels used to blend limited colors. Scaled adapts the pattern to the internal resolution.',
  pcsx2_blending_accuracy: 'Increase the accuracy of transparency and color blending. Higher accuracy can fix effects but costs performance.',
  pcsx2_pcrtc_screen_offsets: 'Respect the screen position offsets requested by the game. This can correct alignment or borders.',
  pcsx2_disable_interlace_offset: 'Remove the offset used for interlaced fields. Use this workaround only for a game with an interlacing alignment issue.',
  pcsx2_auto_flush_software: 'Flush graphics work automatically in the software renderer. This can fix rendering issues at a performance cost.',
  pcsx2_ee_cycle_rate: 'Change the speed of the emulated PS2 CPU. Underclocking may improve performance but can reduce the game’s frame rate; overclocking adds CPU work.',
  pcsx2_ee_cycle_skip: 'Skip emulated CPU cycles to reduce workload. This speed workaround can cause stutter or incorrect game behavior.',
  pcsx2_cpu_sprite_size: 'Set the size limit for sprites drawn using the CPU. This is a game-specific graphics workaround; zero disables it.',
  pcsx2_cpu_sprite_level: 'Choose which small graphics primitives use CPU sprite rendering. Broader coverage can fix a game but costs more CPU time.',
  pcsx2_software_clut_render: 'Render color palette updates in software. Use this to correct palette effects in games that need the workaround.',
  pcsx2_gpu_target_clut: 'Choose when a graphics render target can supply a color palette. This changes palette handling for compatibility.',
  pcsx2_auto_flush: 'Flush pending drawing before certain texture reads. This can fix effects that reuse the image being drawn, at a performance cost.',
  pcsx2_texture_inside_rt: 'Allow a texture to be read from inside an existing render target. This is a compatibility workaround for games that reuse framebuffer data.',
  pcsx2_disable_depth_conversion: 'Skip conversions involving the depth buffer. It may help specific games but can break depth-dependent effects.',
  pcsx2_framebuffer_conversion: 'Enable an alternate framebuffer conversion path. Use only when a game needs it to correct graphics.',
  pcsx2_disable_partial_invalidation: 'Keep partial texture cache entries instead of invalidating them. This workaround may fix one game while producing stale graphics in another.',
  pcsx2_gpu_palette_conversion: 'Convert indexed color palettes on the GPU. This changes how palette textures are handled and can affect compatibility.',
  pcsx2_preload_frame_data: 'Load existing framebuffer data before drawing. This can fix missing effects but increases graphics work.',
  pcsx2_half_pixel_offset: 'Adjust texture coordinates to correct half-pixel alignment at higher resolutions. Use the mode required by a specific game.',
  pcsx2_round_sprite: 'Round sprite coordinates to reduce gaps or misalignment when upscaling. Stronger modes can alter some graphics.',
  pcsx2_align_sprite: 'Adjust sprite alignment to reduce seams when upscaling. This is a game-specific graphics workaround.',
  pcsx2_merge_sprite: 'Combine adjacent sprites to reduce upscaling seams. Some games require this workaround; others may show incorrect graphics.',
  pcsx2_unscaled_palette_draw: 'Draw palette textures at native resolution to avoid upscaling errors in their colors.',
  pcsx2_force_sprite_position: 'Force sprite positions to even coordinates. Use for games with sprite alignment problems at higher resolutions.',
  mame_thread_mode: 'Run MAME’s emulation on a separate thread. This changes how work is scheduled and may affect performance or compatibility.',
  mame_cheats_enable: 'Enable MAME’s cheat support. Cheat files must be available for the selected game.',
  mame_throttle: 'Let MAME limit emulation speed to the original machine’s speed. Timing also depends on RetroArch’s synchronization settings.',
  mame_boot_to_bios: 'Start the emulated machine’s BIOS instead of launching its software directly, where that machine supports it.',
  mame_boot_to_osd: 'Open MAME’s own on-screen menu at startup.',
  mame_read_config: 'Read MAME configuration files when starting content. Their settings can change how the machine runs.',
  mame_write_config: 'Allow MAME to save its own configuration files for later sessions.',
  mame_mame_paths_enable: 'Use paths from MAME INI files for its resources. Enable only when those paths are configured for your console.',
  mame_saves: 'Choose whether save-state filenames are based on the game or the emulated system.',
  mame_auto_save: 'Have MAME save its state on exit and restore it when the same content starts again.',
  mame_softlists_enable: 'Use MAME software lists to identify software for supported consoles and computers.',
  mame_softlists_auto_media: 'Let the software list choose the appropriate media device, such as a cartridge or floppy drive.',
  mame_media_type: 'Choose which emulated media device receives the selected software.',
  mame_joystick_deadzone: 'Ignore small stick movements near the center to prevent drift. Larger values require more movement before input begins.',
  mame_joystick_saturation: 'Set how far the stick must move to reach full input. Lower values reach maximum input sooner.',
  mame_joystick_threshold: 'Set how far an analog stick must move before it counts as a digital direction.',
  mame_mame_4way_enable: 'Restrict joystick directions for arcade games designed for a four-way stick, avoiding unintended diagonal input.',
  mame_buttons_profiles: 'Use the core’s game-specific button layouts instead of one general layout.',
  mame_mouse_enable: 'Allow mouse input for games that support it.',
  mame_lightgun_mode: 'Choose the input method used for lightgun aiming, or disable lightgun input.',
  mame_lightgun_offscreen_mode: 'Choose how the aim position is reported when the lightgun points outside the screen.',
  mame_rotation_mode: 'Choose whether RetroArch or MAME handles screen rotation for vertical games.',
  mame_alternate_renderer: 'Use MAME’s alternate drawing path. Its output size is controlled by Alternate Renderer Resolution.',
  mame_altres: 'Set the output resolution of the alternate renderer. Larger images require more rendering work.',
  mame_cpu_overclock: 'Change the speed of the emulated main CPU. Higher values may reduce original arcade slowdowns but increase workload and can affect timing.',
  mame_cpu_sound_overclock: 'Change the speed of the emulated sound CPU. Keep the default unless a game needs a timing workaround.',
  mame_coin_limit: 'Limit the number of coins that can be inserted. Zero disables the limit.',
  snes9x_gfx_transp: 'Draw the SNES transparency and color blending effects. Turning this off changes the intended appearance.',
  vice_cartridge: 'Select a cartridge image from the VICE system folder. Installed cartridge choices are detected when the core starts.',
  vice_video_options_display: 'Show the extra video options in RetroArch’s on-console core menu. The WebUI groups them under Video.',
  vice_audio_options_display: 'Show the extra audio options in RetroArch’s on-console core menu. The WebUI groups them under Audio.',
  vice_mapping_options_display: 'Show the extra button mapping options in RetroArch’s on-console core menu. They remain grouped in the WebUI.'
};
function coreHelp(key) {
  if (/^pcsx2_axis_deadzone[12]$/.test(key)) return 'Ignore small stick movements near the center to prevent drift. Larger values require more movement before input begins.';
  if (/^pcsx2_button_deadzone[12]$/.test(key)) return 'Ignore light trigger pressure below this threshold. Increase it if a trigger activates unintentionally.';
  if (/^pcsx2_axis_scale[12]$/.test(key)) return 'Adjust analog stick sensitivity. Higher values reach full input with less stick movement.';
  if (/^pcsx2_invert_(left|right)_stick[12]$/.test(key)) return 'Reverse the selected horizontal or vertical stick directions.';
  if (/^pcsx2_enable_rumble[12]$/.test(key)) return 'Set controller vibration strength for this PS2 controller port. Off disables vibration.';
  if (/^snes9x_layer_[1-5]$/.test(key)) return 'Show this SNES graphics layer. Turning it off hides part of the picture and is mainly useful for inspecting graphics.';
  if (/^snes9x_sndchan_volume_[1-8]$/.test(key)) return 'Adjust the volume of this individual SNES sound channel. Zero mutes it; 100 keeps its original level.';
  if (key.startsWith('vice_mapper_')) return 'Choose the Commodore key or emulator action assigned to this control. The unmapped choice removes its assignment.';
  return otherCoreHelp[key] || pspCoreHelp(key);
}

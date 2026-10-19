# Moonvibe Switch: low latency + FSR

The 2026-10-12 test build also includes Npad gyro routing and host-requested
motion compatibility for VibeShine VHF. A follow-up adds an optional
**Switch Pro gyro mode** toggle to compensate motion axes for the
2.0 VHF Pro encoder. Enable it with `vhf_switch`; it defaults off for other
controller profiles. See [gyro details](gyro-vhf.vi.md).

Based on nyanpasu64/Moonlight-Switch `frame-rate-sync` at
`47ebf567a8f5ad111c4cf59ef6d05edad7ef3888`, derived from
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch).

The frame queue and alpha-beta pacing algorithm from
[issue #323](https://github.com/XITRIX/Moonlight-Switch/issues/323) are preserved.
This is not Artemis's latest-frame-wins pacing implementation.

## Session and input changes (2026-10-15)

- **Resume parameters.** `/resume` now sends the same query as `/launch`
  (`mode`, `sops`, audio, controller bitmaps), as moonlight-qt does. Without
  `mode`, Vibeshine's `make_launch_session()` leaves width/height unset and its
  virtual display falls back to an unrelated resolution on reconnect.
- **HOME keeps the stream.** frame-rate-sync ended the session on applet focus
  loss. Now focus loss releases held input and pauses sending until focus
  returns (plus a 300 ms lock so the button that closed HOME is not
  forwarded). Settings > *End stream when opening HOME menu* restores the old
  behaviour.
- **Controller poller.** Gamepad state is read with independent `PadState`
  objects (`SwitchPadReader`, mirroring Borealis' slot and button mapping)
  and sent from a worker every 4 ms instead of once per UI frame. The UI loop
  renews a 100 ms lease each frame it would have handled input, so the poller
  stops in the overlay, HOME menu and input locks. A mutex covers send state
  shared with `dropInput()`/`resetSession()`. Touch, mouse and keyboard stay
  on the UI loop. Toggle: Settings > *High-rate controller polling*.
- **Stick deadzone.** The radial deadzone now rescales the remaining travel to
  0..1 instead of passing raw values past a hard cutoff, and clamps magnitude
  to 1.

### Pacing modes and audio (2026-10-15, second build)

Moonlight Android's frame pacing keeps at most two frames and, in latency
mode, shows only the newest one. The alpha-beta queue instead holds a frame
until its estimated arrival + 6 ms + 1.5 x jitter, with one reserve frame, to
smooth cadence at the cost of latency.

- **Lowest latency frame pacing** (Settings and in-stream overlay, default
  off): `AVFrameQueue::popNewestLocked()` presents the newest decoded frame at
  every draw, discards older ones and skips the startup reserve and jitter
  hold. If no new frame arrived, the previous one repeats. The alpha-beta path
  is untouched and remains the default; the mode can be switched live.
- **Frame age stat.** The debug overlay now shows the average and maximum age
  of the presented frame (decode complete to hand-off to the renderer), for
  both modes. It excludes render time and scanout.
- **Low-latency audio buffer** (Settings, default off, next stream): 1024
  SDL samples (~21 ms) instead of 4096 (~85 ms), and a 12 000 byte overflow
  threshold instead of 24 000. Disabled by default because the code notes the
  Switch SDL2 port broke audio with small buffers.

### Stick response (2026-10-15, third build)

Per stick, applied in `applyStickResponse()` ([InputManager.cpp](../app/src/streaming/InputManager.cpp))
in this order: dead zone, outer limit, curve, anti-dead zone.

- **Anti-dead zone** (0-50%): output floor once the stick leaves the dead zone,
  to cancel the game's own dead zone. Forces a 3% minimum dead zone so centre
  noise cannot drift the camera.
- **Outer limit** (70-100%): travel that counts as fully pushed. Joy-Con
  sticks often do not reach 1.0 raw; travel is stretched over
  deadzone..outer limit.
- **Response curve** (0-100%): output = travel^(1 + 2 x strength), so 0 is
  linear and 100 is cubic. Gentle near the centre, faster towards the rim.
- **Joy-Con only** (default on): the curve and outer limit are skipped for
  Pro Controller and other full-size pads (`SwitchPadReader::hasSmallSticks`
  checks the Npad style: handheld/Joy-Con vs full key).
- **Peak travel** in Debug info: largest raw travel on player 1 since Debug
  info was switched on, to choose an outer limit from real data.
- Settings menu regrouped (audio, latency and session, keys, dead zones,
  stick response, rumble, gyro, single Joy-Con).

### Gyro gain, zero drift and measurement (2026-10-19)

Units match Moonlight Android: gyro is sent in deg/s (HOS value x 360, same as
the XITRIX/Borealis 2pi x 57.3 path) and accel in m/s^2. Android sends every
new sensor reading; Moonvibe sends the latest reading at the host-requested
rate. Neither applies gain.

- **Gyro gain** (0.5x-3.0x, Settings and overlay, live): multiplies gyro only,
  before the optional VHF Pro axis encoding. `applyGyroGain()` in
  [MotionPolicy.hpp](../app/src/streaming/switch/MotionPolicy.hpp).
- **Zero drift mode** (Loose/Standard/Tight, default Standard which is the HOS
  reset value): `hidSetGyroscopeZeroDriftMode` on every started sensor and
  re-applied live. What each mode does is not documented by libnx/switchbrew.
- **Angle measurement** in Debug info: player 1 gyro (after gain) integrated by
  wall clock over each fresh sample, on SDL axes (pitch/yaw/roll), plus peak
  deg/s. Reset when Debug info is switched on.
- **Joy-Con pair pointer mode** (Settings and overlay, default off, live):
  only for a detached Joy-Con pair (`PadKind::JoyDual`), using the Joy-Con
  chosen by "Detached Joy-Con gyro". `pointerGyro()` keeps local pitch, takes
  yaw as the rotation about world vertical from a low-passed gravity vector
  (player-space style, relaxed by 1.41 and capped by local yaw/roll), and
  drops roll. Handheld, Pro and single Joy-Con are unchanged.
- Native tests (`tests/motion`) cover gain clamping and live changes, drift
  mode application, the integrated angle, and pointer mode (tilt 0-80 degrees,
  roll dropped, live toggle, Pro unaffected).

None of these have been verified on hardware yet.

## Docked presentation fix

The Switch deko3d path now passes `Application::windowWidth/windowHeight` to
the video renderer, following the physical-output sizing used by
[Artemis-Switch](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/app/src/streaming_view.cpp).

Borealis View coordinates can remain 1280x720 on a 1920x1080 framebuffer.
Using them for the filtered present viewport limits video to a corner of the
screen, and can also make the renderer incorrectly decide that 720p input
does not need upscaling. The renderer now receives physical output dimensions;
its existing resize handling rebuilds targets and commands when those change.
The output size is detected rather than hardcoded to 1080p.

Existing FSR1/EASU, RCAS and dithering remain available. EASU runs when enabled
and the physical output is larger than the decoded frame. Other renderers keep
their existing coordinate behavior. Tracy profiling defaults to off; explicitly
enable it when collecting traces.

## Build

Use the Nintendo Switch devkitPro toolchain and dependencies from the original
project. Initialize submodules, including Tracy even when profiling is disabled:

```sh
git submodule update --init --recursive
cmake -S . -B build/switch -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON \
  -DENABLE_UPSCALING=ON -DTRACY_ENABLE=OFF
cmake --build build/switch --target Moonlight.nro --parallel
```

The NRO keeps the original application identity and settings location for
compatibility. To test FSR, request a 1280x720 stream, use a 1920x1080 docked
output, and enable Upscaling in Settings or the in-game overlay. Fullscreen
alone does not prove EASU is active.

The local build environment uses `devkitpro/devkita64` pinned to Docker digest
`sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`:
devkitA64 r29.2 (GCC 15.2.0), libnx 4.12.0, deko3d 0.5.0, uam 1.1.0,
Switch FFmpeg 7.1-5, and CMake 3.31.6. As in upstream CI, remove the image's
`switch-enet` package inside the disposable build container before configuring;
the project builds its own pinned ENet submodule instead.

## Device validation

Compilation cannot verify display coverage or input-to-photon latency.
Compare against the original `frame-rate-sync` NRO with the same host, codec,
resolution, FPS, bitrate, console clocks, network and TV picture mode.

- Docked 1080p output / 720p stream: filters off, RCAS only, FSR + RCAS.
  All should cover the full 16:9 output without a 720p corner or black remainder.
- Handheld 720p output / 720p stream: no unnecessary EASU pass.
- Docked 1080p output / 1080p stream: full coverage with sharpening on/off.
- Dock/undock during streaming, and toggle filters in the overlay: no stale
  output target, crash or persistent black area.
- H.264 and HEVC SDR 8-bit: compare smoothness with otherwise identical settings.
- Inspect queue drops/repeats and GPU frame time with FSR on/off. Use high-speed
  video of an input event and its screen response to compare end-to-end latency.

The Release `Moonlight.nro` target built successfully with FSR enabled and
Tracy disabled in the pinned environment above. Frame queue source and EASU
shader source are unchanged from the latency baseline. The packaged NRO is
available as `dist/Moonvibe-Switch.nro` and in `dist/Moonvibe-Switch-SD.zip`.

Display coverage, HEVC stutter and end-to-end latency have not been verified
on hardware.

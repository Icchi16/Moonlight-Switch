#!/usr/bin/env bash
set -euo pipefail

# Run in a disposable devkitpro/devkita64 container, with the repository at /src.
# Its system ENet conflicts with the pinned in-tree ENet implementation.
if pacman -Q switch-enet >/dev/null 2>&1; then
    pacman -R --noconfirm switch-enet
fi

g++ -std=c++17 -Wall -Wextra -Werror -pthread -D__SWITCH__ \
    -Itests/motion/stubs -Iextern/moonlight-common-c/src \
    tests/motion/motion_test.cpp app/src/streaming/switch/SwitchMotion.cpp \
    -o /tmp/moonvibe-motion-test
/tmp/moonvibe-motion-test

# CMake regenerates Borealis' UI binaries during configure. Preserve the
# prepared assets so an SDK compiler difference cannot change this gyro build.
motion_shader_backup=$(mktemp -d /tmp/moonvibe-ui-shaders.XXXXXX)
cp resources/shaders/fill_vsh.dksh resources/shaders/fill_aa_fsh.dksh \
    resources/shaders/fill_fsh.dksh "$motion_shader_backup/"
restore_motion_ui_shaders() {
    cp "$motion_shader_backup/fill_vsh.dksh" "$motion_shader_backup/fill_aa_fsh.dksh" \
        "$motion_shader_backup/fill_fsh.dksh" resources/shaders/
}
trap 'restore_motion_ui_shaders; rm -f "$motion_shader_backup/fill_vsh.dksh" "$motion_shader_backup/fill_aa_fsh.dksh" "$motion_shader_backup/fill_fsh.dksh"; rmdir "$motion_shader_backup"' EXIT

cmake -S . -B build/switch -DCMAKE_BUILD_TYPE=Release \
    -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON \
    -DENABLE_UPSCALING=ON -DTRACY_ENABLE=OFF
restore_motion_ui_shaders
cmake --build build/switch --target Moonlight.nro --parallel 4

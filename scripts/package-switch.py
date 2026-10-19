"""Package an already-successful Switch build, preserving the last test NRO."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
BUILD = ROOT / "build" / "switch"


def git(*args, expected=(0,)):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True)
    if result.returncode not in expected:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


data = (BUILD / "Moonlight.nro").read_bytes()
assert data[0x10:0x14] == b"NRO0", "Invalid NRO header"
asset_base = struct.unpack_from("<I", data, 0x18)[0]
assert data[asset_base:asset_base + 4] == b"ASET", "Missing embedded assets"
icon_offset, icon_length = struct.unpack_from("<QQ", data, asset_base + 8)
icon = data[asset_base + icon_offset:asset_base + icon_offset + icon_length]
assert icon == (ROOT / "resources/img/moonlight_icon.jpg").read_bytes(), "Unexpected NRO icon"
assert (BUILD / "moonvibe-motion/InputStream.c").exists(), "Missing motion compatibility override"
assert b"MoonvibeHostRequestedMotion" in (BUILD / "moonvibe-motion/InputStream.c").read_bytes()
assert b"    handleControllerSensors();" not in (BUILD / "moonvibe-motion/switch_input.cpp").read_bytes()
git("diff", "--check")
for dependency in ("extern/borealis", "extern/moonlight-common-c"):
    assert not git("-C", dependency, "status", "--porcelain"), f"Dirty dependency: {dependency}"
# The alpha-beta path in AVFrameHolder is unchanged; the lowest-latency mode and
# frame-age stats were added there on purpose. Shader sources must stay identical.
assert not git("diff", "--name-only", "HEAD", "--", "app/src/streaming/video/deko3d"), "Shaders changed"
assert not git("diff", "--name-only", "HEAD", "--", "resources/shaders"), "Prepared shader binaries changed"

DIST.mkdir(exist_ok=True)
current = DIST / "Moonvibe-Switch.nro"
previous = DIST / "Moonvibe-Switch-before-gyro.nro"
if current.exists() and not previous.exists():
    shutil.copyfile(current, previous)
previous_axes = DIST / "Moonvibe-Switch-before-vhf-pro-axes.nro"
if current.exists() and not previous_axes.exists():
    shutil.copyfile(current, previous_axes)
previous_input = DIST / "Moonvibe-Switch-before-pacing-modes.nro"
if current.exists() and not previous_input.exists():
    shutil.copyfile(current, previous_input)
current.write_bytes(data)
shutil.copyfile(ROOT / "resources/img/moonlight_icon.jpg", DIST / "nro-icon.jpg")
digest = hashlib.sha256(data).hexdigest().upper()

readme = """Moonvibe Switch — bản thử low latency + FSR + gyro Npad/VibeShine VHF
Build: 15/10/2026

Mới trong bản 15/10:
- Resume gửi đủ resolution/FPS/audio như launch (sửa lỗi Vibeshine reset
  resolution khi vào lại stream đang chạy).
- Bấm HOME không còn ngắt stream; input được nhả và tạm dừng đến khi quay lại.
  Settings > "End stream when opening HOME menu" để dùng lại hành vi cũ.
- Tay cầm được đọc/gửi ở thread riêng ~250 Hz thay vì 60 Hz theo khung hình.
  Settings > "High-rate controller polling" (mặc định bật) để so sánh A/B.
- Deadzone stick co giãn lại (không còn nhảy cóc ở mép deadzone).
- Settings/overlay > "Lowest latency frame pacing" (mặc định tắt): luôn hiện
  frame mới nhất, bỏ frame cũ, không giữ đệm alpha-beta (giống chế độ latency
  của Moonlight Android; có thể giật hơn). Đổi được ngay trong lúc stream.
- Settings > "Low-latency audio buffer" (mặc định tắt, áp dụng ở lần stream
  sau): buffer 1024 mẫu thay vì 4096. Tắt đi nếu âm thanh bị rè/ngắt.
- Debug info (overlay) có thêm "frame age at present avg | max" để so sánh số ms.
- Stick (Settings > Stick dead zones / Stick response), mỗi stick một mức:
  Anti-dead zone (sàn output sau deadzone, bù deadzone của game; tự dùng
  deadzone tối thiểu 3%), Response curve (0-100%, 0 = tuyến tính; output =
  độ gạt^(1+2*curve): nhẹ gần tâm, nhanh dần về mép), Outer limit (70-100%:
  độ gạt coi là full, cho Joy-Con không chạm 1.0).
  "Apply curve and outer limit to Joy-Con only" (mặc định bật) giữ Pro/tay
  3rd party tuyến tính. Thứ tự: deadzone > outer limit > curve > anti-deadzone.
- Debug info hiện "Stick peak travel L | R" (đỉnh độ gạt thô của người chơi 1,
  reset khi bật Debug info): xoay mỗi stick một vòng đầy để biết Joy-Con đạt
  bao nhiêu rồi đặt Outer limit gần mức đó.
- Gyro (Settings và overlay, nhóm Gyro): "Gyro gain" 0.5x-3.0x (mặc định 1.0x,
  chỉ nhân gyro, không đổi accel, đổi ngay trong stream) và "Gyro zero drift
  correction (HOS)" Loose/Standard/Tight (mặc định Standard như hệ thống).
- "Joy-Con pair pointer mode (point to aim)" (Settings và overlay, mặc định
  tắt): chỉ cho cặp Joy-Con tháo rời. Chĩa Joy-Con (đã chọn ở Detached Joy-Con
  gyro) sang trái/phải là quay ngang theo trục thẳng đứng thật (dùng trọng lực),
  dù cầm nghiêng; gật lên/xuống là ngắm dọc; vặn cổ tay (roll) bị bỏ. Handheld,
  Pro và Joy-Con đơn không đổi.
- Debug info có dòng "Gyro P1 angle pitch | yaw | roll": góc cộng dồn từ gyro
  đã gửi (sau gain), reset khi bật Debug info. Xoay tay cầm đúng 90 độ quanh
  một trục: số ~90 nghĩa là client gửi đúng; phần chênh còn lại nằm ở host/game.
- Menu Settings được xếp lại theo nhóm: Stream settings (audio), Latency and
  session, Controller keys, Stick dead zones, Stick response, Rumble, Gyro,
  Single Joy-Con.
Bản trước giữ ở Moonvibe-Switch-before-pacing-modes.nro (và -before-input-fixes.nro) để so sánh.

Cài đặt: giải nén ZIP vào gốc thẻ SD, hoặc chép NRO vào:
sdmc:/switch/Moonvibe-Switch/Moonvibe-Switch.nro
Mở qua hbmenu bằng Title Redirection (giữ R khi mở một game).
Cấu hình dùng thư mục gốc sdmc:/switch/Moonlight-Switch.

Trong Settings/overlay:
- Gyro: bật (mặc định).
- Detached Joy-Con gyro: chọn Left/Right (mặc định Right).
- Switch Pro gyro mode: BẬT khi dùng VibeShine 2.0 vhf_switch.
  Mặc định tắt. Bù trục cho cả gyro và accel, đổi ngay trong stream.
  Tắt khi dùng DS4/DualSense hoặc khi driver Pro đã sửa hệ trục.
- Gyro source (Player 1): Console, Left/Right Joy-Con hoặc Pro Controller.
Console dùng Handheld Npad hiện có; chưa dùng SevenSixAxis.
Một Joy-Con thì tự dùng tay đó. Các nguồn đi theo đúng slot nút bấm.
Joy-Con đơn dùng hướng cầm ngang theo mapping nút của ứng dụng.

Host: giữ driver VHF mới của VibeShine 2.0.0; chọn vhf_switch để thử Pro ảo.
Phần mềm/game trên PC phải hỗ trợ bật và đọc IMU của Switch Pro.
Client chỉ gửi gyro/accel khi host yêu cầu; không giả tính năng touchpad.

Thử trước với H.264 SDR, 720p/60 FPS; TV output 1080p, Upscaling (FSR1) bật.
Kiểm tra full màn khi FSR bật/tắt, rồi thử Console, Left/Right, một Joy-Con,
Pro, gắn/tháo và disconnect/reconnect. So sánh latency với bản nền.
Với compatibility bật: xoay trái/phải phải aim trái/phải, ngẩng/cúi phải
aim lên/xuống. Thử từng trục, không đổi cấu hình aim PC giữa hai lần so sánh.
Để xem log Motion:, bật Debugging View trước khi stream hoặc dùng nxlink.

Đã qua native tests giả lập HID/host và build Release cho Switch.
Người dùng đã xác nhận bản trước truyền được gyro nhưng sai hướng ở ba mode.
Bản bù trục này chưa kiểm thử trên OLED/host thật; cần xác nhận chiều aim.
Low-latency fork frame-rate-sync và logo trắng trên nền xám được giữ.
Chi tiết nguồn/build: docs/gyro-vhf.vi.md trong project.
"""
(DIST / "README.vi.txt").write_text(readme, encoding="utf-8")
info = f"""Build date: 2026-10-15 (Asia/Bangkok)
Baseline: nyanpasu64/Moonlight-Switch frame-rate-sync
Baseline commit: 47ebf567a8f5ad111c4cf59ef6d05edad7ef3888
Docker image: devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
Toolchain: devkitA64 r29.2 / GCC 15.2.0; CMake 3.31.6
Libraries: libnx 4.12.0; deko3d 0.5.0; uam 1.1.0; switch-ffmpeg 7.1-5
Build: bash scripts/build-switch.sh (native motion tests + Release Moonlight.nro)
NRO SHA256: {digest}
Verified: routing/host-request/stale/invalid/pause/reconnect native tests pass.
Verified: VHF Pro inverse axes, both sensors/all sources/signed axes/live toggles.
Verified: NRO0, ASET and embedded launcher icon.
Verified: SD ZIP NRO exactly matches standalone NRO.
Verified: video queue and deko3d shader source unchanged from baseline.
Verified: dependency checkouts pristine; source overrides retained in project CMake.
SevenSixAxis: not used. Console means Handheld Npad.
VHF Pro compatibility: optional, defaults off; axis-only compensation (no gain change).
Input: resume sends launch params; HOME keeps stream (setting); 250 Hz controller poller (setting); scaled radial deadzone.
Pacing: optional lowest-latency mode (newest frame); alpha-beta path unchanged and default; frame-age stat; optional 1024-sample audio buffer.
Sticks: anti-dead zone, response curve, outer limit (Joy-Con-only option), peak-travel debug line; settings menu regrouped.
Gyro: gain 0.5-3.0x (gyro only), HOS zero drift mode option, integrated-angle debug line, Joy-Con pair pointer mode (gravity-based yaw, roll dropped); native tests cover gain/drift/measurement/pointer.
Not verified: hardware gyro/VHF end-to-end, axes, sample rate, latency, display coverage, HEVC,
  Vibeshine resume resolution, HOME resume, poller latency gain, lowest-latency pacing, low-latency audio, stick curve/outer limit feel, gyro gain/zero drift/pointer mode on hardware.
"""
(DIST / "build-info.txt").write_text(info, encoding="utf-8")

# Include existing tracked edits and untracked project files, without staging.
patch = git("diff", "--binary", "HEAD")
for raw_path in git("ls-files", "--others", "--exclude-standard", "-z").split(b"\0"):
    if raw_path:
        path = raw_path.decode("utf-8")
        patch += git("diff", "--no-index", "--binary", "--", "/dev/null", path, expected=(0, 1))
(DIST / "Moonvibe-Switch.patch").write_bytes(patch)

sd = DIST / "sd"
nro_sd = sd / "switch/Moonvibe-Switch/Moonvibe-Switch.nro"
nro_sd.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(current, nro_sd)
(sd / "README-Moonvibe.vi.txt").write_text(readme, encoding="utf-8")
shutil.copyfile(ROOT / "LICENSE", sd / "LICENSE.txt")
with zipfile.ZipFile(DIST / "Moonvibe-Switch-SD.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for file in sorted(sd.rglob("*")):
        if file.is_file():
            archive.write(file, file.relative_to(sd).as_posix())
with zipfile.ZipFile(DIST / "Moonvibe-Switch-SD.zip") as archive:
    assert archive.read("switch/Moonvibe-Switch/Moonvibe-Switch.nro") == data
print(f"Packaged {len(data):,} bytes; NRO SHA256 {digest}")

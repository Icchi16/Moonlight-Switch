# Bản thử gyro Npad + VibeShine VHF — 12/10/2026

## Bản bù trục VHF Pro

Người dùng đã xác nhận bản đầu có gyro nhưng sai hướng giống nhau ở Console,
cặp Joy-Con rời và Pro. Đợt sửa tiếp theo thêm **Switch Pro gyro mode**
trong Settings và overlay. Khi host VibeShine 2.0 dùng `vhf_switch`, bật tùy
chọn này. Mặc định tắt; tắt khi dùng DS4/DualSense hoặc driver Pro đã đổi trục đúng.

Tùy chọn được lưu và đổi ngay trong stream; áp dụng cùng cho gyro/accel,
mọi nguồn và slot, sau chuẩn hóa Npad. Phép bù SDL→raw Pro là `{-Z,-X,Y}`;
không đổi độ nhạy. Khi chuyển chế độ gửi gyro zero rồi cập nhật hệ trục.
Không tự suy profile VHF từ kiểu tay cầm vật lý hoặc cờ touchpad của host.

Kiểm thử bổ sung: round-trip qua phép đọc Pro của SDL ở ba trục có dấu,
gyro/accel mọi nguồn, worker gửi các trục có dấu, bật/tắt trong stream,
stale và reconnect. Ngày 12/10/2026, người dùng xác nhận bản bù đã đúng hướng
ở handheld trên máy thật. Joy-Con rời và Pro chưa được thử lại với bản bù.
Tiếp tục thử Pro và từng bên Joy-Con với cấu hình PC giữ nguyên:
xoay trái/phải → aim trái/phải; ngẩng/cúi → aim lên/xuống; đứng yên → aim dừng.
Nếu sai, ghi rõ nguồn và chiều từng động tác khi tùy chọn bật/tắt.
Xem [phân tích hệ trục](gyro-axis-audit.vi.md) để đối chiếu nguồn.

## Cách dùng

- Bật **Gyro** trong Settings hoặc overlay khi đang stream.
- **Detached Joy-Con gyro** chọn Left/Right; mặc định Right, lưu qua lần mở ứng dụng.
- **Gyro source (Player 1)** hiển thị nguồn đang chọn: Console, Left Joy-Con,
  Right Joy-Con hoặc Pro Controller. Tên Console được dùng theo yêu cầu người dùng;
  đường đọc bên dưới là Handheld Npad qua SixAxis, không dùng SevenSixAxis.
- Một Joy-Con trong kiểu đơn hoặc một bên còn kết nối của cặp rời: tự dùng tay đó,
  bất kể lựa chọn lưu trước đó. Joy-Con đơn giữ hướng cầm ngang theo mapping nút
  của ứng dụng; trục motion xoay tương ứng.
- Nguồn motion theo đúng slot nút bấm của Borealis. Handheld được xếp trước,
  rồi các Npad đánh số của hệ thống. Khi có nhiều tay cầm, gyro đi vào cùng
  controller ảo nhận nút của tay đó; không lấy gyro Pro ghép vào nút Joy-Con.
  Ô nguồn trên giao diện hiển thị riêng người chơi 1.
- Handheld đang hoạt động vẫn dùng Console, kể cả trạng thái chuyển tiếp khi
  chỉ còn một Joy-Con gắn và hệ thống vẫn báo Handheld Npad.

Host Windows: giữ driver VHF mới, chọn `vhf_switch` trong VibeShine 2.0.0 để
thử trực tiếp Nintendo Pro Controller ảo. `vhf` tự động cũng nhận loại Nintendo
được client khai báo. Game/emulator/Steam Input cần hỗ trợ bật và đọc IMU Switch
Pro. ViGEmBus + DS4 là trường hợp đối chiếu, không phải đường sử dụng chính.

## Những thay đổi trong client

- Backend motion riêng dùng API SixAxis/Npad, lấy mẫu trên worker độc lập với
  render 30/60 FPS. Chỉ mở sensor khi host yêu cầu, ứng dụng cho phép gyro và
  input không bị pause. Tần suất tối đa 200 Hz; 100 Hz của VibeShine dùng lịch 10 ms.
- Chọn đúng handle theo style và bên Joy-Con; lấy accel/gyro từ cùng một mẫu.
  Chỉ gửi mẫu mới, có cờ kết nối và các giá trị hữu hạn. Khi dữ liệu ngừng mới
  quá 250 ms hoặc mất nguồn, kết thúc gyro cũ thay vì tiếp tục quay trên host.
- Callback `setMotionEventState` điều khiển tốc độ/bật tắt của từng loại motion,
  từng controller. Overlay/focus loss/disconnect ngừng đọc; reconnect reset yêu
  cầu của phiên cũ. Không thêm smoothing hoặc gate fallback 500 ms.
- Gyro chuyển từ vòng/s sang deg/s; accel từ g sang m/s². Giữ phép đổi trục Npad
  của nền upstream, bổ sung phép xoay cho Joy-Con đơn cầm ngang. Cần kiểm tra
  chiều trục trên máy thật, đặc biệt Joy-Con đơn.
- Khai báo Nintendo và capability mỗi phiên, khi số/style controller thay đổi,
  độc lập với việc nút có đổi hay chưa. Sau arrival luôn gửi lại nút đang giữ.
- Bản vá motion-only cho phép host họ Sunshine gửi motion khi touchpad flag
  tắt **nếu host đã gửi yêu cầu bật đúng loại motion cho slot đó**. Không giả
  cờ touchpad và không mở vô điều kiện cho mọi host. Bản vá nằm trong CMake
  của project; build sinh nguồn override, không sửa checkout submodule.
- Đường đọc motion Borealis cũ được bỏ khỏi vòng chạy trong build Switch để
  tránh hai bộ đọc/sender. Các nền tảng khác giữ đường motion cũ.

Log `Motion:` ghi cờ host, yêu cầu tần suất, nguồn/slot, lỗi mở sensor và lỗi gửi.
Dùng Debugging View bật trước khi stream hoặc console nxlink để quan sát. Chưa
thêm cơ chế lưu log motion riêng ra SD; getter log.log có sẵn của nền upstream
không phải bằng chứng rằng log motion đã được ghi vào file.

## Kiểm chứng và giới hạn

Native test dùng HID/host giả lập kiểm tra lựa chọn nguồn, rớt một bên Joy-Con,
Pro/Console, đơn vị/trục, yêu cầu host, stale/invalid sample, pause và reconnect.
Nó không mô phỏng đầy đủ Horizon OS, radio hoặc driver VHF của máy người dùng.

Việc build thành công không xác nhận gyro end-to-end, chiều trục hay latency
trên OLED. Cần thử:

1. Hai Joy-Con gắn: nguồn Console; gyro/accel tới Pro ảo.
2. Hai Joy-Con rời: đặt một tay yên, xoay tay kia; đổi Left/Right trong overlay.
3. Chỉ một Joy-Con, rồi Pro: kiểm tra nguồn và cả ba chiều xoay.
4. Disconnect/reconnect cùng số tay cầm; gắn/tháo và pause/resume.
5. `vhf_switch` và `vhf` tự động; dùng phần mềm PC đọc được IMU Pro ảo.
6. Đối chiếu DS4/ViGEmBus; so sánh 30/60 FPS và FSR off/on với bản nền.

Low-latency `frame-rate-sync`, FSR1/RCAS, kích thước output thật và logo được giữ.
SevenSixAxis, IMU thân máy độc lập và sửa HEVC không thuộc đợt này.

## Build và đóng gói

Trong image devkitPro đã ghim, mount project vào `/src` và chạy
`bash scripts/build-switch.sh`. Script chạy native tests rồi build Release NRO.
Sau khi build thành công, chạy `python scripts/package-switch.py` để tạo artifact
trong `dist/`, kèm README, thông tin build và patch gồm cả file mới của project.

Nguồn quy ước motion tham khảo:
[Moonlight API](https://github.com/nyanpasu64/moonlight-common-c/blob/318df5d6a3caead19563919141541f0cc7afbf40/src/Limelight.h),
[SDL xử lý Joy-Con ngang](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c).

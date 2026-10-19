# Kiểm tra trục gyro và calibration — 12/10/2026

## Kết quả từ thiết bị

Người dùng xác nhận gyro đã hoạt động. Cùng hiện tượng ở cả Console handheld,
cặp Joy-Con rời và Pro Controller: xoay trái/phải trên mặt bàn làm aim lên/xuống;
ngẩng/cúi làm aim trái/phải. Vì nhiều nguồn cùng bị, chưa có căn cứ sửa riêng
Joy-Con đơn hoặc calibration riêng từng sensor. Chưa biết ứng dụng trên PC
đang chuyển IMU thành aim và cấu hình gyro local/world space của ứng dụng đó.

## Đối chiếu hai fork

Đã kiểm tra HEAD trên GitHub: vẫn trùng hai snapshot local đã nghiên cứu.

| Fork | Commit | Calibration gyro riêng của ứng dụng |
| --- | --- | --- |
| nyanpasu64 `frame-rate-sync` | `47ebf567a8f5ad111c4cf59ef6d05edad7ef3888` | Không thấy: giữ Borealis Npad, chuyển rad/s sang deg/s trước khi gửi |
| Artemis-Switch | `33e6854781a47e86ae349e0a7646ff00bd924ddd` | Không thấy: patch Borealis chỉ đánh dấu nguồn handheld; wrapper ghi diagnostics, kiểm tra quyền gửi/fallback rồi chuyển nguyên x/y/z |

Đã tìm các đường calibration, gyro bias, zero drift, sensor fusion và gửi
motion trong app/patches. Kết luận chỉ nói về xử lý do hai ứng dụng thêm;
không có nghĩa dữ liệu Npad của Horizon hay firmware tay cầm là dữ liệu raw
chưa được hệ thống xử lý. Calibration offset/gain không thay cho đổi hệ trục.

Nguồn:
- [Input fork latency](https://github.com/nyanpasu64/Moonlight-Switch/blob/47ebf567a8f5ad111c4cf59ef6d05edad7ef3888/app/src/streaming/InputManager.cpp)
- [Borealis đã ghim](https://github.com/XITRIX/borealis/blob/8c1f1093ee41a461d9d3bdd6dd2f6dfec166ab7a/library/lib/platforms/switch/switch_input.cpp)
- [Patch handheld Artemis](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/patches/borealis-handheld-motion-source.patch)
- [Wrapper Artemis](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/app/src/streaming/InputManager.cpp)

## Mâu thuẫn hệ trục trên đường VHF Pro

Moonlight API yêu cầu x/y/z theo SDL, gyro deg/s và accel m/s².
Moonvibe đang giữ phép đổi trục cơ sở của Borealis, đổi đơn vị Npad và thêm
xoay Joy-Con đơn ngang. Lỗi xuất hiện ở handheld/Pro (không qua xoay đơn)
nên không thể quy toàn bộ lỗi cho bước xoay đơn đó.

VibeShine 2.0.0 `vhf_gamepad_t::motion()` chỉ đổi float sang milli-units;
không chuyển hệ trục theo profile. Driver được ghim
`4b56fb9da177f320fb2d7ddb1b6262e5d55d2750`, hàm `apply_switch_motion()` chép
trực tiếp request x/y/z vào các trục raw Nintendo. Kiểm tra `main` hiện tại
của driver cũng còn cách chép đó.

SDL đọc IMU Pro raw và chuyển thành `{ -rawY, rawZ, -rawX }` (bỏ qua hệ số).
Do đó, nếu đầu vào driver đã là SDL `{ X, Y, Z }`, đường này trả thành
`{ -Y, Z, -X }`, không trả lại cùng trục. Đây là mâu thuẫn có thể chứng minh
bằng mã nguồn, phù hợp với báo cáo nhiều nguồn cùng bị sai trục. Chưa khẳng
định ứng dụng aim của người dùng sử dụng đúng parser SDL này.

Phép nghịch đảo để encoder Pro giữ nguyên hệ SDL ở đầu đọc là:

```
SDL -> raw Pro: { -Z, -X, Y }
raw Pro -> SDL: { -rawY, rawZ, -rawX }
round-trip:    { X, Y, Z }
```

Cần áp dụng nhất quán cho gyro và accel nếu sửa encoder/compatibility,
vì ứng dụng aim có thể dùng trọng lực để tính world-space gyro.

Nguồn:
- [Moonlight motion API](https://github.com/nyanpasu64/moonlight-common-c/blob/318df5d6a3caead19563919141541f0cc7afbf40/src/Limelight.h)
- [VibeShine 2.0 VHF motion](https://github.com/Nonary/vibeshine/blob/2.0.0/src/platform/windows/vhf_gamepad.cpp)
- [Driver Pro đã ghim](https://github.com/Nonary/libvirtualgamepad/blob/4b56fb9da177f320fb2d7ddb1b6262e5d55d2750/driver/src/switch_pro.cpp)
- [SDL parser IMU và xoay Joy-Con ngang](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c)

## Hướng xử lý tiếp theo

### Kiểm tra bổ sung đường gửi/nhận

Đã lần thêm serializer `LiSendControllerMotionEvent()`/input thread của common
library và decoder `passthrough(... PSS_CONTROLLER_MOTION_PACKET ...)` trong
[VibeShine input.cpp](https://github.com/Nonary/vibeshine/blob/2.0.0/src/input.cpp).
Các trường x/y/z được gửi và đọc theo đúng thứ tự, float little-endian; không
thấy bước swap trục do đóng gói mạng. Motion type vẫn phân biệt gyro và accel.
Phép kiểm tra toán học ba vector cơ sở xác nhận chép SDL thẳng sang raw Pro
rồi qua parser SDL làm đổi cả ba trục; phép nghịch đảo phía encoder trả về
đúng ba vector cơ sở. Đây không phải capture gói từ thiết bị thật.

Vẫn chưa đo đồng thời raw Npad và vector sau chuẩn hóa trên Switch của người
dùng. Vì vậy không loại trừ lỗi đổi trục ở client chỉ vì công thức được kế
thừa từ Borealis. Cần phân biệt: packet gửi sai trường/đơn vị, chuẩn hóa Npad
sai trục, và host encoder Pro thiếu đổi hệ trục là ba khả năng khác nhau.
Mã nguồn hiện tại chứng minh được mâu thuẫn thứ ba; việc xác định toàn bộ
nguyên nhân cảm giác aim thực tế còn cần đo nguồn và đầu đọc.

Ưu tiên client theo yêu cầu đã hợp nhất, nhưng không thay hệ trục gửi của
mọi host để bù riêng một driver. Nếu bù trong client, cần chế độ tương thích
VibeShine VHF Pro rõ ràng, áp dụng sau chuẩn hóa Npad, không bật nhầm khi host
dùng DS4/DualSense hoặc driver đã sửa. Giao thức callback motion hiện tại
không cho client biết chắc profile VHF mà host đang dùng.

Khi thử bản bù cần giữ nguyên ứng dụng aim/profile host thực tế và thử
ba trục độc lập trên Pro, Console, Left/Right. Nếu vẫn sai, ghi rõ ứng dụng
aim và chế độ local/world space để đối chiếu parser. Calibration chống drift chỉ
nghiên cứu thêm nếu tay đứng yên vẫn tự ngắm lệch sau khi hệ trục đã đúng.

Sau bước nghiên cứu, người dùng đã yêu cầu triển khai bản bù ở client.
Đã thêm `Switch Pro gyro mode` trong Settings/overlay, mặc định
tắt, lưu `vhf_pro_compatibility`. Chuyển `{X,Y,Z}` thành `{-Z,-X,Y}` sau
chuẩn hóa, cùng cho gyro/accel và mọi nguồn/slot; bật/tắt có hiệu lực ngay.
Không đổi gain để giữ đợt thử tập trung vào hướng trục.

Native tests kiểm tra ba trục có dấu, round-trip parser Pro, mọi nguồn,
bật/tắt trong stream, zero khi stale, reconnect quay về setting phiên mới.
Kết quả build/đóng gói và hướng dẫn nằm trong `dist/README.vi.txt`.
Ngày 12/10/2026, người dùng xác nhận bản bù đúng hướng ở handheld trên máy thật.
Joy-Con rời và Pro chưa được thử lại với bản bù; chưa suy kết quả handheld
thành xác nhận tất cả nguồn hoặc kết luận riêng về độ chính xác accel/gain.

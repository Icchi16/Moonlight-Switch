# Nghiên cứu chọn nguồn gyro — 12/10/2026

## Kết luận và phạm vi

**Cập nhật quyết định của người dùng — 12/10/2026:** đợt đầu dùng Handheld
Npad qua SixAxis hiện có, hoãn SevenSixAxis. Nghiên cứu API thân máy và kế hoạch
diagnostic bên dưới được giữ làm tham khảo tương lai, không chặn triển khai
Joy-Con/Pro hoặc tương thích VibeShine. Phạm vi hiện hành nằm trong
[yêu cầu hợp nhất](requirements-merged.vi.md).

Thiết bị mục tiêu: Switch OLED, Atmosphère 1.11.2, theo thông tin người dùng.
Chưa biết phiên bản Horizon OS (HOS) đang cài; không suy ra HOS từ phiên bản Atmosphère.

Chọn Joy-Con trái/phải và gyro Pro Controller có cơ sở triển khai trực tiếp.
Đối với hướng thân máy được hoãn: có đường API và cách giải mã tham khảo,
nhưng cần bản đo trên thiết bị nếu sau này triển khai nguồn độc lập. Đặc biệt,
SevenSixAxis được mẫu chính thức mô tả là dữ liệu kết hợp console/Joy-Con;
không đồng nhất việc mở API đó với việc đã chứng minh dữ liệu gyro chỉ lấy từ
thân máy khi hai Joy-Con đang gắn vào.

Tài liệu này ghi lại bước nghiên cứu. Implementation Npad + VHF sau đó nằm
trong [bản thử gyro](gyro-vhf.vi.md), đã build ngày 12/10/2026. Chưa kiểm thử
trên Switch của người dùng; phần SevenSixAxis bên dưới vẫn chỉ là nghiên cứu.

## Hành vi yêu cầu

| Trạng thái | Nguồn phải dùng | Hành vi tùy chọn |
| --- | --- | --- |
| Hai Joy-Con gắn vào OLED, handheld | Handheld Npad qua SixAxis hiện có | Không dùng lựa chọn trái/phải; SevenSixAxis được hoãn |
| Hai Joy-Con rời cùng điều khiển một người chơi | Joy-Con trái hoặc phải | Người dùng chọn, lưu qua lần mở ứng dụng |
| Chỉ một Joy-Con kết nối | Joy-Con đó | Tự chọn, bất kể lựa chọn đã lưu |
| Người chơi đang dùng Pro Controller | Pro Controller đó | Bỏ qua lựa chọn Joy-Con |
| Nguồn đã chọn mất kết nối hoặc không có mẫu hợp lệ | Không gửi dữ liệu cũ | Dừng chuyển động trên host; hiển thị trạng thái |

Phần gyro hiện tại chỉ phục vụ người chơi 1. Nếu Pro và Joy-Con cùng kết nối,
nguồn phải gắn với tay cầm thực sự điều khiển người chơi 1, không chọn một Pro
bất kỳ chỉ vì nó tồn tại. Trường hợp một Joy-Con còn gắn, một Joy-Con đã tháo
không phải trạng thái handheld hai tay của yêu cầu; cần xử lý chuyển tiếp rõ
ràng, không dùng `active_handheld` một mình để nhận diện hai tay đang gắn.

## Bằng chứng từ mã hiện tại

Moonvibe dựa trên fork latency `47ebf567a8f5ad111c4cf59ef6d05edad7ef3888`.
Borealis được ghim tại `8c1f1093ee41a461d9d3bdd6dd2f6dfec166ab7a`, giống
snapshot upstream đã đối chiếu.

- `SwitchInputManager` khởi tạo bốn handle: handheld, Pro của Npad No1,
  JoyDual trái và phải của No1. Chưa mở handle `NpadJoyLeft`/`NpadJoyRight`
  cho chế độ Joy-Con đơn.
- `handleControllerSensors()` ưu tiên handheld, rồi Pro, rồi JoyDual trái;
  JoyDual phải chỉ dùng khi trái không kết nối. Sự kiện luôn mang index 0.
- `hidSetNpadHandheldActivationMode(Single)` cho phép handheld kích hoạt với
  một Joy-Con gắn. Vì vậy cần đọc trạng thái kết nối/gắn của cả hai bên.
- Mã đọc hiện tại không kiểm tra số mẫu trả về và độ mới của `sampling_number`
  trước khi phát sự kiện. Khi đổi nguồn phải tránh phát mẫu rỗng/cũ.
- Gyro được đọc trong `runloopStart()` của vòng ứng dụng. Tách sang luồng riêng
  là công việc tối ưu độ trễ khác, không phải điều kiện để làm lựa chọn nguồn.
- MoonlightSession chưa đăng ký callback `setMotionEventState`. Khi làm backend
  mới nên tôn trọng yêu cầu tần suất/dừng cảm biến từ host và vòng đời phiên stream.

Nguồn: [Borealis đã ghim](https://github.com/XITRIX/borealis/blob/8c1f1093ee41a461d9d3bdd6dd2f6dfec166ab7a/library/lib/platforms/switch/switch_input.cpp),
[input fork latency](https://github.com/nyanpasu64/Moonlight-Switch/blob/47ebf567a8f5ad111c4cf59ef6d05edad7ef3888/app/src/streaming/InputManager.cpp).

## API thân máy: có hướng triển khai, chưa đủ để tuyên bố nguồn độc lập

libnx 4.12.0, phiên bản của môi trường build Moonvibe trước đó, có:
`hidInitializeSevenSixAxisSensor`, `hidResetSevenSixAxisSensorTimestamp`,
`hidStartSevenSixAxisSensor`, `hidGetSevenSixAxisSensorStates`,
`hidStopSevenSixAxisSensor`, `hidFinalizeSevenSixAxisSensor`.
Nó cấp khoảng 512 KiB bộ nhớ cho đường này. Khởi tạo thành công không đồng
nghĩa có mẫu cảm biến; phải kiểm tra mã lỗi, số mẫu và sampling number.

Header libnx vẫn đặt tên payload là `unk_x18[10]`. Mã Eden khai báo cùng nhóm
dữ liệu là accel ba thành phần, gyro ba thành phần, quaternion bốn thành phần.
SWOTS đọc các nhóm tương ứng `[0..2]`, `[3..5]`, `[6..9]`. Đây là cách giải mã
có cơ sở từ triển khai khác, không còn chỉ là phỏng đoán theo số lượng float.
Tuy nhiên, tên trường không chứng minh đơn vị đo hoặc nguồn vật lý trên OLED/HOS này.

Mẫu chính thức switch-examples mô tả SevenSixAxis kết hợp cảm biến console và
Joy-Con, và bật VR mode. SWOTS mở đường này mà không gọi bật VR trong backend
motion đã đọc; dự án ghi nhận thử nghiệm trên HOS 18.1.0/Atmosphère 1.7.1.
Đó là bằng chứng hữu ích cho prototype không VR, không phải kiểm thử tương thích
trên OLED/Atmosphère 1.11.2. Không bật VR trong Moonlight chỉ để ép cảm biến chạy.

Không tự đặt FusionStrength bằng 0 hoặc 1: chưa có bằng chứng giá trị nào chọn
riêng thân máy hoặc làm thay đổi dữ liệu gyro theo cách yêu cầu.
`hidGetGyroBias` là độ lệch gyro, không phải tốc độ xoay tức thời để gửi cho host.

Nguồn:
[libnx 4.12.0 header](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/include/switch/services/hid.h),
[libnx 4.12.0 implementation](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/services/hid.c),
[mẫu SevenSixAxis chính thức](https://github.com/switchbrew/switch-examples/blob/669786898205b7beb25ff1731e72982e6d0397d3/hid/sevensixaxis/source/main.c),
[Eden payload](https://github.com/eden-emulator/mirror/blob/6352612425ae0765802eaa8c5904ff652b97222a/src/hid_core/resources/six_axis/seven_six_axis.h),
[SWOTS backend](https://github.com/Cameudis/SWOTS/blob/3d34c11a6132cc6eefb43804b8ef8d665c797cce/source/motion.cpp),
[SWOTS compatibility](https://github.com/Cameudis/SWOTS/blob/3d34c11a6132cc6eefb43804b8ef8d665c797cce/docs/compatibility.md).

## Joy-Con và Pro: phần khả thi trực tiếp

`hidGetSixAxisSensorHandles` cho `NpadJoyDual` trả handle trái và phải riêng.
Chọn một handle theo setting là đủ cho cặp rời. Với `NpadJoyLeft` hoặc
`NpadJoyRight`, phải mở đúng handle của style đơn, không tái sử dụng handle
JoyDual. `NpadFullKey` cung cấp đường Pro Controller.

Upstream đã có yêu cầu tương tự ở issue #251 và PR Borealis #10, nhưng PR chỉ
đổi ưu tiên trái thành phải, không thêm tùy chọn và không đọc gyro thân máy.
Không dùng PR này như một implementation hoàn chỉnh.

Phải lấy trạng thái pad mới trước khi chọn nguồn, theo dõi thay đổi style/ID
và kiểm tra lỗi start/stop. Nếu Joy-Con đơn được dùng nằm ngang, phép đổi trục
cần phản ánh cách cầm đó; không giả định cùng phép đổi trục cho mọi style.
Gyro và accelerometer nên lấy cùng nguồn để host không nhận dữ liệu mâu thuẫn.

Nguồn: [mẫu SixAxis](https://github.com/switchbrew/switch-examples/blob/669786898205b7beb25ff1731e72982e6d0397d3/hid/sixaxis/source/main.c),
[yêu cầu chọn Joy-Con](https://github.com/XITRIX/Moonlight-Switch/issues/251),
[PR đổi ưu tiên](https://github.com/XITRIX/borealis/pull/10).

## Thiết kế tích hợp Npad cho đợt đầu

1. Backend motion thuộc ứng dụng, chỉ build cho Switch. Chặn đường motion
   Borealis cũ trong lúc backend mới hoạt động để không gửi hai nguồn cùng lúc.
   Tránh chỉnh trực tiếp checkout submodule mà build khác không mang theo.
2. Hàm chọn nguồn thuần nhận topology, ID tay cầm người chơi 1 và lựa chọn
   Joy-Con. Tách khỏi thao tác HID để kiểm thử các trạng thái/chuyển tiếp.
3. Setting `Joy-Con gyro: Left / Right`, mặc định đề xuất Right, lưu trong
   settings.json. Trạng thái nguồn thực tế hiển thị riêng, ví dụ Console (Npad),
   Joy-Con L, Joy-Con R, Pro, Waiting for sensor. Có thể đổi khi đang stream.
4. Start/stop và đọc cảm biến theo vòng đời stream, callback motion của host,
   focus và sleep/wake. Giữ mẫu mới nhất, không phát lại sampling number cũ.
5. Chuyển nguồn gửi gyro zero để kết thúc tốc độ xoay trước đó; reset trạng
   thái theo dõi mẫu và bộ lọc nếu có. Không sao chép gate fallback 500 ms
   của Artemis vào đường chọn nguồn mới. Gate đó kiểm soát lúc fallback được
   phép gửi, không phải giữ lại mỗi gói gyro trong 500 ms.
6. Chuẩn hóa dữ liệu ở một chỗ. Moonlight yêu cầu gyro deg/s, accel m/s² và
   hệ trục SDL. Backend upstream đang đổi gyro sang rad/s rồi deg/s, nhưng
   chuyển accelerometer trực tiếp. Cần xác nhận đơn vị từng nguồn trước khi
   dùng chung hệ số; nếu accel thực đo ở g thì đổi bằng 9.80665.
7. Handheld dùng Npad theo quyết định mới: kiểm tra mẫu hợp lệ, hiển thị nguồn
   Console theo yêu cầu tên UI mới nhất; nếu không có mẫu thì dừng motion.
   Tên Console là tên giao diện, không khẳng định IMU thân máy độc lập.

Nguồn giao thức: [Moonlight motion API](https://github.com/nyanpasu64/moonlight-common-c/blob/318df5d6a3caead19563919141541f0cc7afbf40/src/Limelight.h).

## Kế hoạch đo backend console trong tương lai — đã hoãn

Các bước dưới đây chỉ thực hiện nếu sau này người dùng đưa SevenSixAxis/IMU
thân máy độc lập trở lại phạm vi. Không yêu cầu thực hiện chúng ở đợt Npad này.

Bản diagnostic độc lập cần ghi HOS/Atmosphère, model, chế độ chạy applet/title,
mã lỗi initialize/start/read, timestamp, sampling number, số mẫu, mười float
console và dữ liệu Npad song song. Giữ tên raw trong log; chưa gửi dữ liệu
console lên host trước khi xác nhận mapping/trục/đơn vị.

- Gắn cả hai Joy-Con: đặt máy đứng yên rồi xoay lần lượt ba trục và góc đo
  biết trước; kiểm tra thứ tự accel/gyro/quaternion, chiều trục, mức gyro
  trở về gần zero. Tích phân gyro khi xoay 90° giúp kiểm tra hệ số đơn vị.
- Tháo hai Joy-Con nhưng **giữ diagnostic cố định ở API console**, không tự
  đổi nguồn. Để thân máy đứng yên, xoay riêng từng Joy-Con; rồi giữ Joy-Con
  đứng yên và xoay thân máy. Phép thử phân biệt cảm biến thân máy với dữ liệu
  Joy-Con và phát hiện sự kết hợp nguồn. Lặp lại khi tắt kết nối Joy-Con.
- So sánh khi hai Joy-Con gắn, tháo và một tay gắn. Việc có một mẫu thay đổi
  khi cả máy cùng Joy-Con chuyển động không đủ chứng minh nguồn độc lập.
- Không bật VR trong phép thử đầu. Nếu chỉ có mẫu khi VR bật, đây là vấn đề
  triển khai cần giải quyết; không đưa thủ thuật VR vào stream production.
- Sau khi nguồn console được xác nhận: kiểm tra Left/Right bằng cách giữ
  một tay đứng yên và xoay tay kia; kiểm tra một tay, Pro, hot-plug, đổi option,
  reconnect stream, focus và sleep/wake. Không có hướng xoay ngược, mẫu cũ
  hoặc trạng thái xoay kéo dài sau mất nguồn.
- So sánh cảm giác gyro và tốc độ lấy mẫu ở stream 30/60 fps, FSR off/on.
  Build thành công và kiểm thử policy trên PC không thay cho thử nghiệm này.

Sau nghiên cứu: có cơ sở làm prototype console và routing Joy-Con/Pro.
Quyết định hiện hành là triển khai routing bằng Npad trước, hoãn prototype console.
Điểm còn cần kết quả trên thiết bị là độ độc lập nguồn console, mapping/gain
và khả năng chạy không VR trên cấu hình của người dùng. Không có kết luận
hiện tại rằng hoàn toàn không làm được, cũng chưa có căn cứ cam kết hoạt động
đầy đủ chỉ bằng copy code Artemis hoặc mở SevenSixAxis.

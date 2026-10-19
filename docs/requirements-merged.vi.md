# Yêu cầu hợp nhất Moonvibe Switch — 12/10/2026

## Nguồn và phạm vi

Theo yêu cầu của người dùng, tài liệu này hợp nhất yêu cầu từ hai phiên:

- `Assess latency and upscaling forks` — `01a11921-c435-7401-b59d-f0ff4c3a407e`.
- `Check gyro in Vibeshine 2.0` — `01a13018-a914-7b61-81b5-b91bc55d9933`.

Workspace chung: `D:\Coding\Moonvibe Switch`; ứng dụng tại `Moonvibe-Switch`.
Đây là phạm vi công việc và kết quả nghiên cứu hợp nhất, không hợp nhất lịch sử
chat. Nghiên cứu trước khi code đã hoàn tất. Sau khi chốt dùng Npad và tên UI
Console, phiên `Assess latency and upscaling forks` đã triển khai và build NRO
ngày 12/10/2026. Người dùng đã thử và xác nhận gyro truyền được, nhưng sai
trục giống nhau ở Console, cặp Joy-Con rời và Pro. Theo yêu cầu tiếp theo,
thêm tùy chọn bù trục VibeShine VHF Pro ở client, mặc định tắt, áp dụng cả
gyro/accel. Người dùng đã thử bản bù và xác nhận handheld đúng hướng; Joy-Con
rời và Pro chưa được thử lại. Một phiên phụ trách sửa
mã và build để tránh chỉnh cùng file.

## Mục tiêu chung

Client Moonvibe trên Switch OLED chạy Atmosphère 1.11.2, giữ low-latency và FSR,
đọc đúng nguồn gyro theo cách dùng máy/tay cầm, rồi truyền motion tới host
Windows AMD64 dùng VibeShine 2.0.0 và driver VHF mới để giả lập Nintendo Pro
Controller có gyro. Chưa biết phiên bản Horizon OS thực tế.

Nguồn gyro vật lý trên Switch và loại controller ảo trên PC là hai việc độc lập:
dữ liệu Handheld Npad, Joy-Con hoặc Pro vẫn có thể gửi vào Nintendo Pro
Controller ảo. Theo quyết định mới nhất của người dùng, đợt đầu dùng đường
Npad hiện có, hoãn SevenSixAxis và yêu cầu chứng minh IMU thân máy độc lập.

## Các yêu cầu đã chốt

| Mã | Yêu cầu | Trạng thái hiện tại |
| --- | --- | --- |
| R1 | Dùng low-latency từ fork nyanpasu64, nhánh `frame-rate-sync`, được giới thiệu trong issue #323 của upstream | Đã dùng làm nền; chưa đo latency trên thiết bị |
| R2 | Giữ FSR1/EASU và RCAS; stream 720p trên TV 1080p phải phủ toàn màn hình khi bật hoặc tắt upscale, theo cách tính kích thước output của Artemis-Switch | Đã sửa và build; cần kiểm thử hiển thị trên máy thật |
| R3 | Logo NRO dùng hình Moonlight trắng, phẳng, tám phần trên nền xám solid theo ảnh người dùng; tái tạo cho sắc nét | Đã tạo và đóng gói vào NRO |
| R4 | Handheld: dùng Handheld Npad qua SixAxis hiện có, UI gọi Console | Người dùng xác nhận gyro handheld đúng hướng sau bản bù; SevenSixAxis và IMU thân máy độc lập vẫn hoãn |
| R5 | Hai Joy-Con tháo rời: có lựa chọn Trái/Phải; chỉ một Joy-Con kết nối: tự dùng tay đó | Đã triển khai/build và native tests; cần thử thiết bị |
| R6 | Khi dùng Pro Controller: dùng gyro của chính Pro trong cùng slot nút bấm | Đã triển khai/build và native tests; cần thử thiết bị |
| R7 | Gyro phải tới Nintendo Pro Controller ảo qua driver VHF mới của VibeShine | Người dùng xác nhận bản bù handheld đúng hướng; Joy-Con rời và Pro chờ thử lại |
| R8 | Ưu tiên giải pháp chỉ sửa client, giữ host/driver VibeShine hiện tại | Bản sửa hiện tại chỉ ở client; mức đủ phải xác nhận trên host thật |
| R9 | Có tùy chọn Switch Pro gyro mode để bù hệ trục chung trước khi gửi, cùng cho gyro và accel | Đã triển khai/build, native tests đạt; handheld đã được người dùng xác nhận đúng hướng, còn Joy-Con rời/Pro |

HEVC bị giật đã được người dùng ghi nhận nhưng không phải ưu tiên chính của
đợt này. ViGEmBus + DS4 đang có gyro là mốc đối chiếu, không phải giải pháp thay
cho mục tiêu dùng driver VHF mới.

## Hợp nhất phần gyro trên client

### Chọn nguồn

- Theo quyết định dùng Npad trước, style handheld đang hoạt động dùng sensor
  Handheld Npad, kể cả chuyển tiếp một Joy-Con gắn khi HOS vẫn báo handheld.
- Đọc qua `hidGetSixAxisSensorStates`; hiển thị nguồn **Console** theo yêu cầu
  người dùng. Tên UI này không thay đổi nguồn API hoặc khẳng định IMU thân máy độc lập.
- Với hai Joy-Con rời, cho chọn trái/phải và lưu lựa chọn.
- Một Joy-Con: tự chọn tay đang kết nối, hỗ trợ cả kiểu Joy-Con đơn.
- Pro: chọn nguồn của controller đang điều khiển người chơi 1. Không lấy một
  Pro bất kỳ chỉ vì có kết nối khi Joy-Con và Pro cùng tồn tại.
- Chuyển nguồn trong stream, kiểm tra mẫu hợp lệ/độ mới; ngừng gửi dữ liệu cũ
  khi nguồn mất kết nối. Tránh đường đọc Borealis cũ gửi trùng motion.
- Không thêm smoothing hoặc thời gian chờ cố định làm tăng độ trễ mặc định.

Quyết định mới nhất: dùng `HandheldNpad` cho R4 ở đợt đầu. Không thêm
`SevenSixAxisSensor`, không cần diagnostic SevenSixAxis để bắt đầu hoặc nghiệm
thu đợt này. Phần nghiên cứu nguồn thân máy được giữ để tham khảo cho tương lai,
nhưng không phải điều kiện triển khai hiện tại. Dữ liệu Npad vẫn cần kiểm thử
mẫu hợp lệ, trục, đơn vị và chuyển nguồn trên thiết bị. Chi tiết trong
[nghiên cứu chọn nguồn](gyro-routing-research.vi.md).

### Truyền tới VibeShine và controller ảo

Kết quả nghiên cứu từ phiên VibeShine, chưa được xác nhận bằng phiên stream thật:

- VibeShine 2.0.0 có profile `vhf_switch` và đường motion vào driver
  `libvirtualgamepad`; host yêu cầu gyro/accelerometer ở 100 Hz.
- Profile Switch Pro tắt cờ `controller_touch`, trong khi
  `LiSendControllerMotionEvent()` của thư viện client hiện yêu cầu cờ đó.
  Khi cờ tắt, hàm trả `LI_ERR_UNSUPPORTED` trước khi gửi motion.
- Client hiện báo `LI_CTYPE_UNKNOWN`, chưa đăng ký `setMotionEventState`, và
  việc khai báo controller có thể bị bỏ qua khi reconnect với cùng số tay cầm.
- Driver Switch Pro chỉ xuất IMU khi phần mềm đọc controller trên PC bật IMU.
  Vì vậy gửi thành công từ client chưa đủ chứng minh game/emulator đọc được.

Hướng sửa client được đưa vào phạm vi chung:

1. Bản vá tương thích chỉ ở đường motion cho host hỗ trợ đã xác minh. Không bật
   giả cờ touchpad cho toàn phiên, không mở motion vô điều kiện cho mọi host.
   Phương án dùng điều kiện họ Sunshine của phiên kia cần rà lại giới hạn tương
   thích khi triển khai, không coi mọi host cùng họ mặc nhiên hỗ trợ.
2. Khai báo `LI_CTYPE_NINTENDO` và đúng capability cho controller; khai báo lại
   mỗi phiên/reconnect và khi controller thay đổi.
3. Xử lý `setMotionEventState`, bật/dừng motion và tần suất theo host. Việc
   callback yêu cầu 100 Hz không đồng nghĩa vòng giao diện hiện đạt 100 Hz.
4. Chuẩn hóa trục và đơn vị: gyro deg/s, accelerometer m/s² có trọng lực theo
   giao thức Moonlight; kiểm chứng đường Handheld Npad và các style controller.
5. Ghi log nguồn thực tế, cờ host, yêu cầu motion và lỗi gửi đủ để phân biệt lỗi
   cảm biến, client, truyền dữ liệu và phần mềm đọc controller ảo.
6. Lưu bản vá dependency cùng project để build sạch không mất thay đổi.

Không yêu cầu sửa/cài lại host hoặc driver ở bước hiện tại. Nếu kiểm thử chỉ ra
điểm chặn còn lại trên PC, báo kết quả và phạm vi cần bổ sung thay vì tuyên bố
bản vá client chắc chắn sửa được toàn bộ đường gyro.

Nguồn nghiên cứu của phiên VibeShine:
[cấu hình 2.0.0](https://github.com/Nonary/vibeshine/blob/2.0.0/docs/configuration.md),
[thương lượng tính năng](https://github.com/Nonary/vibeshine/blob/2.0.0/src/platform/windows/input.cpp),
[đường VHF](https://github.com/Nonary/vibeshine/blob/2.0.0/src/platform/windows/vhf_gamepad.cpp),
[driver đã ghim](https://github.com/Nonary/libvirtualgamepad/blob/4b56fb9da177f320fb2d7ddb1b6262e5d55d2750/driver/src/switch_pro.cpp).

## Trình tự triển khai và tiêu chí nghiệm thu

1. Triển khai routing Handheld Npad/Joy-Con/Pro, UI lựa chọn và motion lifecycle
   cùng bản vá tương thích VibeShine trong một đợt sửa phối hợp. Không mở
   SevenSixAxis hoặc thay đổi VR mode.
2. Kiểm thử nguồn Npad, ba trục, đơn vị, mẫu mới, gắn/tháo, reconnect và gyro
   end-to-end qua VHF mới. Diagnostic IMU thân máy độc lập được hoãn.
3. Build một bộ NRO/SD ZIP cuối cùng, giữ nền latency, FSR và logo hiện có.

| Kiểm thử | Kết quả cần đạt |
| --- | --- |
| Handheld, hai Joy-Con gắn | Đọc Handheld Npad, motion hoạt động đúng, hiển thị Console; không yêu cầu chứng minh IMU thân máy độc lập ở đợt đầu |
| Hai Joy-Con rời | Trái/Phải đúng lựa chọn, không lấy đồng thời hai gyro |
| Joy-Con đơn hoặc Pro | Lấy gyro của tay đang điều khiển |
| Gắn/tháo, đổi tay trong stream | Không gửi mẫu cũ, không crash, nguồn chuyển đúng |
| Host ép `vhf_switch` | Client gửi motion dù cờ touchpad tắt; controller ảo nhận IMU |
| Host chọn `vhf` tự động | Nhận đúng Nintendo Pro Controller và có gyro |
| Reconnect cùng số controller | Khai báo lại loại/capability, motion hoạt động lại |
| Phần mềm/game trên PC | Đọc được gyro của Pro ảo sau bước bật IMU |
| DS4/ViGEmBus hiện có | Giữ khả năng motion làm kiểm tra tương thích |
| FSR bật/tắt, stream 720p → TV 1080p | Phủ toàn màn hình; EASU hoạt động khi bật và cần upscale |
| 30/60 FPS, FSR bật/tắt | Motion đúng yêu cầu host, không phụ thuộc sai vào tốc độ video; so sánh latency với bản nền |

NRO đã build có routing Npad và bản vá motion; artifact được đóng gói trong `dist/`.
Chi tiết implementation/kiểm thử/giới hạn: [gyro Npad + VHF](gyro-vhf.vi.md).
Tài liệu về bản build hiện có: [low latency + FSR](moonvibe-switch.md),
[logo NRO](nro-icon.md).

## Rà soát mâu thuẫn và quy tắc xử lý — 12/10/2026

Không thấy mâu thuẫn trực tiếp giữa các mục tiêu R1–R8. Có các trường hợp giao
nhau và giới hạn kỹ thuật cần diễn đạt rõ trước khi triển khai:

| Điểm giao nhau | Quy tắc xử lý/phân biệt |
| --- | --- |
| Hai Joy-Con còn gắn nhưng Pro cũng kết nối | Trước hết xác định controller đang điều khiển người chơi 1. Nếu đó là Pro, dùng Pro; Handheld Npad áp dụng khi người chơi 1 dùng handheld với hai Joy-Con gắn. Không chọn theo danh sách thiết bị đang tồn tại |
| Chỉ một Joy-Con kết nối nhưng Pro cũng tồn tại | Tự chọn Joy-Con đơn khi đó là controller đang dùng. Không để quy tắc “một Joy-Con” giành nguồn của Pro đang điều khiển |
| Tay đã chọn mất kết nối, còn tay kia | Kết thúc dữ liệu nguồn cũ, đánh giá topology mới rồi tự dùng Joy-Con còn lại. “Dừng dữ liệu cũ” không có nghĩa giữ motion tắt dù có nguồn mới hợp lệ |
| Lựa chọn Trái/Phải và handheld | Lựa chọn chỉ áp dụng cho cặp Joy-Con rời; không ghi đè handheld hoặc Pro |
| Low latency và gyro host yêu cầu 100 Hz | Nhịp motion phải độc lập với nhịp hình/giao diện 30/60 FPS. Giữ thuật toán video từ fork 1; kiểm tra chi phí CPU và latency sau khi bổ sung lấy mẫu motion |
| FSR và low latency | Có thể cùng bật, nhưng FSR có chi phí GPU. Giữ tùy chọn bật/tắt; không cam kết FSR miễn phí về latency hoặc bảo đảm latency không đổi trên máy thật |
| Chỉ sửa client và IMU controller ảo | Sửa client xử lý được chỗ chặn gói đã tìm thấy. Phần mềm PC vẫn cần bật/đọc IMU đúng giao thức; nếu lỗi còn lại nằm trên host, báo rõ giới hạn, không âm thầm mở rộng phạm vi sửa host |
| “Console motion” của Artemis và R4 hiện tại | R4 dùng Handheld Npad, UI gọi Console theo lựa chọn người dùng; không sao chép gate fallback 500 ms của Artemis |
| Controller motion và Console motion trong UI | Artemis dùng Controller motion như công tắc tổng, Console motion là quyền bổ sung cho handheld. Thiết kế của Moonvibe cần làm rõ công tắc tổng và nguồn thực tế, tránh hiểu chúng là hai nguồn độc lập |

Thứ tự quyết định nguồn: xác định controller/slot đang dùng → xác định style
và trạng thái gắn/tháo → áp dụng quy tắc handheld/Pro/Joy-Con đơn/cặp rời →
kiểm tra mẫu hợp lệ → tuân theo yêu cầu motion của host. Gyro và accelerometer
lấy cùng nguồn và chỉ gửi một bộ dữ liệu vào controller ảo tương ứng.

Trong implementation hiện tại, slot bám đúng mapping Borealis: handheld đứng
trước các Npad đánh số. Khi handheld và Pro đồng thời tồn tại, gyro mỗi nguồn
đi theo cùng slot nút của nó; không dùng cách đoán “tay vừa bấm gần đây” để
chuyển riêng gyro giữa hai slot. Ô nguồn trong UI hiển thị người chơi 1.

### Setting Console trong Artemis thực sự làm gì?

Đã đối chiếu mã tại commit `33e6854781a47e86ae349e0a7646ff00bd924ddd`;
không có thông tin xác định NRO người dùng đã thử tương ứng đúng commit này.

1. Borealis vẫn đọc `hidGetSixAxisSensorStates(handle[0], ...)` khi handheld
   hoạt động. Patch Artemis chỉ gắn marker `controllerIndex = -1` vào event
   handheld; không thay lệnh đọc bằng SevenSixAxis.
2. Wrapper của ứng dụng nhận marker này dưới dạng `UINT8_MAX`, gọi nguồn đó
   là `MotionSource::Console`, rồi gửi về host controller slot 0 nếu policy cho phép.
3. `Controller motion` là công tắc tổng (`allowGamepadMotionSensors`, mặc định
   bật). `Console motion` bật quyền cho handheld fallback
   (`allowConsoleMotionFallback`, mặc định tắt). Nếu công tắc tổng tắt, bật
   Console một mình cũng không gửi motion.
4. Gate chờ khoảng 500 ms kể từ lần quan sát fallback đầu và yêu cầu không có
   mẫu controller được wrapper ghi nhận trong 500 ms gần đây. Đây là điều kiện
   mở đường fallback, không phải mỗi gói gyro đều bị giữ lại 500 ms.
5. Việc setting được phép bật trên build Switch dựa vào sự có mặt của đường
   Handheld SixAxis trong mã, không phải phép thử thực tế xác nhận IMU thân máy.

Trong đường chạy đã kiểm tra không có lời gọi initialize/start/read SevenSixAxis.
Vì vậy, tên sát nghĩa là **“cho phép gửi motion của handheld”**. Chưa có cơ sở
gọi setting đó là lựa chọn gyro thân máy độc lập hoặc một thuật toán gyro mới.

Nguồn:
[patch đánh dấu handheld](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/patches/borealis-handheld-motion-source.patch),
[wrapper gửi motion](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/app/src/streaming/InputManager.cpp),
[policy và gate](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/app/src/features/input/SwitchMotionPolicy.cpp),
[hai công tắc settings](https://github.com/antoinebou12/Artemis-Switch/blob/33e6854781a47e86ae349e0a7646ff00bd924ddd/app/src/artemis_settings_tab.cpp#L378-L415).

Mình đang làm dự án cổng Darling (lớp tương thích mã nguồn mở, kiểu Wine cho macOS) để chạy các app GUI có trong BaseSystem.dmg của macOS 26.6 trên Linux. Mọi thứ chạy trên máy của mình, với bản macOS mình tự tải. Phiên trước đã làm được khá nhiều. Toàn bộ hiện trạng, cách kết nối hai máy test (x86_64 Ubuntu và Mac M2 thuê, dùng SSH key sẵn trên máy này) cùng các script và quyết định kỹ thuật đã chốt đều nằm trong file `~/DARLING-MACOS26-HANDOFF.md`. Hãy đọc kỹ file đó trước, và đọc thêm memory trong `~/.claude/projects/-home-ubuntu/memory/`.

Sau đó làm tiếp theo thứ tự ưu tiên ở mục 1 và mục 7 của file bàn giao. Ưu tiên thu dữ liệu trên Mac trước, vì máy thuê chỉ còn khoảng 20 ngày. Mục tiêu cuối là app GUI của BaseSystem chạy được trên Darling, dùng tối đa phần mềm sẵn có của Apple. Muốn có kết quả sớm thì cho app AppKit "Hello" hiện cửa sổ trên Linux qua Daylight (vẽ bằng CPU) và gửi ảnh chụp cho mình ngay khi có.

Cách làm việc mình muốn:
- Cứ làm liên tục, tự quyết các lựa chọn kỹ thuật, đừng dừng lại hỏi mình. Nếu một hướng bị chặn hoặc không làm được, chọn hướng khác hợp lý, ghi lý do ngắn gọn, rồi làm tiếp.
- Tận dụng tối đa đồ có sẵn của Apple theo mục "Nguyên tắc: tận dụng tối đa đồ có sẵn của Apple" trong file bàn giao. Thứ tự ưu tiên:
  1. framework và thư viện thật trong shared cache;
  2. mã nguồn mở của Apple, đúng tag macOS 26.5 trong distribution-macOS;
  3. binary daemon thật trong BaseSystem;
  4. SDK thật trên Mac;
  5. RE bằng REA hoặc Ghidra cho phần còn lại.
  
  Chỉ tự viết phần Apple không phát hành (WindowServer/Daylight, lớp kernel của Darling, phần nối với Wayland/X11).
- Giữ các quyết định đã chốt trong file bàn giao: không chặn syscall ở tầng Linux; ở những chỗ Darling không có cơ chế của macOS (code signing, sandbox) thì giữ đúng như Darling vẫn làm và ghi rõ lý do trong code.
- Commit vào git cục bộ thường xuyên và cập nhật memory, để phiên có bị ngắt cũng không mất việc.
- Báo tiến độ ngắn gọn bằng tiếng Việt.

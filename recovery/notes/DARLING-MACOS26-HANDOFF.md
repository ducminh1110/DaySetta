# Bàn giao: chạy app GUI macOS (BaseSystem 26.6) trên Darling

Cập nhật lần cuối: 2026-10-30. Tài liệu này dành cho phiên Claude Code tiếp theo, chạy trên **chính máy aarch64 này** (`/home/ubuntu`). SSH key ở `~/.ssh/id_ed25519` đã được cài sẵn trên cả hai máy test.

## 1. Mục tiêu và bối cảnh

- **Dự án:** cổng Darling (lớp tương thích mã nguồn mở, kiểu Wine cho macOS) lên kernel xnu-12377 (macOS 26) và **chạy được các app GUI có trong BaseSystem.dmg của macOS 26.6** trên Linux. Tận dụng tối đa phần mềm sẵn có của Apple: shared cache, AppKit, SkyLight, CoreFoundation…, cùng các bản mã nguồn mở của Apple.
- **Phạm vi:** mọi thứ chạy trên máy của chính người dùng, với bản macOS do họ tự tải (macrecovery). Không phân phối lại binary của Apple; binary Apple và dylib trích từ cache không bao giờ được commit.
- **Kiến trúc hiện tại (hướng lai):**
  - userland của Apple lấy trọn từ dyld shared cache của BaseSystem 26.6;
  - dyld là bản port của Apple dyld-1378, nạp cache đó;
  - riêng `libsystem_kernel` (lớp kernel của Darling) và `libdispatch` là bản Darling build, nạp đè thư viện trong cache qua cơ chế "root" của dyld;
  - WindowServer thay thế là dự án **Daylight**, sẽ hiển thị qua Wayland hoặc X11.
- **Người dùng muốn làm liên tục, tự quyết các lựa chọn kỹ thuật**, không dừng lại hỏi. Commit vào git cục bộ thường xuyên và cập nhật memory (`~/.claude/projects/-home-ubuntu/memory/`).

### Thứ tự ưu tiên người dùng đặt
1. **Gỡ chặn libdispatch và các thứ đi kèm** để `defaults`, `plutil` và tool viết bằng Swift của BaseSystem chạy được.
2. **Máy Mac thuê chỉ còn khoảng 20 ngày** (tính từ 2026-10-29), nên phải thu dữ liệu sớm và lưu hết về máy:
   - trace WindowServer và CARenderServer cho các tình huống: mở/đóng/resize/kéo cửa sổ, menu, gõ phím, chuột, dark mode, sheet/popover, nhiều cửa sổ;
   - trace XPC/launchd/cfprefsd/LaunchServices/font;
   - build sẵn các app test x86_64 bằng SDK thật;
   - chụp ảnh tham chiếu.
3. Sửa lỗi `ls` (`fts_read`), dựng launchd/XPC và các daemon tối thiểu mà app cần.
4. **Đường tắt:** cho app AppKit "Hello" hiện cửa sổ trên Linux trước (Daylight cộng vẽ bằng CPU) và gửi ảnh chụp cho người dùng ngay khi có.

Nếu một hướng bị chặn: chọn hướng khác hợp lý, ghi lý do ngắn gọn, rồi làm tiếp.

### Nguyên tắc: tận dụng tối đa đồ có sẵn của Apple
Trước khi tự viết một thứ gì, hãy kiểm tra theo thứ tự sau:
1. **Thư viện và framework thật trong shared cache 26.6.** Mặc định dùng bản của Apple (AppKit, SkyLight, QuartzCore, CoreGraphics, CoreText, CoreFoundation, libxpc, libobjc, Swift runtime…). Chỉ thay khi buộc phải thay, như hiện mới thay `libsystem_kernel` và `libdispatch`.
2. **Mã nguồn mở của Apple, đúng phiên bản macOS 26.5.** Lấy tag trong `release.json` của https://github.com/apple-oss-distributions/distribution-macOS (tag `macos-265`; xem memory `apple-oss-distribution-versions`). Ví dụ:
   - xnu-12377.121.6, dyld-1378, libdispatch-1542.100.32;
   - libpthread-539.100.4, libplatform-375.120.2, libmalloc-812.100.31;
   - Libc-1752.120.2, objc4-951.7, Libnotify (`notifyd`), configd, syslog, IOKitUser, Security, launchd/`bootstrap_cmds`;
   - AvailabilityVersions-157.2, cctools/ld64.
   
   Build lại bản Apple, đừng viết lại từ đầu.
3. **Binary daemon và tool thật trong BaseSystem** (`~/basesystem`): `launchd`, `cfprefsd`, `notifyd`, `logd`, `fontd`/`fontservicesd`, `distnoted`, các plist trong `/System/Library/LaunchDaemons` và `LaunchAgents`… Thử chạy chúng trên Darling trước. Chỉ viết bản thay thế khi binary thật đòi hỏi tính năng kernel mà Darling không có.
4. **Header và SDK thật.** Command Line Tools trên Mac M2 có macOS SDK. Dùng nó để build app test x86_64 (`clang -arch x86_64 -isysroot $(xcrun --show-sdk-path)`) và để đối chiếu header private khi cần.
5. **Hành vi thật trên Mac M2.** So sánh với hành vi của macOS thật thay vì đoán: trace bằng `wsprobe`, `lldb`, `log stream`, ảnh chụp.
6. **REA và Ghidra** (memory `rea-reverse-engineering-setup`) cho phần nào không có mã nguồn: SkyLight, QuartzCore, protocol của CARenderServer. Ghi kết quả RE vào `~/daylight/docs`, `~/macos-re/re` và memory.

Chỉ tự viết những gì Apple không phát hành và không thể dùng nguyên bản: phía server của WindowServer (Daylight), lớp kernel của Darling, và phần nối với Wayland/X11.

## 2. Các máy

| Máy | Kết nối | Ghi chú |
|---|---|---|
| aarch64 (máy này) | local | Viết code, RE, symbolize. Ubuntu 26.04, có sudo. |
| x86_64 test | `~/bin/x86 '<lệnh>'` = `ssh ubuntu@remotemac.mooo.com` (key) | Ubuntu 24.04, 8 core, 15 GB RAM, sudo không cần mật khẩu. Darling đã build và cài ở `/usr/local`. |
| Mac M2 thuê | `~/bin/mac '<lệnh>'` = `ssh lichen309@100.75.181.28` qua Tailscale (key) | macOS 27.0.1, Apple M2. Đã tắt Debugging Restrictions. Có Command Line Tools (clang, lldb). `sudo` cần mật khẩu, người dùng giữ. Người dùng đang đăng nhập console nên app GUI chạy được trực tiếp qua SSH. |

- **Không ghi mật khẩu vào file hay chat.** Cả hai máy đều dùng key. Đã khuyên người dùng đổi mật khẩu cũ.
- Tailscale trên máy này: hostname `claude-darling-dev`.

## 3. Bố cục file

**Máy aarch64:**
- `~/darling`: bản sao Darling (đồng bộ một phần). **Nguồn chuẩn là cây trên máy x86.**
- `~/dyld-1378`: bản port dyld (cũng có trên máy x86, bản x86 là chuẩn).
- `~/daylight`: WindowServer thay thế, git nhánh `dev`.
  - `server/`: dispatch core, handshake `session.c`, front end Mach `main_darling.c`.
  - `tests/darling/`: test tích hợp, gồm `handshake_client` và `applep`.
  - `tools/macprobe/`: `wsprobe.c` (interposer ghi các message WindowServer của **app test của chính mình**) và `hello.m`.
  - `docs/protocol.md`: bản đồ 738 routine.
  - `docs/trace-hello-macos27.log`: trace thật, 370 lời gọi tới 57 routine.
- `~/macos-re`: workspace RE (xem `README.md` trong đó).
  - BaseSystem 26.6 ở `recovery/bs/`;
  - dylib trích từ cache ở `dsc-x86_64/` (943 MB, chỉ dùng để RE và symbolize);
  - `protocol/skylight_protocol.json`;
  - `re/SkyLight.dis`;
  - `tools/dsym.py`: symbolizer.
- `~/darling-port-tools`: bản sao các script (xem mục 5).
- `~/darling-port-patches`: patch cũ, giai đoạn port xnu.

**Máy x86 (nguồn chuẩn):**
- `~/darling`: cây Darling. Build ở `~/darling/build`, cài vào `/usr/local`.
  - Commit: nhánh `port/macos26` ở superproject và các submodule, riêng xnu ở nhánh `port/xnu-12377.121.6`.
  - Thư mục mới `src/external/libdispatch-apple`: libdispatch-1542.100.32 của Apple, target `libdispatch_apple`, chỉ x86_64.
- `~/dyld-1378`: dyld port, git đã commit. Output link ở `~/dyld-port/dyld`, cài vào Darling root tại `/usr/lib/dyl4`.
- `~/basesystem`: BaseSystem 26.6 đã giải nén. Shared cache nằm ở `System/Library/dyld/`.
- `~/darling-port-tools`: script.
- `~/daylight`: bản sao để cross-build (`tools/build-darling.sh`).
- `~/darltest/last.log`: log của lần chạy gần nhất.

## 4. Hiện trạng

**Chạy được:**
- Darling chạy trên xnu-12377 (macOS 26.5). Shell, `ps`, `launchctl`, `defaults` (bản Darling) và `perl` đều chạy. `mach_msg2_trap` đã có (test 13/13).
- dyld-1378 port nạp shared cache 26.6 thật. Override chỉ áp cho `libsystem_kernel` (đường dẫn gốc) và `libdispatch` (từ `/usr/lib/darling/macos26/...`).
- Binary BaseSystem chạy trọn vẹn: `/bin/echo`, `/bin/date`, `/usr/bin/uname`, `/bin/cat`, `/usr/bin/sw_vers`.
- Daylight: handshake 29000/29010/32000 chạy thật trong Darling, test đạt.
- Trên Mac: `wsprobe` cộng `hello` ghi được trace đầy đủ.

**Đang kẹt:**
- `/usr/bin/defaults`: libdispatch-1542 gọi `kqueue()`, nhưng `kqueue_impl` của Darling nằm ở thư viện riêng `external/libkqueue`, mà libSystem của Apple không nạp. Hook yếu (weak) trong `emulation/.../kqueue/kqueue.c` vì thế in "No kqueue implementation?!". **Việc cần làm:** đưa kqueue (libkqueue của Darling) vào thẳng `libsystem_kernel`, hoặc cách tương đương, sao cho chạy được với cả libSystem của Darling lẫn của Apple.
- `/usr/bin/plutil` (Swift): `_Concurrency/ExecutorImpl.swift:61 Fatal error`. Nhiều khả năng do dispatch, nên xem lại sau khi sửa kqueue.
- `/bin/ls`: `fts_read: Operation not supported`, có thể do `getattrlistbulk` hoặc `fstatat` trên `..`.
- `__mac_syscall` (381) in ra "Unimplemented syscall". Nên trả `ENOSYS` một cách im lặng, như kernel không nạp MAC policy đó.
- `uname` vẫn báo 20.6.0 vì `EMULATED_RELEASE` trong `emulation/CMakeLists.txt` vẫn là chuỗi cũ.
- Chưa có launchd/XPC hay daemon (cfprefsd, logd…) cho userland Apple.
- Daylight mới có handshake. Chưa có window/surface/CA render/sự kiện, và chưa vẽ được gì.

**Quyết định kỹ thuật đã chốt (đừng làm lại):**
- **Không chặn syscall ở tầng Linux** (kiểu seccomp hay syscall user dispatch). Thay vào đó, dyld bản Darling dùng patch table của chính Apple để dùng `libsystem_kernel` của Darling làm "root" thay thư viện trong cache.
- Cache dylib trích bằng ipsw **không thể** trộn với thư viện của Darling, vì con trỏ đã bị bind sẵn vào địa chỉ trong cache. Chúng chỉ dùng để RE.
- libdispatch: build ở chế độ workqueue của kernel mà Darling đã chạy ổn (`DISPATCH_USE_KEVENT_WORKQUEUE=0`, `KEVENT_WORKLOOP=0`), giữ voucher và QoS theo mặc định của Apple. Nếu kẹt, chuyển sang `DISPATCH_USE_INTERNAL_WORKQUEUE=1` (pool pthread nội bộ).
- Những chỗ Darling không có cơ chế tương ứng thì giữ đúng như Darling cũ: không có AMFI hay code signing, không có sandbox. Ghi rõ lý do trong code (`#ifdef DARLING` kèm comment).
- Một phiên trước bị bộ lọc an toàn cắt ngang vài lần: lúc bàn chuyện chặn syscall, lúc thiết kế kevent workqueue, và lúc dò kqueue. Đó là lý do có các quyết định trên. Hãy tiếp tục bằng các cách đã chốt, đừng quay lại hướng đã bỏ.

**Các sửa đổi chính đã làm (đều nằm trong git):**
- **libsyscall:**
  - thêm skywalk, exclaves, `vm_reclaim`, `notify.defs`, `os_log_coprocessor`;
  - `macos26_exports.c` (kernelVersion, netname, `register_uexc_handler`);
  - getter `work_interval_instance_*`;
  - `voucher_mach_msg_clear`.
- **emulation:**
  - `F_ADDFILESIGS_RETURN` (đọc code limit của CodeDirectory);
  - `libkernel_table.h` (phân biệt bảng `_libkernel_functions` của Darling, bản 4, với của Apple, bản 5; elfcalls lấy từ `apple[]`; `posix_spawn` dùng `vfork` của Apple);
  - `restartable.c` (restartable ranges qua tín hiệu RTMIN+2);
  - `bsdthread_register` báo feature workqueue cho libpthread của Apple;
  - `PROC_PIDUNIQIDENTIFIERINFO` trả đúng kích thước struct;
  - báo cáo tín hiệu chết người kèm backtrace (`sigexc.c`).
- **mldr:** chuỗi `apple[]` thêm `ptr_munge`, `stack_guard`, `malloc_entropy`.
- **darlingserver:** stub task (restartable → `KERN_NOT_SUPPORTED`, exc_guard, dyld cache info); audit token có pid version khác 0.
- **dyld-1378 (`#ifdef DARLING`):**
  - `mach_init(apple)` cộng `sigexc_setup`;
  - `isOverridablePath` chỉ cho phép `libsystem_kernel` và `libdispatch`;
  - thư mục override `/usr/lib/darling/macos26`;
  - bỏ `simulatorFileMatchesDyldCache`;
  - `pageInLinkingMode` bằng 0;
  - fallback đường dẫn cache (thay cho `fsgetpath`);
  - glue.c dùng stdio và Blocks của libc tĩnh;
  - checkpoint debug gated bởi `DARLING_DYLD_TRACE`.
  - Phải link bằng ld64 của Darling với `-fno-jump-tables`, vì lld không xuất được `MH_DYLINKER`.

### Cập nhật 2026-10-30 (phiên 2)

**Đã xong:**
- Thu dữ liệu Mac: `daylight/tools/macprobe/wsprobe2.c` (mọi Mach message kể cả `mach_msg2_internal`, bootstrap lookup, XPC dạng đọc được, IOSurface, `WSPROBE_DENY`), `hello2.m` (12 tình huống: basic multiwin resize move close menu sheet popover text dark input cursor; input tổng hợp bằng `CGEventPostToPid` chạy được), `run_scenarios.sh`. Trace nén + ảnh cửa sổ app ở `daylight/docs/traces/macos27-hello2/` và `docs/reference/macos27/`; trace thô + ảnh toàn màn hình (có desktop người dùng, KHÔNG commit) ở `~/macos-re/traces/macos27-hello2/`. Rosetta chưa cài trên Mac (cần sudo) nên trace là arm64; app test build universal, chạy trên Darling cần `-mmacosx-version-min=14.0`.
- Phát hiện: app nói chuyện với WindowServer (29xxx/30xxx/32xxx/34xxx), CARenderServer (40002 commit), launchservicesd (~271 XPC sync), cfprefsd, fonts, pasteboard, distnoted. CA render server là bắt buộc (chặn lookup thì CA lấy port qua WindowServer msg 30243). QuartzCore có sẵn `CA::Render::Server` và renderer phần mềm `CA::OGL::SWContext` → hướng: Daylight chạy render server của chính Apple, vẽ CPU.
- Darling: `defaults`, `plutil` (Swift), `ls`, `find`, `uname` của Apple chạy được. libkqueue vào libsystem_kernel; libdispatch: mgr queue chạy pthread riêng, `VOUCHER_USE_EMPTY_MACH_BASE_VOUCHER=1`; trap `mach_generate_activity_id`; `__mac_syscall` ENOSYS im lặng; `getattrlistbulk` thật; `..` ở gốc vchroot; Darwin 26.2.0; báo cáo crash có symbol (dladdr) và gửi lại tín hiệu bằng `kill(getpid())` (trước đây `kill(0)` giết luôn shellspawn); audit session GUI (asid 100000); `ulock_wait2`.
- **launchd của Darling hiểu giao thức pipe XPC v5 của libxpc mới** (`src/launchd/src/xpc_pipe_v5.c`): lookup 207/804, register 204, parent 203, session type 301. Trước đó mọi `bootstrap_look_up` từ userland Apple đều thất bại. Reply phải có `req_pid`/`rec_execcnt` của client.
- dyld: chỉ libsystem_kernel/libdispatch được đè bản trong cache (kể cả cache "development" và khi tìm theo đường dẫn).
- `~/darling-port-tools/overlay_basesystem.py`: symlink tài nguyên BaseSystem (SystemAppearance, Resources của framework, font...) vào `~/.darling` (chạy khi container dừng; `--undo` để gỡ).
- Daylight: chế độ phát lại reply ghi từ Mac (`tools/trace/canned.py` → `server/src/dl_canned.c`) cho routine chưa cài; phục vụ thêm `com.apple.CARenderServer`.
- **AppKit hello2 thật trên Darling đã kết nối Daylight**: bắt tay, kết nối, CA, notify... rồi abort trong `get_current_display_system_state`.

- Darling có memory entry (memfd + fileport; RPC fileport mới trong darlingserver, thêm ở CUỐI danh sách RPC). Daylight cài 32006/34006/34003, vùng nhớ 0x40 của kết nối. Với `DAYLIGHT_EMPTY_DISPLAY_STATE=1` (blob trạng thái màn hình của macOS 27 không đọc được trên SkyLight 26.6), **AppKit hello2 chạy tới `applicationDidFinishLaunching` và gọi `CreateWindow`**; reply phát lại làm `NSCGSPanic`.

**Bước tiếp theo (thay mục 7):**
0. **Cập nhật 2026-10-31 (khuya): timer dispatch, vẽ nhãn/toolbar.** libkqueue coi `EV_ADD|EV_ENABLE` trên knote timer có sẵn là chỉ enable (giữ deadline cũ) → timer dispatch trễ ~26 s; đã sửa (darling 0aabeafd6, repro `daylight/tools/macprobe/dispatchprobe5`). Bridge: AppKit 26 lưu phần vẽ của nhiều view dưới dạng `NSViewBackingLayerContents` mà `renderInContext` bỏ qua → bridge cho view tự vẽ các layer đó khi render; vẽ frame vào bộ đệm riêng rồi mới copy sang canvas (hết rách hình). Daylight trả `GetKeyFocusProcess` (daylight 5bc8009). Người dùng chốt: không làm phần ổ đĩa cho app cứu hộ; Disk Utility coi như xong (`docs/basesystem-apps.md`). Ưu tiên tiếp: vẽ lại/animation, menu bar, kéo cửa sổ, rồi chạy lại batch (`~/darltest/batch6.txt`).
0. **Cập nhật 2026-10-31 (tối): authd chạy, app hết treo ngẫu nhiên.** (1) Disk Utility treo trong `AuthorizationCreate` vì authd của BaseSystem abort ("XPC Service cannot be run directly"): launchd thiếu routine 718 của `xpc_main` (reply `identifier` + `xpcservice-dict`), đã thêm vào `xpc_pipe_v5.c`. (2) authd lại treo trong sqlite: sqlite của Apple thử `F_OFD_GETLK`, Darling trả EINVAL nên nó chuyển sang khoá bằng thư mục `auth.db.lock`, authd bị giết thì khoá còn lại mãi → fcntl giờ có khoá OFD (90–93), `F_SETCONFINED`, `F_GETPROTECTIONCLASS`. (3) Khoảng 1/3 lần chạy app treo vì Daylight không bao giờ nhận được một request (receive có timeout 15 ms trên port set của Darling thỉnh thoảng làm mất message; chưa tìm ra gốc trong darlingserver) → Daylight dùng luồng "tick" thay cho receive có timeout: 8/8 lần chạy Disk Utility có cửa sổ. Còn mở: timer dispatch tạo sau khi app khởi động chỉ chạy khi timer bucket đã đặt trước đó hết hạn (~26 s), có thể là nguyên nhân cửa sổ còn trắng tới khi có event; test riêng (`daylight/tools/macprobe/dispatchprobe3/4`) thì chạy đúng. Công cụ debug mới: file host `/tmp/darling-machtrace`, `fpall.sh PID` (gdb backtrace mọi luồng) + `dscsym.py`, `hangscan.sh APP N`, `lastmsg.py`, bridge `DAYLIGHT_BRIDGE_PROBE=1`. Commit: darling e82bb3d1a, daylight d2562e4. Quét app: `~/darltest/batch5.txt`.
0. **MỐC 5 (2026-10-31): chuột, bàn phím, kích hoạt app, xuất X11.** Daylight gửi event vào app đúng cách WindowServer thật làm (datagram `{0, len, event}` trong port stream, event mã hoá kiểu `CGEventCreateData`); `SetFrontProcessWithInfo` → event kích hoạt (type 20) + notification, app thành active có key window; `frontends/x11/daylight-x11` hiển thị `/tmp/daylight/screen.fb` và gửi input qua FIFO `/tmp/daylight/input`. Ảnh: `daylight/docs/milestones/startup-disk-click-input.png`, `terminal-keyboard-input.png` (đã gửi). Thử: `darling-port-tools/inputtest.sh OUT APP SCRIPT`. Sửa Darling kèm theo: `getattrlist`/`getattrlistbulk` viết lại (NSFileManager liệt kê thư mục được), `pathconf` tên macOS, Sandbox check trả "cho phép", birthtime stat64, khoá danh sách luồng workqueue (stand-in os_unfair_lock rỗng), stack luồng workqueue 8 MB (Swift), refcount queue cooperative của libdispatch, mldr `DARLING_DYLINKER`/`DARLING_DYLINKER_PATHS` (tiến trình con của app Apple dùng dyl4), `asroot` (app Recovery chạy root), hard link HFS+ của BaseSystem đã nối lại (`hfs_hardlinks.py`/`hfs_relink.sh`). Debug: `touch /tmp/darling-errtrace` (syscall lỗi) hoặc `/tmp/darling-systrace` (mọi syscall). Kết quả quét app: `~/darltest/batch3.txt`. Tiếp: notifyd (DYLD_INSERT, nguồn điện IOPS), authd (Share Disk), daemon Recovery (recoveryosd, recoverydiagnosed, bosreporter), Language Chooser (`isEnrolledInDEP` IMP NULL), Web Browser (trap), Safari (framework trong cryptex), menu bar.
0. **MỐC 4 (2026-10-31): Terminal.app thật của BaseSystem 26.6 chạy** (bash trong pty, giao diện AppKit thật, ảnh `daylight/docs/milestones/terminal-app-basesystem-26.6.png`, đã gửi). Chạy: `~/darling-port-tools/bsapp.sh System/Applications/Utilities/Terminal.app` (bundle bóng qua `shadowapp.py`; dyld nạp bridge qua biến `DARLING_GUI_BRIDGE` trước `main`; launchservicesd thật của Apple là job launchd qua `mkdaemons.py --only=launchservicesd`). Sửa kèm: `mach_port_is_connection_for_service` (libxpc "Bogus check-in"), asid 100000 trong audit token cho mọi task, remap chia sẻ trong darlingserver (AttributeGraph/SwiftUI). Tiếp: input bàn phím/chuột, kích hoạt app (key window), xuất X11.
0. **MỐC 3 (2026-10-31) — đổi hướng hiển thị:** hướng render server CA đã **gác lại** (`daylight/docs/ca-render-server-status.md`: AppKit không tạo `ContentLayer` cho view). Hướng mới **window canvases** (`daylight/docs/window-canvases.md`): `daylight/bridge/daylight-bridge.dylib` nằm trong app, xin Daylight bộ nhớ chia sẻ cho từng cửa sổ (msg riêng `0x44594c01/02/03`), để QuartzCore/AppKit thật vẽ cửa sổ vào đó (`renderInContext`), Daylight ghép thành `/private/tmp/daylight/desktop.png`. Đã chạy: nhiều cửa sổ, tiêu đề, nút Quit, font SF, bo góc (ảnh `daylight/docs/milestones/hello2-daylight-canvas*.png`, đã gửi). Sửa kèm: IOSurface (iokitd `IOSurfaceRootApple`, dyld vá trap IOKit), font hệ thống (dyld tìm đường dẫn ngắn của framework trong cache), `mk_timer_arm_leeway`, OOL rỗng physical-copy, libdispatch cooperative queue refcount, `Aqua.car` lấy từ Mac của người dùng (BaseSystem thiếu giao diện sáng; `appearance_overlay.py`). Tiếp: nạp bridge vào mọi app (DYLD_INSERT làm app GUI crash do không có notifyd → libnotify đệ quy), kích hoạt app (key window), input, xuất X11.
0. **MỐC 2 (2026-10-30):** `daylight-ca` (render server Core Animation của chính QuartzCore, job launchd `org.daylight.ca`) vẽ cửa sổ AppKit thật lên màn hình ảo `/private/tmp/daylight/screen.png` (ảnh `daylight/docs/milestones/daylight-screen-ca-render.png`, đã gửi). Thiếu: IOSurface (nội dung chữ/nút), khung cửa sổ thật (giải mã transaction 30429), xuất ra X11. Đã sửa thêm: tương thích XPC client Apple ↔ server Darling, membership UUID, /var/folders, workqueue tái dùng thread. Font: registry vẫn trống.
   **MỐC 1:** cửa sổ AppKit hello2 đã được AppKit vẽ (CPU) trên Darling, ảnh ở `daylight/docs/milestones/` (đã gửi người dùng). Display state 26.6 do `tools/display/make_state.py` sinh. Chữ còn là ô vuông (thiếu dịch vụ font). fontd của BaseSystem chạy được (job `com.apple.xtyped` qua `mkdaemons.py --only=fontd`) nhưng CoreText vẫn báo "Unable to substantiate font .SFNS-Regular"; notifyd làm launchd treo lúc khởi động, cfprefsd thoát 78 (chưa rõ). Tiếp: font (font registry ↔ fontd), rồi Daylight dựng CA render server để tự composite lên X11/PNG.
   **Hiện trạng trước đó:** CreateWindow trả memory entry 0xb0 (trạng thái cửa sổ chia sẻ); số hiệu routine 27↔26.6 được ánh xạ lại; reply mặc định theo kích thước bộ đệm của client. hello2 chạy hết kịch bản basic trên Darling (exit 0), CA commit tới Daylight. Còn thiếu: **bộ serialize display state đúng định dạng 26.6** (pseudo-code đã có ở `~/macos-re/ghidra-out/get_current_display_system_state.c`, project Ghidra đã phân tích sẵn ở `~/macos-re/ghidra-proj`), sau đó CA render server (QuartzCore SW renderer) để có pixel.
1. Daylight: cài 34003 (giao thức seed), 32006/34006 trả memory entry thật (scoreboard sự kiện, display shmem). Thu nội dung shmem trên Mac bằng cách hook `mach_vm_map` trong wsprobe2.
2. Chạy daemon của BaseSystem (notifyd, cfprefsd, distnoted, lsd/launchservicesd) qua dyl4 dưới launchd Darling; cần thêm routine check-in (xpc listener) vào `xpc_pipe_v5.c`.
3. Daylight host `CA::Render::Server` của QuartzCore với SWContext, xuất ra PNG/X11 → ảnh chụp cửa sổ Hello.

**Bẫy mới:** chạy container luôn chuyển hướng output (`darling shell true </dev/null >/dev/null 2>&1`), nếu không ssh treo vì darlingserver giữ stdout. `darling` là setuid: kill bằng sudo. Không chèn wsprobe2 vào app trên Darling khi chưa có notifyd (crash đệ quy libnotify).

## 5. Script (cùng có ở `~/darling-port-tools` trên cả hai máy)

**Trên aarch64:**
- `~/bin/x86 '<cmd>'`, `~/bin/mac '<cmd>'`: chạy lệnh trên máy test.
- `~/bin/dlast [-r]`: kéo `last.log` từ máy x86 về và symbolize crash. `-r` lấy luôn bản dyld và `libsystem_kernel` mới nhất.

**Trên x86:**
- `dl4.sh build [file.cpp...]`: biên dịch lại các file dyld, link, rồi cài vào `/usr/lib/dyl4`.
- `dl4.sh run <đường-dẫn-trong-~/basesystem> [args]`: chạy binary của BaseSystem qua dyl4 với cache 26.6.
  - Biến môi trường: `DL4_ENV="DARLING_DYLD_TRACE=1 DYLD_PRINT_..."`, `DL4_TIMEOUT`, `DL4_LINES`.
  - Script tự khởi động lại container nếu shellspawn chết.
- `batch.sh "bin/ls -la /" "usr/bin/sw_vers" ...`: chạy nhiều binary một lượt.
- `kinstall.sh`: build `libsystem_kernel` (dylib và bản tĩnh cho dyld), cài, rồi relink dyl4.
- `mkdispatch.sh [N]`: build `libdispatch_apple` và tóm tắt lỗi. Cài bằng `sudo make -C build/src/external/libdispatch-apple install`.
- `commit_all.sh "<message>"`: commit mọi submodule còn thay đổi và superproject (kèm dòng Co-Authored-By).
- `stackscan.py <pid>` (cần sudo): quét stack của thread đang treo để symbolize.
- `runapp.sh <binary trong ~/darltest/apps> [args]`: chạy binary macOS qua dyl4 + cache 26.6, in exit code.
- `withdaylight.sh <binary> [args]`: khởi động Daylight trong prefix rồi chạy như runapp.
- `overlay_basesystem.py`: đưa tài nguyên BaseSystem vào prefix (symlink).

## 6. Bẫy đã gặp

- **Không dùng `pkill -f`** với pattern xuất hiện trong chính dòng lệnh ssh, vì nó sẽ giết luôn phiên. Dùng `pkill -x mldr` hoặc `pkill -x darlingserver`.
- **Thay file đang chạy** (mldr, darlingserver): `cp` sang `.new` rồi `mv -f`. Chạy `darling shutdown` trước khi thay `libsystem_kernel`.
- **mldr thật** nằm ở `/usr/local/libexec/darling/usr/libexec/darling/mldr`.
- **Lệnh python hay heredoc có dấu `'`** trong chuỗi ssh sẽ vỡ quoting. Hãy ghi ra file, `scp` lên, rồi chạy.
- **Lệnh dài trên x86** (build lớn, chạy app): dùng `timeout`. Nếu vượt khoảng 10 phút thì chạy nền.
- **Include order với C++20:** `__has_feature(modules)` bằng 1 làm header xnu đi vào chế độ `__need_*`. Prelude của dyld include `<stddef.h>` trước để tránh lỗi này.
- **Không dán mật khẩu vào chat.**

## 7. Bước tiếp theo đề xuất (theo thứ tự)

1. **Thu dữ liệu trên Mac**, vì máy sắp hết hạn thuê:
   - mở rộng `wsprobe` để ghi cả message tới CARenderServer, XPC/launchd và cfprefsd;
   - viết thêm app test (`hello.m` với nhiều cửa sổ, menu, sheet, popover, text input, dark mode) và chạy với `HELLO_SECONDS`;
   - build bản **x86_64** của các app test (`clang -arch x86_64`) để chạy luôn trên Darling;
   - chụp ảnh tham chiếu bằng `screencapture -l<windowid>` hoặc `-R`, lưu vào `~/daylight/docs/reference/`;
   - kéo hết về máy này.
2. **Sửa kqueue cho userland Apple**, rồi kiểm tra `defaults`, `plutil` và tool Swift.
3. Sửa `ls`, `__mac_syscall` im lặng và chuỗi `EMULATED_*`.
4. **Daylight:** trả lời đủ 57 routine trong `trace-hello-macos27.log` (dùng bytes thật trong trace làm mẫu). Sau đó tới surface/backing store, vẽ bằng CPU ra X11, Wayland hoặc file PNG trước, và gửi ảnh cho người dùng.
5. launchd/XPC cho userland Apple: cfprefsd, `notifyd`, logd (hoặc stub).

Mỗi bước xong: commit (`commit_all.sh` trên x86; `git commit` trong `~/daylight` và `~/dyld-1378`) và cập nhật memory.

## 2026-11-01 — Web Browser browses (HTTP)
- Web Browser (WebKit UI + WebContent + GPU + Networking XPC services) loads and draws http pages, incl. http://example.com.
- Darling (port/macos26, xnu port/xnu-12377.121.6, dyld-1378): launchd PID-domain XPC services (`src/launchd/src/xpc_domain_v5.c`),
  mach_vm_map alignment, MAP_NORESERVE, darlingserver thread_info (no abort), AMFI/Sandbox/Quarantine MAC answers,
  `/.nofollow/`, dyld cache alias lookup, NECP (`impl/network/necp.c`), change_fdguard_np, connectx, SO_NREAD, libkqueue
  EVFILT_SOCK, libdispatch workloop identity + ulock rc, csops entitlements from the code signature (app-sandbox dropped),
  proc_pidinfo sizes, chflags no-op, renameatx_np.
- Jobs: BaseSystem runningboardd, trustd (+agent) via `darling-port-tools/mkdaemons.py`; DNS stand-in
  `~/daylight/services/dnssd/dnssd-standin.c` (job org.darling.dnssd-standin). Prefix: `/var -> private/var` restored,
  `overlay_basesystem.py` hides Darling's WebKit/AVFoundation stubs.
- Debug: `/var/log/xpc-v5.log`, `/var/log/xpc-services.log` in the prefix; `DL4_ENV="ACTIVITY_LOG_STDERR=1"`.
- Next: HTTPS (trustd agent os_unfair_lock "recursive" abort; TLS ENETDOWN), fast typing drops keys, then the other apps.

## 2026-11-03 — Web Browser browses the real internet by hostname (DNS solved)
- **Triệu chứng cũ:** Web Browser chỉ nạp được `http://<IP>/`; gõ `http://example.com` thì spinner quay mãi, **không có gói DNS nào** rời máy.
- **Gốc rễ:** NetworkProcess dùng `nw_resolver` của **libnetwork trong cache 26.6**. Nó lấy danh sách nameserver qua `dns_configuration_copy()`.
  - Bản trong cache gọi `com.apple.SystemConfiguration.DNSConfiguration` qua configd — Darling không phục vụ dịch vụ đó.
  - Bản `dns_configuration_copy()` của Darling (`configd/darling/src/dnsinfo/dnsinfo.c`) đã có sẵn bộ đọc `/etc/resolv.conf` nhưng **bị compile out (`#if 0`)**, luôn trả NULL.
- **Cách sửa (đúng triết lý "dùng đồ Apple trước, sửa Darling khi thiếu"):**
  1. `darling-configd` commit `1de5c00`: bật parser trong `dnsinfo.c`, đặt mặc định `port=53`, `timeout=5`, `search_order=DEFAULT_SEARCH_ORDER`.
  2. `dyld-1378` commit `4b10c6c`: thêm `/usr/lib/system/libsystem_configuration.dylib` vào `kernelLayerPaths` của `dyld/DyldProcessConfig.cpp`, buộc dyld lấy bản Darling (đọc resolv.conf) thay bản cache — cùng cách đã làm với `libsystem_info`.
  3. Superproject Darling `ee43614a` / `599c2f5e` cập nhật con trỏ Libinfo + configd.
- **Kiểm chứng:**
  - `DL4_TIMEOUT=25 bash ~/darling-port-tools/dl4.sh run usr/sbin/scutil --dns` in ra đúng search domain và 3 nameserver.
  - `WAITLOAD=30 bash /tmp/wbbrowse3.sh http://example.com` + `tcpdump port 53`: thấy **A và AAAA của example.com đi và về**; ảnh `wb-url.png` hiện trang "Example Domain" (đa ngữ + link Learn more).
- **Lưu ý:** `dns-sd -G v4` vẫn `-65563 (Service Not Running)` vì nó dùng đường Unix socket `/var/run/mDNSResponder` của `dnssd_clientstub`, không phải `nw_resolver`; không chặn WebKit. Việc dựng server DNS-SD thật vẫn là hạng mục riêng (mDNS/Bonjour).
- **Bước tiếp:** kiểm tra lại bộ 5 app GUI (Terminal, DiskImageMounter, installer .pkg, TextInputSwitcher/Language Chooser), rồi sang desktop Daylight-CA/WindowServer.

## 2026-11-03 — Hệ thống đầy đủ từ InstallAssistant (payloadv2) + desktop thật

- **Nguồn:** `payloadv2` của InstallAssistant là **Apple Archive (YAA1/AA01)** gói trong **pbzx**; đã giải nén
  `~/pv2raw/payload.000..052` (7.6 GB). `~/darling-port-tools/yaaextract.py` parse YAA dạng streaming, trích ra
  `~/fullsystem`: **458.716 mục → 272.091 file, 16.065 symlink, 170.559 thư mục (~19.3 GB)**.
  - Blob trong YAA là **raw** (không nén thêm); pbzx thì **xz**, trừ vài **khối literal** (`comp == uncomp`).
  - Mục YAA: `magic(YAA1|AA01) | u16 header_size | field... | blob...`; khoá 4 byte, byte cuối = kiểu
    (`1/2/4/8` uint, `P` string 2-byte len, `F/G/H/I/J` hash, `S/T` timespec, `A/B/C` cỡ blob, `*` flag).
  - **Symlink:** `TYP1='L'` + trường `LNKP` (chuỗi) là đích, **không** phải blob `DAT`. Lần trích đầu bỏ sót toàn bộ
    16.065 symlink → đã vá `yaaextract.py` (dùng `LNKP`) và thêm chế độ `--links-only` (tạo lại symlink mà không
    ghi lại ~19 GB dữ liệu).
- **`~/fullsystem` có** `System/Library/CoreServices/{Dock,Finder,SystemUIServer,ControlCenter,NotificationCenter}.app`,
  47 app trong `System/Applications`, 418 LaunchDaemon/LaunchAgent. **Không kèm dyld shared cache** → dùng cache
  của BaseSystem (cùng bản 26.6, hợp lệ).
- **Chạy app fullsystem:** `~/darling-port-tools/fsapp.sh <đường-dẫn-trong-~/fullsystem>` — shadow bundle
  (`shadowapp.py` với `SHADOW_ROOT`/`SHADOW_OUT`), cache BaseSystem, bridge như `bsapp.sh`.
  `fsdesktop.sh` chạy nhiều app trong **một** phiên Daylight; `fstest.sh APP TAG [giây...]` chạy + chụp ảnh;
  `shotloop.sh` giữ `~/shots/00-desktop-now.png` mới cho web.
- **Daylight:** shell dựng sẵn **tắt** khi chạy desktop thật (`DAYLIGHT_SHELL=0`, mặc định trong `fsapp.sh`);
  ghép canvas theo **`(level, thứ tự flush)`** để Dock/menu bar luôn ở trên cửa sổ thường.
- **Web:** gallery công khai tại **http://remotemac.mooo.com:8080/shots/** (websockify `0.0.0.0:8080`, cổng 8080 là
  cổng duy nhất mở qua firewall).

## 2026-11-03 (tiếp) — Dock thật chạy sống, SwiftUI bridge, CA render server

- **Dock thật (SwiftUI) chạy sống** qua 45–110s: tạo 6 cửa sổ `0x1f6–0x1fb`, `SetDockRectWithReason`,
  `GetAppearanceTheme`, `TransactionCommitAsync`, rồi vào `ep_poll` (vòng lặp chính). Không crash.
  Chỉ log `Couldn't connect to the usernoted` (không nghiêm trọng).
- **dyl4 nhận diện SwiftUI** (commit `041388c`): `hasAppKit || hasSwiftUI` → bridge nạp vào Dock thật
  ("AppKit not loaded, SwiftUI loaded"). Đã build + cài dyl4 (`md5 7d67000…`).
- **Probe `DAYLIGHT_BRIDGE_TICK`**: thread nền báo `NSApp=0x0 windows=0` liên tục → Dock (SwiftUI daemon)
  **không có `NSApplication`**. Bridge `NSWindow`/`renderInContext` không áp dụng được cho Dock.
- **Overlay fullsystem** (20.677 link, đã vá skip symlink hỏng tới cryptex): `TimelineUI` và framework
  trên đĩa phân giải được; Finder nạp bridge và đi tới CoreGraphics.
- **Finder** nạp bridge và đi tới CoreGraphics (`CreateApplication`, `SessionDeathWatchPort`, `PlaceTap`,
  `SetNotifications`) rồi **segfault** trong `DesktopServicesPriv` `TNode::GetNodeFromPath` (null deref,
  fault `0x10`) gọi từ `TGlobalNodes::HomeNode`.
- **CA render server** (`daylight-ca`) đã sửa để chạy được (wrapper dyl4 + env): `render server port 0x0,
  registered=1, look_up kr=0`. Thêm scan context 1..65536 → **không tìm thấy context nào từ Dock** ⇒ Dock
  không commit CA layer (hoặc chưa commit). `DAYLIGHT_SHOW_TREE=10` không in vì không có context trong
  bảng cửa sổ.
- **Hello2/AppKit test** không chạy được do toolchain mismatch (`_OBJC_CLASS_$_NSConstantArray` không có
  trong CF 26.6 khi build với SDK macOS 27) và AppKit cần display (crash `X11Display`). CA path với AppKit
  chưa xác nhận được trong môi trường này.
- **Blocker hiện tại:** Dock SwiftUI không vẽ (không NSApp, không CA context, không direct backing store).
  Cần điều tra sâu hơn: (a) Dock có commit CA qua tên khác? (b) Dock cần session/loginwindow để kích hoạt
  drawing? (c) SwiftUI rendering trên Darling có bị chặn bởi `NSViewBackingLayerContents` như AppKit?
- **Files mới/thay đổi:** `~/daylight/bridge/daylight-bridge.m` (tick probe, `__bridge`), `~/daylight/server/src/main_darling.c`
  (dump body gated by `DAYLIGHT_DUMP_PID`), `~/daylight/ca/daylight-ca.m` (scan context + bootstrap lookup),
  `~/darling-port-tools/overlay_basesystem.py` (skip dangling symlink), `~/darltest/daemons/daylight-ca-wrapper.sh`,
  `~/fullsystem` + prefix overlay fullsystem.
- **Web:** gallery công khai tại **http://remotemac.mooo.com:8080/shots/**.


## Latest: Glass panel + web live (2026-11-03)

### Achieved
- Dock.app (original Apple binary, patched loader only) runs and creates 75+ tiles
- Web 8080 live view: http://remotemac.mooo.com:8080/shots/ (JPEG refresh 1s)
- Glass panel: Daylight compositor `glass_draw()` — real backdrop blur + tint + highlight + border
  - bridge sends floor rect via DL_BRIDGE_GLASS when DAYLIGHT_BRIDGE_CA_GLASS=1
  - server composites glass over backdrop, then canvas (icons) on top
- CA render server attempts native SDF backdrop sampling (data2=backdrop)

### Known limitations
- App icons: renderInContext: doesn't capture DOCKTileLayer contents; IconServices blocked by Darling (missing RenderBox binary, lsregister symbol mismatch)
- App launching: most apps depend on shared-cache-only frameworks that Darling doesn't load
- Native CASDF glass: software renderer produces flat grey/blue, not glass; glass_draw is the compositor-level replacement

## Native glass investigation (2026-11-03)

### Tested renderers for CASDF glass:
- CA::OGL software (default): flat grey bar (5e5e5e) without backdrop; backdrop copy (2e5285) with backdrop
- CA::RenderCG: flat blue bar (2e5285), no glass blur/tint
- Both provide SDF geometry (rounded rect) but NOT glass material (no blur/tint/highlight)

### Conclusion:
Native CASDF glass requires GPU/Metal rendering. Under Darling (CPU-only), the software renderers cannot evaluate the glass material shader. The Daylight server's `glass_draw()` (backdrop blur + tint + highlight + border in the compositor) is the correct WindowServer-level implementation — it samples the actual composited backdrop and applies a glass material, exactly what macOS WindowServer does for material evaluation.

### DockGlassMaterialLayerController:
Found in DockCore: methods `configureUsingConfiguration:`, `rebuildLayersWithEnvironment:`, `rootLayer`. This controller manages the glass material layers. However, without GPU, the material shaders it creates cannot be evaluated by software repeaters.

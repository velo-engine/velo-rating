# Velo Rating SDK for V & Velo

Module đánh giá ứng dụng (**App Rating & In-App Review**) đa nền tảng dành cho game và ứng dụng phát triển bằng ngôn ngữ [V](https://vlang.io) và engine [Velo](https://github.com/velo-engine).

Hỗ trợ native:
- **iOS**: Tích hợp trực tiếp **StoreKit** (`SKStoreReviewController`), tự động chọn `requestReviewInScene:` trên iOS 14+ hoặc `requestReview` trên iOS 10.3+. Hỗ trợ mở trang viết đánh giá trực tiếp qua URL scheme `itms-apps://` với fallback web URL an toàn. Hoạt động trên cả thiết bị thật và iOS Simulator mà không cần cài đặt thêm Pods hay thư viện ngoài.
- **Android**: 
  - Hỗ trợ **Google Play In-App Review API** qua lớp cầu nối Java `RatingBridge.java` và JNI. Sử dụng Reflection & Dynamic Proxy nên biên dịch độc lập, an toàn tuyệt đối.
  - Tích hợp sẵn cơ chế **Pure JNI Intent fallback**: Mở trực tiếp trang Google Play Store (`market://details?id=...` hoặc `https://play.google.com/store/apps/details?id=...`) ngay cả khi không nhúng Google Play Review SDK.
- **Desktop (macOS, Windows, Linux) / Web / Testing**: Chế độ Mock thông minh ghi nhận trạng thái kiểm thử, tự động hoàn thành luồng và sinh URL chính xác, không gây crash khi chạy trên máy tính phát triển hoặc CI.
- **Smart Prompting**: Tích hợp sẵn bộ lọc điều kiện thông minh (số session tối thiểu, số sự kiện tích cực/màn thắng, thời gian cooldown giãn cách) để tránh làm phiền người chơi và tuân thủ chính sách của Apple/Google.

---

## 1. Cài đặt

### Cách 1: Thêm vào `v.mod` của dự án

```v
Module {
	name: 'my_game'
	version: '1.0.0'
	dependencies: ['rating']
}
```

### Cách 2: Liên kết phát triển cục bộ (`v link`)

Trong thư mục `velo-rating`:

```bash
cd /Users/antn/Documents/github/velo-rating
v link
```

Lệnh `v link` sẽ tạo symlink `rating` trong thư mục `~/.vmodules/`, cho phép bất kỳ dự án Velo nào có thể gọi `import rating`.

---

## 2. Thiết lập nền tảng

### 2.1 iOS (Không cần cấu hình thêm)

StoreKit đã được tích hợp sẵn trong iOS SDK. Khi biên dịch với `velo build ios`, cờ `#flag -framework StoreKit` sẽ tự động liên kết framework của Apple.

### 2.2 Android

Module hỗ trợ 2 chế độ trên Android:

1. **Chế độ Tiêu chuẩn (Không cần cài thêm thư viện)**:
   Module tự động dùng JNI gọi Intent mở trang Google Play Store của ứng dụng. Hoạt động 100% không cần tải thêm dependency nào.

2. **Chế độ In-App Review (Google Play In-App Review API)**:
   Để hiển thị hộp thoại đánh giá nổi trực tiếp bên trong ứng dụng, chạy script tải thư viện Google Play Review:
   ```bash
   python3 tools/fetch_deps.py android --project /path/to/your/game
   ```
   Script sẽ tự động:
   - Tải `com.google.android.play:review:2.0.2` vào `<project>/android/deps/jars/`
   - Cài đặt `RatingBridge.java` vào `<project>/android/deps/src/io/velo/rating/`

---

## 3. Hướng dẫn sử dụng

### 3.1 Khởi tạo

Khởi tạo một lần khi game khởi động:

```v
import rating

rating.init(rating.Config{
    app_id_ios:           '6738912345'              // ID ứng dụng trên App Store
    package_name_android: 'com.sg.gold.digger'      // Package ID trên Android (hoặc để trống để tự phát hiện)
    fallback_to_store:    true                      // Tự động mở store nếu in-app review không khả dụng
    min_sessions:         3                         // Số lần mở game tối thiểu trước khi tự động xin review
    min_events:           5                         // Số màn chơi hoàn thành tối thiểu
    cooldown_days:        30                        // Giãn cách 30 ngày giữa các lần xin review
    debug:                true                      // Bật log debug
})
```

### 3.2 Nút "Đánh giá game" thủ công (Ví dụ trong Settings)

Khi người chơi chủ động bấm nút "Đánh giá" hoặc "Rate Us" trong menu cài đặt:

```v
// Mở thẳng trang viết đánh giá trên App Store hoặc Google Play
rating.open_write_review()

// Hoặc mở trang thông tin ứng dụng trên Store:
// rating.open_store()
```

### 3.3 Tự động nhắc đánh giá theo điều kiện thông minh

Để giữ tỉ lệ đánh giá 5 sao cao và không vi phạm chính sách của Apple (giới hạn 3 lần/năm) hay Google, chỉ nhắc đánh giá sau các khoảnh khắc tích cực của người chơi:

```v
// 1. Ghi nhận khi mở game (mỗi session):
rating.record_session()

// 2. Ghi nhận khi hoàn thành màn chơi hoặc đạt kỷ lục:
rating.record_event()

// 3. Kiểm tra điều kiện và tự động hiện review nếu thỏa mãn:
if rating.prompt_review_if_ready() {
    println('Đã kích hoạt hộp thoại đánh giá!')
}
```

### 3.4 Lưu và phục hồi trạng thái với `core.Store`

Bạn có thể lưu các biến đếm vào `core.Store` của Velo để không bị mất khi người chơi tắt game:

```v
// Lưu:
sess, evts, prmpts, ts, rated := rating.export_state()
store.set_int('rating_sessions', sess)
store.set_int('rating_events', evts)
store.set_int('rating_prompts', prmpts)
store.set_i64('rating_last_ts', ts)
store.set_int('rating_has_rated', if rated { 1 } else { 0 })
store.save() or {}

// Phục hồi:
rating.restore_state(
    store.get_int('rating_sessions', 0),
    store.get_int('rating_events', 0),
    store.get_int('rating_prompts', 0),
    store.get_i64('rating_last_ts', 0),
    store.get_int('rating_has_rated', 0) == 1,
)
```

---

## 4. Bảng API chính

| Hàm | Mô tả |
| :--- | :--- |
| `rating.init(cfg Config)` | Khởi tạo module với cấu hình nền tảng |
| `rating.request_review()` | Gọi native in-app review dialog (StoreKit / Play Review) |
| `rating.open_store()` | Mở trang thông tin ứng dụng trên Store |
| `rating.open_write_review()` | Mở thẳng trang viết nhận xét & chấm sao |
| `rating.is_available()` | Kiểm tra in-app review có được hỗ trợ trên thiết bị |
| `rating.poll_result()` | Lấy kết quả review bất đồng bộ (`.none`, `.completed`, `.failed`) |
| `rating.record_session()` | Tăng biến đếm số phiên mở game |
| `rating.record_event()` | Tăng biến đếm sự kiện tích cực (thắng màn, phá kỷ lục) |
| `rating.should_prompt_review()` | Kiểm tra xem người chơi đã đủ điều kiện nhận nhắc nhở chưa |
| `rating.prompt_review_if_ready()` | Tự động kích hoạt review nếu đủ điều kiện |
| `rating.mark_rated()` | Đánh dấu người chơi đã đánh giá để không nhắc lại nữa |
| `rating.export_state()` | Trích xuất trạng thái đếm để lưu vào Store |
| `rating.restore_state(...)` | Phục hồi trạng thái đếm từ Store |

module rating

$if android {
	#flag -I @DIR
	#include "@DIR/rating_android.h"

	fn C.sapp_android_get_native_activity() voidptr

	fn C.velo_rating_android_init(activity voidptr)
	fn C.velo_rating_android_is_available(activity voidptr) int
	fn C.velo_rating_android_request_review(activity voidptr, fallback_pkg &char, fallback_to_store int) int
	fn C.velo_rating_android_open_store(activity voidptr, pkg &char) int
	fn C.velo_rating_android_poll_result(activity voidptr) int

	fn init_android(cfg Config) {
		act := C.sapp_android_get_native_activity()
		if act == unsafe { nil } {
			return
		}
		C.velo_rating_android_init(act)
	}

	fn is_available_android() bool {
		act := C.sapp_android_get_native_activity()
		if act == unsafe { nil } {
			return false
		}
		return C.velo_rating_android_is_available(act) == 1
	}

	fn request_review_android(pkg string, fallback_to_store bool) bool {
		act := C.sapp_android_get_native_activity()
		if act == unsafe { nil } {
			return false
		}
		fb := if fallback_to_store { 1 } else { 0 }
		return C.velo_rating_android_request_review(act, &char(pkg.str), fb) == 1
	}

	fn open_store_android(pkg string) bool {
		act := C.sapp_android_get_native_activity()
		if act == unsafe { nil } {
			return false
		}
		return C.velo_rating_android_open_store(act, &char(pkg.str)) == 1
	}

	fn poll_result_android() RatingResult {
		act := C.sapp_android_get_native_activity()
		if act == unsafe { nil } {
			return .none
		}
		return match C.velo_rating_android_poll_result(act) {
			1 { RatingResult.completed }
			2 { RatingResult.failed }
			else { RatingResult.none }
		}
	}
} $else {
	fn init_android(cfg Config) {}
	fn is_available_android() bool {
		return false
	}
	fn request_review_android(pkg string, fallback_to_store bool) bool {
		return false
	}
	fn open_store_android(pkg string) bool {
		return false
	}
	fn poll_result_android() RatingResult {
		return .none
	}
}

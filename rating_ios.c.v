module rating

$if ios {
	#flag -I @DIR
	#flag -framework StoreKit
	#flag -framework UIKit
	#include "@DIR/rating_ios.h"

	fn C.velo_rating_ios_init()
	fn C.velo_rating_ios_is_available() int
	fn C.velo_rating_ios_request_review() int
	fn C.velo_rating_ios_open_store(&char, int) int

	fn init_ios(cfg Config) {
		C.velo_rating_ios_init()
	}

	fn is_available_ios() bool {
		return C.velo_rating_ios_is_available() == 1
	}

	fn request_review_ios(app_id string, fallback_to_store bool) bool {
		res := C.velo_rating_ios_request_review()
		if res == 0 && fallback_to_store && app_id != '' {
			return open_store_ios(app_id, true)
		}
		return res == 1
	}

	fn open_store_ios(app_id string, write_review bool) bool {
		if app_id == '' {
			return false
		}
		w := if write_review { 1 } else { 0 }
		return C.velo_rating_ios_open_store(&char(app_id.str), w) == 1
	}
} $else {
	fn init_ios(cfg Config) {}
	fn is_available_ios() bool {
		return false
	}
	fn request_review_ios(app_id string, fallback_to_store bool) bool {
		return false
	}
	fn open_store_ios(app_id string, write_review bool) bool {
		return false
	}
}

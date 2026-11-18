module rating

import time

// RatingResult represents the result or status of an in-app rating flow.
pub enum RatingResult {
	none      // No active review flow or result pending
	requested // Review request has been submitted to system
	completed // Review flow finished (user rated, dismissed, or system completed)
	failed    // In-app review could not be presented or failed
}

// Config holds the platform store identifiers and prompting criteria.
pub struct Config {
pub:
	app_id_ios           string // Apple App Store ID (e.g. "6738912345" or "id6738912345")
	package_name_android string // Android package name (e.g. "com.sg.gold.digger")
	fallback_to_store    bool = true // If in-app review is unavailable, open store page
	min_sessions         int  = 3    // Minimum app sessions before automatic prompting
	min_events           int  = 5    // Minimum positive events/milestones before prompting
	cooldown_days        int  = 30   // Cooldown period in days between review prompts
	debug                bool        // Enable verbose console logging
}

// RatingManager manages configuration, state, and rate-limiting counters.
pub struct RatingManager {
pub mut:
	initialized       bool
	cfg               Config
	session_count     int
	event_count       int
	prompt_count      int
	last_prompt_ts    i64
	has_rated         bool
	// Mock testing state
	mock_requested    bool
	mock_store_opened bool
	mock_last_url     string
}

const default_manager = &RatingManager{}

// get returns the shared RatingManager singleton.
pub fn get() &RatingManager {
	return default_manager
}

// init initializes the rating SDK with configuration.
pub fn init(cfg Config) {
	mut s := get()
	s.cfg = cfg
	s.initialized = true

	$if android {
		init_android(cfg)
	} $else $if ios {
		init_ios(cfg)
	} $else {
		s.mock_requested = false
		s.mock_store_opened = false
		s.mock_last_url = ''
	}

	if cfg.debug {
		println('[rating] Initialized with iOS ID="${cfg.app_id_ios}", Android pkg="${cfg.package_name_android}"')
	}
}

// config returns the current Rating configuration.
pub fn config() Config {
	return get().cfg
}

// is_initialized returns whether init() has been called.
pub fn is_initialized() bool {
	return get().initialized
}

// is_mock reports whether the current platform uses mock behavior (e.g. desktop/test).
pub fn is_mock() bool {
	$if android || ios {
		return false
	}
	return true
}

// is_available checks if native in-app review is supported on this platform/device.
pub fn is_available() bool {
	$if android {
		return is_available_android()
	} $else $if ios {
		return is_available_ios()
	} $else {
		return true
	}
}

// request_review triggers the native in-app review dialog.
// Uses StoreKit SKStoreReviewController on iOS, Google Play In-App Review on Android.
// If in-app review is unavailable and fallback_to_store is true, it opens the store page.
pub fn request_review() bool {
	return request_review_with_fallback(get().cfg.fallback_to_store)
}

// request_review_with_fallback triggers review dialog with explicit fallback choice.
pub fn request_review_with_fallback(fallback_to_store bool) bool {
	mut s := get()
	if !s.initialized {
		if s.cfg.debug {
			eprintln('[rating] Warning: request_review called before init()')
		}
	}

	$if android {
		return request_review_android(s.cfg.package_name_android, fallback_to_store)
	} $else $if ios {
		return request_review_ios(clean_ios_app_id(s.cfg.app_id_ios), fallback_to_store)
	} $else {
		s.mock_requested = true
		if s.cfg.debug {
			println('[rating] (mock) Native in-app review dialog requested')
		}
		if fallback_to_store && !s.mock_store_opened {
			open_store()
		}
		return true
	}
}

// open_store opens the app product page on Google Play Store or Apple App Store.
pub fn open_store() bool {
	s := get()
	return open_store_page(s.cfg.app_id_ios, s.cfg.package_name_android)
}

// open_store_page opens store page for explicitly provided IDs.
pub fn open_store_page(app_id_ios string, pkg_android string) bool {
	mut s := get()
	$if android {
		return open_store_android(pkg_android)
	} $else $if ios {
		return open_store_ios(clean_ios_app_id(app_id_ios), false)
	} $else {
		s.mock_store_opened = true
		cleaned_ios := clean_ios_app_id(app_id_ios)
		url := if cleaned_ios != '' {
			'https://apps.apple.com/app/id${cleaned_ios}'
		} else if pkg_android != '' {
			'https://play.google.com/store/apps/details?id=${pkg_android}'
		} else {
			'https://store.example.com'
		}
		s.mock_last_url = url
		if s.cfg.debug {
			println('[rating] (mock) Opened store page: ${url}')
		}
		return true
	}
}

// open_write_review opens direct "write review" page on the App Store or Google Play.
pub fn open_write_review() bool {
	mut s := get()
	$if android {
		return open_store_android(s.cfg.package_name_android)
	} $else $if ios {
		return open_store_ios(clean_ios_app_id(s.cfg.app_id_ios), true)
	} $else {
		s.mock_store_opened = true
		cleaned_ios := clean_ios_app_id(s.cfg.app_id_ios)
		url := if cleaned_ios != '' {
			'https://apps.apple.com/app/id${cleaned_ios}?action=write-review'
		} else if s.cfg.package_name_android != '' {
			'https://play.google.com/store/apps/details?id=${s.cfg.package_name_android}'
		} else {
			'https://store.example.com'
		}
		s.mock_last_url = url
		if s.cfg.debug {
			println('[rating] (mock) Opened write review page: ${url}')
		}
		return true
	}
}

// poll_result polls for asynchronous review status changes on mobile.
pub fn poll_result() RatingResult {
	$if android {
		return poll_result_android()
	} $else $if ios {
		return .none
	} $else {
		mut s := get()
		if s.mock_requested {
			s.mock_requested = false
			return .completed
		}
		return .none
	}
}

// clean_ios_app_id strips leading 'id' prefix if present from App Store ID.
pub fn clean_ios_app_id(id string) string {
	trimmed := id.trim_space()
	if trimmed.to_lower().starts_with('id') && trimmed.len > 2 {
		return trimmed[2..].trim_space()
	}
	return trimmed
}

// ---------- Smart Criteria & Rate-Limiting Helpers ----------

// record_session increments the session launch counter.
pub fn record_session() {
	mut s := get()
	s.session_count++
	if s.cfg.debug {
		println('[rating] Session recorded: ${s.session_count}/${s.cfg.min_sessions}')
	}
}

// record_event increments the positive milestone/event counter.
pub fn record_event() {
	mut s := get()
	s.event_count++
	if s.cfg.debug {
		println('[rating] Milestone event recorded: ${s.event_count}/${s.cfg.min_events}')
	}
}

// mark_rated marks that the user has already rated or declined to rate.
pub fn mark_rated() {
	mut s := get()
	s.has_rated = true
}

// should_prompt_review checks whether all prompting criteria and cooldowns are met.
pub fn should_prompt_review() bool {
	s := get()
	if !s.initialized || s.has_rated {
		return false
	}
	if s.session_count < s.cfg.min_sessions {
		return false
	}
	if s.event_count < s.cfg.min_events {
		return false
	}
	if s.prompt_count > 0 {
		now := time.now().unix()
		cooldown_seconds := i64(s.cfg.cooldown_days) * 86400
		if now - s.last_prompt_ts < cooldown_seconds {
			return false
		}
	}
	return true
}

// prompt_review_if_ready triggers in-app review only if criteria are satisfied.
// Updates prompt count and timestamp upon triggering.
pub fn prompt_review_if_ready() bool {
	if !should_prompt_review() {
		return false
	}
	mut s := get()
	s.prompt_count++
	s.last_prompt_ts = time.now().unix()
	return request_review()
}

// reset_counters resets sessions, events, and prompt history.
pub fn reset_counters() {
	mut s := get()
	s.session_count = 0
	s.event_count = 0
	s.prompt_count = 0
	s.last_prompt_ts = 0
	s.has_rated = false
}

// export_state returns the current state for persistence (sessions, events, prompts, last_ts, has_rated).
pub fn export_state() (int, int, int, i64, bool) {
	s := get()
	return s.session_count, s.event_count, s.prompt_count, s.last_prompt_ts, s.has_rated
}

// restore_state loads saved state values.
pub fn restore_state(session_count int, event_count int, prompt_count int, last_prompt_ts i64, has_rated bool) {
	mut s := get()
	s.session_count = session_count
	s.event_count = event_count
	s.prompt_count = prompt_count
	s.last_prompt_ts = last_prompt_ts
	s.has_rated = has_rated
}

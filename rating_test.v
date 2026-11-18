module rating

fn test_rating_init_and_config() {
	cfg := Config{
		app_id_ios:           'id6738912345'
		package_name_android: 'com.sg.gold.digger'
		fallback_to_store:    true
		min_sessions:         3
		min_events:           5
		cooldown_days:        30
		debug:                false
	}

	init(cfg)

	assert is_initialized()
	c := config()
	assert c.app_id_ios == 'id6738912345'
	assert c.package_name_android == 'com.sg.gold.digger'
	assert c.fallback_to_store == true
	assert c.min_sessions == 3
	assert c.min_events == 5
	assert c.cooldown_days == 30
}

fn test_clean_ios_app_id() {
	assert clean_ios_app_id('id6738912345') == '6738912345'
	assert clean_ios_app_id('ID6738912345') == '6738912345'
	assert clean_ios_app_id('6738912345') == '6738912345'
	assert clean_ios_app_id('  id12345  ') == '12345'
	assert clean_ios_app_id('') == ''
}

fn test_mock_review_flow() {
	init(Config{
		app_id_ios:           '6738912345'
		package_name_android: 'com.sg.gold.digger'
		fallback_to_store:    false
	})

	assert is_mock()
	assert is_available()

	// Initial poll is none
	assert poll_result() == .none

	// Trigger review
	ok := request_review()
	assert ok

	// In mock mode, poll returns completed once
	res := poll_result()
	assert res == .completed

	// Subsequent poll returns none
	assert poll_result() == .none
}

fn test_mock_open_store() {
	init(Config{
		app_id_ios:           '6738912345'
		package_name_android: 'com.sg.gold.digger'
	})

	// Open store
	ok1 := open_store()
	assert ok1
	mgr := get()
	assert mgr.mock_store_opened
	assert mgr.mock_last_url.contains('apps.apple.com/app/id6738912345')

	// Open write review
	ok2 := open_write_review()
	assert ok2
	assert mgr.mock_last_url.contains('action=write-review')
}

fn test_smart_prompt_criteria() {
	init(Config{
		app_id_ios:           '6738912345'
		package_name_android: 'com.sg.gold.digger'
		min_sessions:         2
		min_events:           3
		cooldown_days:        14
	})

	reset_counters()

	// 0 sessions, 0 events -> should not prompt
	assert should_prompt_review() == false
	assert prompt_review_if_ready() == false

	// 1 session, 0 events -> should not prompt
	record_session()
	assert should_prompt_review() == false

	// 2 sessions, 2 events -> should not prompt (needs 3 events)
	record_session()
	record_event()
	record_event()
	assert should_prompt_review() == false

	// 2 sessions, 3 events -> ready!
	record_event()
	assert should_prompt_review() == true

	// Prompt triggered
	prompted := prompt_review_if_ready()
	assert prompted

	// Immediately afterwards, cooldown is active -> should not prompt again
	assert should_prompt_review() == false

	// Mark user as rated -> never prompt again
	reset_counters()
	mark_rated()
	record_session()
	record_session()
	record_event()
	record_event()
	record_event()
	assert should_prompt_review() == false
}

fn test_export_and_restore_state() {
	init(Config{
		app_id_ios:           '6738912345'
		package_name_android: 'com.sg.gold.digger'
	})

	reset_counters()
	record_session()
	record_session()
	record_event()

	sess, evts, prmpts, ts, rated := export_state()
	assert sess == 2
	assert evts == 1
	assert prmpts == 0
	assert rated == false

	// Reset then restore
	reset_counters()
	assert get().session_count == 0

	restore_state(sess, evts, 5, 123456789, true)
	s := get()
	assert s.session_count == 2
	assert s.event_count == 1
	assert s.prompt_count == 5
	assert s.last_prompt_ts == 123456789
	assert s.has_rated == true
}

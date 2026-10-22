# Additional actual-field fixture; never replaces the default GUI test object.
# make -f Makefile -f tests/stream_search_ui.mk UI_TEST=1 stream-search-ui-test
ifneq ($(UI_TEST),1)
$(error Streaming search fixture requires UI_TEST=1)
endif

STREAM_SEARCH_UI_OBJS = $(filter-out build_host/tests/graphite_ui_test.o,$(HOST_OBJS))

.PHONY: stream-search-ui-test
stream-search-ui-test: sonix_player_host_stream_search_test

build_host/tests/stream_search_placeholder_ui_test.o: tests/stream_search_placeholder_ui_test.c tests/graphite_ui_test.c $(wildcard tests/graphite_*.inc)
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

sonix_player_host_stream_search_test: $(STREAM_SEARCH_UI_OBJS) build_host/tests/stream_search_placeholder_ui_test.o
	$(HOST_LD) -o $@ $^ $(HOST_LDFLAGS)

-include build_host/tests/stream_search_placeholder_ui_test.d

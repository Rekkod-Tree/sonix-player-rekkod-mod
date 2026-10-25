# Additive host-only fixtures. No production/test object is overwritten.
# make -f Makefile -f tests/mono_ui.mk UI_TEST=1 mono-ui-tests
ifneq ($(UI_TEST),1)
$(error Mono fixtures require UI_TEST=1)
endif

MONO_UI_OBJS = $(filter-out build_host/tests/graphite_ui_test.o,$(HOST_OBJS))
MONO_UI_INCLUDES = tests/graphite_ui_test.c $(wildcard tests/graphite_*.inc)

.PHONY: mono-ui-tests
mono-ui-tests: sonix_player_host_mono_capacity_test

build_host/tests/mono_capacity_ui_test.o: tests/mono_capacity_ui_test.c $(MONO_UI_INCLUDES)
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

sonix_player_host_mono_capacity_test: $(MONO_UI_OBJS) build_host/tests/mono_capacity_ui_test.o
	$(HOST_LD) -o $@ $^ $(HOST_LDFLAGS)

-include build_host/tests/mono_capacity_ui_test.d

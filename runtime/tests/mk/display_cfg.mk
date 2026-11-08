# native-test-display_cfg：包内显示默认值（<模块目录>/s2a_display.tsv，runtime/src/display_cfg.c）。纯 C，不要 GPU：
#   S2A_DOCS 不用（读的是给定目录），各用例在临时目录里写清单。
NATIVE_TEST_OWN += display_cfg
.PHONY: native-test-display_cfg
native-test-display_cfg:
	@mkdir -p $(BUILD)/tests
	$(CC) $(CFLAGS) -I runtime/include -o $(BUILD)/tests/t_display_cfg runtime/tests/t_display_cfg.c runtime/src/display_cfg.c
	timeout 30 $(BUILD)/tests/t_display_cfg
TEST_TARGETS += native-test-display_cfg

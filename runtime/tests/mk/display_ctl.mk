# native-test-display_ctl：运行中改显示参数（<S2A_DOCS>/display.txt，runtime/src/display_ctl.c）。纯 C，不要 GPU：
#   解析是纯函数；执行侧用替身观察 nvn_fps_set / s2a_fps_set_enabled（dlsym(RTLD_DEFAULT) 取，所以要 -export_dynamic）。
NATIVE_TEST_OWN += display_ctl
.PHONY: native-test-display_ctl
native-test-display_ctl:
	@mkdir -p $(BUILD)/tests
	$(CC) $(CFLAGS) -Wl,-export_dynamic -I runtime/include -o $(BUILD)/tests/t_display_ctl \
	    runtime/tests/t_display_ctl.c runtime/src/display_ctl.c
	timeout 30 $(BUILD)/tests/t_display_ctl
TEST_TARGETS += native-test-display_ctl

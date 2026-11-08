# native-test-nvn_fps：帧率上限的取值与运行中改（graphics/nvn/gfx/nvn/nvn_fps.c）。纯 C，不要 GPU / Metal：
#   起局读环境那一次每个取值起一个子进程，运行中改走 nvn_fps_set。显示器速率用替身（144 Hz）。
NATIVE_TEST_OWN += nvn_fps
.PHONY: native-test-nvn_fps
native-test-nvn_fps:
	@mkdir -p $(BUILD)/tests
	$(CC) $(CFLAGS) -o $(BUILD)/tests/t_nvn_fps graphics/nvn/tests/t_nvn_fps.c graphics/nvn/gfx/nvn/nvn_fps.c -lm -lpthread
	timeout 30 $(BUILD)/tests/t_nvn_fps
TEST_TARGETS += native-test-nvn_fps

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include "aviutl2_sdk/module2.h"
#include "aviutl2_sdk/filter2.h" 


EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	return true;
}


EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

static void apply_alpha(SCRIPT_MODULE_PARAM* param) {
	auto n = param->get_param_num();
	if (n != 5) {
		param->set_error("引数の数が正しくありません");
		return;
	}
	auto base = (PIXEL_RGBA*)param->get_param_data(0);
	auto mask = (PIXEL_RGBA*)param->get_param_data(1);
	auto w = param->get_param_int(2);
	auto h = param->get_param_int(3);
	auto invert = param->get_param_boolean(4);
	if (!base || !mask || w <= 0 || h <= 0) {
		param->set_error("引数の値が正しくありません");
		return;
	}

	std::uint64_t ww = (std::uint64_t)w;
	std::uint64_t hh = (std::uint64_t)h;
	std::uint64_t count64 = ww * hh;
	if (ww != 0 && count64 / ww != hh) {
		param->set_error("画像サイズが大きすぎます");
		return;
	}
	if (count64 > (std::uint64_t)(std::numeric_limits<size_t>::max)()) {
		param->set_error("画像サイズが大きすぎます");
		return;
	}
	size_t count = (size_t)count64;
	for (size_t i = 0; i < count; ++i) {
		std::uint8_t a = (std::uint8_t)mask[i].r; // マスク動画はRに入っている想定
		if (invert) a = (std::uint8_t)(255u - a);
		base[i].a = a;
	}
}

SCRIPT_MODULE_FUNCTION functions[] = {
    { L"apply_alpha", apply_alpha },
	{ nullptr }
};


SCRIPT_MODULE_TABLE script_module_table = {
	L"sam3mask",	// モジュールの表示名
	functions
};

EXTERN_C __declspec(dllexport) SCRIPT_MODULE_TABLE* GetScriptModuleTable(void) {
	return &script_module_table;
}
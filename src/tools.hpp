#pragma once
#include "config.hpp"

namespace tools {
	struct params {
		static constexpr uint32_t WIDTH = 1800; // 1800
		static constexpr uint32_t HEIGHT = 900; // 900
	};
	
	template<typename T>
	[[nodiscard]] T get_CreateInfo(auto& factory) {
		auto pinfo = reinterpret_cast<T*>(&factory);
		return *pinfo;
	}

	std::vector <const char*> requested_extensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
}
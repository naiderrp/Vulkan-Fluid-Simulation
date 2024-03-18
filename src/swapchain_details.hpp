#pragma once
#include "config.hpp"

namespace vk_tools {
	struct swapchain_support_details {
		vk::SurfaceCapabilitiesKHR capabilities;
		std::vector<vk::SurfaceFormatKHR> formats;
		std::vector<vk::PresentModeKHR> present_modes;
	};

	auto query_swapchain_support_details(const vk::PhysicalDevice &device, const vk::SurfaceKHR &surface) {
		swapchain_support_details details{
			device.getSurfaceCapabilitiesKHR(surface),
			device.getSurfaceFormatsKHR(surface),
			device.getSurfacePresentModesKHR(surface)
		};
		return details;
	}

	auto choose_surface_format(const std::vector<vk::SurfaceFormatKHR> &available_formats) {
	for (const auto& format : available_formats)
			if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear 
				&&
				format.format == vk::Format::eB8G8R8A8Srgb)
				return format;

		return available_formats.front();
	}
	auto choose_present_mode(const std::vector<vk::PresentModeKHR> &available_modes) {
		for (const auto& mode : available_modes)
			//if (mode == vk::PresentModeKHR::eFifo) return mode;
			if (mode == vk::PresentModeKHR::eImmediate) return mode;
		return available_modes.front();
	}

	auto choose_swap_extent(const vk::SurfaceCapabilitiesKHR &capabilities, GLFWwindow* window) {
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			return capabilities.currentExtent;

		int width;
		int height;

		glfwGetFramebufferSize(window, &width, &height);

		vk::Extent2D actual_extent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height),
		};

		actual_extent.width = 
			std::clamp(
				actual_extent.width,
				capabilities.minImageExtent.width, 
				capabilities.maxImageExtent.width
			);

		actual_extent.height =
			std::clamp(
				actual_extent.height,
				capabilities.minImageExtent.height,
				capabilities.maxImageExtent.height
			);

		return actual_extent;
	}
}
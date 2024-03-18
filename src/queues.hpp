#pragma once
#include "config.hpp"

struct QueueFamilyIndices {
	int graphics_family = -1;
	int present_family = -1;
	int compute_family = -1;

	bool is_complete() const {
		return 
			graphics_family != -1
			&&
			present_family != -1
			&&
			compute_family != -1;
	}
};

QueueFamilyIndices findQueueFamilies(const vk::PhysicalDevice &device, vk::SurfaceKHR &surface) {
	QueueFamilyIndices indices;
	
	auto queue_family_properties = device.getQueueFamilyProperties();
	
	int i = 0;
	for (const auto& property : queue_family_properties) {

		if (property.queueFlags & vk::QueueFlagBits::eGraphics)
			indices.graphics_family = i;
		
		if (bool present_support = device.getSurfaceSupportKHR(i, surface))
			indices.present_family = i;

		if (property.queueFlags & vk::QueueFlagBits::eCompute)
			indices.compute_family = i;

		if (indices.is_complete()) break;

		++i;
	}

	return indices;
}
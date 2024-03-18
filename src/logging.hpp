#pragma once
#include "config.hpp"

namespace vk_tools {
	struct logging {
		static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) {

			switch (messageSeverity) {
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
					return VK_FALSE;
			}

			std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
			return VK_FALSE;
		}

		static void CreateDebugUtilsMessengerEXT(
			vk::Instance instance,
			VkDebugUtilsMessengerCreateInfoEXT& pCreateInfo,
			VkDebugUtilsMessengerEXT& pDebugMessenger) {
			
			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
				vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

			if (func != nullptr)
				func(instance, &pCreateInfo, nullptr, &pDebugMessenger);
			else
				throw std::runtime_error("error in CreateDebugUtilsMessanger");
		}

		static void DestroyDebugUtilsMessengerEXT(vk::Instance instance,
			VkDebugUtilsMessengerEXT debugMessenger) {
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
				vkGetInstanceProcAddr(instance,	"vkDestroyDebugUtilsMessengerEXT");
			
			if (func != nullptr)
				func(instance, debugMessenger, nullptr);
			else
				throw std::runtime_error("error in DestroyDebugUtilsMessanger");
		}
	};
}
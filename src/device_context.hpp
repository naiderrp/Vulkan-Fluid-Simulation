#pragma once
#include "config.hpp"
#include "tools.hpp"
#include <ranges>
#include "logging.hpp"
#include "queues.hpp"
#include "swapchain_details.hpp"

#include <set>
#include <fstream>
#include <sstream>

constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

class device_context {
public:
    inline device_context(const std::vector<glm::vec2> initial_positions) {

        position_ssbo_size = sizeof(glm::vec2) * initial_positions.size();
        velocity_ssbo_size = sizeof(glm::vec2) * initial_positions.size();
        force_ssbo_size = sizeof(glm::vec2) * initial_positions.size();
        density_ssbo_size = sizeof(float) * initial_positions.size();
        pressure_ssbo_size = sizeof(float) * initial_positions.size();

        packed_buffer_size = position_ssbo_size + velocity_ssbo_size + force_ssbo_size + density_ssbo_size + pressure_ssbo_size;

        position_ssbo_offset = 0;
        velocity_ssbo_offset = position_ssbo_size;
        force_ssbo_offset = velocity_ssbo_offset + velocity_ssbo_size;
        density_ssbo_offset = force_ssbo_offset + force_ssbo_size;
        pressure_ssbo_offset = density_ssbo_offset + density_ssbo_size;

        init_window();
        init_vulkan(initial_positions);
    }

    ~device_context() {
        destroy_vulkan();
        destroy_window();
    }

private:
    void init_window() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(window_width_, window_height_, "", nullptr, nullptr);
    }

    void init_vulkan(const std::vector<glm::vec2> initial_positions) {
        create_instance();
        setup_debug_messenger();

        create_surface();
        select_physical_device();
        create_logical_device();

        create_swapchain();
        get_swapchain_images();
        create_swapchain_image_views();
        create_render_pass();
        create_swapchain_frame_buffers();

        create_pipeline_cache();
        
        create_compute_command_pool();
        
        create_vertex_buffer(initial_positions);

        create_graphics_pipeline_layout();
        create_graphics_pipeline();
        create_graphics_command_pool();
        create_graphics_command_buffers();
        create_semaphores();

        create_descriptor_pool();
        create_compute_descriptor_set_layout();
        update_compute_descriptor_sets();
        create_compute_pipeline_layout();
        create_compute_pipelines();

        create_compute_command_buffer();
    }

    void destroy_window() {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void destroy_vulkan() {
        logical_device_.waitIdle();
 
        logical_device_.destroyCommandPool(compute_command_pool_);
        
        logical_device_.destroyDescriptorSetLayout(compute_descriptor_set_layout_);
        
        logical_device_.destroyPipelineLayout(compute_pipeline_layout_);

        logical_device_.destroyPipeline(density_pipeline_);
        logical_device_.destroyPipeline(force_pipeline_);
        logical_device_.destroyPipeline(position_pipeline_);

        logical_device_.destroySemaphore(render_finished_semaphore_);
        logical_device_.destroySemaphore(image_available_semaphore_);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            logical_device_.destroySemaphore(image_available_semaphores[i]);
            logical_device_.destroySemaphore(render_finished_semaphores[i]);
            logical_device_.destroyFence(in_flight_fences[i]);
        }

        logical_device_.destroyCommandPool(graphics_command_pool_);

        logical_device_.destroyPipeline(graphics_pipeline_);

        for (const auto& handle : shader_modules_) {
            logical_device_.destroyShaderModule(handle);
        }
        
        logical_device_.destroyPipelineLayout(graphics_pipeline_layout_);

        for (const auto& handle : swapchain_frame_buffers_) {
            logical_device_.destroyFramebuffer(handle);
        }

        logical_device_.destroyBuffer(packed_particles_buffer_);
        logical_device_.freeMemory(packed_particles_memory_);

        logical_device_.destroyDescriptorPool(compute_descriptor_pool_);

        logical_device_.destroyPipelineCache(global_pipeline_cache_handle);

        logical_device_.destroyRenderPass(renderpass_);

        for (const auto& handle : swapchain_image_views_) {
            logical_device_.destroyImageView(handle);
        }

        logical_device_.destroySwapchainKHR(swapchain_handle);
        instance_.destroySurfaceKHR(surface_);
        logical_device_.destroy();

        vk_tools::logging::DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_);
        instance_.destroy();
    }

    void create_instance() {
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        std::vector<const char*> requested_layers = { "VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_monitor" };

        auto createInfo = vk::InstanceCreateInfo{};
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.enabledLayerCount = static_cast<uint32_t>(requested_layers.size());
        createInfo.ppEnabledLayerNames = requested_layers.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        populate_debug_messenger_create_info(debugCreateInfo);
        createInfo.pNext = reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&debugCreateInfo);

        instance_ = vk::createInstance(createInfo);
    }

    void setup_debug_messenger() {
        VkDebugUtilsMessengerCreateInfoEXT create_info;
        populate_debug_messenger_create_info(create_info);
        vk_tools::logging::CreateDebugUtilsMessengerEXT(instance_, create_info, debug_messenger_);
    }

    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& create_info) {
        create_info = VkDebugUtilsMessengerCreateInfoEXT{};
        create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

        create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

        create_info.pfnUserCallback = vk_tools::logging::debugCallback;
    }

    void create_surface() {
        VkSurfaceKHR c_style_surface;

        if (glfwCreateWindowSurface(instance_, window_, nullptr, &c_style_surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to abstract glfw surface for Vulkan.");

        surface_ = c_style_surface;
    }

    void select_physical_device() {
        physical_device_ = instance_.enumeratePhysicalDevices().front();
    }

    void create_logical_device() {
        auto properties = physical_device_.getProperties();
        auto indices = findQueueFamilies(physical_device_, surface_);

        std::set<int> unique_queue_families = { indices.graphics_family, indices.present_family };;

        std::vector< vk::DeviceQueueCreateInfo> queue_create_infos;

        for (auto queue_family : unique_queue_families) {

            auto queue_create_info = vk::DeviceQueueCreateInfo{};

            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount = 1;

            auto priority = 1.0f;
            queue_create_info.pQueuePriorities = &priority;

            queue_create_infos.emplace_back(queue_create_info);
        }

        auto device_create_info = vk::DeviceCreateInfo{};
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());

        device_create_info.enabledExtensionCount = static_cast<uint32_t>(tools::requested_extensions.size());
        device_create_info.ppEnabledExtensionNames = tools::requested_extensions.data();

        auto features = physical_device_.getFeatures();
        features.samplerAnisotropy = true;

        device_create_info.pEnabledFeatures = &features;

        logical_device_ = physical_device_.createDevice(device_create_info);
        
        present_queue_ = logical_device_.getQueue(indices.graphics_family, 0);
        graphics_queue_ = logical_device_.getQueue(indices.present_family, 0);
        compute_queue_ = logical_device_.getQueue(indices.compute_family, 0);

        graphics_queue_family_index_ = indices.graphics_family;
        compute_queue_family_index_ = indices.compute_family;
    }

    void create_swapchain() {
        vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eImmediate;
        vk::Extent2D extent = { window_width_, window_height_ };

        auto surface_capabilities = physical_device_.getSurfaceCapabilitiesKHR(surface_);

        if (surface_capabilities.currentExtent.width != UINT32_MAX) {
            extent = surface_capabilities.currentExtent;
        }

        auto surface_formats = physical_device_.getSurfaceFormatsKHR(surface_);

        for (const auto& entry : surface_formats) {
            if ((entry.format == vk::Format::eB8G8R8A8Srgb) && (entry.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)) {
                surface_format_ = entry;
                break;
            }
        }

        uint32_t image_count = surface_capabilities.minImageCount + 1;
        
        vk::SwapchainCreateInfoKHR create_info{};        
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format_.format;
        create_info.imageColorSpace = surface_format_.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        create_info.imageSharingMode = vk::SharingMode::eExclusive;
        create_info.queueFamilyIndexCount = 0;
        create_info.preTransform = surface_capabilities.currentTransform;
        create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        create_info.presentMode = swapchain_present_mode;
        create_info.clipped = VK_TRUE;

        swapchain_handle = logical_device_.createSwapchainKHR(create_info);

        swapchain_extent_ = extent;
    }

    void get_swapchain_images() {
        swapchain_images_ = logical_device_.getSwapchainImagesKHR(swapchain_handle);
    }

    void create_swapchain_image_views() {
        swapchain_image_views_.resize(swapchain_images_.size());

        for (int i = 0; i < swapchain_image_views_.size(); ++i) {
            vk::ImageViewCreateInfo image_view_create_info{};

            image_view_create_info.image = swapchain_images_[i];
            image_view_create_info.viewType = vk::ImageViewType::e2D;
            image_view_create_info.format = surface_format_.format;
            image_view_create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            image_view_create_info.subresourceRange.levelCount = 1;
            image_view_create_info.subresourceRange.layerCount = 1;

            swapchain_image_views_[i] = logical_device_.createImageView(image_view_create_info);
        }
    }

    void create_render_pass() {
        vk::AttachmentDescription attachment_description{};

        attachment_description.format = surface_format_.format;
        attachment_description.initialLayout = vk::ImageLayout::eUndefined;
        attachment_description.finalLayout = vk::ImageLayout::ePresentSrcKHR;
        attachment_description.samples = vk::SampleCountFlagBits::e1;
        attachment_description.loadOp = vk::AttachmentLoadOp::eClear;
        attachment_description.storeOp = vk::AttachmentStoreOp::eStore;
        attachment_description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment_description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        
        vk::AttachmentReference color_attachment_reference{};

        color_attachment_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;
        color_attachment_reference.attachment = 0;


        vk::SubpassDescription subpass_description{};

        subpass_description.colorAttachmentCount = 1;
        subpass_description.pColorAttachments = &color_attachment_reference;
        subpass_description.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        

        vk::RenderPassCreateInfo render_pass_create_info{};
        render_pass_create_info.attachmentCount = 1;
        render_pass_create_info.pAttachments = &attachment_description;
        render_pass_create_info.subpassCount = 1;
        render_pass_create_info.pSubpasses = &subpass_description;

        renderpass_ = logical_device_.createRenderPass(render_pass_create_info);
    }

    void create_swapchain_frame_buffers() {
        swapchain_frame_buffers_.resize(swapchain_image_views_.size());

        for (size_t i = 0; i < swapchain_image_views_.size(); ++i) {
            vk::FramebufferCreateInfo framebuffer_create_info{};
            
            framebuffer_create_info.renderPass = renderpass_;
            framebuffer_create_info.height = window_height_;
            framebuffer_create_info.width = window_width_;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments = &swapchain_image_views_[i];
            framebuffer_create_info.layers = 1;

            swapchain_frame_buffers_[i] = logical_device_.createFramebuffer(framebuffer_create_info);
        }
    }

    void create_descriptor_pool() {

        vk::DescriptorPoolSize descriptor_pool_size{};
        descriptor_pool_size.descriptorCount = 5;
        descriptor_pool_size.type = vk::DescriptorType::eStorageBuffer;

        vk::DescriptorPoolCreateInfo create_info{};
        create_info.maxSets = 1;
        create_info.poolSizeCount = 1;
        create_info.pPoolSizes = &descriptor_pool_size;

        compute_descriptor_pool_ = logical_device_.createDescriptorPool(create_info);
    }
    
    void create_pipeline_cache() {
        VkPipelineCacheCreateInfo create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            NULL,
            0,
            0,
            NULL
        };

        global_pipeline_cache_handle = logical_device_.createPipelineCache(create_info);
    }

    void create_vertex_buffer(const std::vector<glm::vec2> &positions) {
        vk::BufferCreateInfo packed_particles_buffer_create_info{};

        packed_particles_buffer_create_info.size = packed_buffer_size;
        packed_particles_buffer_create_info.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        packed_particles_buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

        packed_particles_buffer_ = logical_device_.createBuffer(packed_particles_buffer_create_info);

        auto position_buffer_memory_requirements = logical_device_.getBufferMemoryRequirements(packed_particles_buffer_);
        
        vk::MemoryAllocateInfo particle_buffer_memory_allocation_info{};
        particle_buffer_memory_allocation_info.allocationSize = position_buffer_memory_requirements.size;
        particle_buffer_memory_allocation_info.memoryTypeIndex = get_memory_type_index(position_buffer_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

        packed_particles_memory_ = logical_device_.allocateMemory(particle_buffer_memory_allocation_info);

        logical_device_.bindBufferMemory(packed_particles_buffer_, packed_particles_memory_, 0);

        vk::BufferCreateInfo staging_buffer_create_info{};
        staging_buffer_create_info.size = packed_buffer_size;
        staging_buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc;
        staging_buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

        auto staging_buffer_handle = logical_device_.createBuffer(staging_buffer_create_info);
        auto staging_buffer_memory_requirements = logical_device_.getBufferMemoryRequirements(staging_buffer_handle);

        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.allocationSize = staging_buffer_memory_requirements.size;
        alloc_info.memoryTypeIndex = get_memory_type_index(staging_buffer_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        auto staging_buffer_memory_device_handle = logical_device_.allocateMemory(alloc_info);

        logical_device_.bindBufferMemory(staging_buffer_handle, staging_buffer_memory_device_handle, 0);

        auto mapped_memory = logical_device_.mapMemory(staging_buffer_memory_device_handle, 0, staging_buffer_memory_requirements.size);

        std::memset(mapped_memory, 0, packed_buffer_size);
        std::memcpy(mapped_memory, positions.data(), position_ssbo_size);

        logical_device_.unmapMemory(staging_buffer_memory_device_handle);

        vk::CommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.commandBufferCount = 1;
        command_buffer_allocate_info.commandPool = compute_command_pool_;
        command_buffer_allocate_info.level = vk::CommandBufferLevel::ePrimary;

        auto copy_command_buffer_handle = logical_device_.allocateCommandBuffers(command_buffer_allocate_info).front();

        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

        copy_command_buffer_handle.begin(begin_info);

        vk::BufferCopy buffer_copy_region{};
        buffer_copy_region.dstOffset = 0;
        buffer_copy_region.srcOffset = 0;
        buffer_copy_region.size = staging_buffer_memory_requirements.size;

        copy_command_buffer_handle.copyBuffer(staging_buffer_handle, packed_particles_buffer_, buffer_copy_region);

        copy_command_buffer_handle.end();

        vk::SubmitInfo submit_info{};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &copy_command_buffer_handle;

        compute_queue_.submit(submit_info);

        compute_queue_.waitIdle();

        logical_device_.freeCommandBuffers(compute_command_pool_, { copy_command_buffer_handle });
        logical_device_.freeMemory(staging_buffer_memory_device_handle);
        logical_device_.destroyBuffer(staging_buffer_handle);
    }

    void create_graphics_pipeline_layout() {
        vk::PipelineLayoutCreateInfo create_info{};
        graphics_pipeline_layout_ = logical_device_.createPipelineLayout(create_info);
    }

    void create_graphics_pipeline() {
        std::vector<VkPipelineShaderStageCreateInfo> shader_stage_create_infos;

        VkShaderModule vertex_shader_module = create_shader_module_from_file("particle.vert.spv");

        VkShaderModule fragment_shader_module = create_shader_module_from_file("particle.frag.spv");

        VkPipelineShaderStageCreateInfo vertex_shader_stage_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            NULL,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            vertex_shader_module,
            "main",
            NULL
        };

        VkPipelineShaderStageCreateInfo fragment_shader_stage_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            NULL,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            fragment_shader_module,
            "main",
            NULL
        };

        shader_stage_create_infos.push_back(vertex_shader_stage_create_info);
        shader_stage_create_infos.push_back(fragment_shader_stage_create_info);

        VkVertexInputBindingDescription vertex_input_binding_description
        {
            0,
            sizeof(glm::vec2),
            VK_VERTEX_INPUT_RATE_VERTEX
        };

        VkVertexInputAttributeDescription vertex_input_attribute_description
        {
            0,
            0,
            VK_FORMAT_R32G32_SFLOAT,
            0
        };

        VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            NULL,
            0,
            1,
            &vertex_input_binding_description,
            1,
            &vertex_input_attribute_description
        };

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            NULL,
            0,
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            VK_FALSE
        };

        VkViewport viewport
        {
            0,
            0,
            static_cast<float>(window_width_),
            static_cast<float>(window_height_),
            0,
            1
        };

        VkRect2D scissor
        {
            { 0, 0 },
            { window_width_, window_height_ }
        };

        VkPipelineViewportStateCreateInfo viewport_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            NULL,
            0,
            1,
            &viewport,
            1,
            &scissor
        };

        VkPipelineRasterizationStateCreateInfo rasterization_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            NULL,
            0,
            VK_FALSE,
            VK_FALSE,
            VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE,
            VK_FRONT_FACE_COUNTER_CLOCKWISE,
            VK_FALSE,
            0,
            0,
            0,
            1
        };

        VkPipelineMultisampleStateCreateInfo multisample_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            NULL,
            0,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FALSE,
            0,
            NULL,
            VK_FALSE,
            VK_FALSE
        };

        VkPipelineColorBlendAttachmentState color_blend_attachment
        {
            VK_FALSE,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        VkPipelineColorBlendStateCreateInfo color_blend_state_create_info
        {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            NULL,
            0,
            VK_FALSE,
            VK_LOGIC_OP_COPY,
            1,
            &color_blend_attachment,
            {0, 0, 0, 0}
        };

        VkGraphicsPipelineCreateInfo graphics_pipeline_create_info
        {
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            NULL,
            0,
            static_cast<uint32_t>(shader_stage_create_infos.size()),
            shader_stage_create_infos.data(),
            &vertex_input_state_create_info,
            &input_assembly_state_create_info,
            NULL,
            &viewport_state_create_info,
            &rasterization_state_create_info,
            &multisample_state_create_info,
            NULL,
            &color_blend_state_create_info,
            NULL,
            graphics_pipeline_layout_,
            renderpass_,
            0,
            VK_NULL_HANDLE,
            -1
        };

        graphics_pipeline_ = logical_device_.createGraphicsPipeline(global_pipeline_cache_handle, graphics_pipeline_create_info).value;
    }

    void create_graphics_pipeline1() {
        std::vector<vk::PipelineShaderStageCreateInfo> shader_stage_create_infos;

        auto vertex_shader_module = create_shader_module_from_file("particle.vert.spv");
        auto fragment_shader_module = create_shader_module_from_file("particle.frag.spv");

        vk::PipelineShaderStageCreateInfo vertex_shader_stage_create_info{};
        vertex_shader_stage_create_info.stage = vk::ShaderStageFlagBits::eVertex;
        vertex_shader_stage_create_info.module = vertex_shader_module;
        vertex_shader_stage_create_info.pName = "main";

        vk::PipelineShaderStageCreateInfo fragment_shader_stage_create_info{};
        fragment_shader_stage_create_info.stage = vk::ShaderStageFlagBits::eFragment;
        fragment_shader_stage_create_info.module = fragment_shader_module;
        fragment_shader_stage_create_info.pName = "main";

        shader_stage_create_infos.push_back(vertex_shader_stage_create_info);
        shader_stage_create_infos.push_back(fragment_shader_stage_create_info);

        vk::VertexInputBindingDescription vertex_input_binding_description{};
        vertex_input_binding_description.binding = 0;
        vertex_input_binding_description.inputRate = vk::VertexInputRate::eVertex;
        vertex_input_binding_description.stride = sizeof(glm::vec2);

        vk::VertexInputAttributeDescription vertex_input_attribute_description{};
        vertex_input_attribute_description.format = vk::Format::eR32G32Sfloat;
        vertex_input_attribute_description.location = 0;
        vertex_input_attribute_description.binding = 0;
        vertex_input_attribute_description.offset = 0;

        vk::PipelineVertexInputStateCreateInfo vertex_input_state_create_info{};
        vertex_input_state_create_info.vertexAttributeDescriptionCount = 1;
        vertex_input_state_create_info.pVertexAttributeDescriptions = &vertex_input_attribute_description;
        vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
        vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;
        
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_create_info{};
        input_assembly_state_create_info.topology = vk::PrimitiveTopology::ePointList;
        input_assembly_state_create_info.primitiveRestartEnable = false;

        vk::Viewport viewport{};
        viewport.height = static_cast<float>(window_height_);
        viewport.width = static_cast<float>(window_width_);
        viewport.x = 0;
        viewport.y = 0;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{ 0, 0 };
        scissor.extent = vk::Extent2D{ window_width_, window_height_ };

        vk::PipelineViewportStateCreateInfo viewport_state_create_info{};
        viewport_state_create_info.scissorCount = 1;
        viewport_state_create_info.pScissors = &scissor;
        viewport_state_create_info.viewportCount = 1;
        viewport_state_create_info.pViewports = &viewport;

        vk::PipelineRasterizationStateCreateInfo rasterization_state_create_info{};
        rasterization_state_create_info.cullMode = vk::CullModeFlagBits::eNone;
        rasterization_state_create_info.frontFace = vk::FrontFace::eCounterClockwise;
        rasterization_state_create_info.polygonMode = vk::PolygonMode::eFill;
        rasterization_state_create_info.lineWidth = 1;

        vk::PipelineMultisampleStateCreateInfo multisample_state_create_info{};
        multisample_state_create_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable = false;
        color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eOne;
        color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eZero;
        color_blend_attachment.colorBlendOp = vk::BlendOp::eAdd;
        color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        color_blend_attachment.alphaBlendOp = vk::BlendOp::eAdd;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo color_blend_state_create_info{};
        color_blend_state_create_info.logicOp = vk::LogicOp::eCopy;
        color_blend_state_create_info.attachmentCount = 1;
        color_blend_state_create_info.pAttachments = &color_blend_attachment;
        color_blend_state_create_info.blendConstants[0] = 0.0f;
        color_blend_state_create_info.blendConstants[1] = 0.0f;
        color_blend_state_create_info.blendConstants[2] = 0.0f;
        color_blend_state_create_info.blendConstants[3] = 0.0f;

        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};

        graphics_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stage_create_infos.size());
        graphics_pipeline_create_info.pStages = shader_stage_create_infos.data();
        graphics_pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
        graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
        graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
        graphics_pipeline_create_info.pMultisampleState = &multisample_state_create_info;
        graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
        graphics_pipeline_create_info.renderPass = renderpass_;
        graphics_pipeline_create_info.basePipelineIndex = -1;
        graphics_pipeline_create_info.layout = graphics_pipeline_layout_;

        graphics_pipeline_ = logical_device_.createGraphicsPipeline({}, graphics_pipeline_create_info).value;
    }
    
    void create_graphics_command_pool() {
        vk::CommandPoolCreateInfo create_info{};
        create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        create_info.queueFamilyIndex = graphics_queue_family_index_;

        graphics_command_pool_ = logical_device_.createCommandPool(create_info);
    }

    void create_graphics_command_buffers() {
        graphics_command_buffers_.resize(swapchain_frame_buffers_.size());

        vk::CommandBufferAllocateInfo alloc_info{};
        alloc_info.commandPool = graphics_command_pool_;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = static_cast<uint32_t>(graphics_command_buffers_.size());
 
        graphics_command_buffers_ = logical_device_.allocateCommandBuffers(alloc_info);
    }

    void create_semaphores() {
        vk::SemaphoreCreateInfo semaphore_info{};

        vk::FenceCreateInfo fence_info{};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            image_available_semaphores.emplace_back(logical_device_.createSemaphore(semaphore_info));
            render_finished_semaphores.emplace_back(logical_device_.createSemaphore(semaphore_info));
            in_flight_fences.emplace_back(logical_device_.createFence(fence_info));
        }

        vk::SemaphoreCreateInfo semaphore_create_info{};
        image_available_semaphore_ = logical_device_.createSemaphore(semaphore_create_info);
        render_finished_semaphore_ = logical_device_.createSemaphore(semaphore_create_info);
    }

    void create_compute_descriptor_set_layout() {
        vk::DescriptorSetLayoutBinding position = {};
        position.binding = 0;
        position.descriptorCount = 1;
        position.descriptorType = vk::DescriptorType::eStorageBuffer;
        position.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding velocity = {};
        velocity.binding = 1;
        velocity.descriptorCount = 1;
        velocity.descriptorType = vk::DescriptorType::eStorageBuffer;
        velocity.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding force = {};
        force.binding = 2;
        force.descriptorCount = 1;
        force.descriptorType = vk::DescriptorType::eStorageBuffer;
        force.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding density = {};
        density.binding = 3;
        density.descriptorCount = 1;
        density.descriptorType = vk::DescriptorType::eStorageBuffer;
        density.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding pressure = {};
        pressure.binding = 4;
        pressure.descriptorCount = 1;
        pressure.descriptorType = vk::DescriptorType::eStorageBuffer;
        pressure.stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutBinding bindings[] = { position, velocity, force, density, pressure };

        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
        create_info.pBindings = bindings;

        compute_descriptor_set_layout_ = logical_device_.createDescriptorSetLayout(create_info);
    }

    void update_compute_descriptor_sets() {
        vk::DescriptorSetAllocateInfo descriptor_set_allocate_info
        {
            compute_descriptor_pool_,
            1,
            &compute_descriptor_set_layout_
        };

        logical_device_.allocateDescriptorSets(&descriptor_set_allocate_info, &compute_descriptor_set_);

        VkDescriptorBufferInfo descriptor_buffer_infos[]
        {
            {
                packed_particles_buffer_,
                position_ssbo_offset,
                position_ssbo_size
            },
            {
                packed_particles_buffer_,
                velocity_ssbo_offset,
                velocity_ssbo_size
            },
            {
                packed_particles_buffer_,
                force_ssbo_offset,
                force_ssbo_size
            },
            {
                packed_particles_buffer_,
                density_ssbo_offset,
                density_ssbo_size
            },
            {
                packed_particles_buffer_,
                pressure_ssbo_offset,
                pressure_ssbo_size
            }
        };

        VkWriteDescriptorSet write_descriptor_sets[]
        {
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,
                compute_descriptor_set_,
                0,
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_NULL_HANDLE,
                &descriptor_buffer_infos[0],
                VK_NULL_HANDLE
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,
                compute_descriptor_set_,
                1,
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_NULL_HANDLE,
                &descriptor_buffer_infos[1],
                VK_NULL_HANDLE
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,
                compute_descriptor_set_,
                2,
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_NULL_HANDLE,
                &descriptor_buffer_infos[2],
                VK_NULL_HANDLE
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,
                compute_descriptor_set_,
                3,
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_NULL_HANDLE,
                &descriptor_buffer_infos[3],
                VK_NULL_HANDLE
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                NULL,
                compute_descriptor_set_,
                4,
                0,
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_NULL_HANDLE,
                &descriptor_buffer_infos[4],
                VK_NULL_HANDLE
            }
        };
        vkUpdateDescriptorSets(logical_device_, 5, write_descriptor_sets, 0, NULL);
    }

    void update_compute_descriptor_sets1() {
        vk::DescriptorSetAllocateInfo alloc_info{};
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &compute_descriptor_set_layout_;
        alloc_info.descriptorPool = compute_descriptor_pool_;

        compute_descriptor_set_ = logical_device_.allocateDescriptorSets(alloc_info).front();

        vk::DescriptorBufferInfo position{};
        position.buffer = packed_particles_buffer_;
        position.offset = position_ssbo_offset;
        position.range = position_ssbo_size;

        vk::DescriptorBufferInfo velocity{};
        velocity.buffer = packed_particles_buffer_;
        velocity.offset = velocity_ssbo_offset;
        velocity.range = velocity_ssbo_size;

        vk::DescriptorBufferInfo force{};
        force.buffer = packed_particles_buffer_;
        force.offset = force_ssbo_offset;
        force.range = force_ssbo_size;

        vk::DescriptorBufferInfo density{};
        density.buffer = packed_particles_buffer_;
        density.offset = density_ssbo_offset;
        density.range = density_ssbo_size;

        vk::DescriptorBufferInfo pressure{};
        pressure.buffer = packed_particles_buffer_;
        pressure.offset = pressure_ssbo_offset;
        pressure.range = pressure_ssbo_size;

        vk::WriteDescriptorSet write_position{};
        write_position.descriptorCount = 1;
        write_position.dstBinding = 0;
        write_position.dstSet = compute_descriptor_set_;
        write_position.pBufferInfo = &position;

        vk::WriteDescriptorSet write_velocity{};
        write_velocity.descriptorCount = 1;
        write_velocity.dstBinding = 1;
        write_velocity.dstSet = compute_descriptor_set_;
        write_velocity.pBufferInfo = &velocity;

        vk::WriteDescriptorSet write_force{};
        write_force.descriptorCount = 1;
        write_force.dstBinding = 2;
        write_force.dstSet = compute_descriptor_set_;
        write_force.pBufferInfo = &force;

        vk::WriteDescriptorSet write_density{};
        write_density.descriptorCount = 1;
        write_density.dstBinding = 3;
        write_density.dstSet = compute_descriptor_set_;
        write_density.pBufferInfo = &density;

        vk::WriteDescriptorSet write_pressure{};
        write_pressure.descriptorCount = 1;
        write_pressure.dstBinding = 4;
        write_pressure.dstSet = compute_descriptor_set_;
        write_pressure.pBufferInfo = &pressure;

        vk::WriteDescriptorSet write_descriptor_sets[] = { write_position, write_velocity, write_force, write_density, write_pressure };

        logical_device_.updateDescriptorSets(write_descriptor_sets, {});
    }
    
    void create_compute_pipeline_layout() {
        vk::PipelineLayoutCreateInfo create_info{};
        create_info.pSetLayouts = &compute_descriptor_set_layout_;
        create_info.setLayoutCount = 1;
        
        compute_pipeline_layout_ = logical_device_.createPipelineLayout(create_info);
    }

    void create_compute_pipelines() {
        auto compute_density_pressure_shader_module = create_shader_module_from_file("density_pressure.comp.spv");

        vk::PipelineShaderStageCreateInfo compute_shader_stage_create_info{};
        compute_shader_stage_create_info.pName = "main";
        compute_shader_stage_create_info.stage = vk::ShaderStageFlagBits::eCompute;
        compute_shader_stage_create_info.module = compute_density_pressure_shader_module;

        vk::ComputePipelineCreateInfo compute_pipeline_create_info{};
        compute_pipeline_create_info.stage = compute_shader_stage_create_info;
        compute_pipeline_create_info.layout = compute_pipeline_layout_;

        density_pipeline_ = logical_device_.createComputePipeline(global_pipeline_cache_handle, compute_pipeline_create_info).value;

        auto compute_force_shader_module = create_shader_module_from_file("force.comp.spv");
        compute_shader_stage_create_info.module = compute_force_shader_module;
        compute_pipeline_create_info.stage = compute_shader_stage_create_info;

        force_pipeline_ = logical_device_.createComputePipeline(global_pipeline_cache_handle, compute_pipeline_create_info).value;

        auto integrate_shader_module = create_shader_module_from_file("position.comp.spv");
        compute_shader_stage_create_info.module = integrate_shader_module;
        compute_pipeline_create_info.stage = compute_shader_stage_create_info;

        position_pipeline_ = logical_device_.createComputePipeline(global_pipeline_cache_handle, compute_pipeline_create_info).value;
    }

    void create_compute_command_pool() {
        vk::CommandPoolCreateInfo create_info{};
        create_info.queueFamilyIndex = compute_queue_family_index_;

        compute_command_pool_ = logical_device_.createCommandPool(create_info);
    }

    void create_compute_command_buffer() {
        vk::CommandBufferAllocateInfo alloc_info{};
        alloc_info.commandPool = compute_command_pool_;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;

        compute_command_buffer_ = logical_device_.allocateCommandBuffers(alloc_info).front();
    }

private:
    vk::ShaderModule create_shader_module_from_file(const std::string& path_to_file) {
        std::ifstream shader_file(path_to_file, std::ios::ate | std::ios::binary);
        if (!shader_file) throw std::runtime_error("shader file load error");

        size_t shader_file_size = (size_t)shader_file.tellg();
        std::vector<char> shader_code(shader_file_size);
        shader_file.seekg(0);
        shader_file.read(shader_code.data(), shader_file_size);
        shader_file.close();

        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = shader_code.size();
        create_info.pCode = reinterpret_cast<const uint32_t*>(shader_code.data());

        auto shader_module = logical_device_.createShaderModule(create_info);

        shader_modules_.push_back(shader_module);

        return shader_module;
    }

    uint32_t get_memory_type_index(uint32_t supported_types_mask, vk::MemoryPropertyFlags properties) {
        auto supported_properties = physical_device_.getMemoryProperties();
        for (uint32_t i = 0; i < supported_properties.memoryTypeCount; ++i)
            if (supported_types_mask & (1 << i)
                &&
                (supported_properties.memoryTypes[i].propertyFlags & properties))
                return i;

        throw std::runtime_error("failed to find suitable memory type!");
    }

public:
    GLFWwindow* window_;
    uint32_t window_height_ = 900;
    uint32_t window_width_ = 1800;

    vk::Instance instance_;
    VkDebugUtilsMessengerEXT debug_messenger_;

    vk::PhysicalDevice physical_device_;
    vk::Device logical_device_;
    vk::SurfaceKHR surface_;
    vk::SurfaceFormatKHR surface_format_;

    vk::SwapchainKHR swapchain_handle;
    std::vector<vk::Image> swapchain_images_;
    std::vector<vk::ImageView> swapchain_image_views_;
    std::vector<vk::Framebuffer> swapchain_frame_buffers_;
    vk::Extent2D swapchain_extent_;

    std::vector<vk::ShaderModule> shader_modules_;

    vk::RenderPass renderpass_;

    uint32_t graphics_queue_family_index_;
    uint32_t compute_queue_family_index_;

    vk::Queue present_queue_;
    vk::Queue graphics_queue_;
    vk::Queue compute_queue_;

    vk::CommandPool graphics_command_pool_;
    std::vector<vk::CommandBuffer> graphics_command_buffers_;
    
    vk::CommandPool compute_command_pool_;
    vk::CommandBuffer compute_command_buffer_;

    vk::DescriptorPool compute_descriptor_pool_;

    vk::DescriptorSetLayout compute_descriptor_set_layout_;
    vk::DescriptorSet compute_descriptor_set_;

    vk::PipelineCache global_pipeline_cache_handle;

    vk::PipelineLayout graphics_pipeline_layout_;
    vk::Pipeline graphics_pipeline_;

    vk::PipelineLayout compute_pipeline_layout_;
    vk::Pipeline density_pipeline_;
    vk::Pipeline force_pipeline_;
    vk::Pipeline position_pipeline_;


    vk::Buffer packed_particles_buffer_;
    vk::DeviceMemory packed_particles_memory_;

    vk::Semaphore image_available_semaphore_;
    vk::Semaphore render_finished_semaphore_;

    uint32_t image_index_;

    std::vector<vk::Semaphore> image_available_semaphores; //an image has been acquired from the swapchain and is ready for rendering
    std::vector<vk::Semaphore> render_finished_semaphores; //rendering has finished 
    std::vector<vk::Fence> in_flight_fences; //to make sure only one frame is rendering at a time

    size_t position_ssbo_size;
    size_t velocity_ssbo_size;
    size_t force_ssbo_size;
    size_t density_ssbo_size;
    size_t pressure_ssbo_size;

    size_t packed_buffer_size;
    
    size_t position_ssbo_offset;
    size_t velocity_ssbo_offset;
    size_t force_ssbo_offset;
    size_t density_ssbo_offset;
    size_t pressure_ssbo_offset;
};
#pragma once
#include "device_context.hpp"
#include "fluid.hpp"

class render_system {
public:
    void run() {
        record_compute_command_buffer();
        
        while (!glfwWindowShouldClose(GPU_.window_)) {
            glfwPollEvents();
         
            run_simulation();
            draw_frame();
        }
    }

private:
    void run_simulation() {
        vk::SubmitInfo compute_submit_info{};
        compute_submit_info.commandBufferCount = 1;
        compute_submit_info.pCommandBuffers = &GPU_.compute_command_buffer_;

        GPU_.compute_queue_.submit(compute_submit_info);
    }

    void draw_frame() {
        GPU_.logical_device_.waitForFences(GPU_.in_flight_fences[current_frame_], true, UINT64_MAX);
        GPU_.logical_device_.resetFences(GPU_.in_flight_fences[current_frame_]);

        auto acquire_image_result = GPU_.logical_device_.acquireNextImageKHR(GPU_.swapchain_handle, UINT64_MAX, GPU_.image_available_semaphores[current_frame_]);
        auto image_index = acquire_image_result.value;

        GPU_.graphics_command_buffers_[current_frame_].reset();
        record_graphics_command_buffer(GPU_.graphics_command_buffers_[current_frame_], image_index);

        vk::Semaphore wait_semaphores[] = {
            GPU_.image_available_semaphores[current_frame_]
        };
        vk::PipelineStageFlags wait_stages[] = {
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        };

        vk::SubmitInfo submit_info{};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &GPU_.graphics_command_buffers_[current_frame_];

        vk::Semaphore signal_semaphores[] = {
            GPU_.render_finished_semaphores[current_frame_]
        };
        submit_info.pSignalSemaphores = signal_semaphores;
        submit_info.signalSemaphoreCount = 1;

        GPU_.graphics_queue_.submit(submit_info, GPU_.in_flight_fences[current_frame_]);

        vk::PresentInfoKHR present_info{};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.pSwapchains = &GPU_.swapchain_handle;
        present_info.swapchainCount = 1;
        present_info.pImageIndices = &image_index;

        auto present_result = GPU_.present_queue_.presentKHR(present_info);

        ++current_frame_ %= MAX_FRAMES_IN_FLIGHT;
    }

private:
    void record_graphics_command_buffer(vk::CommandBuffer& commandBuffer, uint32_t image_index) {
        vk::CommandBufferBeginInfo begin_info{};

        commandBuffer.begin(begin_info);

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.renderPass = GPU_.renderpass_;
        render_pass_info.framebuffer = GPU_.swapchain_frame_buffers_[image_index];
        render_pass_info.renderArea.offset = vk::Offset2D(0, 0);
        render_pass_info.renderArea.extent = GPU_.swapchain_extent_;

        vk::ClearValue clear_values[2];
        clear_values[0].color = vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f };
        clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };

        render_pass_info.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]);
        render_pass_info.pClearValues = clear_values;

        commandBuffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
        
        vk::Viewport viewport{};
        viewport.height = static_cast<float>(GPU_.swapchain_extent_.height);
        viewport.width = static_cast<float>(GPU_.swapchain_extent_.width);
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        commandBuffer.setViewport(0, viewport);

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{ 0, 0 };
        scissor.extent = GPU_.swapchain_extent_;

        commandBuffer.setScissor(0, scissor);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, GPU_.graphics_pipeline_);

        commandBuffer.bindVertexBuffers(0, { GPU_.packed_particles_buffer_ }, { 0 });

        commandBuffer.draw(static_cast<uint32_t>(particles_.size()), 1, 0, 0);

        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    void record_graphics_command_buffers() {
        for (int i = 0; i < GPU_.graphics_command_buffers_.size(); ++i) {
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

            GPU_.graphics_command_buffers_[i].begin(begin_info);

            vk::ClearValue clear_value{};
            clear_value.color = vk::ClearColorValue({ 1.0f, 1.0f, 1.0f, 1.0f });

            vk::RenderPassBeginInfo render_pass_begin_info{};

            render_pass_begin_info.renderPass = GPU_.renderpass_;
            render_pass_begin_info.framebuffer = GPU_.swapchain_frame_buffers_[i];
            render_pass_begin_info.clearValueCount = 1;
            render_pass_begin_info.pClearValues = &clear_value;
            render_pass_begin_info.renderArea.offset = vk::Offset2D{ 0, 0 };
            render_pass_begin_info.renderArea.extent = vk::Extent2D{ GPU_.window_width_, GPU_.window_height_ };

            GPU_.graphics_command_buffers_[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

            vk::Viewport viewport{};
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = static_cast<float>(GPU_.window_width_);
            viewport.height = static_cast<float>(GPU_.window_height_);
            viewport.minDepth = 0;
            viewport.maxDepth = 1;

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{ 0, 0 };
            scissor.extent = vk::Extent2D{ GPU_.window_width_, GPU_.window_height_ };

            GPU_.graphics_command_buffers_[i].setViewport(0, { viewport });
            GPU_.graphics_command_buffers_[i].setScissor(0, { scissor });

            GPU_.graphics_command_buffers_[i].bindPipeline(vk::PipelineBindPoint::eGraphics, GPU_.graphics_pipeline_);

            vk::DeviceSize offsets = 0;
            GPU_.graphics_command_buffers_[i].bindVertexBuffers(0, { GPU_.packed_particles_buffer_ }, offsets);

            GPU_.graphics_command_buffers_[i].draw(particles_.size(), 1, 0, 0);

            GPU_.graphics_command_buffers_[i].endRenderPass();

            GPU_.graphics_command_buffers_[i].end();
        }
    }

    void record_compute_command_buffer() {
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

        GPU_.compute_command_buffer_.begin(begin_info);

        const int workgroup_size = 128;
        const int groupcount = (particles_.size() / workgroup_size) + 1;

        uint32_t count = (particles_.size() + workgroup_size - 1) / workgroup_size;

        GPU_.compute_command_buffer_.bindDescriptorSets(vk::PipelineBindPoint::eCompute, GPU_.compute_pipeline_layout_, 0, { GPU_.compute_descriptor_set_ }, {});

        GPU_.compute_command_buffer_.bindPipeline(vk::PipelineBindPoint::eCompute, GPU_.density_pipeline_);
        GPU_.compute_command_buffer_.dispatch(count, 1, 1);
        GPU_.compute_command_buffer_.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), {}, {}, {});


        GPU_.compute_command_buffer_.bindPipeline(vk::PipelineBindPoint::eCompute, GPU_.force_pipeline_);
        GPU_.compute_command_buffer_.dispatch(count, 1, 1);
        GPU_.compute_command_buffer_.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), {}, {}, {});


        GPU_.compute_command_buffer_.bindPipeline(vk::PipelineBindPoint::eCompute, GPU_.position_pipeline_);
        GPU_.compute_command_buffer_.dispatch(count, 1, 1);
        GPU_.compute_command_buffer_.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), {}, {}, {});

        GPU_.compute_command_buffer_.end();
    }

private:
    uint32_t current_frame_;

	std::vector<glm::vec2> particles_ = fluid::generate_initial_positions(4992); // should be a multiple of 64
	
    device_context GPU_{ particles_ };
};

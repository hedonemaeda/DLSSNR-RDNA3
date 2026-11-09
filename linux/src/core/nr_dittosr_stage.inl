// Included inside Stage::Impl. Ditto's resources share the stage's GPU completion lifetime.
struct DittoPrepare {
    uint32_t size[2], source[2], depth_base[2], depth_size[2], motion_base[2], motion_size[2];
    float motion_scale[2];
    uint32_t inverted, guides;
};
static_assert(sizeof(DittoPrepare) == 64);

void ensure_ditto(VkCommandBuffer cmd) {
    if (!programs[kDittoPrepare].pipeline) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.f;
        check(vkCreateSampler(device, &si, nullptr, &ditto_sampler), "Ditto sampler");
        load(programs[kDittoPrepare], "ditto_prepare", sizeof(DittoPrepare));
        load(programs[kDittoConvert], "ditto_convert", 0);
        load(programs[kDittoMotion], "ditto_motion", 0);
        load(programs[kDittoUpscale], "ditto_upscale", 0);
        load(programs[kDittoSharpen], "ditto_sharpen", 0);
        make_buffer(256, &ditto_constants, &ditto_constants_memory, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    if (!ditto_input.handle) {
        const uint32_t NW = cfg.nr_width, NH = cfg.nr_height, W = cfg.width, H = cfg.height;
        const auto rw = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        for (Image* im : {&ditto_reference, &ditto_input, &ditto_mda[0], &ditto_mda[1]})
            *im = make_image(NW, NH, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
        for (Image* im : {&ditto_aux[0], &ditto_aux[1]})
            *im = make_image(NW, NH, VK_FORMAT_R8G8B8A8_UNORM, rw);
        for (Image* im : {&ditto_history[0], &ditto_history[1], &ditto_result})
            *im = make_image(W, H, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
        ditto_depth = make_image(NW, NH, VK_FORMAT_R32_SFLOAT, rw);
        ditto_motion = make_image(NW, NH, VK_FORMAT_R32G32B32A32_SFLOAT, rw);
        ditto_change = make_image(1, 1, VK_FORMAT_R32_SFLOAT, rw);
        init_fresh(cmd);
        // Even reset frames may sample history before discarding it: initialize every texel.
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (Image* im : {&ditto_history[0], &ditto_history[1], &ditto_mda[0], &ditto_mda[1], &ditto_change})
            vkCmdClearColorImage(cmd, im->handle, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
}

static VkImageAspectFlags ditto_aspect(VkFormat f, bool barrier = false) {
    switch (f) {
        case VK_FORMAT_D16_UNORM: case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT: case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | (barrier ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}
void ditto_guide_barrier(VkCommandBuffer cmd, const ColourFrame& im, bool restore) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = restore ? VK_ACCESS_SHADER_READ_BIT : im.before_access;
    b.dstAccessMask = restore ? im.after_access : VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = restore ? VK_IMAGE_LAYOUT_GENERAL : im.before;
    b.newLayout = restore ? im.after : VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im.image; b.subresourceRange = {ditto_aspect(im.format, true), 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, restore ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : im.before_stage,
                        restore ? im.after_stage : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
VkImageView ditto_view(const ColourFrame& im) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = im.format;
    vi.subresourceRange = {ditto_aspect(im.format), 0, 1, 0, 1};
    VkImageView v{};
    check(vkCreateImageView(device, &vi, nullptr, &v), "Ditto guide view");
    return v;
}
DittoPrepare ditto_prepare_constants(const StageFrame& fr) const {
    return {{cfg.nr_width, cfg.nr_height}, {cfg.width, cfg.height},
            {fr.depth_x, fr.depth_y}, {fr.depth.width, fr.depth.height},
            {fr.motion_x, fr.motion_y}, {fr.motion.width, fr.motion.height},
            {fr.motion_scale_x, fr.motion_scale_y}, fr.depth_inverted ? 1u : 0u, 1u};
}
std::map<uint32_t, VkImageView> ditto_prepare_views(VkImageView ref) const {
    return {{0, ref}, {1, ditto_depth.view}, {2, ditto_motion.view},
            {4, ditto_reference.view}, {5, ditto_depth.view}, {6, ditto_motion.view}};
}
void prepare_ditto(VkCommandBuffer cmd, const StageFrame& fr, Borrow*& slot) {
    ensure_ditto(cmd);
    if (!slot) slot = &borrow_slot();
    Borrow& b = *slot;
    b.ditto_depth = ditto_view(fr.depth);
    b.ditto_motion = ditto_view(fr.motion);
    ditto_guide_barrier(cmd, fr.depth, false);
    ditto_guide_barrier(cmd, fr.motion, false);
    auto c = ditto_prepare_constants(fr);
    auto views = ditto_prepare_views(ditto_reference.view);
    views[1] = b.ditto_depth; views[2] = b.ditto_motion;
    dispatch(cmd, kDittoPrepare, set_for(programs[kDittoPrepare], views, {}, b.pool),
             &c, sizeof c, (cfg.nr_width + 15) / 16, (cfg.nr_height + 15) / 16);
    ditto_guide_barrier(cmd, fr.motion, true);
    ditto_guide_barrier(cmd, fr.depth, true);
}
void prepare_ditto_reference(VkCommandBuffer cmd, const StageFrame& fr, VkImageView ref, Borrow* borrowed) {
    auto c = ditto_prepare_constants(fr); c.guides = 0;
    auto views = ditto_prepare_views(ref);
    dispatch(cmd, kDittoPrepare,
             borrowed ? set_for(programs[kDittoPrepare], views, {}, borrowed->pool) : S(kDittoPrepare, views),
             &c, sizeof c, (cfg.nr_width + 15) / 16, (cfg.nr_height + 15) / 16);
}
void run_ditto(VkCommandBuffer cmd, const StageFrame& fr, bool reset, const Image& write) {
    struct Frame {
        uint32_t rw, rh, ow, oh;
        float jitX, jitY, mvToUv[2], mvRes[2];
        bool jitteredMv, depthInverted, hdr;
        float preExposure, exposureMul, reactiveMul;
    } f{cfg.nr_width, cfg.nr_height, cfg.width, cfg.height,
        0.f, 0.f,
        {1.f, 1.f}, {float(cfg.nr_width), float(cfg.nr_height)},
        false, fr.depth_inverted, hdr != 0, fr.pre_exposure, fr.exposure_scale, 1.f};
    DittoSettings defaults;
    DittoConstants c{};
    const bool restart = reset || !ditto_valid;
    float unused_jitter[2] = {};
    ditto_fill_constants(c, defaults, f, restart, false, false, false, unused_jitter);
    nr::ditto::keep_output_phase(c, fr.jitter_x, fr.jitter_y, fr.render_width, fr.render_height,
                               fr.jittered_motion, restart, ditto_previous_jitter);
    memory_barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdUpdateBuffer(cmd, ditto_constants, 0, sizeof c, &c);
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);
    const uint32_t cur = ditto_cur, next = cur ^ 1u;
    const std::map<uint32_t, VkBuffer> constants{{0, ditto_constants}};
    const auto run = [&](P p, std::initializer_list<std::pair<const uint32_t, VkImageView>> bindings, uint32_t x, uint32_t y) {
        dispatch(cmd, p, S(p, std::map<uint32_t, VkImageView>(bindings), constants), nullptr, 0, x, y);
    };
    const uint32_t nx = (cfg.nr_width + 7) / 8, ny = (cfg.nr_height + 7) / 8;
    // t5 and t6 are dynamically disabled; bind valid initialized images nonetheless.
    run(kDittoConvert, {{10, ditto_depth.view}, {11, ditto_motion.view}, {12, ditto_input.view},
                       {13, ditto_history[cur].view}, {15, ditto_change.view},
                       {20, ditto_mda[next].view}, {21, ditto_change.view}}, nx, ny + 1);
    run(kDittoMotion, {{10, ditto_aux[cur].view}, {11, ditto_mda[next].view}, {12, ditto_input.view},
                      {13, ditto_mda[cur].view}, {15, ditto_change.view}, {20, ditto_aux[next].view}}, nx, ny);
    const uint32_t ox = (cfg.width + 7) / 8, oy = (cfg.height + 7) / 8;
    run(kDittoUpscale, {{10, ditto_history[cur].view}, {11, ditto_mda[next].view}, {12, ditto_input.view},
                       {13, ditto_aux[next].view}, {14, ditto_change.view}, {15, ditto_change.view},
                       {16, ditto_change.view}, {20, ditto_history[next].view}}, (cfg.width + 15) / 16, oy);
    run(kDittoSharpen, {{10, ditto_history[next].view}, {11, ditto_mda[next].view}, {15, ditto_change.view},
                       {21, hdr ? write.view : ditto_result.view}}, ox, oy);
    ditto_cur = next; ditto_valid = true;
    if (!ditto_logged) {
        nr::logf("[nr] Ditto SR: NR output %ux%u -> %ux%u before OptiScaler SR (FP32, RGBA16F history)",
                 cfg.nr_width, cfg.nr_height, cfg.width, cfg.height);
        ditto_logged = true;
    }
}

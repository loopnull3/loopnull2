// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/image_base.h"
#include "video_core/texture_cache/util.h"

namespace {

using VideoCommon::ImageBase;
using VideoCommon::ImageInfo;
using VideoCommon::ImageType;
using VideoCommon::IsSameSurfaceAcrossSamples;
using VideoCommon::IsSubresource;
using VideoCommon::MakeShrinkImageCopies;
using VideoCommon::RelaxedOptions;
using VideoCommon::ResolveOverlap;
using VideoCommon::SubresourceBase;
using VideoCore::Surface::PixelFormat;

constexpr GPUVAddr Address = 0x412bf0000;

ImageInfo Info(u32 width, u32 height, int samples) {
    ImageInfo info;
    info.format = PixelFormat::A8B8G8R8_UNORM;
    info.type = ImageType::e2D;
    info.size = {width, height, 1};
    info.num_samples = samples;
    info.block = {0, 4, 0};
    info.resources.levels = 1;
    info.resources.layers = 1;
    return info;
}

// Minecraft 1.26.62: a 2x target of 1280x720 (its height block aligned) and the texture its last
// draw reads the samples through.
const ImageInfo Target = Info(2560, 768, 2);
const ImageInfo Texture = Info(2560, 720, 1);

} // namespace

TEST_CASE("A multisampled target and the wider texture over it are one surface",
          "[video_core][texture_cache]") {
    const ImageBase target(Target, Address, 0x1000);
    const ImageBase texture(Texture, Address, 0x1000);
    REQUIRE(IsSameSurfaceAcrossSamples(Texture, Address, target, false, false));
    REQUIRE(IsSameSurfaceAcrossSamples(Target, Address, texture, false, false));
    // Neither rule that was there takes them, which left them a bad overlap
    REQUIRE_FALSE(
        ResolveOverlap(Texture, Address, 0x1000, target, true, false, false).has_value());
    REQUIRE_FALSE(IsSubresource(Texture, target, Address,
                                RelaxedOptions::Size | RelaxedOptions::Format, false, false));
}

TEST_CASE("Other pairs are not one surface across sample counts", "[video_core][texture_cache]") {
    const ImageBase target(Target, Address, 0x1000);
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(Texture, Address + 0x1000, target, false, false));
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(
        Info(1280, 720, 1), Address, ImageBase(Info(1280, 768, 1), Address, 0x1000), false, false));
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(Info(1280, 720, 1), Address, target, false, false));
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(Info(2560, 600, 1), Address, target, false, false));
    ImageInfo two_levels = Texture;
    two_levels.resources.levels = 2;
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(two_levels, Address, target, false, false));
    ImageInfo depth = Texture;
    depth.format = PixelFormat::D32_FLOAT;
    REQUIRE_FALSE(IsSameSurfaceAcrossSamples(depth, Address, target, false, false));
}

TEST_CASE("The copy into a multisampled target counts its pixels, not its samples",
          "[video_core][texture_cache]") {
    const auto into_texture = MakeShrinkImageCopies(Texture, Target, SubresourceBase{0, 0}, 1, 0);
    REQUIRE(into_texture.size() == 1);
    REQUIRE(into_texture[0].extent.width == 2560);
    REQUIRE(into_texture[0].extent.height == 720);
    // 768 rows where the texture has 720: the join clamps it to what the source holds
    const auto into_target = MakeShrinkImageCopies(Target, Texture, SubresourceBase{0, 0}, 1, 0);
    REQUIRE(into_target.size() == 1);
    REQUIRE(into_target[0].extent.width == 1280);
    REQUIRE(into_target[0].extent.height == 768);
}

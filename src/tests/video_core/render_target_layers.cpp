// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/util.h"

namespace {

using VideoCommon::ImageType;
using VideoCommon::RenderTargetRange;
using VideoCommon::SubresourceBase;
using VideoCommon::SubresourceRange;

} // namespace

// Minecraft 1.26.62 fills a cube map by binding all six layers of it six times, with the face in
// the layer register and no program that selects a layer.
TEST_CASE("A draw into an array goes to the layer the register names",
          "[video_core][texture_cache]") {
    for (u32 face = 1; face < 6; ++face) {
        const SubresourceRange range =
            RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, 6, face);
        REQUIRE(range.base.level == 0);
        REQUIRE(range.base.layer == static_cast<s32>(face));
        REQUIRE(range.extent.levels == 1);
        REQUIRE(range.extent.layers == 1);
    }
    // In an image the target does not start
    const SubresourceRange part =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 1, .layer = 2}, 4, 3);
    REQUIRE(part.base.level == 1);
    REQUIRE(part.base.layer == 5);
    REQUIRE(part.extent.layers == 1);
}

TEST_CASE("Without a layer past the first, a target keeps its layers",
          "[video_core][texture_cache]") {
    // The first layer, a clear, or a program that selects: all layers, as before
    const SubresourceRange whole =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, 6, 0);
    REQUIRE(whole.base.layer == 0);
    REQUIRE(whole.extent.layers == 6);
    // A layer the target does not have
    const SubresourceRange past =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, 1, 3);
    REQUIRE(past.base.layer == 0);
    REQUIRE(past.extent.layers == 1);
    // Volumes keep their slices
    const SubresourceRange volume =
        RenderTargetRange(ImageType::e3D, SubresourceBase{.level = 0, .layer = 0}, 16, 3);
    REQUIRE(volume.base.layer == 0);
    REQUIRE(volume.extent.layers == 16);
}

// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/util.h"

namespace {

using VideoCommon::ImageType;
using VideoCommon::RenderTargetRange;
using VideoCommon::SubresourceBase;
using VideoCommon::SubresourceRange;

} // namespace

// A face of a cube is drawn with the cube's address, the layers counted from its first one and
// the face as the base layer: face 3 has 4 layers and base layer 3.
TEST_CASE("A render target starts at its base layer", "[video_core][texture_cache]") {
    for (s32 face = 0; face < 6; ++face) {
        const SubresourceRange range = RenderTargetRange(
            ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, face + 1,
            static_cast<u32>(face));
        REQUIRE(range.base.level == 0);
        REQUIRE(range.base.layer == face);
        REQUIRE(range.extent.levels == 1);
        REQUIRE(range.extent.layers == 1);
    }
    // Several layers from a base layer, in an image the target does not start
    const SubresourceRange part =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 1, .layer = 2}, 5, 2);
    REQUIRE(part.base.level == 1);
    REQUIRE(part.base.layer == 4);
    REQUIRE(part.extent.layers == 3);
}

TEST_CASE("A render target without a base layer covers its layers from the first",
          "[video_core][texture_cache]") {
    const SubresourceRange whole =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, 6, 0);
    REQUIRE(whole.base.layer == 0);
    REQUIRE(whole.extent.layers == 6);
    // A base layer past the layers the target has is not a layer of it
    const SubresourceRange past =
        RenderTargetRange(ImageType::e2D, SubresourceBase{.level = 0, .layer = 0}, 2, 2);
    REQUIRE(past.base.layer == 0);
    REQUIRE(past.extent.layers == 2);
    // Volumes keep their slices
    const SubresourceRange volume =
        RenderTargetRange(ImageType::e3D, SubresourceBase{.level = 0, .layer = 0}, 16, 3);
    REQUIRE(volume.base.layer == 0);
    REQUIRE(volume.extent.layers == 16);
}

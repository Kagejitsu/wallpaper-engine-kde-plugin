#include <doctest.h>
#include <bit>
#include <limits>

#include "ParserTestFs.hpp"
#include "WPMdlParser.hpp"

using namespace wallpaper;

namespace
{
void U32(std::vector<uint8_t>& data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.push_back((uint8_t)(value >> (8 * i)));
}
void Str(std::vector<uint8_t>& data, const std::string& value) {
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
}
std::vector<uint8_t> MeshPrefix() {
    std::vector<uint8_t> data;
    Str(data, "MDLV0013");
    U32(data, 1);
    U32(data, 1);
    U32(data, 1);
    Str(data, "material.json");
    U32(data, 0);
    return data;
}
std::vector<uint8_t> StaticModel() {
    auto data = MeshPrefix();
    U32(data, 52);
    data.resize(data.size() + 52);
    U32(data, 6);
    data.resize(data.size() + 6);
    Str(data, "MDLS0001");
    U32(data, 0);
    data.resize(data.size() + 4); // zero bones, unknown uint16
    return data;
}
std::vector<uint8_t> AnimatedModel(const std::string& mode, uint32_t frame_bytes,
                                   int32_t length = 1, float fps = 30.0f) {
    auto data = StaticModel();
    Str(data, "MDLA0001");
    U32(data, 0);
    U32(data, 1);
    U32(data, 1);
    U32(data, 0);
    Str(data, "test");
    Str(data, mode);
    U32(data, std::bit_cast<uint32_t>(fps));
    U32(data, (uint32_t)length);
    U32(data, 0);
    U32(data, 1);
    U32(data, 0);
    U32(data, frame_bytes);
    data.resize(data.size() + frame_bytes);
    U32(data, 0);
    return data;
}
bool ParseModel(std::vector<uint8_t> data, WPMdl& mdl) {
    fs::VFS vfs;
    auto    files             = std::make_unique<ParserTestFs>();
    files->files["/test.mdl"] = std::move(data);
    if (! vfs.Mount("/assets", std::move(files))) return false;
    return WPMdlParser::Parse("test.mdl", vfs, mdl);
}
} // namespace

TEST_SUITE("WPMdlParser") {
    TEST_CASE("static models may end after the skeleton") {
        WPMdl mdl;
        REQUIRE(ParseModel(StaticModel(), mdl));
        REQUIRE(mdl.puppet != nullptr);
        CHECK(mdl.vertexs.size() == 1);
        CHECK(mdl.indices.size() == 1);
        CHECK(mdl.puppet->anims.empty());
    }

    TEST_CASE("truncated fixed fields and missing alternate herald fail") {
        const auto valid = StaticModel();
        for (size_t size = 0; size < valid.size(); ++size) {
            INFO("prefix length: ", size);
            WPMdl mdl;
            CHECK_FALSE(ParseModel({ valid.begin(), valid.begin() + (isize)size }, mdl));
        }
        auto alternate = MeshPrefix();
        U32(alternate, 0);
        WPMdl mdl;
        CHECK_FALSE(ParseModel(std::move(alternate), mdl));
    }

    TEST_CASE("truncated animation ids and invalid versions fail") {
        auto data = StaticModel();
        Str(data, "MDLA0001");
        U32(data, 0);
        U32(data, 1);
        U32(data, 0); // Padding with no following animation id.
        WPMdl mdl;
        CHECK_FALSE(ParseModel(std::move(data), mdl));
        data = StaticModel();
        Str(data, "MDLAoops");
        bool parsed = true;
        CHECK_NOTHROW(parsed = ParseModel(std::move(data), mdl));
        CHECK_FALSE(parsed);
    }

    TEST_CASE("animation counts and bone frame sizes are bounded by remaining bytes") {
        auto data = StaticModel();
        Str(data, "MDLA0001");
        U32(data, 0);
        U32(data, 0xFFFFFFFFu);
        WPMdl mdl;
        CHECK_FALSE(ParseModel(std::move(data), mdl));

        data = StaticModel();
        Str(data, "MDLA0001");
        U32(data, 0);
        U32(data, 1);
        U32(data, 1);
        U32(data, 0);
        Str(data, "test");
        Str(data, "loop");
        U32(data, 0x41F00000u); // 30.0f
        U32(data, 1);
        U32(data, 0);
        U32(data, 1);
        U32(data, 0);
        U32(data, 36); // One bone frame with no frame payload.
        CHECK_FALSE(ParseModel(std::move(data), mdl));
    }
    TEST_CASE("empty bone tracks and unknown playback modes fail before runtime") {
        WPMdl mdl;
        CHECK_FALSE(ParseModel(AnimatedModel("loop", 0), mdl));
        CHECK_FALSE(ParseModel(AnimatedModel("invalid", 36), mdl));
        REQUIRE(ParseModel(AnimatedModel("loop", 36), mdl));
        REQUIRE(mdl.puppet->anims.size() == 1);
        CHECK(mdl.puppet->anims[0].bframes_array[0].frames.size() == 1);
    }
    TEST_CASE("animation timing and track lengths are safe for interpolation") {
        WPMdl mdl;
        for (const int32_t length : { 0, -1, 2 }) {
            INFO("length: ", length);
            CHECK_FALSE(ParseModel(AnimatedModel("loop", 36, length), mdl));
        }
        for (const float fps : { 0.0f,
                                 -30.0f,
                                 std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity() }) {
            INFO("fps: ", fps);
            CHECK_FALSE(ParseModel(AnimatedModel("loop", 36, 1, fps), mdl));
        }
        REQUIRE(ParseModel(AnimatedModel("loop", 72, 2), mdl));
        double      time          = 0.0;
        const auto& animation     = mdl.puppet->anims[0];
        const auto  interpolation = animation.getInterpolationInfo(&time);
        CHECK(interpolation.frame_a < (idx)animation.bframes_array[0].frames.size());
        CHECK(interpolation.frame_b < (idx)animation.bframes_array[0].frames.size());
    }
}

#include <doctest.h>

#include "WPUserProperties.hpp"
#include "wpscene/WPMaterial.h"

using namespace wallpaper;
using namespace wallpaper::wpscene;

TEST_SUITE("WPMaterial") {
    TEST_CASE("user shader values override defaults in materials and pass overrides") {
        WPUserProperties properties;
        properties.SetProperty("tint", "0.2 0.4 0.6");
        properties.SetProperty("enabled", true);
        properties.SetProperty("opacity", 0.75);
        UserPropertiesScope  scope(&properties);
        const nlohmann::json pass = { { "shader", "test" },
                                      { "constantshadervalues",
                                        { { "color", "1 1 1" }, { "fallback", 0.25 } } },
                                      { "usershadervalues",
                                        { { "tint", "color" },
                                          { "enabled", "flag" },
                                          { "opacity", "alpha" },
                                          { "missing", "fallback" } } } };
        WPMaterial           material;
        REQUIRE(material.FromJson({ { "passes", { pass } } }));
        CHECK(material.constantshadervalues.at("color") == std::vector<float> { 0.2f, 0.4f, 0.6f });
        CHECK(material.constantshadervalues.at("flag") == std::vector<float> { 1.0f });
        CHECK(material.constantshadervalues.at("alpha") == std::vector<float> { 0.75f });
        CHECK(material.constantshadervalues.at("fallback") == std::vector<float> { 0.25f });
        WPMaterialPass override_pass;
        REQUIRE(override_pass.FromJson(pass));
        CHECK(override_pass.constantshadervalues == material.constantshadervalues);
    }

    TEST_CASE("absent context and invalid mappings preserve defaults") {
        WPMaterialPass pass;
        REQUIRE(pass.FromJson({ { "constantshadervalues", { { "alpha", 0.25 } } },
                                { "usershadervalues", { { "opacity", "alpha" } } } }));
        CHECK(pass.constantshadervalues.at("alpha") == std::vector<float> { 0.25f });
        WPUserProperties properties;
        properties.SetProperty("opacity", 0.75);
        UserPropertiesScope scope(&properties);
        CHECK_NOTHROW(pass.FromJson({ { "usershadervalues", { "malformed" } } }));
        CHECK(pass.constantshadervalues.at("alpha") == std::vector<float> { 0.25f });
    }
}

#pragma once

#include <memory>
#include <optional>
#include <rwe/render/SpriteSeries.h>
#include <string>
#include <variant>

namespace rwe
{
    struct FeatureSpriteInfo
    {
        std::shared_ptr<SpriteSeries> animation;
        bool transparentAnimation;
        std::optional<std::shared_ptr<SpriteSeries>> shadowAnimation;
        bool transparentShadow;
        /** Play-once frames selected by the simulation's burn countdown. */
        std::optional<std::shared_ptr<SpriteSeries>> burnAnimation;
    };

    struct FeatureObjectInfo
    {
        std::string objectName;
    };

    using FeatureRenderInfo = std::variant<FeatureSpriteInfo, FeatureObjectInfo>;

    struct FeatureMediaInfo
    {
        std::string world;
        std::string description;
        std::string category;

        FeatureRenderInfo renderInfo;

        /** The GAF the feature's sequences live in (without path or extension). */
        std::string fileName;

        std::string seqNameReclamate;

        std::string seqNameBurn;
        std::string seqNameBurnShad;

        std::string seqNameDie;
    };
}

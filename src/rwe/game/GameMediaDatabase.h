#pragma once

#include <array>
#include <memory>
#include <rwe/AudioService.h>
#include <rwe/util/hash_combine.h>
#include <rwe/collections/SimpleVectorMap.h>
#include <rwe/game/FeatureMediaInfo.h>
#include <rwe/game/UnitPieceMeshInfo.h>
#include <rwe/game/WeaponMediaInfo.h>
#include <rwe/geometry/CollisionMesh.h>
#include <rwe/io/soundtdf/SoundClass.h>
#include <rwe/math/Matrix4f.h>
#include <rwe/render/GlMesh.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/sim/FeatureDefinitionId.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/util/rwe_string.h>
#include <utility>

namespace rwe
{
    /**
     * Everything about a model that the renderer would otherwise work out
     * again for every piece of every unit, every frame: which piece is whose
     * parent, an order that visits a parent before its children, the piece
     * meshes themselves, and the transforms a model has when nothing has
     * moved.
     *
     * Before this existed, drawing a piece looked its parent chain up by
     * name -- a toUpper() allocation and a hash lookup a link -- and then
     * asked the media database for the mesh with a case-insensitive hash of
     * two strings. Eight hundred Peewees have 10,400 visible pieces between
     * them and the shadow pass walks all of them a second time, which put
     * 17.7 ms a frame into name lookups alone. None of it varies over the
     * life of a model, so it is resolved once, on first use.
     */
    struct UnitModelRenderInfo
    {
        /** Parent of each piece, or -1 for a root. */
        std::vector<int> parentIndices;

        /** Piece indices in an order that puts every parent before its children. */
        std::vector<int> evaluationOrder;

        /** The piece meshes, in piece order. */
        std::vector<const UnitPieceMeshInfo*> pieces;

        /** Each piece's transform with every offset and rotation at rest, in piece order. */
        std::vector<Matrix4f> restTransforms;
    };

    class GameMediaDatabase
    {
    private:
        struct CaseInsensitiveHash
        {
            std::hash<std::string> hash;

            std::size_t operator()(const std::string& key) const
            {
                return hash(toUpper(key));
            }
        };

        struct CaseInsensitiveEquals
        {
            bool operator()(const std::string& a, const std::string& b) const
            {
                return toUpper(a) == toUpper(b);
            }
        };

        struct CaseInsensitivePairHash
        {
            std::size_t operator()(const std::pair<std::string, std::string>& key) const
            {
                std::size_t seed = 0;
                hashCombine(seed, std::hash<std::string>{}(toUpper(key.first)));
                hashCombine(seed, std::hash<std::string>{}(toUpper(key.second)));
                return seed;
            }
        };

        struct CaseInsensitivePairEquals
        {
            bool operator()(const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) const
            {
                return toUpper(a.first) == toUpper(b.first) && toUpper(a.second) == toUpper(b.second);
            }
        };

    private:
        std::unordered_map<std::pair<std::string, std::string>, UnitPieceMeshInfo, CaseInsensitivePairHash, CaseInsensitivePairEquals> unitPieceMeshesMap;
        std::unordered_map<std::string, std::array<Vector3f, 4>, CaseInsensitiveHash, CaseInsensitiveEquals> selectionQuadsMap;

        std::unordered_map<std::pair<std::string, std::string>, std::shared_ptr<SpriteSeries>, CaseInsensitivePairHash, CaseInsensitivePairEquals> spritesMap;

        std::unordered_map<std::string, WeaponMediaInfo> weaponMap;

        std::unordered_map<std::string, SoundClass> soundClassMap;

        std::unordered_map<std::string, AudioService::SoundHandle> soundMap;

        std::unordered_map<std::string, std::shared_ptr<CollisionMesh>> selectionCollisionMeshesMap;

        SimpleVectorMap<FeatureMediaInfo, FeatureDefinitionIdTag> featureMap;

        // Resolved on first use and kept for the life of the database, keyed
        // by the definition's address: model definitions live in the
        // simulation's own map and are never moved or rebuilt while a game
        // is running. Mutable because resolving is a cache fill, not a
        // change: every caller of getUnitModelRenderInfo holds the database
        // by const reference.
        mutable std::unordered_map<const UnitModelDefinition*, UnitModelRenderInfo> unitModelRenderInfoCache;

    public:
        void addUnitPieceMesh(const std::string& unitName, const std::string& pieceName, const UnitPieceMeshInfo& pieceMesh);

        std::optional<std::reference_wrapper<const UnitPieceMeshInfo>> getUnitPieceMesh(const std::string& objectName, const std::string& pieceName) const;

        /** The model's hierarchy and meshes, resolved once. See UnitModelRenderInfo. */
        const UnitModelRenderInfo& getUnitModelRenderInfo(const std::string& objectName, const UnitModelDefinition& modelDefinition) const;

        /** The corners of the model's selection plate, model space. */
        std::optional<std::array<Vector3f, 4>> getSelectionQuad(const std::string& objectName) const;

        void addSelectionQuad(const std::string& objectName, const std::array<Vector3f, 4>& corners);

        void addSpriteSeries(const std::string& gafName, const std::string& animName, std::shared_ptr<SpriteSeries> sprite);

        std::optional<std::shared_ptr<SpriteSeries>> getSpriteSeries(const std::string& gaf, const std::string& anim) const;

        const WeaponMediaInfo& getWeapon(const std::string& weaponName) const;

        std::optional<std::reference_wrapper<const WeaponMediaInfo>> tryGetWeapon(const std::string& weaponName) const;

        void addWeapon(const std::string& name, WeaponMediaInfo&& weapon);

        const SoundClass& getSoundClassOrDefault(const std::string& className) const;

        const SoundClass& getSoundClass(const std::string& className) const;

        void addSoundClass(const std::string& className, SoundClass&& soundClass);

        const AudioService::SoundHandle& getSoundHandle(const std::string& sound) const;

        std::optional<AudioService::SoundHandle> tryGetSoundHandle(const std::string& sound) const;

        void addSound(const std::string& soundName, const AudioService::SoundHandle& sound);

        void addSelectionCollisionMesh(const std::string& objectName, std::shared_ptr<CollisionMesh> mesh);

        std::optional<std::shared_ptr<CollisionMesh>> getSelectionCollisionMesh(const std::string& objectName) const;

        const FeatureMediaInfo& getFeature(FeatureDefinitionId featureId) const;

        FeatureMediaInfo& getFeature(FeatureDefinitionId featureId);

        FeatureDefinitionId addFeature(FeatureMediaInfo&& feature);
    };
}

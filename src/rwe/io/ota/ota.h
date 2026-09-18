#pragma once

#include <optional>
#include <rwe/io/tdf/TdfBlock.h>
#include <string>
#include <vector>

namespace rwe
{
    class OtaParseException : public std::runtime_error
    {
    public:
        explicit OtaParseException(const std::string& __arg);
        explicit OtaParseException(const char* string);
    };

    struct OtaSpecial
    {
        std::string specialWhat;
        int xPos;
        int zPos;
    };

    struct OtaFeature
    {
        std::string featureName;
        int xPos;
        int zPos;
    };

    struct OtaSchema
    {
        std::string type;
        std::string aiProfile;
        int surfaceMetal;
        int mohoMetal;
        int humanMetal;
        int computerMetal;
        int humanEnergy;
        int computerEnergy;
        std::string meteorWeapon;
        int meteorRadius;
        float meteorDensity;
        int meteorDuration;
        int meteorInterval;

        std::vector<OtaFeature> features;
        std::vector<OtaSpecial> specials;
    };

    struct OtaRecord
    {
        std::string missionName;
        std::string missionDescription;
        std::string planet;
        std::string missionHint;
        std::string brief;
        std::string narration;
        std::string glamour;
        int lineOfSight;
        int mapping;
        int tidalStrength;
        int solarStrength;
        bool lavaWorld;
        int killMul;
        int timeMul;
        int minWindSpeed;
        int maxWindSpeed;
        int gravity;
        std::string numPlayers;
        std::string size;
        std::string memory;
        std::string useOnlyUnits;
        int schemaCount;

        std::vector<OtaSchema> schemas;
    };

    OtaSchema parseOtaSchema(const TdfBlock& tdf);

    /**
     * The StartPos<n> special the schema declares for player slot n
     * (1-based), if it declares one. Maps may leave gaps in the numbering
     * and campaign schemas usually stop at StartPos1, so a caller must be
     * ready for nothing to come back.
     */
    std::optional<OtaSpecial> findStartPosition(const OtaSchema& schema, int n);

    /** How many of StartPos1 to StartPos10 the schema declares, gaps not counted. */
    int countStartPositions(const OtaSchema& schema);

    OtaRecord parseOta(const TdfBlock& tdf);

    OtaRecord parseOtaGlobalHeader(const TdfBlock& tdf);

    OtaFeature parseOtaFeature(const TdfBlock& tdf);

    OtaSpecial parseOtaSpecial(const TdfBlock& tdf);
}

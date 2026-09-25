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

    /**
     * One order of a mission unit's InitialMission string, as the
     * interpreter at 0x487BF0 reads it (TOTALA-EXE-DATA.md S:105). The
     * string is comma separated; each order is a letter, upper or lower
     * case, and its arguments.
     */
    struct MissionOrder
    {
        enum class Kind
        {
            /** m x z: move to the point (mission 2). */
            Move,
            /** p x z [y]: patrol to the point (mission 9). */
            Patrol,
            /** a x z: attack the point (mission 3). */
            AttackPoint,
            /** a NAME: attack every unit of that type (ATTACKUTYPE). */
            AttackType,
            /** g IDENT: guard the unit with that Ident (mission 7). */
            Guard,
            /** i IDENT: start the mission aboard the transport with that Ident (0x48AAC0, at once; nothing is queued). */
            Link,
            /** o move fire: the standing orders, written straight into the unit (bits 18-19 move, 20-21 fire); nothing is queued. */
            StandingOrders,
            /** w seconds [radius]: wait that long, or until an enemy comes within the radius (WAIT). */
            Wait,
            /** wa [IDENT]: wait until the unit, or the one named, is hit (WAITFORATTACK). */
            WaitForAttack,
            /** u x z: by its shape, unload at the point (mission 5); no shipped mission uses it. */
            Unload,
            /** b NAME n x z: build the named unit at the point, or n of them from a plant. */
            Build,
            /** bw n: stockpile n missiles (BUILDWEAPON); nothing that makes the unit held. */
            BuildWeapon,
            /** d: self-destruct (SELFDESTRUCTFG). */
            SelfDestruct,
            /** s: hand the unit to the player (MAKESELECTABLE), at that point in the list. */
            MakeSelectable,
            /** Any other letter: 0x487E50 skips it and nothing happens. */
            Skip,
        };

        Kind kind{Kind::MakeSelectable};
        /** The numbers the order's format scanned, in order; fewer than the format asks for when the text stops early, as sscanf leaves them. */
        std::vector<float> numbers;
        /** The name for a/g/i/b. */
        std::string name;
    };

    /**
     * Parses an InitialMission string. The MAKESELECTABLE the interpreter
     * appends at the end of a list that queued anything and had no `s`, `p`,
     * point `a` or `d` in it (0x487E76) is not included: the list is what the
     * mission file says.
     */
    std::vector<MissionOrder> parseInitialMission(const std::string& text);

    /** One [unitN] of a mission schema's [units] (0x436DFE-0x437002, "MISSIONUNIT DATA"). */
    struct OtaMissionUnit
    {
        std::string unitName;
        std::string ident;
        std::string initialMission;
        std::vector<MissionOrder> orders;
        int xPos{0};
        int yPos{0};
        int zPos{0};
        /** Degrees, as written; 0x436EF9 turns it into the 16-bit angle. */
        int angle{0};
        int player{0};
        int healthPercentage{100};
        int buildPriority{0};
        int creationCountdown{0};
        /** InitialGroup, a squad number 0-15. */
        int initialGroup{0};
        bool missionCriticalUnit{false};
        bool aiIgnore{false};
        bool aiPriorityTarget{false};
        bool immunity{false};
    };

    /** A unit type and a number: KillUnitType, UnitTypePassesX/Z, UnitTypeKilled (`%[a-zA-Z],%i`). */
    struct OtaUnitTypeAndNumber
    {
        std::string unitType;
        int number{0};
    };

    /** MoveUnitToRadius: a unit type (or ANYTYPE) and a circle (`%[a-zA-Z],%i,%i,%i`). */
    struct OtaMoveUnitToRadius
    {
        std::string unitType;
        int x{0};
        int z{0};
        int radius{0};
    };

    /**
     * A mission's win and lose conditions. The mission reader does not read
     * these; the rule builder at 0x48E010 reads them off the GlobalHeader
     * when the mission starts, integers defaulting to 0 (AnyUnitPassesX and
     * Z to -1) and strings to absent. Victory conditions first, then defeat,
     * in the builder's order. buildMissionRules makes the rules of them.
     */
    struct OtaMissionRules
    {
        int killEnemyCommander{0};
        int destroyAllUnits{0};
        int killAllMobileUnits{0};
        std::optional<std::string> buildUnitType;
        std::optional<std::string> captureUnitType;
        std::optional<std::string> killAllOfType;
        std::optional<OtaUnitTypeAndNumber> killUnitType;
        std::optional<OtaMoveUnitToRadius> moveUnitToRadius;
        std::optional<OtaUnitTypeAndNumber> unitTypePassesX;
        std::optional<OtaUnitTypeAndNumber> unitTypePassesZ;
        int victoryTimerRunsOut{0};

        int commanderKilled{0};
        int allUnitsKilled{0};
        std::optional<std::string> allUnitsKilledOfType;
        std::optional<OtaUnitTypeAndNumber> unitTypeKilled;
        int deathTimerRunsOut{0};
        /** Unlike the other integers these default to -1, and 0 is a real line (0x48E8CE). */
        int anyUnitPassesX{-1};
        int anyUnitPassesZ{-1};
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
        /** A mission's starting units; empty on a skirmish map. */
        std::vector<OtaMissionUnit> units;
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

        /** The mission's unit cap (0x4C46C0, default 200). */
        int maxUnits{200};
        /** camps\\briefs\\<glamoursound>.WAV. */
        std::string glamourSound;
        bool noMovie{false};
        bool noSeaLevelTrigger{false};
        bool waterDoesDamage{false};
        int waterDamage{0};
        OtaMissionRules rules;

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

    OtaMissionRules parseOtaMissionRules(const TdfBlock& tdf);
    OtaMissionUnit parseOtaMissionUnit(const TdfBlock& tdf);
    OtaFeature parseOtaFeature(const TdfBlock& tdf);

    OtaSpecial parseOtaSpecial(const TdfBlock& tdf);
}

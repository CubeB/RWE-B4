#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * Turning a Total Annihilation installation into an RWE data directory.
     *
     * The parts that decide *what* to copy live here and take listings rather
     * than touching the disk, so they can be tested against the real shape of
     * a GOG install without one being present. The parts that walk the
     * filesystem, read the registry and copy bytes live in `rwe_setup.cpp`.
     */

    /** What a file in a TA installation is, as far as RWE cares. */
    enum class TaFileKind
    {
        /** An HPI-family archive: the game data proper. */
        Archive,

        /** A Smacker film. The GOG release ships them as `.ZRB` under `Data`. */
        Movie,

        /** A soundtrack file. */
        Music,

        /** Everything else -- the executable, docs, saves, the launcher. */
        Ignored,
    };

    /**
     * The archive extensions the engine's VFS mounts, lower-cased.
     *
     * Kept in step with `addToVfs` by the test, so that a new extension there
     * cannot silently leave files behind here.
     */
    const std::vector<std::string>& taArchiveExtensions();

    /** Classifies a file by name alone; `subdirectory` is "" for the root. */
    TaFileKind classifyTaFile(const std::string& subdirectory, const std::string& fileName);

    /**
     * Where a file of this kind belongs, relative to RWE's data directory.
     *
     * Archives sit flat in the data directory, because that is the single
     * directory the VFS scans for them. Films and music go into the
     * subdirectories the engine asks the VFS for by name -- `movies/2.zrb`
     * and `music`. The VFS is case-insensitive (see `findPathCaseInsensitive`),
     * so the mixed casing the GOG release ships -- `1.ZRB` beside `2.zrb` --
     * is carried across untouched rather than normalised.
     */
    std::filesystem::path taFileDestination(TaFileKind kind, const std::string& fileName);

    /** One file the setup would copy. */
    struct TaCopyItem
    {
        std::filesystem::path source;
        std::filesystem::path destination;
        TaFileKind kind;
        std::uintmax_t size{0};

        /** Already present at the destination with the same size. */
        bool alreadyPresent{false};
    };

    /** A listing of one candidate installation, as read off the disk. */
    struct TaInstallListing
    {
        std::filesystem::path root;

        /** `(subdirectory, filename, size)`; subdirectory is "" for the root. */
        std::vector<std::tuple<std::string, std::string, std::uintmax_t>> files;
    };

    /**
     * The archives without which the engine has no game to load.
     *
     * `totala1.hpi` and `totala2.hpi` are the base game. Everything else is
     * content: the expansions, the map packs, and `rev31.gp3`, which carries
     * the v3.1 unit and weapon data the findings in `docs/TOTALA-EXE.md` are
     * written against. A missing `rev31.gp3` is a warning rather than an
     * error, because the game still runs on the 1.0 data -- it simply is not
     * the data most of the behavioural work was checked against.
     */
    const std::vector<std::string>& requiredArchives();
    const std::vector<std::string>& recommendedArchives();

    /**
     * Whether a directory listing looks like a Total Annihilation install.
     *
     * The test is the required archives being present, which is also what
     * makes it safe to point the tool at a drive and let it search: no other
     * directory on a machine has `totala1.hpi` in it by accident.
     */
    bool looksLikeTaInstall(const TaInstallListing& listing);

    /** What the setup intends to do, before it does any of it. */
    struct TaSetupPlan
    {
        std::vector<TaCopyItem> items;

        /** Required archives the source does not have. */
        std::vector<std::string> missingRequired;

        /** Recommended archives the source does not have. */
        std::vector<std::string> missingRecommended;

        std::uintmax_t bytesToCopy{0};
        std::size_t alreadyPresentCount{0};
    };

    /**
     * Whether any of these directories holds game data the engine can load.
     *
     * The engine asks this before it starts, so that a first run without data
     * produces an explanation rather than a filesystem error from inside the
     * VFS. The test is deliberately weaker than looksLikeTaInstall: a player
     * may legitimately have assembled a data directory by hand, or be running
     * a total conversion, so the question is whether there is *an* archive
     * present rather than whether the stock ones are.
     */
    bool anyPathHasGameData(const std::vector<std::filesystem::path>& paths);

    /**
     * Works out what to copy where.
     *
     * `existing` is a listing of the destination in the same shape, so a
     * second run can tell what is already there and copy nothing. Files are
     * compared by name and size: TA data is immutable once installed, and
     * hashing 1.1 GB to discover that would cost more than the copy it saves.
     */
    TaSetupPlan planTaSetup(
        const TaInstallListing& source,
        const std::filesystem::path& dataDirectory,
        const std::vector<std::tuple<std::string, std::string, std::uintmax_t>>& existing);
}

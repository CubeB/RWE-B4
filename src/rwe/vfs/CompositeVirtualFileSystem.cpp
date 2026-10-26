#include "CompositeVirtualFileSystem.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <rwe/io/hpi/hpi_util.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>
#include <rwe/vfs/DirectoryFileSystem.h>
#include <rwe/vfs/HpiFileSystem.h>
#include <set>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        /**
         * Archives disagree about a directory's case (`features/Corpses` in
         * one, `features/corpses` in another), so a case-sensitive merge lists
         * the same file twice and the second read is wasted. Compare letters
         * without case; the first archive's spelling wins and the result stays
         * ordered.
         */
        struct CaseInsensitiveLess
        {
            bool operator()(const std::string& a, const std::string& b) const
            {
                return std::lexicographical_compare(
                    a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
                        return std::tolower(x) < std::tolower(y);
                    });
            }
        };
    }

    std::optional<std::vector<char>> CompositeVirtualFileSystem::readFile(const std::string& filename) const
    {
        for (const auto& fs : filesystems)
        {
            // An entry that will not extract (a damaged chunk, a size past
            // the cap) is that entry missing from that archive; the search
            // goes on, and the file costs itself rather than the launch.
            try
            {
                auto file = fs->readFile(filename);
                if (file)
                {
                    // The whole-file loader (0x4BBE50) hands back nothing
                    // unless the size is above zero, and does not go on to
                    // another archive: an empty file here hides any copy
                    // further down and then reads as missing (BT-170, #605;
                    // static byte-tactics reading at 97d7ac59, not executed
                    // against the original). Callers already treat nullopt
                    // as no such file; the film player skips it.
                    if (file->empty())
                    {
                        return std::nullopt;
                    }
                    return file;
                }
            }
            catch (const HpiException& e)
            {
                LOG_WARN << "Ignoring " << filename << " in " << fs->getPath() << ": " << e.what();
            }
        }

        return std::nullopt;
    }

    std::optional<std::vector<char>> CompositeVirtualFileSystem::readFileFromSource(const std::string& source, const std::string& filename) const
    {
        for (const auto& fs : filesystems)
        {
            if (fs->getPath() != source)
            {
                continue;
            }

            auto file = fs->readFile(filename);
            if (file && !file->empty())
            {
                return file;
            }
            return std::nullopt;
        }

        return std::nullopt;
    }

    std::vector<std::string>
    CompositeVirtualFileSystem::getFileNames(const std::string& directory, const std::string& extension)
    {
        std::set<std::string> entries;

        for (const auto& fs : filesystems)
        {
            auto v = fs->getFileNames(directory, extension);
            entries.insert(v.begin(), v.end());
        }

        std::vector<std::string> v(entries.begin(), entries.end());
        return v;
    }

    std::vector<std::pair<std::string, std::string>>
    CompositeVirtualFileSystem::getFileNamesWithSources(const std::string& directory, const std::string& extension)
    {
        std::map<std::string, std::string> entries;

        for (const auto& fs : filesystems)
        {
            auto v = fs->getFileNames(directory, extension);
            for (const auto& filename : v)
            {
                entries.insert({filename, fs->getPath()});
            }
        }

        std::vector<std::pair<std::string, std::string>> v(entries.begin(), entries.end());
        return v;
    }

    std::vector<std::string>
    CompositeVirtualFileSystem::getFileNamesRecursive(const std::string& directory, const std::string& extension)
    {
        std::set<std::string, CaseInsensitiveLess> entries;

        for (const auto& fs : filesystems)
        {
            auto v = fs->getFileNamesRecursive(directory, extension);
            entries.insert(v.begin(), v.end());
        }

        std::vector<std::string> v(entries.begin(), entries.end());
        return v;
    }

    std::vector<std::pair<std::string, std::string>>
    CompositeVirtualFileSystem::getFileNamesRecursiveWithSources(const std::string& directory, const std::string& extension)
    {
        std::map<std::string, std::string, CaseInsensitiveLess> entries;

        for (const auto& fs : filesystems)
        {
            auto v = fs->getFileNamesRecursive(directory, extension);
            for (const auto& filename : v)
            {
                entries.insert({filename, fs->getPath()});
            }
        }

        std::vector<std::pair<std::string, std::string>> v(entries.begin(), entries.end());
        return v;
    }

    std::vector<std::string> CompositeVirtualFileSystem::getDirectoryNames(const std::string& directory)
    {
        std::set<std::string> entries;
        for (const auto& fs : filesystems)
        {
            auto v = fs->getDirectoryNames(directory);
            entries.insert(v.begin(), v.end());
        }

        std::vector<std::string> v(entries.begin(), entries.end());
        return v;
    }

    void CompositeVirtualFileSystem::clear()
    {
        filesystems.clear();
    }

    void addHpisWithExtension(CompositeVirtualFileSystem& vfs, const fs::path& searchPath, const std::string& extension)
    {
        fs::directory_iterator it(searchPath);
        fs::directory_iterator end;

        for (; it != end; ++it)
        {
            const auto& e = *it;
            auto ext = e.path().extension().string();
            if (toUpper(ext) == toUpper(extension))
            {
                // An archive that will not open is one archive less, not a
                // launch that ends: its files are simply not there.
                try
                {
                    vfs.emplaceFileSystem<HpiFileSystem>(e.path().string());
                }
                catch (const std::exception& ex)
                {
                    LOG_WARN << "Skipping the archive " << e.path().string() << ": " << ex.what();
                }
            }
        }
    }

    void addToVfs(CompositeVirtualFileSystem& vfs, const std::filesystem::path& searchPath)
    {
        // Added in reverse, so a .gp3 is searched before a .hpi -- readFile
        // takes the first match. The launcher mirrors this list and this order
        // in launcher/src/common/archives.ts, to work out whether two players
        // are about to play with the same data; a change here belongs there
        // too.
        std::vector<std::string> hpiExtensions{".hpi", ".ufo", ".ccx", ".gpf", ".gp3"};
        vfs.emplaceFileSystem<DirectoryFileSystem>(searchPath);

        for (auto it = hpiExtensions.rbegin(); it != hpiExtensions.rend(); ++it)
        {
            addHpisWithExtension(vfs, searchPath, *it);
        }
    }

    CompositeVirtualFileSystem constructVfs(const std::filesystem::path& searchPath)
    {
        CompositeVirtualFileSystem vfs;
        addToVfs(vfs, searchPath);
        return vfs;
    }
}

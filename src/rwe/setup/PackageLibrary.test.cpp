#include <catch2/catch_test_macros.hpp>
#include <rwe/setup/PackageLibrary.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rwe
{
    TEST_CASE("package storage uses one user-owned layout and keeps mods at its existing path", "[setup][packages]")
    {
        const auto paths = packageLibraryPaths(fs::path("/home/user/.local/share/rwe"));
        CHECK(paths.root == fs::path("/home/user/.local/share/rwe/Library"));
        CHECK(paths.registry == paths.root / "registry.json");
        CHECK(paths.packages == paths.root / "packages");
        CHECK(paths.mods == fs::path("/home/user/.local/share/rwe/mods"));
        CHECK(paths.mapPacks == fs::path("/home/user/.local/share/rwe/MapPacks"));
        CHECK(paths.staging == paths.root / "staging");
    }

    namespace
    {
        struct PackageFixture
        {
            fs::path root = fs::temp_directory_path()
                / ("rwe-package-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "-" + std::to_string(std::random_device{}()));

            PackageFixture()
            {
                if (!fs::create_directory(root))
                {
                    throw std::runtime_error("Cannot create unique package test directory");
                }
            }

            ~PackageFixture()
            {
                std::error_code error;
                const auto parent = fs::weakly_canonical(root.parent_path(), error);
                if (!error && parent == fs::weakly_canonical(fs::temp_directory_path(), error)
                    && root.filename().string().compare(0, 17, "rwe-package-test-") == 0)
                {
                    fs::remove_all(root, error);
                }
            }

            fs::path write(const fs::path& name, const std::string& contents)
            {
                const auto path = root / name;
                fs::create_directories(path.parent_path());
                std::ofstream file(path, std::ios::binary);
                file << contents;
                file.close();
                return path;
            }

            std::string read(const fs::path& name)
            {
                std::ifstream file(root / name, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(file), {});
            }
        };

        json registryFixture()
        {
            const auto id = "p-" + std::string(32, 'a');
            return {{"schemaVersion", 1}, {"packages", json::array({{
                {"id", id}, {"name", "Example mod"}, {"kind", "mod"}, {"version", "1.0"},
                {"state", "installed"}, {"compatibility", "unverified"}, {"managed", true},
                {"source", "original-download.ufo"}, {"contentPath", "packages/" + id},
                {"dependencies", json::array()}, {"resourceDirectories", {{"units", "units"}}},
                {"files", json::array({{{"path", "content.ufo"}, {"size", 3}, {"sha256", std::string(64, 'a')}}})}
            }})}};
        }

        json packageEntry(const std::string& id, const std::string& name)
        {
            return {{"id", id}, {"name", name}, {"kind", "mod"}, {"version", nullptr},
                {"state", "installed"}, {"compatibility", "unverified"}, {"managed", true},
                {"source", "concurrent-test"}, {"contentPath", "packages/" + id},
                {"dependencies", json::array()}, {"resourceDirectories", json::object()}, {"files", json::array()}};
        }

        fs::path currentExecutable()
        {
#ifdef _WIN32
            std::wstring path(32768, L'\0');
            const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (size == 0 || size == path.size())
            {
                throw std::runtime_error("Cannot locate package registry test executable");
            }
            path.resize(size);
            return fs::path(path);
#else
#ifdef __APPLE__
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::vector<char> path(size);
            if (size == 0 || _NSGetExecutablePath(path.data(), &size) != 0)
            {
                throw std::runtime_error("Cannot locate package registry test executable");
            }
            return fs::canonical(path.data());
#else
            std::vector<char> path(4096);
            const auto size = ::readlink("/proc/self/exe", path.data(), path.size());
            if (size <= 0 || static_cast<std::size_t>(size) == path.size())
            {
                throw std::runtime_error("Cannot locate package registry test executable");
            }
            return fs::path(std::string(path.data(), static_cast<std::size_t>(size)));
#endif
#endif
        }

        struct RegistryWorkerProcess
        {
#ifdef _WIN32
            HANDLE process{nullptr};
#else
            pid_t process{-1};
#endif
        };

        RegistryWorkerProcess startRegistryWorker(const fs::path& executable, const fs::path& registry,
                                                  const fs::path& barrier, const std::string& id)
        {
#ifdef _WIN32
            struct EnvironmentNameLess
            {
                bool operator()(const std::wstring& left, const std::wstring& right) const
                {
                    return _wcsicmp(left.c_str(), right.c_str()) < 0;
                }
            };
            std::map<std::wstring, std::wstring, EnvironmentNameLess> environment;
            auto* block = GetEnvironmentStringsW();
            if (!block)
            {
                throw std::runtime_error("Cannot read process environment");
            }
            for (auto* item = block; *item != L'\0'; item += std::wcslen(item) + 1)
            {
                std::wstring entry(item);
                const auto separator = entry.find(L'=');
                if (separator != std::wstring::npos)
                {
                    const auto key = entry.substr(0, separator);
                    if (_wcsnicmp(key.c_str(), L"RWE_PACKAGE_TEST_", 17) != 0)
                    {
                        environment[key] = entry.substr(separator + 1);
                    }
                }
            }
            FreeEnvironmentStringsW(block);
            environment[L"RWE_PACKAGE_TEST_CHILD"] = L"1";
            environment[L"RWE_PACKAGE_TEST_REGISTRY"] = registry.native();
            environment[L"RWE_PACKAGE_TEST_BARRIER"] = barrier.native();
            environment[L"RWE_PACKAGE_TEST_ID"] = std::wstring(id.begin(), id.end());

            std::vector<wchar_t> environmentBlock;
            for (const auto& [key, value] : environment)
            {
                const auto entry = key + L"=" + value;
                environmentBlock.insert(environmentBlock.end(), entry.begin(), entry.end());
                environmentBlock.push_back(L'\0');
            }
            environmentBlock.push_back(L'\0');

            auto command = L"\"" + executable.native() + L"\" \"[registry-worker]\"";
            std::vector<wchar_t> commandLine(command.begin(), command.end());
            commandLine.push_back(L'\0');
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                    CREATE_UNICODE_ENVIRONMENT, environmentBlock.data(), nullptr, &startup, &process))
            {
                throw std::runtime_error("Cannot start package registry worker (Windows error "
                    + std::to_string(GetLastError()) + ")");
            }
            CloseHandle(process.hThread);
            return {process.hProcess};
#else
            const auto child = ::fork();
            if (child < 0)
            {
                throw std::runtime_error("Cannot fork package registry worker");
            }
            if (child == 0)
            {
                ::setenv("RWE_PACKAGE_TEST_CHILD", "1", 1);
                ::setenv("RWE_PACKAGE_TEST_REGISTRY", registry.c_str(), 1);
                ::setenv("RWE_PACKAGE_TEST_BARRIER", barrier.c_str(), 1);
                ::setenv("RWE_PACKAGE_TEST_ID", id.c_str(), 1);
                ::execl(executable.c_str(), executable.c_str(), "[registry-worker]", static_cast<char*>(nullptr));
                ::_exit(127);
            }
            return {child};
#endif
        }

        int waitRegistryWorker(RegistryWorkerProcess worker)
        {
#ifdef _WIN32
            WaitForSingleObject(worker.process, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(worker.process, &exitCode);
            CloseHandle(worker.process);
            return static_cast<int>(exitCode);
#else
            int status = 0;
            if (::waitpid(worker.process, &status, 0) != worker.process)
            {
                throw std::runtime_error("Cannot wait for package registry worker");
            }
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
        }

        std::string workerVariable(const char* name)
        {
#ifdef _WIN32
            const auto* value = _wgetenv(std::wstring(name, name + std::strlen(name)).c_str());
            if (!value)
            {
                return {};
            }
            const std::wstring wide(value);
            return std::string(wide.begin(), wide.end());
#else
            const auto* value = std::getenv(name);
            return value ? value : "";
#endif
        }

        fs::path workerPathVariable(const char* name)
        {
#ifdef _WIN32
            const auto* value = _wgetenv(std::wstring(name, name + std::strlen(name)).c_str());
            return value ? fs::path(value) : fs::path{};
#else
            const auto* value = std::getenv(name);
            return value ? fs::path(value) : fs::path{};
#endif
        }
    }

    TEST_CASE("package registry reads are non-destructive and updates replace an existing file", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        CHECK(loadPackageRegistry(filename) == emptyPackageRegistry());
        CHECK_FALSE(fs::exists(filename.parent_path()));
        const auto legacyFile = fixture.write("Legacy/registry.json", registryFixture().dump());
        CHECK(loadPackageRegistry(legacyFile)["generation"] == 0);

        auto registry = registryFixture();
        registry["annotation"] = "preserve unknown metadata";
        savePackageRegistry(filename, registry);
        registry = loadPackageRegistry(filename);
        CHECK(registry["generation"] == 1);
        registry["packages"][0]["name"] = "Renamed mod";
        savePackageRegistry(filename, registry);
        registry = loadPackageRegistry(filename);
        CHECK(registry["generation"] == 2);
        CHECK(std::distance(fs::directory_iterator(filename.parent_path()), fs::directory_iterator{}) == 2);

        const auto saved = fixture.read("Library/registry.json");
        auto invalid = registry;
        invalid["schemaVersion"] = 2;
        CHECK_THROWS(savePackageRegistry(filename, invalid));
        CHECK(fixture.read("Library/registry.json") == saved);
        fixture.write("Library/registry.json", "{broken");
        CHECK_THROWS(loadPackageRegistry(filename));
        CHECK(fixture.read("Library/registry.json") == "{broken");
    }

    TEST_CASE("package registry rejects stale snapshots and releases its lock after failure", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        savePackageRegistry(filename, registryFixture());
        const auto staleSnapshot = loadPackageRegistry(filename);
        auto committed = updatePackageRegistry(filename, [](json& registry) {
            registry["packages"].push_back(packageEntry("p-" + std::string(32, 'b'), "Second mod"));
        });
        REQUIRE(committed["generation"] == 2);
        CHECK(committed["packages"].size() == 2);
        CHECK_THROWS(savePackageRegistry(filename, staleSnapshot));
        const auto afterStaleSave = loadPackageRegistry(filename);
        CHECK(afterStaleSave["generation"] == 2);
        REQUIRE(afterStaleSave["packages"].size() == 2);

        const auto beforeFailure = fixture.read("Library/registry.json");
        CHECK_THROWS(updatePackageRegistry(filename, [](json& registry) {
            registry["packages"][0]["kind"] = "invalid";
        }));
        CHECK(fixture.read("Library/registry.json") == beforeFailure);
        CHECK(updatePackageRegistry(filename, [](json&) {}).at("generation") == 3);
    }

    TEST_CASE("package registry subprocess worker", "[registry-worker]")
    {
        if (workerVariable("RWE_PACKAGE_TEST_CHILD") != "1")
        {
            return;
        }
        const auto filename = workerPathVariable("RWE_PACKAGE_TEST_REGISTRY");
        const auto barrier = workerPathVariable("RWE_PACKAGE_TEST_BARRIER");
        const auto id = workerVariable("RWE_PACKAGE_TEST_ID");
        REQUIRE_FALSE(filename.empty());
        REQUIRE_FALSE(barrier.empty());
        REQUIRE_FALSE(id.empty());
        auto ready = barrier;
        ready += "." + (id == "crash" ? id : id.substr(2, 1)) + ".ready";
        std::ofstream(ready) << "ready";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!fs::exists(barrier) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(fs::exists(barrier));
        if (id == "crash")
        {
            updatePackageRegistry(filename, [](json&) { std::_Exit(23); });
            FAIL("crash worker unexpectedly returned from update");
        }
        updatePackageRegistry(filename, [&](json& registry) {
            registry["packages"].push_back(packageEntry(id, "Concurrent " + id.substr(2, 1)));
        });
    }

    TEST_CASE("concurrent package registry transactions from separate processes keep both records", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        const auto barrier = fixture.root / "start-workers";
        const auto executable = currentExecutable();
        const auto workerA = startRegistryWorker(executable, filename, barrier, "p-" + std::string(32, 'a'));
        const auto workerB = startRegistryWorker(executable, filename, barrier, "p-" + std::string(32, 'b'));
        auto readyA = barrier;
        readyA += ".a.ready";
        auto readyB = barrier;
        readyB += ".b.ready";
        const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while ((!fs::exists(readyA) || !fs::exists(readyB)) && std::chrono::steady_clock::now() < readyDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::ofstream(barrier) << "start";
        const auto exitA = waitRegistryWorker(workerA);
        const auto exitB = waitRegistryWorker(workerB);
        CHECK(fs::exists(readyA));
        CHECK(fs::exists(readyB));
        REQUIRE(exitA == 0);
        REQUIRE(exitB == 0);
        const auto registry = loadPackageRegistry(filename);
        REQUIRE(registry["generation"] == 2);
        REQUIRE(registry["packages"].size() == 2);
        CHECK(registry["packages"][0]["id"] != registry["packages"][1]["id"]);
    }

    TEST_CASE("package registry readers see complete files while transactions replace them", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        savePackageRegistry(filename, registryFixture());
        std::atomic<bool> reading{true};
        std::atomic<bool> valid{true};
        std::string readerFailure;
        std::thread reader([&] {
            while (reading.load())
            {
                try
                {
                    const auto snapshot = loadPackageRegistry(filename);
                    validatePackageRegistry(snapshot);
                }
                catch (const std::exception& error)
                {
                    readerFailure = error.what();
                    valid = false;
                }
                catch (...)
                {
                    readerFailure = "unknown reader failure";
                    valid = false;
                }
            }
        });
        try
        {
            for (int i = 0; i < 30; ++i)
            {
                updatePackageRegistry(filename, [i](json& registry) {
                    registry["readerProbe"] = std::string(4096, static_cast<char>('a' + (i % 26)));
                });
            }
        }
        catch (...)
        {
            reading = false;
            reader.join();
            throw;
        }
        reading = false;
        reader.join();
        INFO(readerFailure);
        CHECK(valid.load());
    }

#ifdef _WIN32
    TEST_CASE("package registry replacement preserves an open Windows reader", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        savePackageRegistry(filename, registryFixture());

        HANDLE reader = CreateFileW(filename.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(reader != INVALID_HANDLE_VALUE);
        try
        {
            const auto updated = updatePackageRegistry(filename, [](json& registry) {
                registry["annotation"] = "new registry snapshot";
            });
            REQUIRE(updated["generation"] == 2);
            CHECK(updated["annotation"] == "new registry snapshot");

            LARGE_INTEGER start{};
            REQUIRE(SetFilePointerEx(reader, start, nullptr, FILE_BEGIN));
            LARGE_INTEGER size{};
            REQUIRE(GetFileSizeEx(reader, &size));
            REQUIRE(size.QuadPart >= 0);
            std::string oldText(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD bytesRead = 0;
            REQUIRE(ReadFile(reader, oldText.data(), static_cast<DWORD>(oldText.size()), &bytesRead, nullptr));
            REQUIRE(bytesRead == oldText.size());
            const auto oldSnapshot = json::parse(oldText);
            CHECK(oldSnapshot["generation"] == 1);
            CHECK_FALSE(oldSnapshot.contains("annotation"));
            CHECK(loadPackageRegistry(filename)["annotation"] == "new registry snapshot");
        }
        catch (...)
        {
            CloseHandle(reader);
            throw;
        }
        CloseHandle(reader);
    }
#endif

    TEST_CASE("a crashed process releases the package registry lock", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        const auto barrier = fixture.root / "start-crash-worker";
        const auto executable = currentExecutable();
        std::ofstream(barrier) << "start";
        const auto worker = startRegistryWorker(executable, filename, barrier, "crash");
        REQUIRE(waitRegistryWorker(worker) == 23);
        const auto registry = updatePackageRegistry(filename, [](json& current) {
            current["annotation"] = "lock was released by process exit";
        });
        CHECK(registry["generation"] == 1);
        CHECK(registry["annotation"] == "lock was released by process exit");
    }

    TEST_CASE("package registry validates identity and file ownership independently of labels", "[setup][packages]")
    {
        auto registry = registryFixture();
        SECTION("two versions can share a display name while their IDs differ")
        {
            auto other = registry["packages"][0];
            other["id"] = "p-" + std::string(32, 'b');
            other["contentPath"] = "packages/p-" + std::string(32, 'b');
            registry["packages"].push_back(other);
            CHECK_NOTHROW(validatePackageRegistry(registry));
        }
        SECTION("duplicate IDs cannot hide behind different labels")
        {
            registry["packages"].push_back(registry["packages"][0]);
            registry["packages"][1]["name"] = "Different label";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("external installations remain explicitly unmanaged")
        {
            registry["packages"][0]["managed"] = false;
            registry["packages"][0].erase("contentPath");
            registry["packages"][0]["externalPath"] = fs::temp_directory_path().string();
            CHECK_NOTHROW(validatePackageRegistry(registry));
        }
        SECTION("managed destinations must belong to their own IDs")
        {
            registry["packages"][0]["contentPath"] = "../Original TA";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("file manifests reject traversal and Windows aliases")
        {
            for (const auto* path : {"../source.ufo", "/source.ufo", "C:/source.ufo", "a\\b", "a//b", "a/./b", "a/../b"})
            {
                registry["packages"][0]["files"][0]["path"] = path;
                CHECK_THROWS(validatePackageRegistry(registry));
            }
        }
        SECTION("case aliases cannot identify two owned files")
        {
            auto file = registry["packages"][0]["files"][0];
            file["path"] = "CONTENT.UFO";
            registry["packages"][0]["files"].push_back(file);
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("compatibility cannot be asserted by an arbitrary string")
        {
            registry["packages"][0]["compatibility"] = "all mods supported";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("self-dependencies are invalid")
        {
            registry["packages"][0]["dependencies"].push_back(registry["packages"][0]["id"]);
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("registry generations must be nonnegative integers")
        {
            registry["generation"] = -1;
            CHECK_THROWS(validatePackageRegistry(registry));
            registry["generation"] = 1.5;
            CHECK_THROWS(validatePackageRegistry(registry));
            registry["generation"] = 7;
            CHECK_NOTHROW(validatePackageRegistry(registry));
        }
    }

    TEST_CASE("package inspection discovers nested resource roots without changing source files", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto data = fs::path("Delivery") / "Actual data with spaces";
        fixture.write(data / "units" / "ARMFOO.fbi", "[UNITINFO]{UnitName=ARMFOO;Objectname=ARMFOO;}");
        fixture.write(data / "Music" / "song.ogg", "companion asset");
        const auto metadata = fixture.write(data / "rwe_mod.json", R"({"shortname":"test","name":"The real mod name","version":"2.1"})");
        fixture.write("Delivery/README.txt", "How to install the package.");
        const auto original = fixture.read(data / "rwe_mod.json");
        const auto modified = fs::last_write_time(metadata);
        const auto report = inspectContentPackage(fixture.root / "Delivery");
        REQUIRE(report["candidates"].size() == 1);
        const auto candidate = report["candidates"][0];
        CHECK(candidate["kind"] == "mod");
        CHECK(candidate["name"] == "The real mod name");
        CHECK(candidate["version"] == "2.1");
        CHECK(candidate["recognition"] == "rwe_mod.json");
        CHECK(candidate["compatibility"] == "unverified");
        CHECK(candidate["fileCount"] == 3);
        CHECK(report["complete"] == true);
        CHECK(fixture.read(data / "rwe_mod.json") == original);
        CHECK(fs::last_write_time(metadata) == modified);
        CHECK_FALSE(fs::exists(fixture.root / "registry.json"));
    }

    TEST_CASE("loose map selection finds its case-insensitive partner without importing sibling maps", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto ota = fixture.write("Blue Planet.OTA", "[GlobalHeader]{missionname=Blue Planet;SCHEMACOUNT=0;}");
        fixture.write("blue planet.tnt", "terrain validation is a separate task");
        fixture.write("Other.ota", "other map");
        auto report = inspectContentPackage(ota);
        REQUIRE(report["candidates"].size() == 1);
        const auto candidate = report["candidates"][0];
        CHECK(candidate["kind"] == "maps");
        REQUIRE(candidate["maps"].size() == 1);
        CHECK(candidate["maps"][0]["paired"] == true);
        CHECK(candidate["maps"][0]["validation"] == "not-run");
        CHECK(candidate["compatibility"] == "unverified");
        report = inspectContentPackage(fixture.root / "Other.ota");
        CHECK(report["candidates"][0]["maps"][0]["paired"] == false);
    }

    TEST_CASE("package inspection reports deferred extraction and damaged input explicitly", "[setup][packages]")
    {
        PackageFixture fixture;
        SECTION("ZIP RAR and 7z wait for the bundled extraction task")
        {
            for (const auto* name : {"maps.zip", "mod.RAR", "pack.7z"})
            {
                const auto file = fixture.write(name, "not extracted by the inspection foundation");
                const auto report = inspectContentPackage(file);
                CHECK(report["complete"] == false);
                CHECK(report["candidates"].empty());
                CHECK(report["containers"][0]["state"] == "extraction-required");
            }
        }
        SECTION("bad TA archives do not become successful inspections")
        {
            const auto file = fixture.write("broken.ufo", "broken");
            const auto report = inspectContentPackage(file);
            CHECK(report["complete"] == false);
            CHECK_FALSE(report["candidates"][0]["warnings"].empty());
            CHECK(fixture.read("broken.ufo") == "broken");
        }
        SECTION("bad display metadata is reported")
        {
            fixture.write("rwe_mod.json", "{broken");
            const auto report = inspectContentPackage(fixture.root);
            CHECK(report["complete"] == false);
        }
        SECTION("missing input throws instead of installing anything")
        {
            CHECK_THROWS(inspectContentPackage(fixture.root / "missing"));
        }
    }

    TEST_CASE("loose original AI profiles retain their own package category", "[setup][packages]")
    {
        PackageFixture fixture;
        fixture.write("Default.txt", "plan hard\nweight ARM .1\nlimit ARMSOLAR 12\n");
        const auto report = inspectContentPackage(fixture.root);
        REQUIRE(report["candidates"].size() == 1);
        CHECK(report["candidates"][0]["kind"] == "ai");
        CHECK(report["candidates"][0]["compatibility"] == "unverified");
    }
}

# Lua 5.4.7, vendored from https://www.lua.org/ftp/lua-5.4.7.tar.gz.
# Pinned for the modding SDK's authoring runtime. Its MIT licence is in
# README. Only the interpreter and standard libraries are built here; the
# standalone `lua` and `luac` programs are not part of RWE.
add_library(rwe_lua STATIC
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldebug.c
    ldblib.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c)

target_include_directories(rwe_lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Compiled as C++, not C (MOD-SDK-023). Lua then raises errors as C++
# exceptions (ldo.c's LUAI_THROW), so an error raised from an RWE host
# function -- which is C++ -- unwinds its frames properly. Built as C, the
# same error is a longjmp across C++ frames, which is undefined behaviour and
# killed MSVC Release test runs. No Lua source file changes for this, and
# every RWE file includes the Lua headers without extern "C".
get_target_property(RWE_LUA_SOURCES rwe_lua SOURCES)
set_source_files_properties(${RWE_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
set_target_properties(rwe_lua PROPERTIES LINKER_LANGUAGE CXX)

if(MSVC)
    # Lua is C and predates MSVC's stricter CRT deprecation warnings.
    target_compile_definitions(rwe_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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
set_target_properties(rwe_lua PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

if(MSVC)
    # Lua is C and predates MSVC's stricter CRT deprecation warnings.
    target_compile_definitions(rwe_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

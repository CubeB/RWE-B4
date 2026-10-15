# Lockstep simulation requires separate, consistently rounded operations.
# Reject unsafe flags even when a later option would override them. Generator
# expressions are inspected conservatively, including inactive configurations:
# an unsafe configuration must not quietly become available to another peer.
function(rwe_check_floating_point_flags origin flags)
    if("${flags}" MATCHES "(^|[ \t\r\n;:>,\"])(-ffast-math|-Ofast|[-/]fp:(fast|contract))($|[ \t\r\n;>,\"])")
        message(FATAL_ERROR
            "Unsafe floating-point flag '${CMAKE_MATCH_2}' reaches librwe through ${origin}. "
            "Remove it: lockstep simulation requires /fp:precise on MSVC and "
            "-ffp-contract=off on GCC/Clang.")
    endif()
endfunction()

function(rwe_require_deterministic_floating_point target)
    # Includes toolchain/cache flags and every configuration, also custom ones.
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
        if(variable MATCHES "^CMAKE_CXX_FLAGS($|_)" AND NOT variable MATCHES "_INIT$")
            rwe_check_floating_point_flags("${variable}" "${${variable}}")
        endif()
    endforeach()
    rwe_check_floating_point_flags("CMAKE_CXX_COMPILER_ARG1" "${CMAKE_CXX_COMPILER_ARG1}")
    if(MSVC)
        rwe_check_floating_point_flags("environment CL" "$ENV{CL}")
        rwe_check_floating_point_flags("environment _CL_" "$ENV{_CL_}")
    endif()

    foreach(property COMPILE_OPTIONS COMPILE_FLAGS)
        get_target_property(flags "${target}" "${property}")
        rwe_check_floating_point_flags("${target}.${property}" "${flags}")
    endforeach()
    get_target_property(sources "${target}" SOURCES)
    foreach(source IN LISTS sources)
        foreach(property COMPILE_OPTIONS COMPILE_FLAGS)
            get_source_file_property(flags "${source}" "${property}")
            rwe_check_floating_point_flags("${source}.${property}" "${flags}")
        endforeach()
    endforeach()

    # Only dependency INTERFACE options propagate to librwe. A dependency's
    # private compiler options do not affect the simulation's compilation.
    set(pending "${target}")
    set(visited "")
    while(pending)
        list(POP_FRONT pending dependency)
        if(dependency IN_LIST visited OR NOT TARGET "${dependency}")
            continue()
        endif()
        list(APPEND visited "${dependency}")
        get_target_property(flags "${dependency}" INTERFACE_COMPILE_OPTIONS)
        rwe_check_floating_point_flags("${dependency}.INTERFACE_COMPILE_OPTIONS" "${flags}")
        if(dependency STREQUAL target)
            get_target_property(links "${dependency}" LINK_LIBRARIES)
        else()
            get_target_property(links "${dependency}" INTERFACE_LINK_LIBRARIES)
        endif()
        # Extract target names from plain links and conditional generator
        # expressions, retaining namespace separators (SDL3::SDL3-static).
        string(REGEX MATCHALL "[A-Za-z0-9_.+:-]+" candidates "${links}")
        foreach(candidate IN LISTS candidates)
            # A generator expression's final ':' can precede a target name.
            string(REGEX REPLACE "^.*:([^:]+::.*)$" "\\1" candidate "${candidate}")
            if(TARGET "${candidate}")
                list(APPEND pending "${candidate}")
            else()
                string(REGEX REPLACE "^.*:" "" candidate "${candidate}")
                if(TARGET "${candidate}")
                    list(APPEND pending "${candidate}")
                endif()
            endif()
        endforeach()
    endwhile()
endfunction()

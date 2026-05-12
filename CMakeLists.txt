cmake_minimum_required(VERSION 3.15)
project(ezdib C)

enable_testing()

option(BUILD_STATIC "Link statically" OFF)

set(EZD_MATH_MODE "CORDIC" CACHE STRING
    "Arc math implementation: CORDIC (built-in, no math.h, default), SYSTEM (math.h), or NONE (arcs disabled)")
set_property(CACHE EZD_MATH_MODE PROPERTY STRINGS SYSTEM CORDIC NONE)

add_executable(test_ezdib
    ezdib.c
    main.c
)

target_compile_options(test_ezdib PRIVATE
    $<$<CONFIG:Debug>:-g -DDEBUG -D_DEBUG>
    $<$<CONFIG:Release>:-O2>
)

if(BUILD_STATIC)
    target_link_options(test_ezdib PRIVATE -static)
endif()

if(EZD_MATH_MODE STREQUAL "CORDIC")
    target_compile_definitions(test_ezdib PRIVATE EZD_MATH_CORDIC)
elseif(EZD_MATH_MODE STREQUAL "NONE")
    target_compile_definitions(test_ezdib PRIVATE EZD_MATH_NONE)
else()
    # SYSTEM mode: ezdib.c includes math.h so the binary needs libm
    target_compile_definitions(test_ezdib PRIVATE EZD_MATH_SYSTEM)
    target_link_libraries(test_ezdib PRIVATE m)
endif()

set_target_properties(test_ezdib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(tests)

set(PROFILE_SRC
    "internal_cmd.h"
    "internal_funclevel.h"
    "internal_highlevel.h"
    "internal_result.h"
    "internal.h"
    "profile_cmd.cpp"
    "profile_doc.h"
    "profile_funclevel.cpp"
    "profile_funclevel.h"
    "profile_highlevel.cpp"
    "profile_highlevel.h"
    "profile_result.cpp"
    "profile_result.h"
    "profile.cpp"
    "profile.h"
)

add_library(core_profile STATIC)

target_sources(core_profile PRIVATE ${PROFILE_SRC})

target_include_directories(core_profile INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(PROFILE_PCH_HEADERS
    [["Utility/CppMacros.h"]] # Must be first, to be removed when abandoning VC6
    "profile.h"
    "internal.h"
)
if(WIN32)
    list(APPEND PROFILE_PCH_HEADERS <windows.h>)
endif()
target_precompile_headers(core_profile PRIVATE ${PROFILE_PCH_HEADERS})

target_link_libraries(core_profile PRIVATE
    core_utility
    core_wwcommon
    corei_always
)

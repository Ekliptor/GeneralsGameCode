# Only fetch the Bink stub when Bink is the selected video backend.
# See cmake/config-build.cmake RTS_VIDEO.
if(NOT RTS_VIDEO_LOWER STREQUAL "bink")
    return()
endif()

FetchContent_Declare(
    bink
    GIT_REPOSITORY https://github.com/TheSuperHackers/bink-sdk-stub.git
    GIT_TAG        180fc4620ed72fd700347ab837a5271fd0259901
)

FetchContent_MakeAvailable(bink)

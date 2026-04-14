# Only fetch the Miles stub when Miles is the selected audio backend.
# See cmake/config-build.cmake RTS_AUDIO.
if(NOT RTS_AUDIO_LOWER STREQUAL "miles")
    return()
endif()

FetchContent_Declare(
    miles
    GIT_REPOSITORY https://github.com/TheSuperHackers/miles-sdk-stub.git
    GIT_TAG        6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264
)

FetchContent_MakeAvailable(miles)

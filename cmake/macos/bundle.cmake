# Phase 6f — macOS .app bundle helper.
#
# Invoke: rts_configure_macos_bundle(<target> <display_name> <bundle_id>)
#
# Turns <target> into a `MACOSX_BUNDLE` and populates the minimum Info.plist
# keys SDL3 + Metal need (NSHighResolutionCapable, LSMinimumSystemVersion, etc.).
# Icon, code signing, and notarization are deferred.
#
# See docs/Phase6f-MacOSAppBundle.md.
function(rts_configure_macos_bundle target display_name bundle_id)
    if(NOT APPLE)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "${display_name}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.04"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.04"
        MACOSX_BUNDLE_COPYRIGHT "EA Pacific / community port"
    )
endfunction()

set(LIB_NAME wpAudio)

add_library(${LIB_NAME}
    STATIC
    SoundManager.cpp
    AudioCapture.cpp
)

target_link_libraries(${LIB_NAME}
PUBLIC
    wpUtils
PRIVATE
    wpFs
)

# System-audio capture for audio-responsive scenes (optional; silent without it)
option(WP_AUDIO_CAPTURE "Capture system audio through PipeWire for audio-responsive wallpapers" ON)
if(WP_AUDIO_CAPTURE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    endif()
    if(PIPEWIRE_FOUND)
        target_link_libraries(${LIB_NAME} PRIVATE PkgConfig::PIPEWIRE)
        target_compile_definitions(${LIB_NAME} PRIVATE WP_HAVE_PIPEWIRE=1)
        message(STATUS "Audio capture: PipeWire ${PIPEWIRE_VERSION}")
    else()
        message(STATUS "Audio capture: libpipewire-0.3 not found, audio-responsive scenes stay silent")
    endif()
endif()
target_include_directories(${LIB_NAME} PUBLIC include PRIVATE include/Audio)
target_compile_options(${LIB_NAME} PRIVATE ${warn_opts})
set_property(TARGET ${LIB_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

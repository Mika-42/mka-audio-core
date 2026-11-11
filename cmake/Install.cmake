include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(mka_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/mkaAudioCore")
set(MKA_INSTALLED_BACKENDS)
set_target_properties(mka-core-audio PROPERTIES EXPORT_NAME core)
add_library(mka::core ALIAS mka-core-audio)
install(TARGETS mka-core-audio EXPORT mkaAudioCoreTargets-core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mka-audio-core)
install(EXPORT mkaAudioCoreTargets-core NAMESPACE mka:: DESTINATION ${mka_package_dir}
    CXX_MODULES_DIRECTORY cxx-modules-core)
foreach(backend alsa jack pipewire pulseaudio wasapi coreaudio)
    if(NOT TARGET mka-audio-${backend})
        continue()
    endif()
    list(APPEND MKA_INSTALLED_BACKENDS ${backend})
    set_target_properties(mka-audio-${backend} PROPERTIES EXPORT_NAME ${backend})
    add_library(mka::${backend} ALIAS mka-audio-${backend})
    install(TARGETS mka-audio-${backend} EXPORT mkaAudioCoreTargets-${backend}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mka-audio-core)
    install(EXPORT mkaAudioCoreTargets-${backend} NAMESPACE mka:: DESTINATION ${mka_package_dir}
        CXX_MODULES_DIRECTORY cxx-modules-${backend})
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/mka/audio/build_config.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mka/audio)
# Private global-module-fragment headers are needed when consumers rebuild BMIs.
install(DIRECTORY src/backend/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mka-audio-core/backend
    FILES_MATCHING PATTERN "*.hpp")
configure_package_config_file(cmake/mkaAudioCoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mkaAudioCoreConfig.cmake" INSTALL_DESTINATION ${mka_package_dir})
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/mkaAudioCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mkaAudioCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mkaAudioCoreConfigVersion.cmake" DESTINATION ${mka_package_dir})

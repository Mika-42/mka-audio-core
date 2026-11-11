cmake_minimum_required(VERSION 4.0)

project(mka-core-audio VERSION 0.1.0
        DESCRIPTION "Backend-agnostic real-time audio framework"
        LANGUAGES CXX
)

# ============================================================
# Project options
# ============================================================

option(MKA_BUILD_TESTS
        "Build unit and integration tests"
        ON
)

option(MKA_BUILD_ALSA
        "Build the ALSA backend"
        ${LINUX}
)

option(MKA_BUILD_PIPEWIRE
        "Build the PipeWire backend"
        ${LINUX}
)

option(MKA_BUILD_JACK
        "Build the JACK backend"
        ${LINUX}
)

option(MKA_BUILD_PULSEAUDIO
        "Build the PulseAudio backend"
        ${LINUX}
)

option(MKA_BUILD_WASAPI "Build the Windows WASAPI backend" ${WIN32})
option(MKA_BUILD_ASIO "Build the optional Windows ASIO backend" OFF)
option(MKA_BUILD_DIRECTSOUND "Build the optional Windows DirectSound backend" OFF)
option(MKA_BUILD_ASIO_SDK_TESTS "Validate a separately supplied ASIO SDK on Windows" OFF)
set(MKA_ASIO_SDK_DIR "" CACHE PATH "Audited ASIOSDK directory; see docs/asio-sdk.md")
if(MKA_BUILD_WASAPI AND NOT WIN32)
    message(FATAL_ERROR "MKA_BUILD_WASAPI requires a Windows target and Windows SDK")
endif()
if(MKA_BUILD_ASIO AND NOT WIN32)
    message(FATAL_ERROR "MKA_BUILD_ASIO requires a Windows target")
endif()
if(MKA_BUILD_DIRECTSOUND AND NOT WIN32)
    message(FATAL_ERROR "MKA_BUILD_DIRECTSOUND requires a Windows target and Windows SDK")
endif()
if(MKA_BUILD_ASIO_SDK_TESTS AND (NOT WIN32 OR NOT MKA_BUILD_TESTS))
    message(FATAL_ERROR "MKA_BUILD_ASIO_SDK_TESTS requires Windows and MKA_BUILD_TESTS=ON")
endif()
if(MKA_BUILD_ASIO OR MKA_BUILD_ASIO_SDK_TESTS)
    include(cmake/AsioSdk.cmake)
    mka_validate_asio_sdk("${MKA_ASIO_SDK_DIR}" mka_asio_sdk_root)
endif()


# ============================================================
# Global C++ configuration
# ============================================================

# CMake currently maps MSVC's /std:c++latest to its cxx_std_23 feature.
# Keep that actual draft language mode instead of claiming an unknown CMake feature.
set(MKA_CXX_STANDARD 26)
if(MSVC)
    if(MSVC_VERSION LESS 1944)
        message(FATAL_ERROR "Windows builds require Visual Studio 2022 17.14+ (/std:c++latest)")
    endif()
    set(MKA_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD ${MKA_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Instrumentation.cmake)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /EHsc)
    # GoogleTest must use the same dynamic CRT as the application and modules.
    set(gtest_force_shared_crt ON CACHE BOOL "Match the project MSVC runtime" FORCE)
endif()

if(MKA_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# ============================================================
# Core library
# ============================================================

add_library(mka-core-audio)

target_sources(mka-core-audio
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS
        src
        FILES
        src/core/backend.cppm
        src/core/process.cppm
        src/core/endpoint.cppm
        src/core/constants.cppm
        src/core/error.cppm
        src/core/convert.cppm
        src/core/plateform.cppm
)

target_compile_features(
        mka-core-audio
        PUBLIC
        cxx_std_${MKA_CXX_STANDARD}
)

# ============================================================
# ALSA backend
# ============================================================

if (MKA_BUILD_ALSA)

    find_package(ALSA REQUIRED)

    add_library(mka-audio-alsa)

    target_sources(mka-audio-alsa
            PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS
            src
            FILES
            src/backend/linux/alsa.cppm
    )

    target_link_libraries(mka-audio-alsa
            PRIVATE
            mka-core-audio
            ALSA::ALSA
    )

    target_compile_features(
            mka-audio-alsa
            PRIVATE
            cxx_std_${MKA_CXX_STANDARD}
    )

endif ()

# ============================================================
# PipeWire backend
# ============================================================

if (MKA_BUILD_PIPEWIRE)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

    add_library(mka-audio-pipewire)

    target_sources(mka-audio-pipewire
            PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS
            src
            FILES
            src/backend/linux/pipewire.cppm
    )

    target_link_libraries(mka-audio-pipewire
            PRIVATE
            mka-core-audio
            PkgConfig::PIPEWIRE
    )

    target_compile_features(
            mka-audio-pipewire
            PRIVATE
            cxx_std_${MKA_CXX_STANDARD}
    )

endif ()

# ============================================================
# JACK backend
# ============================================================

if (MKA_BUILD_JACK)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)

    add_library(mka-audio-jack)

    target_sources(mka-audio-jack
            PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS
            src
            FILES
            src/backend/linux/jack.cppm
    )

    target_link_libraries(mka-audio-jack
            PRIVATE
            mka-core-audio
            PkgConfig::JACK
    )

    target_compile_features(
            mka-audio-jack
            PRIVATE
            cxx_std_${MKA_CXX_STANDARD}
    )

endif ()

# ============================================================
# PulseAudio backend
# ============================================================

if (MKA_BUILD_PULSEAUDIO)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSEAUDIO REQUIRED IMPORTED_TARGET libpulse)

    add_library(mka-audio-pulseaudio)

    target_sources(mka-audio-pulseaudio
            PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS
            src
            FILES
            src/backend/linux/pulseaudio.cppm
    )

    target_link_libraries(mka-audio-pulseaudio
            PRIVATE
            mka-core-audio
            PkgConfig::PULSEAUDIO
    )

    target_compile_features(
            mka-audio-pulseaudio
            PRIVATE
            cxx_std_${MKA_CXX_STANDARD}
    )

endif ()

# ============================================================
# Tests
# ============================================================

if (MKA_BUILD_TESTS)

    enable_testing()

    # --------------------------------------------------------
    # Core tests
    # --------------------------------------------------------
    add_executable(mka-core-audio-tests
            test/test_abstract_backend.cpp
            test/test_backend_userdata.cpp
            test/test_backend_events.cpp
            test/test_backend_robustness.cpp
            test/test_lifecycle_stress.cpp
            test/test_hook_rollback.cpp
            test/test_sprint02.cpp
            test/test_convert.cpp
            test/test_convert_extremes.cpp
            test/test_endpoint_contract.cpp
            test/test_error_contract.cpp
            test/test_manual_audio.cpp
    )

    target_link_libraries(mka-core-audio-tests
            PRIVATE
            mka-core-audio
            GTest::gtest_main
    )

    target_compile_features(
            mka-core-audio-tests
            PRIVATE
            cxx_std_${MKA_CXX_STANDARD}
    )

    include(GoogleTest)

    gtest_discover_tests(
            mka-core-audio-tests
            TEST_PREFIX "core."
    )
    # --------------------------------------------------------
    # ALSA tests
    # --------------------------------------------------------

    if (MKA_BUILD_ALSA)

        add_executable(mka-audio-alsa-tests test/linux/alsa/test_alsa.cpp)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-alsa-tests PRIVATE test/linux/alsa/test_rt_alsa.cpp test/utils/alloc_probe.cpp)
        endif()

        target_link_libraries(mka-audio-alsa-tests
                PRIVATE
                mka-audio-alsa
                ALSA::ALSA
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-alsa-tests
                PRIVATE
                cxx_std_${MKA_CXX_STANDARD}
        )

        gtest_discover_tests(
                mka-audio-alsa-tests
                TEST_PREFIX "alsa."
        )

    endif ()

    # --------------------------------------------------------
    # PipeWire tests
    # --------------------------------------------------------

    if (MKA_BUILD_PIPEWIRE)

        add_executable(mka-audio-pipewire-tests test/linux/pipewire/test_pipewire.cpp)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-pipewire-tests PRIVATE test/linux/pipewire/test_rt_pipewire.cpp test/utils/alloc_probe.cpp)
        endif()

        target_link_libraries(mka-audio-pipewire-tests
                PRIVATE
                mka-audio-pipewire
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-pipewire-tests
                PRIVATE
                cxx_std_${MKA_CXX_STANDARD}
        )

        gtest_discover_tests(
                mka-audio-pipewire-tests
                TEST_PREFIX "pipewire."
        )

    endif ()

    if (MKA_BUILD_PIPEWIRE)
        add_executable(mka-audio-pipewire-buffer-tests test/linux/pipewire/test_buffer_contract.cpp)
        target_link_libraries(mka-audio-pipewire-buffer-tests PRIVATE GTest::gtest_main PkgConfig::PIPEWIRE)
        gtest_discover_tests(mka-audio-pipewire-buffer-tests TEST_PREFIX "pipewire.buffer.")
    endif ()

    # --------------------------------------------------------
    # JACK tests
    # --------------------------------------------------------

    if (MKA_BUILD_JACK)

        add_executable(mka-audio-jack-tests test/linux/jack/test_jack.cpp)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-jack-tests PRIVATE test/linux/jack/test_rt_jack.cpp test/utils/alloc_probe.cpp)
        endif()

        target_link_libraries(mka-audio-jack-tests
                PRIVATE
                mka-audio-jack
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-jack-tests
                PRIVATE
                cxx_std_${MKA_CXX_STANDARD}
        )

        gtest_discover_tests(
                mka-audio-jack-tests
                TEST_PREFIX "jack."
        )

    endif ()

    # --------------------------------------------------------
    # PulseAudio tests
    # --------------------------------------------------------

    if (MKA_BUILD_PULSEAUDIO)

        add_executable(mka-audio-pulseaudio-tests test/linux/pulseaudio/test_pulseaudio.cpp)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-pulseaudio-tests PRIVATE test/linux/pulseaudio/test_rt_pulseaudio.cpp test/utils/alloc_probe.cpp)
        endif()

        target_link_libraries(mka-audio-pulseaudio-tests
                PRIVATE
                mka-audio-pulseaudio
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-pulseaudio-tests
                PRIVATE
                cxx_std_${MKA_CXX_STANDARD}
        )

        gtest_discover_tests(
                mka-audio-pulseaudio-tests
                TEST_PREFIX "pulseaudio."
        )

    endif ()
endif ()

if (MKA_BUILD_TESTS AND MKA_BUILD_PULSEAUDIO)
    add_executable(mka-audio-pulse-configuration-tests test/linux/pulseaudio/test_configuration.cpp)
    target_link_libraries(mka-audio-pulse-configuration-tests PRIVATE
            mka-audio-pulseaudio GTest::gtest_main PkgConfig::PULSEAUDIO)
    target_link_options(mka-audio-pulse-configuration-tests PRIVATE "-Wl,--wrap=pa_stream_get_sample_spec")
    foreach(symbol pa_stream_get_buffer_attr pa_stream_connect_playback pa_stream_connect_record
            pa_context_get_state pa_stream_get_state pa_operation_get_state pa_operation_cancel pa_stream_cork pa_context_get_sink_info_list)
        target_link_options(mka-audio-pulse-configuration-tests PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
    gtest_discover_tests(mka-audio-pulse-configuration-tests TEST_PREFIX "pulseaudio.config.")
endif ()

if (MKA_BUILD_TESTS AND MKA_BUILD_ALSA)
    add_executable(mka-audio-alsa-contract-tests test/linux/alsa/test_contract.cpp)
    target_link_libraries(mka-audio-alsa-contract-tests PRIVATE mka-audio-alsa ALSA::ALSA GTest::gtest_main)
    foreach(symbol snd_pcm_open snd_pcm_close snd_pcm_hw_params_any snd_pcm_hw_params_set_access snd_pcm_hw_params_set_rate snd_pcm_hw_params_get_rate
            snd_pcm_hw_params_set_period_size snd_pcm_hw_params_set_periods_integer snd_pcm_hw_params_set_periods_near snd_pcm_hw_params_get_period_size
            snd_pcm_hw_params_get_buffer_size snd_pcm_hw_params_get_periods
            snd_pcm_hw_params_set_channels snd_pcm_hw_params_get_channels snd_pcm_link snd_pcm_unlink
            snd_pcm_avail_update snd_pcm_resume snd_pcm_mmap_begin snd_pcm_mmap_commit)
        target_link_options(mka-audio-alsa-contract-tests PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
    gtest_discover_tests(mka-audio-alsa-contract-tests TEST_PREFIX "alsa.contract." PROPERTIES TIMEOUT 15)
endif()

if (MKA_BUILD_TESTS AND MKA_BUILD_JACK)
    add_executable(mka-audio-jack-contract-tests test/linux/jack/test_contract.cpp)
    target_link_libraries(mka-audio-jack-contract-tests PRIVATE mka-audio-jack PkgConfig::JACK GTest::gtest_main)
    foreach(symbol jack_activate jack_get_sample_rate jack_get_buffer_size jack_port_register jack_connect)
        target_link_options(mka-audio-jack-contract-tests PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
    gtest_discover_tests(mka-audio-jack-contract-tests TEST_PREFIX "jack.contract." PROPERTIES TIMEOUT 20)
endif()

if (MKA_BUILD_TESTS AND MKA_BUILD_JACK)
    add_executable(mka-audio-jack-loss-tests test/linux/jack/test_server_loss.cpp)
    target_link_libraries(mka-audio-jack-loss-tests PRIVATE mka-audio-jack PkgConfig::JACK GTest::gtest_main)
    gtest_discover_tests(mka-audio-jack-loss-tests TEST_PREFIX "jack.loss." PROPERTIES TIMEOUT 25)
endif()

if (MKA_BUILD_TESTS AND MKA_BUILD_PIPEWIRE)
    add_executable(mka-audio-pipewire-contract-tests test/linux/pipewire/test_negotiation.cpp)
    target_link_libraries(mka-audio-pipewire-contract-tests PRIVATE mka-audio-pipewire PkgConfig::PIPEWIRE GTest::gtest_main)
    foreach(symbol pw_stream_new_simple pw_stream_connect pw_stream_destroy pw_stream_dequeue_buffer pw_stream_queue_buffer)
        target_link_options(mka-audio-pipewire-contract-tests PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
    gtest_discover_tests(mka-audio-pipewire-contract-tests TEST_PREFIX "pipewire.contract." PROPERTIES TIMEOUT 20)
endif()

if (MKA_BUILD_TESTS AND MKA_BUILD_PULSEAUDIO)
    add_executable(mka-audio-pulse-buffer-tests test/linux/pulseaudio/test_buffers.cpp)
    target_link_libraries(mka-audio-pulse-buffer-tests PRIVATE mka-audio-pulseaudio PkgConfig::PULSEAUDIO GTest::gtest_main)
    foreach(symbol pa_threaded_mainloop_new pa_stream_set_write_callback pa_stream_set_read_callback
            pa_stream_begin_write pa_stream_cancel_write pa_stream_write pa_stream_peek pa_stream_drop)
        target_link_options(mka-audio-pulse-buffer-tests PRIVATE "-Wl,--wrap=${symbol}")
    endforeach()
    gtest_discover_tests(mka-audio-pulse-buffer-tests TEST_PREFIX "pulseaudio.buffer." PROPERTIES TIMEOUT 20)
endif()

# Allocation interposition and sanitizer runtimes are verified separately.
if (MKA_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MKA_ALLOCATOR_TESTS)
    add_executable(mka-audio-alloc-probe-tests test/test_alloc_probe.cpp test/utils/alloc_probe.cpp)
    target_link_libraries(mka-audio-alloc-probe-tests PRIVATE GTest::gtest_main)
    gtest_discover_tests(mka-audio-alloc-probe-tests TEST_PREFIX "probe." PROPERTIES TIMEOUT 10)
endif()


if(MKA_ENABLE_COVERAGE)
    include(cmake/Coverage.cmake)
endif()

if(MKA_BUILD_WASAPI)
    add_library(mka-audio-wasapi)
    target_sources(mka-audio-wasapi PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES
        src/backend/windows/wasapi.cppm src/backend/windows/wasapi_compat.cppm)
    if(MINGW)
        target_sources(mka-audio-wasapi PRIVATE src/backend/windows/detail/wasapi_guids.cpp)
    endif()
    target_link_libraries(mka-audio-wasapi PRIVATE mka-core-audio ole32 uuid avrt)
    target_compile_features(mka-audio-wasapi PUBLIC cxx_std_${MKA_CXX_STANDARD})
    target_compile_definitions(mka-audio-wasapi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MKA_BUILD_TESTS)
        target_compile_definitions(mka-audio-wasapi PRIVATE MKA_WASAPI_TESTING=1)
        add_executable(mka-audio-wasapi-tests test/windows/test_wasapi.cpp test/windows/test_compat.cpp test/windows/test_com.cpp test/windows/test_devices.cpp test/windows/test_capabilities.cpp test/windows/test_open.cpp test/windows/test_format.cpp test/windows/test_shared.cpp test/windows/test_render_event.cpp test/windows/test_capture.cpp test/windows/test_planar.cpp test/windows/test_lifecycle.cpp test/windows/test_invalidation.cpp test/windows/test_public_wasapi.cpp test/windows/test_hardware.cpp test/windows/test_exclusive.cpp)
        target_link_libraries(mka-audio-wasapi-tests PRIVATE mka-audio-wasapi GTest::gtest_main ole32 uuid avrt)
        target_sources(mka-audio-wasapi-tests PRIVATE test/windows/test_manual_audio.cpp)
        if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # MinGW's std::print console support is provided by libstdc++exp.
            target_link_libraries(mka-audio-wasapi-tests PRIVATE stdc++exp)
        endif()
        target_compile_definitions(mka-audio-wasapi-tests PRIVATE MKA_WASAPI_TESTING=1)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-wasapi-tests PRIVATE test/windows/rt_alloc.cpp test/windows/test_rt.cpp)
        endif()
        gtest_discover_tests(mka-audio-wasapi-tests TEST_PREFIX "wasapi.")
    endif()
endif()

if(MKA_BUILD_DIRECTSOUND)
    add_library(mka-audio-directsound)
    target_sources(mka-audio-directsound PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES
        src/backend/windows/ds.cppm src/backend/windows/ds_compat.cppm)
    target_link_libraries(mka-audio-directsound PRIVATE mka-core-audio dsound dxguid)
    target_compile_features(mka-audio-directsound PUBLIC cxx_std_${MKA_CXX_STANDARD})
    target_compile_definitions(mka-audio-directsound PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN DIRECTSOUND_VERSION=0x0800)
    if(MKA_BUILD_TESTS)
        add_executable(mka-audio-directsound-tests test/windows/directsound/test_module.cpp
            test/windows/directsound/test_sdk.cpp)
        target_link_libraries(mka-audio-directsound-tests PRIVATE mka-audio-directsound GTest::gtest_main)
        target_compile_definitions(mka-audio-directsound-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN DIRECTSOUND_VERSION=0x0800)
        gtest_discover_tests(mka-audio-directsound-tests TEST_PREFIX "directsound.")
    endif()
endif()

if(MKA_BUILD_ASIO)
    add_library(mka-audio-asio)
    target_sources(mka-audio-asio PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES
        src/backend/windows/asio.cppm src/backend/windows/asio_compat.cppm)
    target_include_directories(mka-audio-asio PRIVATE "${mka_asio_sdk_root}/common")
    target_link_libraries(mka-audio-asio PRIVATE mka-core-audio)
    target_link_libraries(mka-audio-asio PRIVATE ole32 advapi32 user32)
    target_compile_features(mka-audio-asio PUBLIC cxx_std_${MKA_CXX_STANDARD})
    if(MKA_BUILD_TESTS)
        add_executable(mka-audio-asio-tests test/windows/asio/test_module.cpp)
        target_link_libraries(mka-audio-asio-tests PRIVATE mka-audio-asio GTest::gtest_main)
        gtest_discover_tests(mka-audio-asio-tests TEST_PREFIX "asio.")
        target_compile_definitions(mka-audio-asio PRIVATE MKA_ASIO_TESTING=1)
        add_executable(mka-audio-asio-native-tests test/windows/asio/test_host.cpp test/windows/asio/test_devices.cpp
            test/windows/asio/test_driver.cpp test/windows/asio/test_control.cpp test/windows/asio/test_channels.cpp
            test/windows/asio/test_buffer_limits.cpp test/windows/asio/test_channel_layout.cpp test/windows/asio/test_rates.cpp
            test/windows/asio/test_audio_buffers.cpp test/windows/asio/test_processing.cpp test/windows/asio/test_timing.cpp
            test/windows/asio/test_input_convert.cpp test/windows/asio/test_output_convert.cpp test/windows/asio/test_transport.cpp
            test/windows/asio/test_invalidation.cpp test/windows/asio/test_cleanup.cpp test/windows/asio/test_integration.cpp)
        if(MKA_ALLOCATOR_TESTS)
            target_sources(mka-audio-asio-native-tests PRIVATE test/windows/rt_alloc.cpp test/windows/asio/test_rt_asio.cpp)
        endif()
        target_include_directories(mka-audio-asio-native-tests PRIVATE "${mka_asio_sdk_root}/common")
        target_compile_definitions(mka-audio-asio-native-tests PRIVATE MKA_ASIO_TESTING=1)
        target_link_libraries(mka-audio-asio-native-tests PRIVATE mka-audio-asio GTest::gtest_main)
        gtest_discover_tests(mka-audio-asio-native-tests TEST_PREFIX "asio.native.")
        add_executable(mka-audio-asio-hardware-tests test/windows/asio/test_hardware.cpp test/windows/asio/test_manual.cpp)
        target_include_directories(mka-audio-asio-hardware-tests PRIVATE "${mka_asio_sdk_root}/common")
        target_compile_definitions(mka-audio-asio-hardware-tests PRIVATE MKA_ASIO_TESTING=1)
        target_link_libraries(mka-audio-asio-hardware-tests PRIVATE mka-audio-asio GTest::gtest_main)
        gtest_discover_tests(mka-audio-asio-hardware-tests TEST_PREFIX "asio.hardware.")
    endif()
endif()

if(MKA_BUILD_ASIO_SDK_TESTS)
    add_executable(mka-asio-sdk-tests test/windows/asio/test_sdk.cpp)
    target_include_directories(mka-asio-sdk-tests PRIVATE "${mka_asio_sdk_root}/common")
    target_link_libraries(mka-asio-sdk-tests PRIVATE mka-core-audio GTest::gtest_main)
    target_compile_definitions(mka-asio-sdk-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    gtest_discover_tests(mka-asio-sdk-tests TEST_PREFIX "asio.sdk.")
    add_test(NAME asio.sdk.ConfigurationValidation COMMAND "${CMAKE_COMMAND}"
        "-DASIO_MODULE=${PROJECT_SOURCE_DIR}/cmake/AsioSdk.cmake"
        "-DSDK_ROOT=${mka_asio_sdk_root}" "-DWORK=${PROJECT_BINARY_DIR}/asio-sdk-validation"
        -P "${PROJECT_SOURCE_DIR}/test/cmake/asio_sdk.cmake")
    set_tests_properties(asio.sdk.ConfigurationValidation PROPERTIES LABELS "unit;asio;nonrt" TIMEOUT 30)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME asio.sdk.DownloadValidation COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/test/ci/test_fetch_asio_sdk.py")
    set_tests_properties(asio.sdk.DownloadValidation PROPERTIES LABELS "unit;asio;nonrt" TIMEOUT 30)
endif()

if(MKA_BUILD_TESTS)
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClassifyTests.cmake")
endif()

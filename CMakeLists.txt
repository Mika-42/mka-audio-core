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


# ============================================================
# Global C++ configuration
# ============================================================

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

FetchContent_MakeAvailable(googletest)

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
        cxx_std_26
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
            cxx_std_26
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
            cxx_std_26
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
            cxx_std_26
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
            cxx_std_26
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
            test/test_sprint02.cpp
            test/test_convert.cpp
            test/test_endpoint_contract.cpp
            test/test_error_contract.cpp
    )

    target_link_libraries(mka-core-audio-tests
            PRIVATE
            mka-core-audio
            GTest::gtest_main
    )

    if (MKA_BUILD_ALSA)
        target_sources(mka-core-audio-tests PRIVATE test/linux/alsa/test_alsa.cpp test/linux/alsa/test_rt_alsa.cpp test/utils/alloc_probe.cpp)
        target_link_libraries(mka-core-audio-tests PRIVATE mka-audio-alsa)
    endif ()

    if (MKA_BUILD_PIPEWIRE)
        target_sources(mka-core-audio-tests PRIVATE test/linux/pipewire/test_pipewire.cpp test/linux/pipewire/test_rt_pipewire.cpp test/utils/alloc_probe.cpp)
        target_link_libraries(mka-core-audio-tests PRIVATE mka-audio-pipewire)
    endif ()

    if (MKA_BUILD_JACK)
        target_sources(mka-core-audio-tests PRIVATE test/linux/jack/test_jack.cpp test/linux/jack/test_rt_jack.cpp test/utils/alloc_probe.cpp)
        target_link_libraries(mka-core-audio-tests PRIVATE mka-audio-jack)
    endif ()

    if (MKA_BUILD_PULSEAUDIO)
        target_sources(mka-core-audio-tests PRIVATE test/linux/pulseaudio/test_pulseaudio.cpp test/linux/pulseaudio/test_rt_pulseaudio.cpp test/utils/alloc_probe.cpp)
        target_link_libraries(mka-core-audio-tests PRIVATE mka-audio-pulseaudio)
    endif ()

    target_compile_features(
            mka-core-audio-tests
            PRIVATE
            cxx_std_26
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

        add_executable(mka-audio-alsa-tests
                test/linux/alsa/test_alsa.cpp
                test/linux/alsa/test_rt_alsa.cpp
                test/utils/alloc_probe.cpp
        )

        target_link_libraries(mka-audio-alsa-tests
                PRIVATE
                mka-audio-alsa
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-alsa-tests
                PRIVATE
                cxx_std_26
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

        add_executable(mka-audio-pipewire-tests
                test/linux/pipewire/test_pipewire.cpp
                test/linux/pipewire/test_rt_pipewire.cpp
                test/utils/alloc_probe.cpp
        )

        target_link_libraries(mka-audio-pipewire-tests
                PRIVATE
                mka-audio-pipewire
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-pipewire-tests
                PRIVATE
                cxx_std_26
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

        add_executable(mka-audio-jack-tests
                test/linux/jack/test_jack.cpp
                test/linux/jack/test_rt_jack.cpp
                test/utils/alloc_probe.cpp
        )

        target_link_libraries(mka-audio-jack-tests
                PRIVATE
                mka-audio-jack
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-jack-tests
                PRIVATE
                cxx_std_26
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

        add_executable(mka-audio-pulseaudio-tests
                test/linux/pulseaudio/test_pulseaudio.cpp
                test/linux/pulseaudio/test_rt_pulseaudio.cpp
                test/utils/alloc_probe.cpp
        )

        target_link_libraries(mka-audio-pulseaudio-tests
                PRIVATE
                mka-audio-pulseaudio
                GTest::gtest_main
        )

        target_compile_features(
                mka-audio-pulseaudio-tests
                PRIVATE
                cxx_std_26
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
    foreach(symbol snd_pcm_open snd_pcm_close snd_pcm_hw_params_set_rate snd_pcm_hw_params_get_rate
            snd_pcm_hw_params_set_period_size snd_pcm_hw_params_set_periods snd_pcm_hw_params_get_period_size
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

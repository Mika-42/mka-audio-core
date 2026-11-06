# Assessment targets never enter the installed Core/backend exports.
option(MKA_STUDY_ASIO "Compile the callback assessment against a separately supplied ASIO SDK" OFF)
set(MKA_ASIO_SDK_DIR "" CACHE PATH "Unpacked ASIOSDK directory containing common/asio.h")
if(MKA_STUDY_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "The ASIO ABI assessment requires a Windows target")
    endif()
    if(NOT EXISTS "${MKA_ASIO_SDK_DIR}/common/asio.h")
        message(FATAL_ERROR "Supply MKA_ASIO_SDK_DIR; see docs/studies/asio.md for license/provenance")
    endif()
    add_executable(mka-study-asio-tests asio/test_callbacks.cpp)
    if(MKA_ALLOCATOR_TESTS)
        target_sources(mka-study-asio-tests PRIVATE ../test/windows/rt_alloc.cpp)
        target_compile_definitions(mka-study-asio-tests PRIVATE MKA_STUDY_ALLOCATOR_PROBE=1)
    endif()
    target_include_directories(mka-study-asio-tests PRIVATE "${MKA_ASIO_SDK_DIR}/common")
    target_link_libraries(mka-study-asio-tests PRIVATE mka-core-audio GTest::gtest_main)
    gtest_discover_tests(mka-study-asio-tests TEST_PREFIX "study.asio."
        PROPERTIES LABELS "unit\\;study\\;nonrt" TIMEOUT 30)
endif()

if(MKA_BUILD_PIPEWIRE)
    add_executable(mka-study-pipewire-tests duplex/test_pipewire.cpp)
    target_link_libraries(mka-study-pipewire-tests PRIVATE mka-core-audio GTest::gtest_main PkgConfig::PIPEWIRE)
    gtest_discover_tests(mka-study-pipewire-tests TEST_PREFIX "study.pipewire."
        PROPERTIES LABELS "integration\\;study\\;virtual" TIMEOUT 30)
endif()
add_executable(mka-study-timeline-tests duplex/test_timeline.cpp)
target_link_libraries(mka-study-timeline-tests PRIVATE GTest::gtest_main)
gtest_discover_tests(mka-study-timeline-tests TEST_PREFIX "study.timeline."
    PROPERTIES LABELS "unit\\;study\\;nonrt" TIMEOUT 30)

if(WIN32)
    add_executable(mka-study-legacy-tests windows/test_legacy.cpp)
    target_link_libraries(mka-study-legacy-tests PRIVATE mka-core-audio GTest::gtest_main winmm dsound dxguid)
    target_compile_definitions(mka-study-legacy-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    gtest_discover_tests(mka-study-legacy-tests TEST_PREFIX "study.legacy."
        PROPERTIES LABELS "unit\\;study\\;nonrt" TIMEOUT 30
        TEST_FILTER "-LegacyHardware.*")
    gtest_discover_tests(mka-study-legacy-tests TEST_PREFIX "study.legacy.hardware."
        PROPERTIES LABELS "integration\\;study\\;hardware" TIMEOUT 30
        TEST_FILTER "LegacyHardware.*" TEST_LIST mka_legacy_hardware)
endif()

# Instrumentation is configured before targets in Instrumentation.cmake.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
string(REGEX MATCH "^[0-9]+" mka_llvm_major "${CMAKE_CXX_COMPILER_VERSION}")
get_filename_component(mka_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(MKA_LLVM_COV NAMES "llvm-cov-${mka_llvm_major}" llvm-cov
    HINTS "${mka_compiler_directory}" REQUIRED)
find_program(MKA_LLVM_PROFDATA NAMES "llvm-profdata-${mka_llvm_major}" llvm-profdata
    HINTS "${mka_compiler_directory}" REQUIRED)
foreach(tool IN ITEMS MKA_LLVM_COV MKA_LLVM_PROFDATA)
    execute_process(COMMAND "${${tool}}" --version OUTPUT_VARIABLE version
        ERROR_VARIABLE version_error RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT version MATCHES "LLVM version ${mka_llvm_major}\\.")
        message(FATAL_ERROR "${tool} must match Clang ${mka_llvm_major}: ${version}${version_error}")
    endif()
endforeach()
add_custom_target(coverage
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/ci/coverage.py"
        --build-dir "${PROJECT_BINARY_DIR}" --source-dir "${PROJECT_SOURCE_DIR}"
        --binary "$<TARGET_FILE:mka-core-audio-tests>" --ctest "${CMAKE_CTEST_COMMAND}"
        --profdata "${MKA_LLVM_PROFDATA}" --cov "${MKA_LLVM_COV}"
    DEPENDS mka-core-audio-tests
    USES_TERMINAL VERBATIM
    COMMENT "Run core tests and generate LLVM HTML/LCOV coverage")

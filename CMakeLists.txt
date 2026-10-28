cmake_minimum_required(VERSION 3.18)
project(arbor VERSION 0.1.8 DESCRIPTION "Arbor: a 2D and 3D graphing calculator built with Grove and Walnut" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ARBOR_WITH_WALNUT "Build Walnut alongside Grove so Arbor's Walnut scripts work" ON)
option(ARBOR_BUILD_TESTS "Build the Arbor tests" ON)
option(ARBOR_PACKAGE "Make release packages: arbor with its window and compiled script built in, linked so it runs without a compiler" OFF)
option(ARBOR_BUNDLE "Run arbor-host --bundle as part of the build to make arbor with its window built in (turn off when cross-compiling, then bundle on the target)" ON)
set(ARBOR_FIZMO_TAG "v0.3.7" CACHE STRING "Fizmo git tag to fetch (needs the numerics component)")
set(ARBOR_GROVE_TAG "v0.2.9" CACHE STRING "Grove git tag to fetch")
set(ARBOR_WALNUT_TAG "v0.2.9" CACHE STRING "Walnut git tag Grove fetches")

if(ANDROID)
    set(ARBOR_WITH_WALNUT OFF CACHE BOOL "" FORCE)
    set(ARBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ARBOR_PACKAGE OFF CACHE BOOL "" FORCE)
    set(ARBOR_BUNDLE OFF CACHE BOOL "" FORCE)
endif()

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(GROVE_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(GROVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GROVE_WITH_WALNUT ${ARBOR_WITH_WALNUT} CACHE BOOL "" FORCE)
set(GROVE_WALNUT_TAG ${ARBOR_WALNUT_TAG} CACHE STRING "" FORCE)
set(GROVE_FIZMO_TAG ${ARBOR_FIZMO_TAG} CACHE STRING "" FORCE)

include(FetchContent)
set(ARBOR_FETCH_EXTRA "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    list(APPEND ARBOR_FETCH_EXTRA SYSTEM)
endif()

FetchContent_Declare(fizmo
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Fizmo.git
    GIT_TAG        ${ARBOR_FIZMO_TAG}
    GIT_SHALLOW    TRUE
    ${ARBOR_FETCH_EXTRA})

FetchContent_Declare(grove
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Grove.git
    GIT_TAG        ${ARBOR_GROVE_TAG}
    GIT_SHALLOW    TRUE
    ${ARBOR_FETCH_EXTRA})
FetchContent_MakeAvailable(fizmo grove)

if(NOT TARGET fizmo::numerics)
    message(FATAL_ERROR "Arbor needs Fizmo's numerics component (fizmo::numerics). Fizmo ${ARBOR_FIZMO_TAG} doesn't have it; use v0.2.38 or newer, or point FETCHCONTENT_SOURCE_DIR_FIZMO at a Fizmo checkout that does.")
endif()

function(arbor_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(ARBOR_BUILD_TESTS)
    enable_testing()
endif()

add_library(arbor_core STATIC
    Source/expr.cpp
    Source/tidy.cpp
    Source/program.cpp
    Source/evaluator.cpp
    Source/evaluator_fizmo.cpp
    Source/evaluator_f32.cpp
    Source/evaluator_f64.cpp
    Source/evaluator_f128.cpp
    Source/evaluator_f256.cpp
    Source/evaluator_f512.cpp
    Source/evaluator_f1024.cpp
    Source/evaluator_f2048.cpp
    Source/library.cpp
    Source/vec.cpp
    Source/kinds.cpp
    Source/graph.cpp
    Source/build_common.cpp
    Source/build2d.cpp
    Source/build_misc.cpp
    Source/build3d.cpp
    Source/build_complex.cpp
    Source/build_regions.cpp
    Source/build_pde.cpp
    Source/pde_engine.cpp
    Source/scene.cpp
    Source/examples.cpp
    Source/render.cpp
    Source/png.cpp)
add_library(arbor::core ALIAS arbor_core)
target_compile_features(arbor_core PUBLIC cxx_std_17)
target_include_directories(arbor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")
target_compile_definitions(arbor_core PRIVATE FIZMO)
target_link_libraries(arbor_core PUBLIC fizmo::numerics)
if(TARGET fizmo::core)
    target_link_libraries(arbor_core PUBLIC fizmo::core)
else()
    target_link_libraries(arbor_core PUBLIC fizmo::fizmo)
endif()
find_package(Threads REQUIRED)
target_link_libraries(arbor_core PUBLIC Threads::Threads grove::core)
arbor_warnings(arbor_core)

if(ANDROID)
    set(ARBOR_ANDROID_LIBS_DIR "${CMAKE_BINARY_DIR}/android-libs" CACHE PATH "Where the Android build puts <abi>/libarbor_android.so for grove build --runtime-lib")
    add_library(arbor_android SHARED Tools/arbor.cpp Tools/arbor_dialog.cpp)
    target_link_libraries(arbor_android PRIVATE arbor_core grove::runtime)
    target_compile_definitions(arbor_android PRIVATE "ARBOR_VERSION_STRING=\"${PROJECT_VERSION}\"")
    target_link_options(arbor_android PRIVATE
        "LINKER:-z,max-page-size=16384"
        "LINKER:-u,ANativeActivity_onCreate"
        "LINKER:--gc-sections"
        "$<$<NOT:$<CONFIG:Debug>>:LINKER:--strip-debug>"
        "LINKER:--version-script=${grove_SOURCE_DIR}/cmake/android_exports.map")
    set_property(TARGET arbor_android APPEND PROPERTY LINK_DEPENDS "${grove_SOURCE_DIR}/cmake/android_exports.map")
    set_target_properties(arbor_android PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${ARBOR_ANDROID_LIBS_DIR}/${ANDROID_ABI}")
    arbor_warnings(arbor_android)
    return()
endif()

set(ARBOR_BUNDLE_FLAGS "")
if(ARBOR_PACKAGE)
    set(ARBOR_BUNDLE_FLAGS --portable)
endif()

add_executable(arbor-host Tools/arbor.cpp Tools/arbor_dialog.cpp)
target_link_libraries(arbor-host PRIVATE arbor_core grove::runtime)
target_compile_definitions(arbor-host PRIVATE
    "ARBOR_VERSION_STRING=\"${PROJECT_VERSION}\""
    "ARBOR_SOURCE_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/UI\"")
arbor_warnings(arbor-host)
if(WIN32)
    set_target_properties(arbor-host PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

add_custom_command(TARGET arbor-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/UI" "$<TARGET_FILE_DIR:arbor-host>/ui")

set(ARBOR_BUNDLED "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/arbor${CMAKE_EXECUTABLE_SUFFIX}")
set(ARBOR_BUNDLE_DEPENDS arbor-host)
if(TARGET walnut)
    list(APPEND ARBOR_BUNDLE_DEPENDS walnut)
endif()
if(ARBOR_BUNDLE)
    set(ARBOR_BUNDLE_COMMAND ${CMAKE_COMMAND} -E env "ARBOR_UI_DIR=${CMAKE_CURRENT_SOURCE_DIR}/UI")
    set(ARBOR_BUNDLE_FIXUP "")
    if(APPLE AND ARBOR_PACKAGE)
        set(ARBOR_SCRIPT_CACHE_DIR "${CMAKE_BINARY_DIR}/grove-cache")
        list(APPEND ARBOR_BUNDLE_COMMAND "GROVE_CACHE_DIR=${ARBOR_SCRIPT_CACHE_DIR}")
        set(ARBOR_BUNDLE_FIXUP
            COMMAND ${CMAKE_COMMAND} "-DMODULE_DIR=${ARBOR_SCRIPT_CACHE_DIR}/arbor" "-DLIB_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_libs.cmake"
            COMMAND ${ARBOR_BUNDLE_COMMAND} $<TARGET_FILE:arbor-host> --bundle "${ARBOR_BUNDLED}" ${ARBOR_BUNDLE_FLAGS})
    endif()
    add_custom_command(OUTPUT "${ARBOR_BUNDLED}"
        COMMAND ${ARBOR_BUNDLE_COMMAND} $<TARGET_FILE:arbor-host> --bundle "${ARBOR_BUNDLED}" ${ARBOR_BUNDLE_FLAGS}
        ${ARBOR_BUNDLE_FIXUP}
        DEPENDS ${ARBOR_BUNDLE_DEPENDS}
            "${CMAKE_CURRENT_SOURCE_DIR}/UI/arbor.grove"
            "${CMAKE_CURRENT_SOURCE_DIR}/UI/arbor.bark"
            "${CMAKE_CURRENT_SOURCE_DIR}/UI/arbor.wal"
        COMMENT "Building arbor with its window and compiled script built in"
        VERBATIM)
    add_custom_target(arbor ALL DEPENDS "${ARBOR_BUNDLED}")
endif()

add_executable(arbor-render Tools/arbor_render.cpp)
target_link_libraries(arbor-render PRIVATE arbor_core)
arbor_warnings(arbor-render)

if(ARBOR_PACKAGE)
    if(MSVC)
        message(FATAL_ERROR "ARBOR_PACKAGE needs a GCC or Clang toolchain, like Grove and Walnut")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arbor_cpu)
    if(_arbor_cpu MATCHES "^(x86_64|amd64|x64)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_arbor_arch i686)
        else()
            set(_arbor_arch x86_64)
        endif()
    elseif(_arbor_cpu MATCHES "^(aarch64|arm64)$")
        set(_arbor_arch arm64)
    elseif(_arbor_cpu MATCHES "^(i[3-6]86|x86)$")
        set(_arbor_arch i686)
    else()
        set(_arbor_arch ${_arbor_cpu})
    endif()

    if(MINGW)
        foreach(t arbor-host arbor-render)
            target_link_options(${t} PRIVATE -static)
        endforeach()
        include(CheckCXXSourceCompiles)
        check_cxx_source_compiles("#include <cstdlib>\n#ifndef _UCRT\n#error msvcrt\n#endif\nint main() { return 0; }" ARBOR_MINGW_UCRT)
        if(_arbor_arch STREQUAL "arm64")
            set(_arbor_platform windows-clangarm64)
        elseif(_arbor_arch STREQUAL "i686")
            set(_arbor_platform windows-i686)
        elseif(ARBOR_MINGW_UCRT)
            set(_arbor_platform windows-ucrt64)
        else()
            set(_arbor_platform windows-mingw64)
        endif()
        set(CPACK_GENERATOR ZIP)
    elseif(APPLE)
        set(_arbor_platform macos-${_arbor_arch})
        set(CPACK_GENERATOR TGZ)
        set(_arbor_mac_libs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib")
        foreach(t arbor-host arbor-render)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} "-DFILES=$<TARGET_FILE:${t}>" "-DLIB_DIR=${_arbor_mac_libs}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_libs.cmake"
                COMMENT "Copying the libraries ${t} needs into ${_arbor_mac_libs}"
                VERBATIM)
        endforeach()
        add_dependencies(arbor-render arbor-host)
        if(ARBOR_BUNDLE)
            add_dependencies(arbor arbor-render)
        endif()
        install(DIRECTORY "${_arbor_mac_libs}/" DESTINATION lib COMPONENT arbor USE_SOURCE_PERMISSIONS)
    else()
        foreach(t arbor-host arbor-render)
            target_link_options(${t} PRIVATE -static-libstdc++ -static-libgcc)
        endforeach()
        set(_arbor_platform linux-${_arbor_arch})
        set(CPACK_GENERATOR TGZ)
    endif()
    set(ARBOR_PACKAGE_PLATFORM ${_arbor_platform} CACHE STRING "Platform name used in the package file name")

    if(ARBOR_BUNDLE)
        install(PROGRAMS "${ARBOR_BUNDLED}" DESTINATION . COMPONENT arbor)
    endif()
    install(TARGETS arbor-render RUNTIME DESTINATION . COMPONENT arbor)
    install(FILES README.md DESTINATION . COMPONENT arbor)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
        install(FILES LICENSE DESTINATION . COMPONENT arbor)
    endif()
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};Arbor;arbor;/")

    set(CPACK_PACKAGE_NAME "Arbor")
    set(CPACK_PACKAGE_VENDOR "ThatNewCoderJSCpp")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "Arbor-${PROJECT_VERSION}-${ARBOR_PACKAGE_PLATFORM}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Arbor")
    include(CPack)
else()
    if(ARBOR_BUNDLE)
        install(PROGRAMS "${ARBOR_BUNDLED}" DESTINATION bin)
    endif()
    install(TARGETS arbor-render RUNTIME DESTINATION bin)
endif()

if(ARBOR_BUILD_TESTS)
    add_executable(arbor_tests Tests/arbor_tests.cpp)
    target_link_libraries(arbor_tests PRIVATE arbor_core)
    arbor_warnings(arbor_tests)
    add_test(NAME arbor_tests COMMAND arbor_tests)
endif()

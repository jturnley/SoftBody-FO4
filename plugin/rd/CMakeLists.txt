# SoftBodyFO4 built on CommonLibF4RD: one DLL for the original (1.10.163), next-gen (1.10.984) and current (1.11.x)
# game, through the Runtime Database (Nexus 108394, Data/F4SE/Plugins/f4rd-runtime.bin). The same source as the
# xmake build (../src), with SOFTBODY_RD defined. Build:
#   cmake -S plugin/rd -B plugin/rd/build -G "Visual Studio 17 2022" -A x64
#   cmake --build plugin/rd/build --config RelWithDebInfo
cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED VCPKG_ROOT)
	if (DEFINED ENV{VCPKG_ROOT})
		set(VCPKG_ROOT $ENV{VCPKG_ROOT})
	else ()
		set(VCPKG_ROOT "C:/vcpkg")
	endif ()
endif ()
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")

project(SoftBodyFO4 VERSION 0.4.3 LANGUAGES CXX)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../external/CommonLibF4RD/CommonLibF4" CommonLibF4)
find_package(fmt REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)

add_library(${PROJECT_NAME} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	SOFTBODY_RD
	SOFTBODY_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}u
	SOFTBODY_VERSION_MINOR=${PROJECT_VERSION_MINOR}u
	SOFTBODY_VERSION_PATCH=${PROJECT_VERSION_PATCH}u
	_UNICODE)
target_include_directories(${PROJECT_NAME} PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../include"
	"${CMAKE_CURRENT_SOURCE_DIR}/../../../4Strands/extern/4stim")
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibF4::CommonLibF4 fmt::fmt spdlog::spdlog)
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/PCH.h")
if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive- /Zc:preprocessor /Zi /W4)
	target_link_options(${PROJECT_NAME} PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif ()

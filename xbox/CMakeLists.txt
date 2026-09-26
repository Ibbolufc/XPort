# XPort — Xbox Series X|S (UWP, x64) app.
#
# Configure with the Visual Studio generator targeting WindowsStore, e.g.:
#   cmake -S xbox -B build-xbox -G "Visual Studio 17 2022" -A x64 `
#         -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0
# xbox/build.ps1 wraps configure, build, packaging and signing; use that instead.
#
# Milestone 1: the app shell only. The shared Remote Play library (lib/) is linked in
# Phase 2 of the migration plan.

cmake_minimum_required(VERSION 3.20)

# Single version source: the CHIAKI_VERSION_* lines in the root CMakeLists.txt.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _xport_version_lines
	REGEX "^set\\(CHIAKI_VERSION_(MAJOR|MINOR|PATCH) [0-9]+\\)")
foreach(_line ${_xport_version_lines})
	string(REGEX MATCH "CHIAKI_VERSION_(MAJOR|MINOR|PATCH) ([0-9]+)" _ "${_line}")
	set(XPORT_VERSION_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
set(XPORT_VERSION "${XPORT_VERSION_MAJOR}.${XPORT_VERSION_MINOR}.${XPORT_VERSION_PATCH}")

project(XPort VERSION ${XPORT_VERSION} LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
	message(FATAL_ERROR "xbox/ must be configured with -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 (use xbox/build.ps1)")
endif()
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
	message(FATAL_ERROR "xbox/ requires a Visual Studio generator (UWP packaging is done by MSBuild)")
endif()

set(XPORT_PUBLISHER "CN=XPort Dev" CACHE STRING "Package publisher; must match the signing certificate subject")
# App package versions are always four-part.
set(XPORT_APPX_VERSION "${XPORT_VERSION}.0")

set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XPORT_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest")
configure_file(Package.appxmanifest.in "${XPORT_MANIFEST}" @ONLY)

set(XPORT_SOURCES
	src/pch.h
	src/App.cpp
	src/Log.h
	src/Log.cpp
	src/NetProbe.h
	src/NetProbe.cpp
	src/Renderer.h
	src/Renderer.cpp)

set(XPORT_ASSETS
	Assets/StoreLogo.png
	Assets/Square44x44Logo.png
	Assets/Square150x150Logo.png
	Assets/Wide310x150Logo.png
	Assets/SplashScreen.png)
set_property(SOURCE ${XPORT_ASSETS} PROPERTY VS_DEPLOYMENT_CONTENT 1)
set_property(SOURCE ${XPORT_ASSETS} PROPERTY VS_DEPLOYMENT_LOCATION "Assets")

add_executable(XPort WIN32 ${XPORT_SOURCES} ${XPORT_ASSETS} "${XPORT_MANIFEST}")

target_compile_definitions(XPort PRIVATE
	XPORT_VERSION="${XPORT_VERSION}"
	UNICODE _UNICODE
	WIN32_LEAN_AND_MEAN NOMINMAX)
# C++/WinRT (standard C++17), not C++/CX: no /ZW.
target_compile_options(XPort PRIVATE /EHsc /permissive- /utf-8 /W3 /bigobj)
target_link_libraries(XPort PRIVATE WindowsApp.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib ws2_32.lib)

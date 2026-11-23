cmake_minimum_required(VERSION 3.21)
project(CineVaultArchitectureAudit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Sql Test)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(ArchitectureAudit
    frame_and_lock_probe.cpp
    "${REPO}/dit-tools-src/cinevault-pro/src/infrastructure/ffmpeg/FFmpegAdapter.cpp")
target_include_directories(ArchitectureAudit PRIVATE "${REPO}/dit-tools-src/cinevault-pro/src")
target_link_libraries(ArchitectureAudit PRIVATE Qt6::Core Qt6::Gui Qt6::Sql)
if(MSVC)
    target_compile_options(ArchitectureAudit PRIVATE /utf-8)
endif()
set(CMAKE_AUTOMOC ON)
add_executable(CurrentFrameExtractionTest
    "${REPO}/dit-tools-src/cinevault-pro/tests/unit/FFmpegAdapterFrameExtractionTest.cpp"
    "${REPO}/dit-tools-src/cinevault-pro/src/infrastructure/ffmpeg/FFmpegAdapter.cpp")
target_include_directories(CurrentFrameExtractionTest PRIVATE "${REPO}/dit-tools-src/cinevault-pro/src")
target_link_libraries(CurrentFrameExtractionTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
if(MSVC)
    target_compile_options(CurrentFrameExtractionTest PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME CurrentFrameExtractionTest COMMAND CurrentFrameExtractionTest)
add_executable(CurrentSourceMonitorProbe
    source_monitor_probe.cpp
    "${REPO}/dit-tools-src/cinevault-pro/src/application/SourceChangeMonitor.cpp"
    "${REPO}/dit-tools-src/cinevault-pro/src/application/SourceChangeMonitor.h")
target_compile_features(CurrentSourceMonitorProbe PRIVATE cxx_std_20)
target_compile_definitions(CurrentSourceMonitorProbe PRIVATE CINEVAULT_TESTING)
target_include_directories(CurrentSourceMonitorProbe PRIVATE "${REPO}/dit-tools-src/cinevault-pro/src")
target_link_libraries(CurrentSourceMonitorProbe PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(CurrentSourceMonitorProbe PRIVATE /utf-8)
endif()

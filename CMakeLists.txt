cmake_minimum_required(VERSION 3.16)
project(hids LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -O2)

# ---------------------------------------------------------------------------
# Tier-0 offline replay: inference core only, no collectors / no networking.
# Runs on any architecture (x86 dev box, or armhf under qemu).
# ---------------------------------------------------------------------------
add_executable(hids_replay src/replay_main.cpp)
target_include_directories(hids_replay PRIVATE include)

# ---------------------------------------------------------------------------
# Full HIDS service: live collectors -> aggregator -> inference -> debouncer ->
# MQTT telemetry (Mosquitto). No Wazuh, no desktop app in this build.
# Linux-only (perf_event_open, /proc); guarded so the replay tool still builds
# on other hosts.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(hids
    src/main.cpp
    src/hids_service.cpp
    src/perf_collector.cpp
    src/proc_collector.cpp
    src/config.cpp
    src/mqtt_publisher.cpp)
  target_include_directories(hids PRIVATE include)
  target_link_libraries(hids PRIVATE pthread)
else()
  message(STATUS "Non-Linux host: skipping 'hids' service target (replay only)")
endif()

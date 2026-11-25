cmake_minimum_required(VERSION 3.25)
project(graw2_campaign LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(rexglue 0.11.0 REQUIRED CONFIG)
rexglue_project(graw2_campaign OUTPUT_DIR generated/campaign)

add_executable(graw2_campaign WIN32 src/main.cpp src/native_hooks.cpp
               ../common/native_host.cpp)
target_include_directories(graw2_campaign PRIVATE . ../common ../../native/include)
rexglue_setup_target(graw2_campaign)

option(GRAWBOX_TEST_TOOLS "Build the scripted controller for unattended tests" ON)
if (GRAWBOX_TEST_TOOLS)
  add_library(rexhid-autopilot SHARED ../../tools/autopilot/autopilot_driver.cpp)
  set_target_properties(
    rexhid-autopilot
    PROPERTIES DEBUG_POSTFIX d
               RELWITHDEBINFO_POSTFIX rd
               RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:graw2_campaign>
  )
  target_link_libraries(rexhid-autopilot PRIVATE rex::base)
  # Driver modules must share the program's allocator.
  rexglue_link_host_allocator(rexhid-autopilot)
endif ()

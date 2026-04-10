cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "html_to_image_flutter")
project(${PROJECT_NAME} LANGUAGES CXX)

cmake_policy(VERSION 3.14...3.25)

set(PLUGIN_NAME "html_to_image_flutter_plugin")

list(APPEND PLUGIN_SOURCES
  "html_to_image_flutter_plugin.cpp"
  "html_to_image_flutter_plugin.h"
)

add_library(${PLUGIN_NAME} SHARED
  "include/html_to_image_flutter/html_to_image_flutter_plugin_c_api.h"
  "html_to_image_flutter_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Plugin public headers (for dependents)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Add the plugin's own windows/ directory to the include path.
# This makes the bundled "WebView2.h" and "WebView2EnvironmentOptions.h"
# (located next to this CMakeLists.txt) visible to the compiler via
# #include "WebView2.h".
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

# ── WebView2Loader (bundled) ─────────────────────────────────────────────────
# The loader DLL is required to call CreateCoreWebView2EnvironmentWithOptions.
# It discovers and loads the system-installed WebView2 Runtime at runtime.
add_library(WebView2Loader SHARED IMPORTED GLOBAL)
set_target_properties(WebView2Loader PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/x64/WebView2Loader.dll"
  IMPORTED_IMPLIB   "${CMAKE_CURRENT_SOURCE_DIR}/libs/x64/WebView2Loader.dll.lib")

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  WebView2Loader)

# Bundle WebView2Loader.dll next to the app executable so it is found at runtime
set(html_to_image_flutter_bundled_libraries
  "$<TARGET_FILE:WebView2Loader>"
  PARENT_SCOPE
)

# ── Tests ────────────────────────────────────────────────────────────────────
if(${include_${PROJECT_NAME}_tests})
  set(TEST_RUNNER "${PROJECT_NAME}_test")
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/release-1.11.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of googletest" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(${TEST_RUNNER}
    test/html_to_image_flutter_plugin_test.cpp
    ${PLUGIN_SOURCES}
  )
  apply_standard_settings(${TEST_RUNNER})
  target_include_directories(${TEST_RUNNER} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${TEST_RUNNER} PRIVATE flutter_wrapper_plugin gtest_main gmock)
  add_custom_command(TARGET ${TEST_RUNNER} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${FLUTTER_LIBRARY}" $<TARGET_FILE_DIR:${TEST_RUNNER}>
  )
  include(GoogleTest)
  gtest_discover_tests(${TEST_RUNNER})
endif()

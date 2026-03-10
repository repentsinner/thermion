# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

# Project-level configuration.
set(PROJECT_NAME "thermion_flutter")
project(${PROJECT_NAME})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wno-unused-variable -Wno-unused-function")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wno-unused-variable -Wno-unused-function")

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "thermion_flutter_plugin")

# The plugin library only handles Flutter texture registration and
# method-channel plumbing.  All Filament/rendering work is compiled into
# the thermion_dart native-asset shared library and accessed from Dart
# via FFI.
add_library(${PLUGIN_NAME} SHARED
  "thermion_flutter_plugin.cc"
  "filament_texture.cc"
  "filament_pb_texture.cc"
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)

set(thermion_flutter_bundled_libraries
  ""
  PARENT_SCOPE
)

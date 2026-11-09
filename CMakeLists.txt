cmake_minimum_required(VERSION 3.10)
project(SimpleMap VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets Svg)

add_library(zhili_core STATIC src/document.cpp src/layout.cpp)
target_link_libraries(zhili_core PUBLIC Qt5::Core)
add_executable(simplemap WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(simplemap PRIVATE zhili_core Qt5::Widgets Qt5::Svg)
set_target_properties(simplemap PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER org.zhili.simplemap
  MACOSX_BUNDLE_BUNDLE_NAME ZhiLi
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(zhili_core PRIVATE /utf-8)
  target_compile_options(simplemap PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Qt5 5.9 REQUIRED COMPONENTS Test)
  add_executable(zhili_smoke src/main.cpp)
  target_compile_definitions(zhili_smoke PRIVATE ZHILI_TEST)
  target_link_libraries(zhili_smoke PRIVATE zhili_core Qt5::Widgets Qt5::Svg Qt5::Test)
  if(MSVC)
    target_compile_options(zhili_smoke PRIVATE /utf-8)
  endif()
  add_test(NAME zhili_smoke COMMAND zhili_smoke)
  set_tests_properties(zhili_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()

install(TARGETS simplemap RUNTIME DESTINATION bin BUNDLE DESTINATION .)

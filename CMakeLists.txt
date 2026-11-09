cmake_minimum_required(VERSION 3.10)
project(SimpleMap VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets Svg)

add_library(zhili_core STATIC src/document.cpp src/layout.cpp)
target_link_libraries(zhili_core PUBLIC Qt5::Core)
qt5_add_resources(zhili_font_resources assets/fonts.qrc)
add_library(zhili_appearance STATIC src/appearance.cpp ${zhili_font_resources})
target_link_libraries(zhili_appearance PUBLIC Qt5::Widgets)
add_executable(simplemap WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(simplemap PRIVATE zhili_core zhili_appearance Qt5::Svg)
set_target_properties(simplemap PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER org.zhili.simplemap
  MACOSX_BUNDLE_BUNDLE_NAME ZhiLi
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(zhili_core PRIVATE /utf-8)
  target_compile_options(zhili_appearance PRIVATE /utf-8)
  target_compile_options(simplemap PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Qt5 5.9 REQUIRED COMPONENTS Test)
  add_executable(zhili_smoke src/main.cpp)
  target_compile_definitions(zhili_smoke PRIVATE ZHILI_TEST)
  target_link_libraries(zhili_smoke PRIVATE zhili_core zhili_appearance Qt5::Svg Qt5::Test)
  add_executable(zhili_document_test tests/document_test.cpp)
  target_link_libraries(zhili_document_test PRIVATE zhili_core)
  if(MSVC)
    target_compile_options(zhili_smoke PRIVATE /utf-8)
    target_compile_options(zhili_document_test PRIVATE /utf-8)
  endif()
  add_test(NAME zhili_document COMMAND zhili_document_test)
  set(zhili_test_platform offscreen)
  if(WIN32)
    # Qt 5's Windows font engine needs the native QPA integration.
    set(zhili_test_platform windows)
  endif()
  foreach(scale IN ITEMS 1 1.25 1.5 2)
    add_test(NAME zhili_smoke_${scale}x COMMAND zhili_smoke)
    set_tests_properties(zhili_smoke_${scale}x PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=${zhili_test_platform};QT_FORCE_STDERR_LOGGING=1;QT_FONT_DPI=96;QT_SCALE_FACTOR=${scale};ZHILI_EXPECT_SCALE=${scale}" TIMEOUT 30)
  endforeach()
endif()

install(TARGETS simplemap RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(FILES assets/fonts/OFL-SourceHanSans.txt assets/fonts/OFL-SourceHanSerif.txt
  DESTINATION share/licenses/zhili)

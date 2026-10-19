cmake_minimum_required(VERSION 3.21)
project(MediaVault VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(MEDIAVAULT_BUILD_TESTS "Build the library scanner tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

# Scanner has no GUI dependencies so it can be tested headless.
add_library(mediavault_core STATIC
    src/MediaLibrary.cpp
    src/MediaLibrary.h
)
target_include_directories(mediavault_core PUBLIC src)
target_link_libraries(mediavault_core PUBLIC Qt6::Core)

qt_add_executable(MediaVault WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)
target_link_libraries(MediaVault PRIVATE
    mediavault_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

if(MEDIAVAULT_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(tst_medialibrary tests/tst_medialibrary.cpp)
    target_link_libraries(tst_medialibrary PRIVATE mediavault_core Qt6::Test)
    add_test(NAME medialibrary COMMAND tst_medialibrary)
endif()

install(TARGETS MediaVault
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

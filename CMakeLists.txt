cmake_minimum_required(VERSION 3.16)
project(PhysLabEge VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------
# Интерактивная физико-математическая лаборатория ЕГЭ №10
# Сборка: CMake + Qt6 Widgets. Все расчёты выполняются локально.
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# Библиотека ядра лаборатории: математика, сцены, элементы интерфейса.
file(GLOB_RECURSE LAB_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)
file(GLOB_RECURSE LAB_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM LAB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(lab_lib STATIC ${LAB_SOURCES} ${LAB_HEADERS})
target_include_directories(lab_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lab_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

# Исполняемое приложение.
add_executable(physlab src/main.cpp)
target_link_libraries(physlab PRIVATE lab_lib)

set_target_properties(physlab PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF)

# ------------------------- Тесты -------------------------------
option(LAB_BUILD_TESTS "Build unit tests for the math core" ON)

if(LAB_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET GTest::gtest_main)
        find_package(GTest QUIET)
    endif()
    if(NOT TARGET GTest::gtest_main)
        # Если GoogleTest не установлен системно — получим его из сети.
        include(FetchContent)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    add_subdirectory(tests)
endif()

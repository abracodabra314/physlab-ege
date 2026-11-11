# Тесты математического ядра лаборатории (GoogleTest).
# Тестируются только модели и утилиты — без окна и без Graphics View.

add_executable(lab_tests
    test_core.cpp
    test_motion.cpp
    test_boat.cpp
    test_tank.cpp
    test_well.cpp
    test_work.cpp
    test_mixture.cpp)

target_include_directories(lab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lab_tests PRIVATE lab_lib GTest::gtest_main)

include(GoogleTest OPTIONAL RESULT_VARIABLE googleTestModule)
if(googleTestModule)
    gtest_discover_tests(lab_tests)
else()
    add_test(NAME lab_tests COMMAND lab_tests)
endif()

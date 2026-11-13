# Тесты лаборатории (GoogleTest): математическое ядро моделей и геометрия
# сцен. Тесты отрисовки создают QApplication с offscreen-платформой и
# проверяют векторы и графики по кадру, не полагаясь на цвет пикселей
# (антиалиасинг смешивает цвета тонких линий).

add_executable(lab_tests
    test_core.cpp
    test_motion.cpp
    test_boat.cpp
    test_tank.cpp
    test_well.cpp
    test_work.cpp
    test_mixture.cpp
    test_spring.cpp
    test_spring_scene.cpp
    test_spring_vertical.cpp
    test_projectile.cpp
    test_projectile_scene.cpp
    test_horizon.cpp
    test_horizon_scene.cpp
    test_sound.cpp
    test_sound_scene.cpp
    test_adiabatic.cpp
    test_adiabatic_scene.cpp
    test_bacteria.cpp
    test_bacteria_scene.cpp
    test_decay.cpp
    test_decay_scene.cpp)

target_include_directories(lab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lab_tests PRIVATE lab_lib GTest::gtest_main)

include(GoogleTest OPTIONAL RESULT_VARIABLE googleTestModule)
if(googleTestModule)
    gtest_discover_tests(lab_tests)
else()
    add_test(NAME lab_tests COMMAND lab_tests)
endif()

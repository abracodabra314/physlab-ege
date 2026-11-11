# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/lab_lib_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lab_lib_autogen.dir/ParseCache.txt"
  "CMakeFiles/physlab_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/physlab_autogen.dir/ParseCache.txt"
  "lab_lib_autogen"
  "physlab_autogen"
  "tests/CMakeFiles/lab_tests_autogen.dir/AutogenUsed.txt"
  "tests/CMakeFiles/lab_tests_autogen.dir/ParseCache.txt"
  "tests/lab_tests_autogen"
  )
endif()

 cmake_minimum_required(VERSION 2.8.12)
 project(await)

 include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
 conan_basic_setup()

 add_executable(await await.c)
 target_link_libraries(await ${CONAN_LIBS})

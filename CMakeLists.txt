cmake_minimum_required(VERSION 3.12)
project(OS-Project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(dokan_disk dokan_disk.cpp dokan_log.cpp file.cpp dev_io.cpp)
add_executable(format format.cpp dev_io.cpp)

set(LIBS "C:/Program Files/Dokan/DokanLibrary-1.3.1/dokan1.dll")
set(INC "C:/Program Files/Dokan/DokanLibrary-1.3.1/include")
target_link_libraries(dokan_disk PUBLIC ${LIBS})
target_include_directories(dokan_disk PUBLIC ${INC})
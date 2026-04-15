cmake_minimum_required(VERSION 3.20)
project(gh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.*")

# WIN32 = no console window; manifest embedded via app.manifest source
add_executable(gh WIN32
    src/main.cpp
    src/app.manifest
)

target_include_directories(gh PRIVATE src)

target_link_libraries(gh PRIVATE
    shell32
    user32
    gdi32
    advapi32
)

# VS: set startup project
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT gh)

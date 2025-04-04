cmake_minimum_required(VERSION 3.28)
project(2DPlatform-Client LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)


# # 헤더 파일 디렉토리 추가
# include_directories(${PROJECT_SOURCE_DIR}/include)

# 소스 파일 목록
set(SOURCES
    src/main.cpp
)


# add_executable(client src/main.cpp)
add_executable(client ${SOURCES})
target_compile_features(client PRIVATE cxx_std_17)
target_link_libraries(client PRIVATE SFML::Graphics SFML::Network)


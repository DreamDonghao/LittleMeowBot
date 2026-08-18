cmake_minimum_required(VERSION 3.20)
project(LittleMeowBot CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt CONFIG REQUIRED)

# 将所有源文件集中到两个主要源码目录（src 和 controllers）
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp controllers/*.cpp)
# 单独添加根目录的 main.cpp
list(APPEND SRC_FILES main.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# 静态链接 C++ 标准库和 GCC 运行时
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            LINK_FLAGS "-static-libgcc -static-libstdc++"
    )
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        include
)

# 7. 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
        Drogon::Drogon
        spdlog::spdlog_header_only
        fmt::fmt
        SQLite3_lib
)

# 前端构建 (Vue3) - 前端源码变化时自动重新构建，输出到 build/bot/public
find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)
file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/frontend/src/*)
set(FRONTEND_DEPS
        ${FRONTEND_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend/package.json
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend/package-lock.json
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend/tsconfig.json
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend/vite.config.ts)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/build/bot/public/index.html
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontend
        DEPENDS ${FRONTEND_DEPS}
        COMMENT "Building frontend (npm run build)")
add_custom_target(frontend ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build/bot/public/index.html)
add_dependencies(${PROJECT_NAME} frontend)

# 可执行文件直接输出到部署目录 build/bot/exe
set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/bot/exe)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/build/bot/
        COPYONLY
)
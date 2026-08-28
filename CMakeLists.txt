cmake_minimum_required(VERSION 3.20)
project(insoulforge VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF)
set(DEPLOY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/insoulforge)

find_package(Drogon CONFIG REQUIRED)
# 若 Drogon 没有 SQLite3 应用独立的包
if (NOT TARGET SQLite3 AND NOT TARGET SQLite3::SQLite3 AND NOT TARGET SQLite3_lib)
    find_package(SQLite3 REQUIRED)
endif ()
find_package(spdlog REQUIRED)
find_package(fmt CONFIG REQUIRED)

# 尽量静态链接：能找到系统静态库（.a）就直接链，找不到再回退到 Drogon 传入的动态依赖
find_library(SQLITE3_STATIC_LIB NAMES libsqlite3.a)
find_library(JSONCPP_STATIC_LIB NAMES libjsoncpp.a)
find_path(JSONCPP_INCLUDE_DIR NAMES json/value.h)

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
        fmt::fmt-header-only
        SQLite3_lib
)

if (SQLITE3_STATIC_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SQLITE3_STATIC_LIB})
endif ()

if (JSONCPP_STATIC_LIB)
    target_include_directories(${PROJECT_NAME} PRIVATE ${JSONCPP_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${JSONCPP_STATIC_LIB})
endif ()

# 前端构建 (Vue3) - 前端源码变化时自动重新构建，输出到 build/insoulforge/public
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
        OUTPUT ${DEPLOY_DIR}/public/index.html
        COMMAND ${NPM_ENV_CMD} ${NPM_EXECUTABLE} install --no-audit --no-fund
        COMMAND ${NPM_ENV_CMD} ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontend
        DEPENDS ${FRONTEND_DEPS}
        COMMENT "Installing deps and building frontend"
)

add_custom_target(frontend ALL
        DEPENDS ${DEPLOY_DIR}/public/index.html)
add_dependencies(${PROJECT_NAME} frontend)

# 可执行文件直接输出到部署目录 build/insoulforge/exe
set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${DEPLOY_DIR}/exe)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/runScript/run.sh
        ${DEPLOY_DIR}/run.sh
        COPYONLY
)
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/runScript/run.bat
        ${DEPLOY_DIR}/run.bat
        COPYONLY
)
cmake_minimum_required(VERSION 3.31)
project(LittleMeowBot CXX)
include_directories(include)
include(CheckIncludeFileCXX)

set(BUILD_SHARED_LIBS OFF)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源文件
set(SRC_FILES
        # storage
        src/storage/Database.cpp
        src/storage/Statement.cpp
        # agent
        src/agent/AgentTypes.cpp
        src/agent/AgentToolManager.cpp
        src/agent/RouterAgent.cpp
        src/agent/PlannerAgent.cpp
        src/agent/ExecutorAgent.cpp
        src/agent/AgentSystem.cpp
        # service
        src/service/ToolRegistry.cpp
        src/service/MemoryService.cpp
        src/service/PromptService.cpp
        src/service/RAGFlowClient.cpp
        src/service/MessageService.cpp
        src/service/WebSocketManager.cpp
        # model
        src/model/QQMessage.cpp
        src/model/ChatRecordManager.cpp
        src/model/MemoryManager.cpp
        src/model/GroupConfigManager.cpp
        # config
        src/config/Config.cpp
        # api
        src/api/ApiClient.cpp
        # handler
        src/handler/CommandHandler.cpp
        # util
        src/util/Prompt.cpp
        src/util/Log.cpp
        # main
        main.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# 静态链接 C++ 标准库和 GCC 运行时
set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-static-libgcc -static-libstdc++"
)
# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon
# add_subdirectory(drogon)
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

# spdlog - 尝试静态链接
find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog_header_only)

# fmt - 静态链接
find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# SQLite3 - use Drogon's SQLite3
target_link_libraries(${PROJECT_NAME} PRIVATE SQLite3_lib)

aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
        ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(${PROJECT_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(${PROJECT_NAME}
        PRIVATE
        ${CTL_SRC}
        ${FILTER_SRC}
        ${PLUGIN_SRC})
# ##############################################################################
# uncomment the following line for dynamically loading views
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################
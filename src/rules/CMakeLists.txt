project(src_rules)

file(GLOB ${PROJECT_NAME}_sources "${PROJECT_SOURCE_DIR}/*.cpp")
file(GLOB ${PROJECT_NAME}_headers "${PROJECT_SOURCE_DIR}/*.h")

set(${PROJECT_NAME}_files ${${PROJECT_NAME}_sources} ${${PROJECT_NAME}_headers} CACHE INTERNAL "")

add_subdirectory(functions)
set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${${PROJECT_NAME}_functions_files} CACHE INTERNAL "")
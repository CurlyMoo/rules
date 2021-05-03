project(src)

set(${PROJECT_NAME}_files "" CACHE INTERNAL "")

add_subdirectory(rules)
set(${PROJECT_NAME}_files	${${PROJECT_NAME}_rules_files} CACHE INTERNAL "")
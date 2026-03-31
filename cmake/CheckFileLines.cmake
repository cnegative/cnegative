if(NOT DEFINED ROOT_DIR)
    message(FATAL_ERROR "ROOT_DIR is required")
endif()

set(CNEGATIVE_LINE_LIMIT 5000)
set(CNEGATIVE_LINE_FILES)

foreach(base IN ITEMS .github include src docs scripts tests examples cmake)
    file(GLOB_RECURSE current_files
        LIST_DIRECTORIES false
        "${ROOT_DIR}/${base}/*.c"
        "${ROOT_DIR}/${base}/*.h"
        "${ROOT_DIR}/${base}/*.md"
        "${ROOT_DIR}/${base}/*.yml"
        "${ROOT_DIR}/${base}/*.yaml"
        "${ROOT_DIR}/${base}/*.sh"
        "${ROOT_DIR}/${base}/*.cneg"
        "${ROOT_DIR}/${base}/*.cn"
        "${ROOT_DIR}/${base}/*.S"
        "${ROOT_DIR}/${base}/*.cmake"
    )
    list(APPEND CNEGATIVE_LINE_FILES ${current_files})
endforeach()

list(APPEND CNEGATIVE_LINE_FILES "${ROOT_DIR}/CMakeLists.txt")

list(SORT CNEGATIVE_LINE_FILES)
set(CNEGATIVE_LINE_ERRORS "")

foreach(file IN LISTS CNEGATIVE_LINE_FILES)
    file(READ "${file}" content)
    string(REGEX MATCHALL "\n" newlines "${content}")
    list(LENGTH newlines line_count)
    string(LENGTH "${content}" content_length)
    if(NOT content_length EQUAL 0 AND NOT content MATCHES "\n$")
        math(EXPR line_count "${line_count} + 1")
    endif()

    if(line_count GREATER CNEGATIVE_LINE_LIMIT)
        file(RELATIVE_PATH relpath "${ROOT_DIR}" "${file}")
        string(APPEND CNEGATIVE_LINE_ERRORS
            "line-limit violation: ${relpath} has ${line_count} lines (limit ${CNEGATIVE_LINE_LIMIT})\n"
        )
    endif()
endforeach()

if(NOT CNEGATIVE_LINE_ERRORS STREQUAL "")
    message(FATAL_ERROR "${CNEGATIVE_LINE_ERRORS}")
endif()

# envlogger_core: pure C11 product logic. No ESP-IDF, FreeRTOS or OS headers allowed in here,
# so the same sources build for the ESP32 (component) and for host unit tests (static lib).
# Every src/*.c is compiled: adding a module never requires editing this file.
if(ESP_PLATFORM)
    # ESP-IDF also evaluates this file in CMake script mode, where CONFIGURE_DEPENDS is invalid.
    file(GLOB ENVLOGGER_CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
    idf_component_register(SRCS ${ENVLOGGER_CORE_SRCS} INCLUDE_DIRS "include")
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
else()
    cmake_minimum_required(VERSION 3.16)
    file(GLOB ENVLOGGER_CORE_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
    add_library(envlogger_core STATIC ${ENVLOGGER_CORE_SRCS})
    target_include_directories(envlogger_core PUBLIC include)
    set_target_properties(envlogger_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_compile_options(envlogger_core PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion
                                                -Wno-sign-conversion -Wdouble-promotion)
    target_link_libraries(envlogger_core PUBLIC m)
endif()

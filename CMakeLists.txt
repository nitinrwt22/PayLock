cmake_minimum_required(VERSION 3.15)
project(PayLock VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find MySQL via mysql_config or Homebrew fallback paths
find_program(MYSQL_CONFIG_EXECUTABLE mysql_config
    HINTS /opt/homebrew/bin /usr/local/bin
)

if (MYSQL_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${MYSQL_CONFIG_EXECUTABLE} --cflags
        OUTPUT_VARIABLE MYSQL_CFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
        OUTPUT_VARIABLE MYSQL_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Found mysql_config: ${MYSQL_CONFIG_EXECUTABLE}")
    message(STATUS "MySQL CFLAGS: ${MYSQL_CFLAGS}")
    message(STATUS "MySQL LIBS: ${MYSQL_LIBS}")
else()
    message(WARNING "mysql_config not found, using default Homebrew locations.")
    set(MYSQL_CFLAGS "-I/opt/homebrew/include/mysql -I/opt/homebrew/include")
    set(MYSQL_LIBS "-L/opt/homebrew/lib -lmysqlclient")
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    /opt/homebrew/include
)

# Parse include paths from mysql_config
if (MYSQL_CONFIG_EXECUTABLE)
    string(REGEX MATCHALL "-I([^ ]+)" MYSQL_INCLUDES "${MYSQL_CFLAGS}")
    foreach(INC ${MYSQL_INCLUDES})
        string(SUBSTRING ${INC} 2 -1 INC_PATH)
        include_directories(${INC_PATH})
    endforeach()
endif()

link_directories(
    /opt/homebrew/lib
)

# Core Sources
set(PAYLOCK_CORE_SOURCES
    src/database/DatabaseManager.cpp
    src/transaction/TransactionManager.cpp
    src/concurrency/ConcurrentTransactionProcessor.cpp
)

# Main Executable
add_executable(paylock
    src/main.cpp
    ${PAYLOCK_CORE_SOURCES}
)

# Link Libraries
target_link_libraries(paylock PRIVATE
    mysqlclient
    zstd
    ssl
    crypto
    z
    resolv
)

# Tests Executable
add_executable(test_db
    tests/test_db.cpp
    ${PAYLOCK_CORE_SOURCES}
)

target_link_libraries(test_db PRIVATE
    mysqlclient
    zstd
    ssl
    crypto
    z
    resolv
)

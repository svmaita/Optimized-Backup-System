cmake_minimum_required(VERSION 3.16)

project(OptimizedBackup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(OptimizedBackup
    src/main.cpp
    src/backup/BackupEngine.cpp
    src/backup/BackupStatistics.cpp
    src/backup/UsnJournal.cpp
    src/filesystem/FileSystemUtils.cpp
)

target_include_directories(OptimizedBackup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(OptimizedBackup PRIVATE
    shell32
    comctl32
    ole32
)

set_target_properties(OptimizedBackup PROPERTIES
    WIN32_EXECUTABLE YES
)

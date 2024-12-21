cmake_minimum_required(VERSION 3.30)
project(vma_user_app C)

set(SOURCES vma_user.c)

add_executable(vma_user ${SOURCES})

target_include_directories(vma_user PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
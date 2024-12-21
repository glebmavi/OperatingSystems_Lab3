cmake_minimum_required(VERSION 3.30)
project(vma_user_app C)

add_executable(vma_user vma_user.c)

target_include_directories(vma_user PRIVATE ${CMAKE_SOURCE_DIR})
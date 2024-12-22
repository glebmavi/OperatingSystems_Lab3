cmake_minimum_required(VERSION 3.30)
project(vma_kernel_module C)

add_definitions(-D__KERNEL__ -DMODULE)

# Add the cmake module for finding kernels
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(KernelHeaders REQUIRED)

# Get current kernel version
execute_process(COMMAND uname -r OUTPUT_VARIABLE KERNEL_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
set(KERNELDIR /lib/modules/${KERNEL_VERSION}/build)

set(KERNEL_SOURCES vma_driver.c)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Makefile
        "obj-m := vma_driver.o

KDIR := ${KERNELDIR}
PWD := \$(shell pwd)

all:
\t make -C \$(KDIR) M=\$(PWD) modules

clean:
\t make -C \$(KDIR) M=\$(PWD) clean
")

# Now define a custom target in CMake:
add_custom_target(vma_driver ALL
        COMMAND make -f Makefile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${KERNEL_SOURCES}
)

# Define a 'clean' target:
add_custom_target(vma_driver_clean
        COMMAND make -f Makefile clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Copy source files to binary dir:
foreach(src ${KERNEL_SOURCES})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${src}
            ${CMAKE_CURRENT_BINARY_DIR}/${src} COPYONLY)
endforeach()

# Copy headers
configure_file(${CMAKE_CURRENT_LIST_DIR}/../structs.h
        ${CMAKE_CURRENT_BINARY_DIR}/../structs.h COPYONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/../ioctl_vma.h
        ${CMAKE_CURRENT_BINARY_DIR}/../ioctl_vma.h COPYONLY)

# this is needed in order for CLion IDE to provide syntax highlightning
# this is independent from the actual kernel object that is built
add_executable(dummy
        # add all *.h and *.c files here that # CLion should cover
        vma_driver.c
)

# CLion IDE will find symbols from <linux/*>
target_include_directories("dummy" PRIVATE ${KERNELHEADERS_INCLUDE_DIRS})

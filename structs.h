#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_VMA_COUNT 256
#define MAX_FILE_PATH 256
#include <sys/types.h>

/*
Each entry describes one VMA region, including:
 - start address
 - end address
 - flags (from vm_area_struct->vm_flags)
 - file_name if associated file exists
*/
struct vma_info {
    unsigned long start;
    unsigned long end;
    unsigned long flags;
    char file_name[MAX_FILE_PATH];
};

/*
This structure is used for communication between user space
and kernel space. The user sets 'pid' before calling ioctl;
the kernel then populates 'count' and the array of 'vmas'
upon success.
*/
struct vma_info_buffer {
    pid_t pid; // Transferred PID number
    int count; // Count of VMAs in the array
    struct vma_info vmas[MAX_VMA_COUNT];
};

#endif

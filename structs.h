#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_VMA_COUNT 4096
#define MAX_FILE_PATH 256
#define MAX_REGION_NAME 32
#define MAX_ADDRESS_NAME 32
#define MAX_SPECIAL_ADDRESSES 16

/*
Each entry describes one VMA region, including:
 - start address
 - end address
 - flags (from vm_area_struct->vm_flags)
 - file_name if associated file exists
 - region_name (heap, stack, vdso, vvar, or "other")
*/
struct vma_info {
    unsigned long start;
    unsigned long end;
    unsigned long flags;
    char region_name[MAX_REGION_NAME];
    char file_name[MAX_FILE_PATH];
};

struct special_address {
    unsigned long address;
    char address_name[MAX_ADDRESS_NAME];
};

/*
Structure for communication between user space and kernel space. The user sets PID before calling ioctl;
the kernel then populates 'count' and the array of 'vmas' upon success.
*/
struct vma_info_buffer {
    pid_t pid; // Transferred PID number
    int vma_count; // Count of VMAs in the array
    int speadd_count; // Count of special addresses in the array
    struct special_address speadds[MAX_SPECIAL_ADDRESSES];
    struct vma_info vmas[MAX_VMA_COUNT];
};

#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

#ifndef VM_READ
#define VM_READ    0x00000001
#endif
#ifndef VM_WRITE
#define VM_WRITE   0x00000002
#endif
#ifndef VM_EXEC
#define VM_EXEC    0x00000004
#endif

#include "structs.h"
#include "../ioctl_vma.h"

// Function to decode flags
void print_flags(const unsigned long flags, char *buffer, const size_t size) {
    snprintf(buffer, size, "%c%c%c",
             (flags & VM_READ) ? 'R' : '-',
             (flags & VM_WRITE) ? 'W' : '-',
             (flags & VM_EXEC) ? 'X' : '-');
}

// Function to format size into human-readable string
const char* format_size(const unsigned long size, char *buffer, const size_t buf_size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double readable_size = (double)size;

    while (readable_size >= 1024 && unit_index < 4) {
        readable_size /= 1024;
        unit_index++;
    }

    snprintf(buffer, buf_size, "%.1f %s", readable_size, units[unit_index]);
    return buffer;
}

int main(const int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <PID>\n", argv[0]);
        return 1;
    }

    // Parse PID from the command line
    const pid_t pid = (pid_t)atoi(argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %d\n", pid);
        return 1;
    }

    // Open the device
    const int fd = open("/dev/vma_device", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/vma_device");
        return 1;
    }

    // Prepare the buffer
    struct vma_info_buffer buf = {0};
    buf.pid = pid;
    buf.vma_count = 0;

    // Make the ioctl call
    if (ioctl(fd, VMA_IOC_GET_INFO, &buf) < 0) {
        perror("ioctl(VMA_IOC_GET_INFO) failed");
        close(fd);
        return 1;
    }

    close(fd);

    printf("VMA info for PID: %d\n", pid);
    printf("Count of VMAs: %d\n", buf.vma_count);
    printf("Count of special addresses: %d\n\n", buf.speadd_count);

    if (buf.speadd_count > 0) {
        printf("Special Addresses:\n");
        for (int i = 0; i < buf.speadd_count; i++) {
            printf("  0x%016lx  %-12s\n",
                   buf.speadds[i].address,
                   buf.speadds[i].address_name);
        }
        printf("\n");
    }

    printf("%-4s %-16s %-16s %-10s %-5s %-16s %s\n",
           "Idx", "Start", "End", "Size", "Flags", "Region", "FilePath");
    printf("-------------------------------------------------------------------------------\n");

    char size_str[16];

    for (int i = 0; i < buf.vma_count; i++) {
        struct vma_info *vma = &buf.vmas[i];
        char flags[4] = {0};

        print_flags(vma->flags, flags, sizeof(flags));

        format_size(vma->size, size_str, sizeof(size_str));

        printf("%-4d 0x%-14lx 0x%-14lx %-10s %-5s %-16s %s\n",
               i,
               vma->start,
               vma->end,
               size_str,
               flags,
               vma->region_name,
               vma->file_name);
    }

    return 0;
}

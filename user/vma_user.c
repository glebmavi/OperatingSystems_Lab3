#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

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
void print_flags(unsigned long flags, char *buffer, const size_t size) {
    snprintf(buffer, size, "%c%c%c",
             (flags & VM_READ) ? 'R' : '-',
             (flags & VM_WRITE) ? 'W' : '-',
             (flags & VM_EXEC) ? 'X' : '-');
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
    buf.count = 0;

    // Make the ioctl call
    if (ioctl(fd, VMA_IOC_GET_INFO, &buf) < 0) {
        perror("ioctl(VMA_IOC_GET_INFO) failed");
        close(fd);
        return 1;
    }

    close(fd);

    printf("VMA info for PID: %d\n", pid);
    printf("Count of VMAs: %d\n", buf.count);
    printf("%-4s %-16s %-16s %-9s %-10s %s\n",
           "Idx", "Start", "End", "Flags", "Region", "FilePath");
    printf("--------------------------------------------------------------------------\n");

    for (int i = 0; i < buf.count; i++) {
        struct vma_info *vma = &buf.vmas[i];
        char flags[4] = {0};

        print_flags(vma->flags, flags, sizeof(flags));

        printf("%-4d 0x%-14lx 0x%-14lx %-9s %-10s %s\n",
               i,
               vma->start,
               vma->end,
               flags,
               vma->region_name,
               vma->file_name);
    }

    return 0;
}

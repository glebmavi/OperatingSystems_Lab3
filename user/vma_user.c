#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "structs.h"
#include "../ioctl_vma.h"

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

    // Make the ioctl call
    if (ioctl(fd, VMA_IOC_GET_INFO, &buf) < 0) {
        perror("ioctl(VMA_IOC_GET_INFO) failed");
        close(fd);
        return 1;
    }

    close(fd);

    // Print results
    printf("VMA info for PID %d:\n", pid);
    printf("Count of VMAs: %d\n", buf.count);
    for (int i = 0; i < buf.count; i++) {
        struct vma_info *vma = &buf.vmas[i];
        printf("[%d] Start: 0x%lx, End: 0x%lx, Flags: 0x%lx, File: %s\n",
               i, vma->start, vma->end, vma->flags, vma->file_name);
    }

    return 0;
}


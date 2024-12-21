#ifndef IOCTL_CMDS_H
#define IOCTL_CMDS_H

#include <linux/ioctl.h>
#include <linux/types.h>

// "V" - random "magic" symbol
#define VMA_IOC_MAGIC  'V'

/*
Ioctl command definition that is read/write:
- user passes in vma_info_buffer with 'pid' set
- kernel fills in the rest of the fields
*/
#define VMA_IOC_GET_INFO _IOWR(VMA_IOC_MAGIC, 1, struct vma_info_buffer)

#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/path.h>

#include "../structs.h"
#include "../ioctl_vma.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gleb Malikov");
MODULE_DESCRIPTION("Simple kernel module to get VMA info by PID");
MODULE_VERSION("0.1");

static dev_t dev_number;
static struct cdev vma_cdev;
static struct class *vma_class = NULL;

#define DEVICE_NAME "vma_device"

// Forward declarations of file operations
static long vma_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int vma_open(struct inode *inode, struct file *file);
static int vma_release(struct inode *inode, struct file *file);

static const struct file_operations vma_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = vma_ioctl,
    .open           = vma_open,
    .release        = vma_release,
};

static int vma_open(struct inode *inode, struct file *file) {
    pr_info("vma_driver: device opened\n");
    return 0;
}

static int vma_release(struct inode *inode, struct file *file) {
    pr_info("vma_driver: device released\n");
    return 0;
}

// Helper function that fills the vma_info_buffer with the VMAs of the given PID.
static int fill_vma_info(struct vma_info_buffer *buf) {
    int count = 0;
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;

    // Look up the task by PID
    task = pid_task(find_vpid(buf->pid), PIDTYPE_PID);
    if (!task) {
        pr_err("vma_driver: Could not find task for PID %d\n", buf->pid);
        return -ESRCH;
    }

    mm = get_task_mm(task);
    if (!mm) {
        pr_err("vma_driver: Could not get mm for PID %d\n", buf->pid);
        put_task_struct(task);
        return -EFAULT;
    }

    // Lock the mmap to safely iterate over VMAs.
    down_read(&mm->mmap_lock);

    for_each_vma(vma, mm, vma_start(vma)) {
        if (count >= MAX_VMA_COUNT) {
            pr_warn("vma_driver: Reached maximum VMA count (%d)\n", MAX_VMA_COUNT);
            break;
        }

        struct vma_info *info = &buf->vmas[count];
        memset(info, 0, sizeof(struct vma_info));

        // Fill information about VMA
        info->start = vma->vm_start;
        info->end = vma->vm_end;
        info->flags = vma->vm_flags;

        // Retrieval of file path, if VMA linked with file
        if (vma->vm_file) {
            struct path path;

            path = vma->vm_file->f_path;
            path_get(&path);

            char* path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
            if (!path_buf) {
                pr_err("vma_driver: Failed to allocate memory for path\n");
                info->file_name[0] = '\0';
                goto next_vma;
            }

            const int ret = d_path(&path, path_buf, PATH_MAX);
            if (ret >= PATH_MAX) {
                pr_warn("vma_driver: Path truncated for VMA %d\n", count);
                strncpy(info->file_name, path_buf, MAX_FILE_PATH - 1);
                info->file_name[MAX_FILE_PATH - 1] = '\0';
            } else if (ret < 0) {
                pr_err("vma_driver: d_path failed for VMA %d\n", count);
                strcpy(info->file_name, "Unknown");
            } else {
                strncpy(info->file_name, path_buf, MAX_FILE_PATH - 1);
                info->file_name[MAX_FILE_PATH - 1] = '\0';
            }

            kfree(path_buf);
            path_put(&path);
        } else {
            strcpy(info->file_name, "Anonymous");
        }
        count++;
    }

    next_vma:
        up_read(&mm->mmap_lock);

    buf->count = count;

    mmput(mm);
    return 0;
}

static long vma_ioctl(struct file *file, const unsigned int cmd, const unsigned long arg) {
    struct vma_info_buffer kbuf;

    if (_IOC_TYPE(cmd) != VMA_IOC_MAGIC) {
        pr_err("vma_driver: Invalid magic number\n");
        return -EINVAL;
    }

    switch (cmd) {
    case VMA_IOC_GET_INFO:
        // Copy the user-space buffer to kernel space to get the PID.
        if (copy_from_user(&kbuf, (void __user *)arg, sizeof(kbuf))) {
            pr_err("vma_driver: copy_from_user failed\n");
            return -EFAULT;
        }

        // Correct PID check
        if (kbuf.pid <= 0) {
            pr_err("vma_driver: Invalid PID %d\n", kbuf.pid);
            return -EINVAL;
        }

        // Fill the VMA info in our kernel buffer.
        if (fill_vma_info(&kbuf) != 0) {
            return -EFAULT;
        }

        // Copy the updated structure (with VMA info) back to user.
        if (copy_to_user((void __user *)arg, &kbuf, sizeof(kbuf))) {
            pr_err("vma_driver: copy_to_user failed\n");
            return -EFAULT;
        }
        break;

    default:
        pr_err("vma_driver: Unknown ioctl command\n");
        return -ENOTTY;
    }

    return 0;
}

static int __init vma_driver_init(void) {
    // Allocate a device number dynamically
    int ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("vma_driver: Failed to allocate a major number\n");
        return ret;
    }

    // Create cdev structure
    cdev_init(&vma_cdev, &vma_fops);
    vma_cdev.owner = THIS_MODULE;

    // Add cdev to the system
    ret = cdev_add(&vma_cdev, dev_number, 1);
    if (ret < 0) {
        pr_err("vma_driver: Failed to add cdev\n");
        unregister_chrdev_region(dev_number, 1);
        return ret;
    }

    // Create class for udev
    vma_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(vma_class)) {
        pr_err("vma_driver: Failed to create class\n");
        cdev_del(&vma_cdev);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(vma_class);
    }

    // Create device node /dev/vma_device
    if (IS_ERR(device_create(vma_class, NULL, dev_number, NULL, DEVICE_NAME))) {
        pr_err("vma_driver: Failed to create device\n");
        class_destroy(vma_class);
        cdev_del(&vma_cdev);
        unregister_chrdev_region(dev_number, 1);
        return -1;
    }

    pr_info("vma_driver: Module loaded, device /dev/%s\n", DEVICE_NAME);
    return 0;
}

static void __exit vma_driver_exit(void)
{
    // Remove device
    device_destroy(vma_class, dev_number);
    // Destroy class
    class_destroy(vma_class);
    // Delete cdev
    cdev_del(&vma_cdev);
    // Unregister device number
    unregister_chrdev_region(dev_number, 1);

    pr_info("vma_driver: Module unloaded\n");
}

module_init(vma_driver_init);
module_exit(vma_driver_exit);

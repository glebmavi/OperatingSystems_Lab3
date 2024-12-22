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

static int vma_open(struct inode *inode, struct file *file)
{
    /*
     * Nothing special to do on open, but typically we would
     * do things like increment usage counters, etc.
     */
    pr_info("vma_driver: device opened\n");
    return 0;
}

static int vma_release(struct inode *inode, struct file *file)
{
    /*
     * Nothing special to do on release in this example.
     */
    pr_info("vma_driver: device released\n");
    return 0;
}

// Helper function that fills the vma_info_buffer with the VMAs of the given PID.
static int fill_vma_info(struct vma_info_buffer *buf)
{
    int count = 0;

    // Look up the task by PID
    struct task_struct* task = get_pid_task(find_vpid(buf->pid), PIDTYPE_PID);
    if (!task) {
        pr_err("vma_driver: Could not find task for PID %d\n", buf->pid);
        return -ESRCH;
    }

    struct mm_struct* mm = get_task_mm(task);
    if (!mm) {
        pr_err("vma_driver: Could not get mm for PID %d\n", buf->pid);
        put_task_struct(task);
        return -EFAULT;
    }

    // Lock the mmap to safely iterate over VMAs.
    down_read(&mm->mmap_lock);

    struct vm_area_struct* vma = mm->mmap;
    while (vma && count < MAX_VMA_COUNT) {
        const struct file *file = vma->vm_file;
        struct vma_info *info = &buf->vmas[count];

        info->start = vma->vm_start;
        info->end   = vma->vm_end;
        info->flags = vma->vm_flags;

        if (file && file->f_path.dentry) {
            const char *name = file->f_path.dentry->d_name.name;
            strncpy(info->file_name, name, MAX_FILE_PATH);
            info->file_name[MAX_FILE_PATH - 1] = '\0';
        } else {
            strcpy(info->file_name, "Anonymous");
        }

        count++;
        vma = vma->vm_next;
    }
    up_read(&mm->mmap_lock);

    mmput(mm);
    put_task_struct(task);

    buf->count = count;
    return 0;
}

static long vma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct vma_info_buffer kbuf;

    if (_IOC_TYPE(cmd) != VMA_IOC_MAGIC) {
        pr_err("vma_driver: Invalid magic number\n");
        return -EINVAL;
    }

    switch (cmd) {
    case VMA_IOC_GET_INFO:
        /*
         * Copy the user-space buffer to kernel space to get the PID.
         */
        if (copy_from_user(&kbuf, (void __user *)arg, sizeof(kbuf))) {
            pr_err("vma_driver: copy_from_user failed\n");
            return -EFAULT;
        }

        /*
         * Fill the VMA info in our kernel buffer.
         */
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

static int __init vma_driver_init(void)
{
    int ret;

    // Allocate a device number dynamically
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
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
    if (!device_create(vma_class, NULL, dev_number, NULL, DEVICE_NAME)) {
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

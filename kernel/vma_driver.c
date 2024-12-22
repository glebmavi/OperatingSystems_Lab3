#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched/signal.h>
#include <linux/pid.h>
#include <linux/namei.h>

#include "../structs.h" // Contains our vma_info_buffer, vma_info
#include "../ioctl_vma.h" // Contains the IOCTL definitions

#define DEVICE_NAME "vma_device"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gleb Malikov");
MODULE_DESCRIPTION("Simple driver to retrieve a task's VMA info using Maple Tree");
MODULE_VERSION("0.1");

static dev_t devt;
static struct cdev vma_cdev;
static struct class *vma_class = NULL;

/*
 * Forward declarations
 */

static int vma_open(struct inode *inode, struct file *file);
static int vma_release(struct inode *inode, struct file *file);
static long vma_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
bool is_addr_within_range(const unsigned long addr, const unsigned long start, const unsigned long end);

/*
 * File operations structure
 */
static const struct file_operations vma_fops = {
    .owner          = THIS_MODULE,
    .open           = vma_open,
    .release        = vma_release,
    .unlocked_ioctl = vma_unlocked_ioctl,
};

/**
 * Called when userspace opens /dev/vma_device
 */
static int vma_open(struct inode *inode, struct file *file) {
    pr_info("vma_driver: device opened\n");
    return 0;
}

/**
 * Called when userspace closes /dev/vma_device
 */
static int vma_release(struct inode *inode, struct file *file) {
    pr_info("vma_driver: device released\n");
    return 0;
}

/**
 * Helper to copy the file name from vm_area_struct->vm_file.
 * We use d_path() to attempt to get a path, or mark as "anonymous"
 */
static void fill_vma_filename(const struct vm_area_struct *vma, char *filename, const size_t max_len)
{
    if (!vma->vm_file) {
        strscpy(filename, "anonymous", max_len);
        return;
    }

    /* We have a file pointer; attempt to get a path */
    {
        char *tmp;
        char path_buf[256] = {0};

        tmp = d_path(&vma->vm_file->f_path, path_buf, sizeof(path_buf));
        if (IS_ERR(tmp)) {
            strscpy(filename, "unknown", max_len);
        } else {
            strscpy(filename, tmp, max_len);
        }
    }
}

/**
 * Fill the special_address array with interesting mm_struct addresses:
 *   - start_code, end_code
 *   - start_data, end_data
 *   - start_brk, brk
 *   - start_stack
 *   - arg_start, arg_end
 *   - env_start, env_end
 */
static void retrieve_special_addresses(struct mm_struct *mm, struct special_address *speadds, int *speadd_count) {
    int idx = 0;

    speadds[idx].address = mm->start_code;
    strscpy(speadds[idx].address_name, "start_code", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->end_code;
    strscpy(speadds[idx].address_name, "end_code", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->start_data;
    strscpy(speadds[idx].address_name, "start_data", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->end_data;
    strscpy(speadds[idx].address_name, "end_data", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->start_brk;
    strscpy(speadds[idx].address_name, "start_brk", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->brk;
    strscpy(speadds[idx].address_name, "brk", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->start_stack;
    strscpy(speadds[idx].address_name, "start_stack", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->arg_start;
    strscpy(speadds[idx].address_name, "arg_start", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->arg_end;
    strscpy(speadds[idx].address_name, "arg_end", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->env_start;
    strscpy(speadds[idx].address_name, "env_start", MAX_ADDRESS_NAME);
    idx++;

    speadds[idx].address = mm->env_end;
    strscpy(speadds[idx].address_name, "env_end", MAX_ADDRESS_NAME);
    idx++;

    *speadd_count = idx;
}

bool is_addr_within_range(const unsigned long addr, const unsigned long start, const unsigned long end) {
    return addr >= start && addr < end;
}

/**
 * Sets the region name for a given vma
 */
static const char *identify_vma_region(const struct vm_area_struct *vma) {
    const struct mm_struct *mm = vma->vm_mm;

    if (is_addr_within_range(mm->start_code, vma->vm_start, vma->vm_end) || is_addr_within_range(mm->end_code, vma->vm_start, vma->vm_end)) {
        return "code";
    }

    if (is_addr_within_range(mm->start_data, vma->vm_start, vma->vm_end) || is_addr_within_range(mm->end_data, vma->vm_start, vma->vm_end)) {
        return "data";
    }

    if (is_addr_within_range(mm->arg_start, vma->vm_start, vma->vm_end) || is_addr_within_range(mm->arg_end, vma->vm_start, vma->vm_end)) {
        return "arguments";
    }

    if (is_addr_within_range(mm->env_start, vma->vm_start, vma->vm_end) || is_addr_within_range(mm->env_end, vma->vm_start, vma->vm_end)) {
        return "environment";
    }

    if (is_addr_within_range(mm->start_brk, vma->vm_start, vma->vm_end) || is_addr_within_range(mm->brk, vma->vm_start, vma->vm_end)) {
        return "heap";
    }

    if (is_addr_within_range(mm->start_stack, vma->vm_start, vma->vm_end)) {
        return "stack";
    }

    if (vma->vm_start == (unsigned long)mm->context.vdso) {
        return "vdso";
    }

    return "other";
}


/**
 * The main ioctl function which handles our VMA dump request.
 */
static long vma_unlocked_ioctl(struct file *file, const unsigned int cmd, const unsigned long arg) {
    struct vma_info_buffer *kbuf; /* Kernel copy of the user struct */
    struct task_struct *task;
    struct mm_struct *mm;
    int ret = 0;

    /*
     * We only handle one command: VMA_IOC_GET_INFO
     */
    if (cmd != VMA_IOC_GET_INFO)
        return -EINVAL;

    /*
     * Allocate memory for kbuf dynamically
     */
    kbuf = kmalloc(sizeof(struct vma_info_buffer), GFP_KERNEL);
    if (!kbuf) {
        pr_err("vma_driver: failed to allocate memory for kbuf\n");
        return -ENOMEM;
    }

    /*
     * Copy the user-provided buffer into kernel space.
     */
    if (copy_from_user(kbuf, (void __user *)arg, sizeof(struct vma_info_buffer))) {
        pr_err("vma_driver: failed to copy data from user\n");
        kfree(kbuf);
        return -EFAULT;
    }

    /*
     * Look up the task_struct by PID
     */
    task = get_pid_task(find_vpid(kbuf->pid), PIDTYPE_PID);
    if (!task) {
        pr_err("vma_driver: failed to find task for PID %d\n", kbuf->pid);
        return -ESRCH;
    }

    /*
     * Acquire the mm_struct for the given task.
     * get_task_mm() increments mm->mm_count, so we must call mmput() later.
     */
    mm = get_task_mm(task);
    if (!mm) {
        pr_err("vma_driver: could not get mm for PID %d\n", kbuf->pid);
        put_task_struct(task);
        return -EINVAL;
    }

    /*
     * We will read-lock the mmap_lock while traversing the Maple Tree.
     */
    down_read(&mm->mmap_lock);

    /*
     * Get "special" addresses from mm_struct
     */
    retrieve_special_addresses(mm, kbuf->speadds, &kbuf->speadd_count);

    /*
     * Use Maple Tree iteration to gather VMAs.
     * The kernel provides a helper approach with vma_iter_init(...) + vma_next(...).
     */
    {
        struct vma_iterator vmi;
        struct vm_area_struct *vma;
        int count = 0;

        vma_iter_init(&vmi, mm, 0);

        for (vma = vma_next(&vmi); vma && count < MAX_VMA_COUNT; vma = vma_next(&vmi)) {
            struct vma_info *info = &kbuf->vmas[count];

            info->start = vma->vm_start;
            info->end   = vma->vm_end;
            info->flags = vma->vm_flags;

            /* Fill the file path */
            fill_vma_filename(vma, info->file_name, MAX_FILE_PATH);

            /* Identify region type: "stack", "heap", "code", etc. */
            {
                const char *region = identify_vma_region(vma);
                strscpy(info->region_name, region, MAX_REGION_NAME);
            }
            count++;
        }
        /* Store the actual number of VMAs we found */
        kbuf->vma_count = count;
    }

    /*
     * Unlock and release references
     */
    up_read(&mm->mmap_lock);
    mmput(mm);
    put_task_struct(task);

    /*
     * Copy the results back to user space
     */
    if (copy_to_user((void __user *)arg, kbuf, sizeof(struct vma_info_buffer))) {
        pr_err("vma_driver: failed to copy data back to user\n");
        ret = -EFAULT;
    }

    kfree(kbuf);
    return ret;
}

/**
 * Module init: create device node /dev/vma_device
 */
static int __init vma_driver_init(void)
{
    /* Allocate a device number dynamically */
    int err = alloc_chrdev_region(&devt, 0, 1, DEVICE_NAME);
    if (err < 0) {
        pr_err("vma_driver: failed to allocate char device region\n");
        return err;
    }

    /* Create a device class */
    vma_class = class_create(DEVICE_NAME);
    if (IS_ERR(vma_class)) {
        pr_err("vma_driver: failed to create class\n");
        unregister_chrdev_region(devt, 1);
        return PTR_ERR(vma_class);
    }

    /* Initialize and add our cdev structure */
    cdev_init(&vma_cdev, &vma_fops);
    vma_cdev.owner = THIS_MODULE;

    err = cdev_add(&vma_cdev, devt, 1);
    if (err) {
        pr_err("vma_driver: failed to add cdev\n");
        class_destroy(vma_class);
        unregister_chrdev_region(devt, 1);
        return err;
    }

    /* Create the device node in /dev */
    device_create(vma_class, NULL, devt, NULL, DEVICE_NAME);

    pr_info("vma_driver: module loaded, device /dev/%s\n", DEVICE_NAME);
    return 0;
}

/**
 * Module exit: clean up everything
 */
static void __exit vma_driver_exit(void) {
    device_destroy(vma_class, devt);
    cdev_del(&vma_cdev);
    class_destroy(vma_class);
    unregister_chrdev_region(devt, 1);
    pr_info("vma_driver: module unloaded\n");
}

module_init(vma_driver_init);
module_exit(vma_driver_exit);

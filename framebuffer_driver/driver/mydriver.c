#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/delay.h>

#define TESTDEV_VENDOR_ID   0x1B36
#define TESTDEV_PRODUCT_ID  0x0005
#define DATA_OFFSET         8
#define MYDRIVER_BAR_NUM 2
#define MYDRIVER_BAR_MASK    (1 < MYDRIVER_BAR_NUM)

#define SET_COUNT 0x42
#define GET_MODES 0x43
#define SET_MODE 0x44
#define GET_CURRENT 0x45
#define CLEAR_SCREEN 0x46

#define REG_MODES_COUNT     0
#define REG_MODES_ADDR      8
#define REG_BUFFER_ADDR     16
#define REG_STATUS          24
#define REG_MODES_BUFFER    32
#define REG_FRAMEBUFFER     4096

#define BMP_OFFSET 138
#define WIDTH 640
#define HEIGHT 480

static struct pci_device_id mydriver_id_table[] = {
        { PCI_DEVICE(TESTDEV_VENDOR_ID, TESTDEV_PRODUCT_ID)},
        { 0, }
};

MODULE_DEVICE_TABLE(pci, mydriver_id_table);

static int mydriver_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void mydriver_remove(struct pci_dev *pdev);

static struct pci_driver mydriver = {
        .name = "my_pci_driver",
        .id_table = mydriver_id_table,
        .probe = mydriver_probe,
        .remove = mydriver_remove
};

typedef struct display {
    u32 width;
    u32 height;
} display;

struct display_info {
    u32 count;
    u32 current_mode;
    u64 modes_addr;
};

typedef struct struct_mode {
    uint32_t height;
    uint32_t width;
} struct_mode;


struct mydriver_data {
    u8 __iomem *hwmem;
    int data_len;
    struct device* mydriver;
    struct cdev cdev;
    u32 count_mode;
    u64 addr_struct_mode;
    u64 addr_buf_mode;
    u32 status;
    struct_mode* buf_mode;
    u8* framebuffer;
    u32 fb_size;
    u32 current_mode;
    u32 data_offset;
};

int create_char_devs(struct mydriver_data* drv);
int destroy_char_devs(void);

static int mydriver_open(struct inode *inode, struct file *file);
static int mydriver_release(struct inode *inode, struct file *file);
static long mydriver_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t mydriver_read(struct file *file, char __user* buf, size_t count, loff_t *offset);
static ssize_t mydriver_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);
//static int mydriver_mmap(struct file *file, struct vm_area_struct *vma);

static const struct file_operations mydriver_fops = {
        .owner		= THIS_MODULE,
        .open		= mydriver_open,
        .release	= mydriver_release,
        .unlocked_ioctl = mydriver_ioctl,
        .read		= mydriver_read,
        .write		= mydriver_write,
        //.mmap       = mydriver_mmap
};

static int dev_major = 0;

static struct class *mydriverclass = NULL;

static struct mydriver_data* mydriver_data_private = NULL;

static ssize_t count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", mydriver_data_private->count_mode);
}

static ssize_t count_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
    int new_value;
    struct_mode* new_buf;

    if (kstrtoint(buf, 0, &new_value) != 0)
        return -EINVAL;
    if (new_value <= 0) {
        printk(KERN_INFO "Count must be positive!\n");
        return -EINVAL;
    }
    if (!mydriver_data_private->buf_mode) {
        mydriver_data_private->buf_mode = kzalloc(new_value * sizeof(struct_mode), GFP_KERNEL);
        if (!mydriver_data_private->buf_mode) {
            return -ENOMEM;
        }
    }
    else {
        new_buf = krealloc(mydriver_data_private->buf_mode, new_value * sizeof(struct_mode),
                                                  GFP_KERNEL);
        if (!new_buf) {
            return -ENOMEM;
        }
        mydriver_data_private->buf_mode = new_buf;
        if (new_value > mydriver_data_private->count_mode) {
            size_t old_count = mydriver_data_private->count_mode;
            memset(&mydriver_data_private->buf_mode[old_count], 0, (new_value - old_count) * sizeof(struct_mode));
        }
    }
    mydriver_data_private->count_mode = new_value;
    printk(KERN_INFO "mydriver_data_private: count_mode changed to %d\n", new_value);
    return count;
}

static ssize_t buf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    size_t len = 0;
    if (!mydriver_data_private->buf_mode || mydriver_data_private->count_mode <= 0) {
        return sprintf(buf, "No modes configured\n");
    }
    for (int i = 0; i < mydriver_data_private->count_mode; i++) {
        size_t increment = sprintf(buf + len, "%d*%d\n", mydriver_data_private->buf_mode[i].width, mydriver_data_private->buf_mode[i].height);
        len += increment;
    }
    return len;
}

static ssize_t buf_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    if (mydriver_data_private->count_mode <= 0) {
        printk(KERN_INFO "ERROR! Set count of mode!\n");
        return count;
    }
    char* str_buf = kstrdup(buf, GFP_KERNEL);
    char* token = strsep(&str_buf, " ");
    //mydriver_data_private->buf_mode = kzalloc(mydriver_data_private->count_mode * sizeof(struct_mode), GFP_KERNEL);
    for (int i = 0; i < mydriver_data_private->count_mode; i++) {
        if (!token) {
            break;
        }
        char *tmp_token = kstrdup(token, GFP_KERNEL);
        char *width_str = strsep(&tmp_token, "*");
        char *height_str = strsep(&tmp_token, "*");
        u32 width, height;
        if (kstrtou32(width_str, 0, &width) == 0 && kstrtou32(height_str, 0, &height) == 0) {
            if (width < 0 || height < 0) {
                printk(KERN_INFO "ERROR! Width and height must be positive!\n");
                return count;
            }
            mydriver_data_private->buf_mode[i].width = width;
            mydriver_data_private->buf_mode[i].height = height;
            pr_info("Mode %d: %dx%d\n", i, width, height);
        }
        kfree(tmp_token);
        token = strsep(&str_buf, " ");

    }
    kfree(str_buf);
    return count;
}

static ssize_t current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    size_t len = sprintf(buf, "%d*%d\n", mydriver_data_private->buf_mode[mydriver_data_private->current_mode].width,
                  mydriver_data_private->buf_mode[mydriver_data_private->current_mode].height);
    return len;
}

static ssize_t current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int new_value;

    if (kstrtoint(buf, 0, &new_value) != 0)
        return -EINVAL;

    if (new_value <= 0 || new_value >= mydriver_data_private->count_mode) {
        new_value = 0;
    }
    mydriver_data_private->current_mode = new_value;
    //может надо сделать так чтобы записанное изображение при изменении размера буфера сохранялось?
    mydriver_data_private->fb_size = mydriver_data_private->buf_mode[new_value].width *
            mydriver_data_private->buf_mode[new_value].height * 3;
    void* new_fb = kzalloc(mydriver_data_private->fb_size, GFP_KERNEL);
    if (!new_fb)
        return -ENOMEM;
    if (mydriver_data_private->framebuffer) {
        kfree(mydriver_data_private->framebuffer);
    }
    mydriver_data_private->framebuffer = new_fb;

    printk(KERN_INFO "mydriver_data_private: current_mode changed to %d\n", new_value);
    return count;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    size_t len = sprintf(buf, "%d\n", mydriver_data_private->status);
    return len;
}

static ssize_t status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int new_value;

    if (kstrtoint(buf, 0, &new_value) != 0)
        return -EINVAL;

    if (new_value <= 0 || new_value >= mydriver_data_private->count_mode) {
        new_value = 0;
    }

    mydriver_data_private->status = new_value;

    printk(KERN_INFO "mydriver_data_private: status set to %d\n", new_value);
    return count;
}


static ssize_t clear_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    int new_value;

    void __iomem *status_addr = mydriver_data_private->hwmem + REG_STATUS;
    while (ioread8(status_addr) != 1) {
        udelay(1000);
    }

    if (kstrtoint(buf, 0, &new_value) != 0)
        return -EINVAL;

    if (mydriver_data_private->buf_mode[mydriver_data_private->current_mode].width < 1
    || mydriver_data_private->buf_mode[mydriver_data_private->current_mode].height < 1) {
        printk(KERN_INFO "Nothing to clean\n");
        return count;
    }

    if (new_value != 1) {
        printk(KERN_INFO "If you want to clear the buffer, enter 1\n");
        return count;
    }

    void __iomem *fb_addr = mydriver_data_private->hwmem + REG_FRAMEBUFFER;
    //printk(KERN_INFO "FB_SIZE: %ld\n", drv->fb_size);
    void __iomem *count_mode_addr = mydriver_data_private->hwmem + REG_MODES_COUNT;
    iowrite8(mydriver_data_private->count_mode, count_mode_addr);
    void __iomem *current_addr = mydriver_data_private->hwmem + REG_BUFFER_ADDR ;
    iowrite8(mydriver_data_private->current_mode, current_addr);
    void __iomem *mode_addr = mydriver_data_private->hwmem + REG_MODES_BUFFER;
    for (size_t j = 0; j < mydriver_data_private->count_mode; j++) {
        iowrite32(mydriver_data_private->buf_mode[mydriver_data_private->current_mode].height, mode_addr + j*8);
        iowrite32(mydriver_data_private->buf_mode[mydriver_data_private->current_mode].width, mode_addr + j*8 + 4);
    }
    int count_i = 0;
    for (size_t i = 0; i < mydriver_data_private->fb_size; i++) {
        iowrite8(0, fb_addr + i);
        count_i++;
    }
    iowrite8(2, status_addr);
 //    printk(KERN_INFO "STATUS: %ld\n", ioread8(status_addr));
 //    printk(KERN_INFO "COUNT I: %ld\n", count_i);

    printk(KERN_INFO "Screen cleaning was successful!\n", new_value);
    return count;
}

static DEVICE_ATTR(count_mode, 0664, count_show, count_store);
static DEVICE_ATTR(buf_mode, 0664, buf_show, buf_store);
static DEVICE_ATTR(current_mode, 0664, current_show, current_store);
static DEVICE_ATTR(status_mode, 0664, status_show, status_store);
static DEVICE_ATTR(clear_mode, 0200, NULL, clear_store);

#define CREATE_SYSFS_ATTR(_cls, _attr)                                       \
	err = device_create_file(_cls, &dev_attr_##_attr);                   \
	if (err < 0)                                                         \
		printk(KERN_ERR " " "mydriver_driver"                                   \
				": Failed to create sysfs attribute " #_attr \
				"\n");

//#define CREATE_SYSFS_ATTR(_cls, _attr) do {                              \
//    int local_err = device_create_file(_cls, &dev_attr_##_attr);         \
//    if (local_err < 0) {                                                 \
//        printk(KERN_ERR "mydriver: Failed to create " #_attr "\n");      \
//    }                                                                    \
//} while (0)

int create_char_devs(struct mydriver_data *drv)
{
    int err;
    dev_t dev;

    err = alloc_chrdev_region(&dev, 0, 1, "mydriver");

    dev_major = MAJOR(dev);

    mydriver_data_private = drv;
    mydriverclass = class_create(THIS_MODULE, "mydriver");

    cdev_init(&mydriver_data_private->cdev, &mydriver_fops);
    mydriver_data_private->cdev.owner = THIS_MODULE;
    cdev_add(&mydriver_data_private->cdev, MKDEV(dev_major, 0), 1);

    mydriver_data_private->mydriver = device_create(mydriverclass, NULL, MKDEV(dev_major, 0), NULL, "mydriver");

    CREATE_SYSFS_ATTR(mydriver_data_private->mydriver, count_mode);
    CREATE_SYSFS_ATTR(mydriver_data_private->mydriver, buf_mode);
    CREATE_SYSFS_ATTR(mydriver_data_private->mydriver, current_mode);
    CREATE_SYSFS_ATTR(mydriver_data_private->mydriver, status_mode);
    CREATE_SYSFS_ATTR(mydriver_data_private->mydriver, clear_mode);

    mydriver_data_private->fb_size = 0;
    mydriver_data_private->count_mode = 0;
    mydriver_data_private->current_mode = 0;
    mydriver_data_private->status = 0;

    return 0;
};

static int mydriver_open(struct inode *inode, struct file *file)
{
    file->private_data  = kmemdup(mydriver_data_private, sizeof(*mydriver_data_private), GFP_KERNEL);
    if (!file->private_data)
        return -ENOMEM;
    return 0;
};

static ssize_t mydriver_read(struct file *file, char __user *buf, size_t count, loff_t *offset) {
    struct mydriver_data* drv = file->private_data;
    //unsigned long size = min(count, drv->fb_size);
    void __iomem *status_addr = drv->hwmem + REG_STATUS;
    while (ioread8(status_addr) != 1) {
        udelay(1000);
    }
    if (drv->fb_size < *offset + count) {
        count = drv->fb_size - *offset;
    }
    char* buffer = kmalloc(count, GFP_KERNEL);
    void* read_addr = drv->hwmem + REG_FRAMEBUFFER + *offset;
    for (unsigned long i = 0; i < count; i++) {
        buffer[i] = ioread8(read_addr + i);
        //drv->framebuffer[i + *offset] = ioread8(read_addr + i);
    }
    if (copy_to_user(buf, buffer, count)) {
        kfree(buffer);
        return -EFAULT;
    }
    kfree(buffer);
    *offset += count;
    return count;
}

size_t bar2_size = 0;
size_t param = 0;
int reserv = 0;
size_t real_offset = 0;
int ost_image = 0;
int padding = 0;
int count_pixel = 0;

static ssize_t mydriver_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    struct mydriver_data* drv = file->private_data;
    if (drv->status == 2) {
        return count;
    }
    void __iomem *status_addr = drv->hwmem + REG_STATUS;
    //u8 current_status = ioread8(status_addr);
    //printk(KERN_INFO "mydriver_write: status is %zu\n", current_status);
 //    if (current_status != 1) {
 //        printk(KERN_INFO "mydriver_write: status is %zu\n", ioread8(status_addr));
 //        return -EAGAIN;
 //    }
    while (ioread8(status_addr) != 1) {
        udelay(1000);
    }
    printk(KERN_INFO "mydriver_write: request %zu bytes, framebuffer size: %u bytes\n", count, drv->fb_size);
    int size = count;
    int i = 0;
    void __iomem *fb_addr = drv->hwmem + REG_FRAMEBUFFER;
    if (*offset == 0) {

        void __iomem *count_mode_addr = drv->hwmem + REG_MODES_COUNT;
        iowrite8(drv->count_mode, count_mode_addr);
        void __iomem *current_addr = drv->hwmem + REG_BUFFER_ADDR ;
        iowrite8(drv->current_mode, current_addr);
        void __iomem *mode_addr = drv->hwmem + REG_MODES_BUFFER;
        for (size_t j = 0; j < drv->count_mode; j++) {
            iowrite32(drv->buf_mode[j].height, mode_addr + j*8);
            iowrite32(drv->buf_mode[j].width, mode_addr + j*8 + 4);
        }
        int row_size = drv->buf_mode[drv->current_mode].width * 3;
        padding = (4 - (row_size % 4)) % 4;
        int row_padded = row_size + padding;
        int image_size = row_padded * drv->buf_mode[drv->current_mode].height;
        drv->fb_size = BMP_OFFSET + image_size;
        for (size_t j = 0; j < drv->fb_size; j++) {
            iowrite8(0, fb_addr + j);
        }
        param = 0;
        reserv = 0;
        size -= BMP_OFFSET;
        real_offset = 0;
        count_pixel = 0;
        i += BMP_OFFSET;
        ost_image = 0;
    }
    if ((drv->fb_size + BMP_OFFSET) < real_offset) {
        size = drv->fb_size - *offset;
        //printk(KERN_INFO "SIZE: %ld\n", size);
        if (size <= 0) {
            //printk(KERN_INFO "YES\n");
            *offset += count;
            return count;
        }
    }
    if (!drv->hwmem) {
        return -ENODEV;
    }
    pr_info("mydriver: write: hwmem=%p, count=%zu, offset=%lld\n", drv->hwmem, count, *offset);
    char* buffer = kmalloc(size, GFP_KERNEL);
    if (copy_from_user(buffer, buf, count) != 0) {
        kfree(buffer);
        return -EFAULT;
    }
    size_t max_offset = bar2_size - REG_FRAMEBUFFER;



    printk(KERN_INFO "OST IMAGE: %ld\n", ost_image);
    int n = (size + ost_image) / (WIDTH * 3);
    printk(KERN_INFO "N1: %ld\n", n);
    int ost = (size + ost_image) % (WIDTH * 3);

    int c = 0;
    size_t current_offset = 0;

    int min_width = min(WIDTH, drv->buf_mode[drv->current_mode].width);
    //int off = 0;
    int real_size = 0;
    printk(KERN_INFO "RESERV: %ld\n", reserv);
    printk(KERN_INFO "I begin: %ld\n", i);
    if (reserv == 0 && *offset != 0) {
        i += (WIDTH * 3 - ost_image);
        c++;
    }
    int copy_padding = 0;
    if (reserv != 0) {
        printk(KERN_INFO "REAL OFFSET11: %ld\n", real_offset);
        int counter = 0;
        while (counter != (min_width * 3)-reserv) {
            if (real_offset >= max_offset) {
                return -EIO;
            }
            iowrite8(buffer[i], fb_addr+ real_offset);
            counter++;
            real_offset++;
            count_pixel++;
            copy_padding = padding;
            if (count_pixel % (min_width * 3) == 0) {
                while (copy_padding != 0) {
                    iowrite8(0, fb_addr+ real_offset);
                    copy_padding--;
                    real_offset++;
                }
            }
            i++;
        }
        c++;
        if (WIDTH > drv->buf_mode[drv->current_mode].width) {i = WIDTH * 3 - reserv;}
        if (WIDTH < drv->buf_mode[drv->current_mode].width) {
            int diff = (drv->buf_mode[drv->current_mode].width - WIDTH) * 3;
            while (diff != 0) {
                iowrite8(0, fb_addr + real_offset);
                diff--;
                real_offset++;
                count_pixel++;
                copy_padding = padding;
                if (count_pixel % (min_width * 3) == 0) {
                    while (copy_padding != 0) {
                        iowrite8(0, fb_addr+ real_offset);
                        copy_padding--;
                        real_offset++;
                    }
                }
            }
        }
    }
    printk(KERN_INFO "I full: %ld\n", i);
    printk(KERN_INFO "N: %ld\n", n);
    while (c < n) {
        printk(KERN_INFO "i in while: %ld\n", i);
        int counter = 0;
        while (counter != (min_width * 3) && real_offset < drv->fb_size) {
            iowrite8(buffer[i], fb_addr+ real_offset);
            i++;
            counter++;
            real_offset++;
            count_pixel++;
            copy_padding = padding;
            if (count_pixel % (min_width * 3) == 0) {
                while (copy_padding != 0) {
                    iowrite8(0, fb_addr+ real_offset);
                    copy_padding--;
                    real_offset++;
                }
            }
        }
        if (WIDTH > drv->buf_mode[drv->current_mode].width) {
            i += (WIDTH - drv->buf_mode[drv->current_mode].width) * 3;
        }
        c++;
        int diff = (drv->buf_mode[drv->current_mode].width - WIDTH) * 3;
//        printk(KERN_ERR "DIFF: %lld I:%lld\n", diff, i);
//
        if (drv->buf_mode[drv->current_mode].width > WIDTH) {
            printk("YES\n");
            param += diff;
            while (diff != 0) {
                iowrite8(0, fb_addr + real_offset);
                diff--;
                real_offset++;
                count_pixel++;
                copy_padding = padding;
                if (count_pixel % (min_width * 3) == 0) {
                    while (copy_padding != 0) {
                        iowrite8(0, fb_addr+ real_offset);
                        copy_padding--;
                        real_offset++;
                    }
                }
            }
        }

        //c++;
    }
    int k = 0;
    ost_image = ost;
    int new_n = ost / (drv->buf_mode[drv->current_mode].width * 3);
    printk(KERN_INFO "I full ost: %ld\n", i);
    //int copy_new_n = new_n;
    while (new_n != 0 && real_offset < drv->fb_size) {
        printk(KERN_INFO "REAL OFFSET: %ld\n", real_offset);
        printk(KERN_INFO "OCT NEW_N: %ld\n", new_n);
        ost = 0;
        int counter = 0;
        while (counter != (min_width * 3) && real_offset < drv->fb_size) {
            iowrite8(buffer[i], fb_addr + real_offset);
            counter++;
            i++;
            real_offset++;
            count_pixel++;
            copy_padding = padding;
            if (count_pixel % (min_width * 3) == 0) {
                while (copy_padding != 0) {
                    iowrite8(0, fb_addr+ real_offset);
                    copy_padding--;
                    real_offset++;
                }
            }
        }
        if (WIDTH > drv->buf_mode[drv->current_mode].width) {
            i += (WIDTH - drv->buf_mode[drv->current_mode].width) * 3;
        }
        new_n--;
    }
    reserv = ost;
    printk(KERN_INFO "i ost: %lld\n", i);
    printk(KERN_INFO "reserv: %lld\n", reserv);
    printk(KERN_INFO "REAL OFFSET: %ld\n", real_offset);
    while (k != ost && real_offset < drv->fb_size) {
        if (real_offset >= max_offset) {
                //printk(KERN_ERR "Offset %lld exceeds BAR2 size\n", *offset);
                return -EIO;
        }
        iowrite8(buffer[i], fb_addr + real_offset);
        real_size++;
        real_offset++;
        count_pixel++;
        copy_padding = padding;
        if (count_pixel % (min_width * 3) == 0) {
            while (copy_padding != 0) {
                iowrite8(0, fb_addr+ real_offset);
                copy_padding--;
                real_offset++;
            }
        }

        k++;
        i++;
 //        if (k == drv->buf_mode[drv->current_mode].width) {
 //            count_ost--;
 //        }
    }
//    copy_padding = padding;
//    if (real_offset % (min_width * 3) == 0) {
//        while (copy_padding != 0) {
//        iowrite8(0, fb_addr+ real_offset);
//        copy_padding--;
//        real_offset++;
//        }
//    }
    if (*offset == 0) {
        void __iomem *count_mode_addr = drv->hwmem + REG_MODES_COUNT;
        iowrite32(drv->count_mode, count_mode_addr);
 //        void __iomem *status_addr = drv->hwmem + REG_STATUS;
 //        iowrite32(drv->status, status_addr);
        void __iomem *current_addr = drv->hwmem + REG_BUFFER_ADDR ;
        iowrite32(drv->current_mode, current_addr);
        void __iomem *mode_addr = drv->hwmem + REG_MODES_BUFFER;
        for (size_t i = 0; i < drv->count_mode; i++) {
            iowrite32(drv->buf_mode[i].height, mode_addr + i*8);
            iowrite32(drv->buf_mode[i].width, mode_addr + i*8 + 4);
        }
    }
 //    if (drv->framebuffer) {
 //        memcpy(drv->framebuffer + *offset, buffer, size);
 //    }
    kfree(buffer);
    if (*offset == 0) {
        size += BMP_OFFSET;
    }
    printk(KERN_INFO "REAL SIZE: %ld\n", real_size);
    *offset += size;
    if (real_offset >= drv->fb_size || *offset >= WIDTH * HEIGHT * 3) {
        iowrite8(2, status_addr);
        drv->status = 2;
    }
    return size;
}


static long mydriver_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mydriver_data* drv = file->private_data;
    void __iomem *status_addr = drv->hwmem + REG_STATUS;
    while (ioread8(status_addr) != 1) {
        udelay(1000);
    }
    switch (cmd) {
        case SET_COUNT: {
            int new_count;
            if (copy_from_user(&new_count, (void __user *)arg, sizeof(new_count))) {
                return -EFAULT;
            }
            if (new_count == drv->count_mode) {
                break;
            }
            if (new_count <= 0) {
                return -EINVAL;
            }

            if (drv->buf_mode) {
                kfree(drv->buf_mode);
                drv->buf_mode = NULL;
            }
            drv->buf_mode = kmalloc(new_count * sizeof(struct struct_mode), GFP_KERNEL);
            if (!drv->buf_mode) {
                drv->count_mode = 0;
                return -ENOMEM;
            }
            drv->count_mode = new_count;
            break;
        }
        case GET_MODES:
            if (drv->count_mode <= 0) {
                printk(KERN_INFO "ERROR! Set count of mode!\n");
                break;
            }
            if (copy_from_user(drv->buf_mode, (void __user *)arg, sizeof(struct_mode) * drv->count_mode)) {
                return -EFAULT;
            }
            for (int i = 0; i < drv->count_mode; i++) {
                printk(KERN_INFO "Mode %d: %dx%d\n", i,
                        drv->buf_mode[i].width, drv->buf_mode[i].height);
            }
            break;

        case SET_MODE:
            int mode_index;
            if (copy_from_user(&mode_index, (void __user *)arg, sizeof(mode_index))) {
                //ret = -EFAULT;
                break;
            }
            if (mode_index >= drv->count_mode) {
                //ret = -EINVAL;
                break;
            }
            drv->current_mode = mode_index;
            drv->fb_size = drv->buf_mode[mode_index].width * drv->buf_mode[mode_index].height * 3;
            drv->status = 1;

            break;
        case GET_CURRENT:
            u32 curr = drv->current_mode;

            if (copy_to_user((void __user *)arg, &curr, sizeof(curr))) {
                //ret = -EFAULT;
            }
            break;
        case CLEAR_SCREEN:
            void __iomem *fb_addr = drv->hwmem + REG_FRAMEBUFFER;
            printk(KERN_INFO "FB_SIZE: %ld\n", drv->fb_size);
            void __iomem *count_mode_addr = drv->hwmem + REG_MODES_COUNT;
            iowrite8(drv->count_mode, count_mode_addr);
            void __iomem *current_addr = drv->hwmem + REG_BUFFER_ADDR ;
            iowrite8(drv->current_mode, current_addr);
            void __iomem *mode_addr = drv->hwmem + REG_MODES_BUFFER;
            for (size_t j = 0; j < drv->count_mode; j++) {
                iowrite32(drv->buf_mode[j].width, mode_addr + j*8);
                iowrite32(drv->buf_mode[j].height, mode_addr + j*8 + 4);
            }
            int count_i = 0;
            for (size_t i = 0; i < drv->fb_size; i++) {
                iowrite8(0, fb_addr + i);
                count_i++;
            }
            iowrite8(2, status_addr);
            printk(KERN_INFO "STATUS: %ld\n", ioread8(status_addr));
            printk(KERN_INFO "COUNT I: %ld\n", count_i);
            break;
        default:
            return -EINVAL;
    }
    //iowrite8(1, status_addr);
    return 0;
}

static int mydriver_release(struct inode *inode, struct file *file) {
    struct mydriver_data* priv = file->private_data;
    kfree(priv);
    priv = NULL;
    return 0;
}

int destroy_char_devs(void)
{
    device_remove_file(mydriver_data_private->mydriver, &dev_attr_count_mode);
    device_remove_file(mydriver_data_private->mydriver, &dev_attr_buf_mode);
    device_remove_file(mydriver_data_private->mydriver, &dev_attr_current_mode);
    device_remove_file(mydriver_data_private->mydriver, &dev_attr_clear_mode);

    device_destroy(mydriverclass, MKDEV(dev_major, 0));

    class_unregister(mydriverclass);
    class_destroy(mydriverclass);
    unregister_chrdev_region(MKDEV(dev_major, 0), MINORMASK);

    return 0;
}

static int __init mypci_driver_init(void)
{
	return pci_register_driver(&mydriver);
}

static void __exit mypci_driver_exit(void)
{
	pci_unregister_driver(&mydriver);
}

void release_device(struct pci_dev *pdev)
{
    pci_release_region(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
    pci_disable_device(pdev);
}

static int mydriver_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int bar, err;
    u16 vendor, device;
    unsigned long mmio_start, mmio_len;
    struct mydriver_data *drv_data;
    if (!pdev) {
        printk(KERN_ERR "pdev is NULL\n");
        return -ENODEV;
    }
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);

    printk(KERN_INFO "Device vid: 0x%X  pid: 0x%X\n", vendor, device);

    bar = pci_select_bars(pdev, IORESOURCE_MEM);
    if (!(bar & MYDRIVER_BAR_MASK))
        return -ENODEV;

    err = pci_enable_device_mem(pdev);

    if (err)
        return err;

    err = pci_request_region(pdev, bar, "mydriver");

    if (err) {
        pci_disable_device(pdev);
        return err;
    }

    mmio_start = pci_resource_start(pdev, MYDRIVER_BAR_NUM);
    mmio_len = pci_resource_len(pdev, MYDRIVER_BAR_NUM);
    bar2_size = mmio_len;

    drv_data = kzalloc(sizeof(struct mydriver_data), GFP_KERNEL);

    if (!drv_data) {
        release_device(pdev);
        return -ENOMEM;
    }

    drv_data->hwmem = ioremap(mmio_start, mmio_len);
    //drv_data->data_len = mmio_len;

    if (!drv_data->hwmem) {
        release_device(pdev);
        return -EIO;
    }
    //drv_data->addr_buf_mode = readq(drv_data->hwmem);
    //drv_data->data_offset = drv_data->addr_buf_mode - mmio_start;
//    drv_data->data_offset = sizeof(struct mydriver_data) +
//                       (drv_data->count_mode * sizeof(struct_mode));
//    drv_data->buf_mode = kmalloc(3 * sizeof(struct_mode), GFP_KERNEL);
//    drv_data->buf_mode[0] = (WIDTH, 480);
//    drv_data->buf_mode[1] = (800, 600);
//    drv_data->buf_mode[2] = (1024, 768);
//    drv_data->current_mode = 3;
//    drv_data->status = 1;
    //void __iomem *bar2_data = drv_data->hwmem;
    //drv_data->count_mode = ioread32(bar2_data);

    //drv_data->fb_size = WIDTH * 480 * 4;
//    drv_data->count_mode = ioread32(bar2_data);
//    drv_data->addr_struct_mode = readq(bar2_data);
//    drv_data->addr_buf_mode = readq(bar2_data);
//    drv_data->status = ioread8(bar2_data);
//    drv_data->buf_mode = ioread8(bar2_data);
//    drv_data->framebuffer = ioread8(bar2_data);
//    drv_data->current_mode = 0;
    //drv_data->fb_size = drv_data->buf_mode[drv_data->current_mode].width
            //* drv_data->buf_mode[drv_data->current_mode].height * 3;

    printk(KERN_INFO "MYDRIVER mapped resource 0x%lx to 0x%p\n", mmio_start, drv_data->hwmem);

    create_char_devs(drv_data);

    pci_set_drvdata(pdev, drv_data);

    return 0;
}

static void mydriver_remove(struct pci_dev *pdev)
{
    struct mydriver_data *drv_data = pci_get_drvdata(pdev);

    destroy_char_devs();

//    if (drv_data->buf_mode) {
//        kfree(drv_data->buf_mode);
//        drv_data->buf_mode = NULL;
//    }
//    drv_data->count_mode = 0;
//    drv_data->current_mode = 0;
    if (mydriver_data_private) {
        if (mydriver_data_private->buf_mode) {
            kfree(mydriver_data_private->buf_mode);
            mydriver_data_private->buf_mode = NULL;
        }
        if (mydriver_data_private->framebuffer) {
            kfree(mydriver_data_private->framebuffer);
            mydriver_data_private->framebuffer = NULL;
        }
    }

    if (drv_data) {
        if (drv_data->hwmem)
            iounmap(drv_data->hwmem);
        //kfree(drv_data->framebuffer);
        kfree(drv_data);
    }
    mydriver_data_private = NULL;

    release_device(pdev);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Makarova <juliamakarowaa@yandex.ru>");
MODULE_DESCRIPTION("Example of a driver for a QEMU PCI-testdev device");
MODULE_VERSION("0.1");


module_init(mypci_driver_init);
module_exit(mypci_driver_exit);
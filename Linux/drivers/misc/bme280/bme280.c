#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>

#include "bme280.h"


/* 
 * The structure to represent 'bme_dev' devices.
 *  data - data buffer;
 *  buffer_size - size of the data buffer;
 *  block_size - maximum number of bytes that can be read or written 
 *    in one call;
 *  bme_mutex - a mutex to protect the fields of this structure;
 *  cdev - character device structure.
 */
struct bme_dev {
	unsigned char *data;
	struct i2c_client *client;
	struct mutex bme_mutex;
	struct cdev cdev;
};

#define BME_DEVICE_NAME     "bme280_misc"
#define BME_PAGE_SIZE       128
#define BME_SIZE           	32 //To output the string upto a maximum of 32bytes 

static unsigned int bme_major = 0;
static unsigned int minor = 0;
static struct class *bme_class = NULL;


int bme_open(struct inode *inode, struct file *filp)
{
	struct bme_dev *dev = NULL;
	dev = container_of(inode->i_cdev, struct bme_dev, cdev);
	
	if (dev == NULL){
	    pr_err("Container_of did not found any valid data\n");
		return -ENODEV; /* No such device */
	}
    
	/* store a pointer to struct bme_dev here for other methods */
    filp->private_data = dev;

    if (inode->i_cdev != &dev->cdev){
        pr_err("Device open: internal error\n");
        return -ENODEV; /* No such device */
    }

    dev->data = (unsigned char*)kzalloc(BME_SIZE, GFP_KERNEL);
    if (dev->data == NULL){
        pr_err("Error allocating memory\n");
        return -ENOMEM;
    }
    return 0;
}

/*
 * Release is called when device node is closed
 */
int bme_release(struct inode *inode, struct file *filp)
{
    struct bme_dev *dev = filp->private_data;
    if (dev->data != NULL){
        kfree(dev->data);
        dev->data = NULL ;
    }
    return 0;
}

int bme280_write(struct i2c_client *client,
        u8 reg_addr, 
		unsigned char data)
{    
    struct i2c_msg msg[1];
    ssize_t retval = 0;

    msg[0].addr = client->addr;
    msg[0].flags = 0;   /* Write */
    msg[0].len = 1; 	/* Address is 2bytes coded */
    msg[0].buf = &data;

    if (i2c_transfer(client->adapter, msg, 1) < 0){
        pr_err("bme280: i2c_transfer for write failed\n");  
        return -1;
     }
	
	return retval;  
}

ssize_t bme280_read(struct i2c_client *client, 
		u8 reg_addr, 
		unsigned char *data)
{
	struct i2c_msg msg[2];
    ssize_t retval = 0;

	msg[0].addr = client->addr;
	msg[0].flags = 0;                    /* Write */
	msg[0].len = 1;                      /* Address is 1byte coded */
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;    //client->addr;
	msg[1].flags = I2C_M_RD;            // We need to read
	msg[1].len = 1; 					// count
	msg[1].buf = data;
	
	if (i2c_transfer(client->adapter, msg, 2) < 0) {		
		pr_err("bme280: i2c_transfer for read failed\n");
		return -ENXIO;
	}

	return retval;
}

ssize_t bme280_read_temperature(struct bme_dev *dev)
{
	unsigned char data;
	unsigned int value;
	ssize_t retval = 0;

	if (bme280_read(dev->client, BME280_REGISTER_TEMPDATA_MSB, &data) < 0) {
		return -ENXIO;
	}

	value = (unsigned int)((data & 0xFF) << 12);

	if (bme280_read(dev->client, BME280_REGISTER_TEMPDATA_LSB, &data) < 0) {
		return -ENXIO;
	}

	value = value | (unsigned int)((data & 0xFF) << 4);

	if (bme280_read(dev->client, BME280_REGISTER_TEMPDATA_XLSB, &data) < 0) {
		return -ENXIO;
	}

	value = value | (unsigned int)(data & 0xF);

	memcpy(dev->data, &value, sizeof(unsigned int));

	return retval;
}

ssize_t  bme_read(struct file *filp, char __user *buf,
                    size_t count, loff_t *f_pos)
{
	ssize_t bufsize;
	char *value_buf = NULL;
    struct bme_dev *dev = filp->private_data;
    ssize_t retval = 0;

    if (mutex_lock_killable(&dev->bme_mutex)) {
        return -EINTR;
	}

    if (*f_pos >= BME_SIZE) { /* EOF */
        goto end_read;
	}

    if (count > BME_SIZE) {
		count = BME_SIZE;
	}

	memset(dev->data, 0, BME_SIZE);

	if (bme280_read_temperature(dev) < 0) {
        retval = -EIO;
        goto end_read;
	}
	
	bufsize = snprintf(NULL, 0, "%d,\n", *((unsigned int*)dev->data));

	value_buf = kmalloc(bufsize + 1, GFP_KERNEL);
	if (value_buf == NULL) {
	    retval = -ENOMEM;
        goto end_read;
	}

	sprintf(value_buf, "%d,\n", *((unsigned int*)dev->data));
    
	if(copy_to_user(buf, value_buf, bufsize) != 0){
        retval = -EIO;
        goto end_read;
    }

    retval = bufsize;
end_read:
	if (value_buf)
		kfree(value_buf);
    mutex_unlock(&dev->bme_mutex);
    return retval;
}

struct file_operations bme_fops = {
	.owner =    THIS_MODULE,
	.read =     bme_read,
	.open =     bme_open,
	.release =  bme_release,
};

static const struct of_device_id bme280_of_ids[] = {
	{ .compatible = "bosch,bme280", },
	{ }
};

bool is_reading_calibration(struct i2c_client *client)
{
	unsigned char data;

	if (bme280_read(client, BME280_REGISTER_STATUS, &data) < 0) {
		return -ENXIO;
	}

	return (data & 1) == 1;
}

void bme280_set_sampling(struct i2c_client *client)
{
	// Sensor should be in sleep mode before setting config
	bme280_write(client, BME280_REGISTER_CONTROL, MODE_SLEEP);

	bme280_write(client, BME280_REGISTER_CONTROLHUMID, 0);
	bme280_write(client, BME280_REGISTER_CONFIG, 0xC); //filter 8, inactive .5ms
	bme280_write(client, BME280_REGISTER_CONTROL, 0x92); //forced mode, temp and pressure oversampling  
}

static int bme280_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
    unsigned char data;
    int err = 0;
    dev_t devno = 0;
    struct bme_dev *bme_device = NULL;
    struct device *device = NULL;
	int reset_check = 0;
        
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        return -EIO;

   /* 
    * We send a simple i2c transaction to read the chip id before proceeding.
    */
	if (bme280_read(client, BME280_REGISTER_CHIPID, &data) < 0) {
        pr_err("bme280: failed to read device id\n");
		return -ENXIO;
	}
	
	if (data != 0x60) {
    	pr_err("bme280: read device id 0x%x instead of 0x60\n", data & 0xFF);
		return -ENODEV;
	}

	/* Write 0xB6 in 0xE0 register for soft-reset */
	if (bme280_write(client, BME280_REGISTER_SOFTRESET, 0xB6) < 0) {
        pr_err("Error while trying soft-reset %s", BME_DEVICE_NAME);
        return -ENXIO;
	}

	// Wait for chip to wake up.
	mdelay(1 * 1000);	

	// If chip is still reading calibration, delay
	while (is_reading_calibration(client)) {
		mdelay(1 * 1000);
		if (reset_check++ > 10) {
			pr_err("Error reading calibration %s", BME_DEVICE_NAME);
			return -ENXIO;
		}
	}

	bme280_set_sampling(client);
	
    /* Get a range of minor numbers (starting with 0) to work with */
    err = alloc_chrdev_region(&devno, 0, 1, BME_DEVICE_NAME);
    if (err < 0) {
        pr_err("alloc_chrdev_region() failed for %s\n", BME_DEVICE_NAME);
        return err;
    }
    bme_major = MAJOR(devno);

    /* Create device class */
    bme_class = class_create(THIS_MODULE, BME_DEVICE_NAME);
    if (IS_ERR(bme_class)) {
        err = PTR_ERR(bme_class);
        goto fail;
    }

    bme_device = (struct bme_dev *)kzalloc(sizeof(struct bme_dev), GFP_KERNEL);
    if (bme_device == NULL) {
        err = -ENOMEM;
        goto fail;
    }

    /* Memory is to be allocated when the device is opened the first time */
    bme_device->data = NULL;
    bme_device->client = client;
    mutex_init(&bme_device->bme_mutex);

    cdev_init(&bme_device->cdev, &bme_fops);
    bme_device->cdev.owner = THIS_MODULE;
    err = cdev_add(&bme_device->cdev, devno, 1);

    if (err){
        pr_err("Error while trying to add %s", BME_DEVICE_NAME);
        goto fail;
    }

    device = device_create(bme_class, NULL, /* no parent device */
                            devno, NULL, /* no additional data */
                            BME_DEVICE_NAME);

    if (IS_ERR(device)) {
        err = PTR_ERR(device);
        pr_err("failure while trying to create %s device", BME_DEVICE_NAME);
        cdev_del(&bme_device->cdev);
        goto fail;
    }

    i2c_set_clientdata(client, bme_device);
    return 0; /* success */

fail:
    if(bme_class != NULL){
        device_destroy(bme_class, MKDEV(bme_major, minor));
        class_destroy(bme_class);
    }
    if (bme_device != NULL)
        kfree(bme_device);
    return err;
}

static int bme280_remove(struct i2c_client *client)
{
    struct bme_dev *bme_device = i2c_get_clientdata(client);
    device_destroy(bme_class, MKDEV(bme_major, 0));

    kfree(bme_device->data);
    mutex_destroy(&bme_device->bme_mutex);
    kfree(bme_device);
    class_destroy(bme_class);
    unregister_chrdev_region(MKDEV(bme_major, 0), 1);
	return 0;
}

static const struct i2c_device_id bme280_id[] = {
	{"bme280", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bme280_id);

static struct i2c_driver bme_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "bme280",
		.of_match_table = of_match_ptr(bme280_of_ids),
	},
	.probe = bme280_probe,
	.remove = bme280_remove,
	.id_table = bme280_id,
};

module_i2c_driver(bme_i2c_driver);

MODULE_AUTHOR("Jeevan Sam <jeevansam@gmail.com>");
MODULE_LICENSE("GPL");

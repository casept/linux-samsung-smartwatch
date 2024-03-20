/* Copyright Notice
 * ================
 * This file is part of the fpga_prog linux kernel module.
 * It is subject to the license terms in the LICENSE.txt
 * file found in the top-level directory of this distribution and at
 * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 *
 * No part of the software, including this file, may be copied, modified,
 * propagated, or distributed except according to the terms contained in
 * the LICENSE.txt file.
 *
 * Till Straumann <till.straumann@alumni.tu-berlin.de>, 2016-2023
 */

/*
 * 'Glue' driver to use FPGA-manager without device-tree overlays etc.
 *
 * This driver can be used in two ways:
 *
 *   1. together with device-tree definition
 *   2. w/o device-tree modification
 *
 * 1. Device-tree
 *
 * In the first use case a device-tree entry must be created (at the top-level, i.e.,
 * the 'platform bus' level):
 *
 *  prog_fpga0: prog-fpga0 {
 *      compatible = "tills,fpga-programmer-1.0";
 *      fpga-mgr   = <&devcfg>;     # reference must point to the fpga controller (e.g., zynq devcfg)
 *      file       = "zzz.bin";     # optional firmware file name (must be present in one of the directories
 *                                  # automatically searched by the kernel or in a path as given in
 *                                  # /sys/module/firmware_class/parameters/path
 *      autoload   = 1;             # when autoload is nonzero then firmware is loaded when the driver
 *                                  # is bound or whenever the 'file' property is written in sysfs
 *                                  # (see below). If autoload is '0' then you must explicitly write
 *                                  # to the 'program' property in sysfs (see below).
 *  };
 *
 *
 * When the driver is bound then it will add a few sysfs properties to the device
 *
 *   /sys/bus/platform/devices/prog-fpga0/
 *
 *    file:     name of firmware file; if 'autoload' is nonzero then writing a filename
 *              to 'file' triggers programming.
 *
 *    autoload: whether binding the driver or writing 'file' triggers programming
 *
 *    program:  writing nonzero here triggers programming (required if autoload is zero)
 *
 * The device-tree use-case allows to automatically load a default firmware file during
 * boot-up.
 *
 * 2. No device-tree
 *
 * This driver can also be used without a modified device tree. In this case, the user must
 * instruct the driver to create 'soft' devices which replace the auto-generated ones that
 * the kernel builds when processing the device tree.
 *
 * You can simply create a soft device by writing the device-tree path to the controller
 * to the driver's 'add_programmer' property.
 * 
 * Note that the controller itself still must be defined in the device tree. E.g., for
 * zynq we have (on amba bus):
 *
 *    amba: amba {
 * 
 *      ...
 *
 *      devcfg: devcfg@f8007000 {
 *          compatible = "xlnx,zynq-devcfg-1.0";
 *          reg = <0xf8007000 0x100>;
 *          interrupt-parent = <&intc>;
 *          interrupts = <0 8 4>;
 *          clocks = <&clkc 12>;
 *          clock-names = "ref_clk";
 *          syscon = <&slcr>;
 *      };
 *   };
 *  
 * Thus the device-tree path to the controller is '/amba/devcfg@f8007000' and
 *
 *   echo -n '/amba/devcfg@f8007000' > /sys/bus/platform/driver/fpga_programmer/add_programmer
 *
 * generates a new device (note that the naming is slightly different than in use case 1.)
 *
 *   /sys/bus/platform/devices/prog-fpga.0/
 *
 * which is bound to the driver and features the same 'file', 'autoload' etc. properties.
 *
 * Soft devices can be removed (write nonzero to 'remove').
 *
 * PROGRAMMING (identical for use case 1. and 2.):
 *
 *  E.g.:
 *
 *      echo -n '/' > /sys/module/firmware_class/parameters/path
 *
 *      echo -n '/mnt/somewhere/something.bin' > /sys/bus/platform/devices/prog-fpga.0/file
 * 
 */
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/version.h>

MODULE_LICENSE("Dual BSD/GPL");

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,16,1)
#define FW_NAME firmware_name
#else
#define FW_NAME info.firmware_name
#define HAS_NEW_API
#endif

/* Forward Declarations
 */
struct fpga_prog_drvdat;

static struct platform_driver fpga_prog_driver;

static ssize_t
add_programmer_store(struct device_driver *drv, const char *buf, size_t sz);

static ssize_t
remove_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz);

static ssize_t
file_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz);
static ssize_t
file_show(struct device *dev, struct device_attribute *att, char *buf);

static ssize_t
program_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz);

static ssize_t
autoload_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz);
static ssize_t
autoload_show(struct device *dev, struct device_attribute *att, char *buf);

static int
fpga_prog_probe(struct platform_device *pdev);
static int
fpga_prog_remove(struct platform_device *dev);

static struct fpga_prog_drvdat *
get_drvdat(struct device *dev);

static void
release_drvdat(struct fpga_prog_drvdat *prg);

static void
release_pdev(struct device *dev);

static int
load_fw(struct fpga_prog_drvdat *prg);


#define OF_COMPAT "tills,fpga-programmer-1.0"

static const char *drvnam = "prog-fpga";

static DEFINE_IDA(fpga_prog_ida);


DRIVER_ATTR_WO( add_programmer );

DEVICE_ATTR_WO( remove   );
DEVICE_ATTR_RW( file     );
DEVICE_ATTR_WO( program  );
DEVICE_ATTR_RW( autoload );

static struct device_attribute *dev_attrs[] = {
	&dev_attr_program,
	&dev_attr_file,
	&dev_attr_autoload,
};

#define N_DEV_ATTRS (sizeof(dev_attrs)/sizeof(dev_attrs[0]))

/* 'Soft' programmer device - used if we don't have a device-tree entry
 */
struct fpga_prog_dev {
	struct platform_device pdev;
    /* Keep a reference to the fpga_manager's of_node;
	 * we hold it for the lifetime of this device
	 */
	struct device_node    *mgrNode;
};

/* Programmer data
 */
struct fpga_prog_drvdat {
	struct platform_device *pdev;
    /* Keep a reference to the fpga_manager's of_node;
	 * we hold while the driver is attached/bound
	 */
	struct device_node     *mgrNode;
	int                    autoload;
#if !defined(HAS_NEW_API)
	char                   *firmware_name;
#endif
	/* Info data for the manager
	 */
	struct fpga_image_info info;
};

/* Retrieve the fpga_manager associated with a platform device.
 *
 * RETURNS: - the manager (exclusive ref; must be released after use)
 *          - a reference to the manager's of node (in *mgrNode)
 *
 * If this routine fails then no reference (neither to the manager nor of-node)
 * is held. On error, a status is encoded in the pointer return value.
 *
 * There are two cases (correponding to the two use cases described above):
 *
 *  1. the pdev has a of_node -> there is a devtree node and its 'fpga-mgr'
 *     property must point to the fpga controller. We use that to obtain
 *     the reference to the manager.
 *  2. the pdev has no of_node -> it is a 'soft' created device by this driver.
 *     The device has a reference to the manager's of_node which we use to
 *     obtain the manager itself.
 */

static struct fpga_manager*
of_get_mgr_from_pdev(struct platform_device *pdev, struct device_node **mgrNode)
{
struct device_node   *pnod = pdev->dev.of_node;
struct device_node   *mnod = 0;
struct fpga_manager  *mgr  = ERR_PTR( -ENODEV );
struct fpga_prog_dev *prgd;

	if ( ! pnod ) {
		/* this is a fpga_prog_dev (run-time created; no OF) */
		prgd = container_of( pdev, struct fpga_prog_dev, pdev );
		mnod = prgd->mgrNode;
		/* increment ref-count */
		of_node_get( mnod );
	} else {
		/*  we have an of_node; retrieve 'fpga-mgr'... */
		of_node_get( pnod );
			if ( of_device_is_compatible(pnod, OF_COMPAT) ) {
				mnod = of_parse_phandle(pnod, "fpga-mgr", 0);
			}
		of_node_put( pnod );
	}
	if ( mnod ) {
		mgr = of_fpga_mgr_get( mnod );
		if ( IS_ERR( mgr ) ) {
			of_node_put( mnod );
			mnod = 0;
		}
	} else {
		printk( KERN_ERR "%s: invalid or missing 'fpga-mgr' property\n", drvnam);
		mgr = ERR_PTR( -EINVAL );
	}
	/* at this point we have either a valid manager + its OF node
	 * (also with incremented ref-count) or mknod == NULL and an error
	 * code in 'mgr'.
	 */
	*mgrNode = mnod;
	return mgr;
}


/* Retrieve an fpga_manager from its OF path (this is the device-tree path; not 
 * the sysfs path!)
 *
 * RETURNS: - the manager (exclusive ref; must be released after use)
 *          - a reference to the manager's of node (in *mgrNode)
 *
 * If this routine fails then no reference (neither to the manager nor of-node)
 * is held. On error, a status is encoded in the pointer return value.
 *
 */

static struct fpga_manager *
of_get_mgr_from_path(const char *path, struct device_node **mgrNode)
{
struct device_node  *mnod = of_find_node_by_path( path );
struct fpga_manager *mgr  = ERR_PTR( -ENOENT );

	if ( mnod ) {

		mgr = of_fpga_mgr_get( mnod );

		if ( IS_ERR( mgr ) ) {
			of_node_put( mnod );
			mnod = 0;
		}
	}

	*mgrNode = mnod;
	return mgr;
}

/* Helper to locate a matching device which
 * is already using the fpga_manager with of-node 'data'
 */
static int
cmp_mgr_node(struct device *dev, const void *data)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );

	return (const void *)prg->mgrNode == data;
}

/* Helper to locate a matching soft device which
 * is already using the fpga_manager with of-node 'data'.
 *
 * Since it may be used on other devices we must ensure
 * that the device we use for comparison is indeed
 * one of ours - we do this by checking it's 'release'
 * member.
 */
static int
cmp_release_func(struct device *dev, void *data)
{
struct fpga_prog_dev *prgd;
	if ( dev->release != release_pdev )
		return 0;

	prgd = container_of( dev, struct fpga_prog_dev, pdev.dev );

	return (void*)prgd->mgrNode == data;
}

/* Load firmware using the fpga_manager
 */
static int
load_fw(struct fpga_prog_drvdat *prg)
{
struct fpga_manager   *mgr;
int                    err;

	if ( ! prg->FW_NAME )
		return -EINVAL;

	mgr = of_fpga_mgr_get( prg->mgrNode );

	if ( IS_ERR( mgr ) ) {
		err = PTR_ERR( mgr );
	} else {
#if defined(HAS_NEW_API)
		err = fpga_mgr_load( mgr, &prg->info );
#else
		err = fpga_mgr_firmware_load( mgr, &prg->info, prg->FW_NAME );
#endif
		fpga_mgr_put( mgr );
	}

	return err;
}

/* Release private data associated with our
 * 'soft' device (fpga_prog_dev)
 */
static void
release_pdev(struct device *dev)
{
struct fpga_prog_dev *pdev = container_of( dev, struct fpga_prog_dev, pdev.dev );

	of_node_put( pdev->mgrNode );
	ida_simple_remove( &fpga_prog_ida, dev->id );
	kfree( dev );

	/* We hold on to this module for as long as any soft device exists
	 */
	module_put( THIS_MODULE );
}

/* Create a soft programmer device (use case 2.)
 *
 * On call: 'mgr'     -> fpga_manager with exclusive reference held
 *          'mgrNode' -> manager's of-node with refcnt incremented
 *
 * RETURN:
 *   On success (0): - the platform device was successfully added to the
 *                     system and is managed by it now.
 *                   - the reference count of 'THIS_MODULE' is incremented
 *   On failure    : - negative error status is returned
 *
 *   In any case (error or success):
 *                   - the fpga_manager is released (put)
 *                   - the reference count of 'mgrNode' is 'taken over'
 *                     (ref either held by device or dropped).
 */
static int
create_pdev(struct fpga_manager *mgr, struct device_node *mgrNode)
{
struct fpga_prog_dev   *prgd = 0;
struct fpga_prog_dev   *mem  = 0;
int                     id   = -1;
int                     stat;
int                     mgrPut = 0;

	if ( ! ( prgd = kzalloc( sizeof(*prgd), GFP_KERNEL ) ) ) {
		stat = -ENOMEM;
		goto bail;
	}

	mem = prgd;

	prgd->pdev.name = "prog-fpga";

	id = ida_simple_get( &fpga_prog_ida, 0, 0, GFP_KERNEL );
	if ( id < 0 ) {
		stat = id;
		goto bail;
	}

	prgd->pdev.id           = mgr->dev.id;
	prgd->pdev.dev.id       = id;
	prgd->pdev.dev.release  = release_pdev;

	prgd->mgrNode           = mgrNode;

	/* must 'put' the manager before registering the device
	 * because this may trigger the driver to be bound which
	 * then will need the manager. Avoid deadlock by releasing
	 * here. (The manager can only be held exclusivly.)
	 */
	fpga_mgr_put( mgr );
	mgrPut = 1;

	/* If the device can be successfully registered then
	 * we must not remove this module while it is in use.
	 */
	__module_get( THIS_MODULE );

	stat = platform_device_register( &prgd->pdev );
	if ( stat ) {
		module_put( THIS_MODULE );
		goto bail;
	}

	/* After this point 'platform_device_unregister()' should take care
	 * of the module, mgrNode, id and memory.
	 */

	mgrNode = 0;
	mem     =  0;
	id      = -1;

	/* Add a property that allows for the user to hot-unplug this device
	 */
	if ( (stat = device_create_file( &prgd->pdev.dev, &dev_attr_remove )) ) {
		platform_device_unregister( &prgd->pdev );
		goto bail;
	}

bail:
	if ( ! mgrPut ) {
		fpga_mgr_put( mgr );
	}

	if ( id >= 0 ) {
		ida_simple_remove( &fpga_prog_ida, id );
	}

	if ( mgrNode ) {
		of_node_put( mgrNode );
	}

	if ( mem ) {
		kfree( mem );
	}

	return stat;
}


/* Create and initialize driver-private data. The ref. to the 'mgrNode' is
 * either consumed by this routine (success) or must be dropped by the caller
 * (failure).
 */
static struct fpga_prog_drvdat *
create_drvdat( struct platform_device *pdev, struct device_node *mgrNode )
{
struct fpga_prog_drvdat *prog;
struct device           *dev;
struct device_node      *pnod;
int                      stat;
const char              *str;
u32                      val;

	if ( (dev = driver_find_device( &fpga_prog_driver.driver, 0, mgrNode, cmp_mgr_node )) ) {
		put_device( dev );
		return ERR_PTR(-EEXIST);
	}

	if ( ! ( prog = kzalloc( sizeof(*prog), GFP_KERNEL ) ) ) {
		return ERR_PTR(-ENOMEM);
	}

	prog->pdev                            = pdev;
	prog->mgrNode                         = mgrNode;
	prog->autoload                        = 1;

	prog->info.flags                      = 0;
	prog->info.enable_timeout_us          = 1000000;
	prog->info.disable_timeout_us         = 1000000;
	prog->info.config_complete_timeout_us = 1000000;

	if ( (pnod = pdev->dev.of_node) ) {
		/* try to load parameters from OF */
		of_node_get( pnod );

		stat = of_property_read_string( pnod, "file", &str );
		if ( 0 == stat ) {
			prog->FW_NAME = kstrdup( str, GFP_KERNEL );
		} else if ( stat != -EINVAL ) {
			printk(KERN_WARNING "%s: unable to read 'file' property from OF (%d)\n", drvnam, stat);
		}

		stat = of_property_read_u32( pnod, "autoload", &val );
		if ( 0 == stat ) {
			prog->autoload = val;
		} else if ( stat != -EINVAL ) {
			printk(KERN_WARNING "%s: unable to read 'autload' property from OF (%d)\n", drvnam, stat);
		}
			
		of_node_put( pnod );
	}

	return prog;
}

/* Attach to a programmer device (either soft or 'hard', i.e., from device-tree)
 *
 * Call: with refs. to fpga_manager and it's of-node. The 'mgrNode' reference is
 * 'consumed' by this routine, i.e., either stored in the driver-private data or
 * dropped. The ref. to the manager is unchanged.
 */
static int
dev_attach(struct platform_device *pdev, struct fpga_manager *mgr, struct device_node *mgrNode)
{
struct fpga_prog_drvdat *drvdat;
struct fpga_prog_drvdat *mem  = 0;
int                      stat = 0;
int                      dev_attr_stat[ N_DEV_ATTRS ];
int                      i;
struct kobject          *mgrObj = 0;

	for ( i=0; i<N_DEV_ATTRS; i++ ) {
		dev_attr_stat[i] = -1;
	}

	drvdat = create_drvdat( pdev, mgrNode );

	if ( IS_ERR( drvdat ) ) {
		of_node_put( mgrNode );
		stat = PTR_ERR( drvdat );
		goto bail;
	}

	mem = drvdat;

	/* for any error after here the mgrNode is 'put' by release_drvdat */

	platform_set_drvdata( pdev, drvdat );

	/* Add sysfs entries to device ('file', 'autoload' etc.) */
	for ( i=0; i<N_DEV_ATTRS; i++ ) {
		if ( (dev_attr_stat[i] = device_create_file( &pdev->dev, dev_attrs[i] )) )
			break;
	}
	if ( i < N_DEV_ATTRS ) {
		stat = dev_attr_stat[i];
		goto bail;
	}

	/* Add symlink to fpga_manager */
	mgrObj = & mgr->dev.kobj;	

	kobject_get( mgrObj );

	stat = sysfs_create_link( &pdev->dev.kobj, mgrObj, "fpga_manager" );

	kobject_put( mgrObj );

	if ( stat ) {
		goto bail;
	}

	dev_attr_stat[0] = -1;
	mem              = 0;

bail:
	for ( i=0; i < N_DEV_ATTRS && dev_attr_stat[i] == 0; i++ ) {
		device_remove_file( &pdev->dev, dev_attrs[i] );
	}

	if ( mem )
		release_drvdat( mem );

	return stat;
}

/* Driver probe function
 */
static int
fpga_prog_probe(struct platform_device *pdev)
{
struct fpga_manager     *mgr;
struct device_node      *mgrNode;
int                      stat, fwstat;
struct fpga_prog_drvdat *prg;

	/* Locate an fpga_manager for this device */
	mgr = of_get_mgr_from_pdev( pdev, &mgrNode );
	if ( IS_ERR( mgr ) ) {
		printk(KERN_ERR "%s: no fpga-manager found (%ld)\n", drvnam, PTR_ERR(mgr));
		return PTR_ERR( mgr );
	}

	/* dev_attach() 'consumes' the reference to mgrNode -
	 * either by storing in drvdat or releasing it on failure
	 */
	stat = dev_attach( pdev, mgr, mgrNode );

	if ( 0 == stat ) {
		/* If - after successfully attaching to the device we
		 * have enough information then we can attempt to load
		 * firmware.
		 */
		prg = platform_get_drvdata( pdev );
		if ( prg->FW_NAME && prg->autoload ) {
#if defined(HAS_NEW_API)
			fwstat = fpga_mgr_load( mgr, &prg->info );
#else
			fwstat = fpga_mgr_firmware_load( mgr, &prg->info, prg->FW_NAME );
#endif
			if ( fwstat ) {
				printk(KERN_WARNING "%s: programming firmware failed (%d)\n", drvnam, fwstat);
			}
		}
	}

	/* Release manager */
	fpga_mgr_put( mgr );

	return stat;
}

/* Cleanup driver private data
 */
static void
release_drvdat(struct fpga_prog_drvdat *prg)
{
	if ( prg->mgrNode ) {
		of_node_put( prg->mgrNode );
	}

	if ( prg->FW_NAME ) {
		kfree( prg->FW_NAME );
		prg->FW_NAME = 0;
	}

	kfree( prg );
}

/* Driver remove function
 */
static int
fpga_prog_remove(struct platform_device *pdev)
{
struct fpga_prog_drvdat *prg = platform_get_drvdata( pdev );
int                      i;

	for ( i=0; i < N_DEV_ATTRS; i++ ) {
		device_remove_file( &pdev->dev, dev_attrs[i] );
	}

	sysfs_remove_link( &pdev->dev.kobj, "fpga_manager" );

	release_drvdat( prg );

	return 0;
}

/* Add a 'soft' device (support 'remove' device attribute in sysfs)
 */
static ssize_t
add_programmer_store(struct device_driver *drv, const char *buf, size_t sz)
{
struct fpga_manager     *mgr;
int                      stat;
struct device           *dev;
struct device_node      *mgrNode = 0;

	if ( buf[sz] )
		return -EINVAL;


	if ( sz > PAGE_SIZE - 1 ) {
		return -ENOMEM;
	}

	mgr = of_get_mgr_from_path( buf, &mgrNode );

	if ( IS_ERR( mgr ) ) {
		return PTR_ERR( mgr );
	}
	/* Hold refs to manager and mgrNode here */

	if ( (dev = device_find_child( &platform_bus, mgrNode , cmp_release_func )) ) {
		put_device  ( dev     );
		fpga_mgr_put( mgr     );
		of_node_put ( mgrNode );
		stat = -EEXIST;
	} else {
		/* create_pdev takes over the manager and mgrNode references
		 */
		stat = create_pdev( mgr, mgrNode );
	}

	if ( stat ) {
		sz = stat;
	}

	return sz;
}

/* Remove a 'soft' device (support 'remove' device attribute in sysfs)
 */
static ssize_t
remove_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz)
{
int val;

	if ( kstrtoint(buf, 0, &val) ) {
		return -EINVAL;
	}

	if ( val && device_remove_file_self( dev, att ) ) {
		device_unregister( dev );
	}

	return sz;
}

/* Helper to find driver-private data
 */
static struct fpga_prog_drvdat *
get_drvdat(struct device *dev)
{
struct platform_device *pdev = container_of( dev, struct platform_device, dev );
	return (struct fpga_prog_drvdat*) platform_get_drvdata( pdev );
}

/* Sysfs attribute 'file' (store)
 */
static ssize_t
file_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );
int    err;

	if ( prg->FW_NAME ) {
		kfree( prg->FW_NAME );
        prg->FW_NAME = 0;
	}

	prg->FW_NAME = kstrdup( buf, GFP_KERNEL );
	if ( ! prg->FW_NAME ) {
		return -ENOMEM;
	} else {
		if ( prg->autoload && (err = load_fw( prg ) ) ) {
			sz = err;
		}
	}
	return sz;
}

/* Sysfs attribute 'file' (show)
 */
static ssize_t
file_show(struct device *dev, struct device_attribute *att, char *buf)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );
int                   len;

	if ( ! prg->FW_NAME ) {
		buf[0] = 0;
		len    = 0;
	} else {
		len = snprintf(buf, PAGE_SIZE, "%s", prg->FW_NAME);
		if ( len >= PAGE_SIZE )
			len = PAGE_SIZE - 1;
	}
	return len;
}

/* Sysfs attribute 'program' (store)
 */
static ssize_t
program_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );
int                   val;
int                   err;

	if ( kstrtoint(buf, 0, &val) ) {
		return -EINVAL;
	}

	if ( val ) {
		if ( (err = load_fw( prg )) ) {
			sz = err;
		}
	}
	return sz;
}

/* Sysfs attribute 'autoload' (show)
 */
static ssize_t
autoload_show(struct device *dev, struct device_attribute *att, char *buf)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );

	/* dont see how that can overflow PAGE_SIZE */
	return snprintf(buf, PAGE_SIZE, "%d\n", prg->autoload);
}

/* Sysfs attribute 'autoload' (store)
 */
static ssize_t
autoload_store(struct device *dev, struct device_attribute *att, const char *buf, size_t sz)
{
struct fpga_prog_drvdat *prg = get_drvdat( dev );

	if ( kstrtoint(buf, 0, &prg->autoload) ) {
		return -EINVAL;
	}

	return sz;
}

/* Boilerplate
 */
#ifdef CONFIG_OF
static const struct of_device_id fpga_prog_of_match[] = {
	{ .compatible = OF_COMPAT, },
	{},
};

MODULE_DEVICE_TABLE(of, fpga_prog_of_match);
#endif

static struct platform_device_id fpga_prog_ids[] = {
	{ .name = "prog-fpga" },
	{}
};

static struct platform_driver fpga_prog_driver = {
	.driver = {
		.name           = "fpga_programmer",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr( fpga_prog_of_match )
	},
	.id_table = fpga_prog_ids,
	.probe    = fpga_prog_probe,
	.remove   = fpga_prog_remove
};

static int __init
fpga_prog_init(void)
{
int                    err     = 0;

	err = platform_driver_register( &fpga_prog_driver );

	if ( ! err ) {
		if ( (err = driver_create_file( &fpga_prog_driver.driver, &driver_attr_add_programmer )) ) {
			platform_driver_unregister( &fpga_prog_driver );
		}
	}

	return err;
}

static void
fpga_prog_exit(void)
{
	platform_driver_unregister( &fpga_prog_driver );
}

module_init( fpga_prog_init );
module_exit( fpga_prog_exit );

#include <linux/init.h>

#include "lock_dlm.h"

int init_lock_dlm()
{
	int error;

	error = gfs_register_lockproto(&gdlm_ops);
	if (error) {
		printk(KERN_WARNING "lock_dlm:  can't register protocol: %d\n",
		       error);
		return error;
	}

	error = gdlm_sysfs_init();
	if (error) {
		gfs_unregister_lockproto(&gdlm_ops);
		return error;
	}

	printk(KERN_INFO
	       "Lock_DLM (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

void exit_lock_dlm()
{
	gdlm_sysfs_exit();
	gfs_unregister_lockproto(&gdlm_ops);
}

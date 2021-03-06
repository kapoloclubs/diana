/*
 * pm.c - Common OMAP2+ power management-related code
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>

// 20110425 prime@sdcmicro.com Patch for INTC autoidle management to make sure it is done in atomic operation with interrupt disabled [START]
#include <linux/notifier.h>
// 20110425 prime@sdcmicro.com Patch for INTC autoidle management to make sure it is done in atomic operation with interrupt disabled [END]

#include <plat/omap-pm.h>
#include <plat/omap_device.h>
#include <plat/common.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_P970_OVERCLOCK_ENABLED
#include <plat/opp.h>
#include <plat/voltage.h>
#endif
#include <plat/smartreflex.h>

#ifdef CONFIG_LGE_DVFS
#include <linux/dvs_suite.h>
#endif	// CONFIG_LGE_DVFS

#include "omap3-opp.h"
#include "opp44xx.h"

// LGE_UPDATE_S
#if defined(CONFIG_MACH_LGE_OMAP3)
#include "pm.h"

u32 sleep_while_idle;
u32 enable_off_mode;
#endif
// LGE_UPDATE_E

// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [START_LGE]
// TODO: make more pretty...
enum {
	RESET_NORMAL,
	RESET_CHARGER_DETECT,
	RESET_GLOBAL_SW_RESET,
	RESET_KERNEL_PANIC,
	RESET_HIDDEN_SW_RESET,
};

int reset_status = RESET_NORMAL;
int hidden_reset_enabled = 0;
/* LGE_CHANGE_S bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
static int hub_secure_mode = 0;
/* LGE_CHANGE_E bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */

static ssize_t reset_status_show(struct kobject *, struct kobj_attribute *, char *);
static struct kobj_attribute reset_status_attr =
	__ATTR(reset_status, 0644, reset_status_show, NULL);

static ssize_t hidden_reset_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t hidden_reset_store(struct kobject *k, struct kobj_attribute *,
			  const char *buf, size_t n);
static struct kobj_attribute hidden_reset_attr =
	__ATTR(hidden_reset, 0644, hidden_reset_show, hidden_reset_store);
/* LGE_CHANGE_S bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
static ssize_t secure_mode_show(struct kobject *, struct kobj_attribute *, char *);
static struct kobj_attribute secure_mode_attr =
	__ATTR(secure_mode, 0644, secure_mode_show, NULL);
/* LGE_CHANGE_E bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */

static void reset_status_setup(char *str)
{
        if (str[0] == 'p')
            reset_status = RESET_KERNEL_PANIC;
        else if (str[0] == 'h')
            reset_status = RESET_HIDDEN_SW_RESET;
        else if (str[0] == 'c')
            reset_status = RESET_CHARGER_DETECT;

        printk("reset_status: %c\n", str[0]);
}
__setup("rs=", reset_status_setup);

/* LGE_CHANGE_S bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
static void hub_secure_mode_setup(char *str)
{
	if (str[0] == '1')
		hub_secure_mode = 1;
	else 
		hub_secure_mode = 0;

	printk("hub_secure_mode: %d\n", hub_secure_mode);
}
__setup("secure=", hub_secure_mode_setup);
/* LGE_CHANGE_E bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
static ssize_t reset_status_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (attr == &reset_status_attr)
		return sprintf(buf, "%d\n", reset_status);
	else
		return -EINVAL;
}

/* LGE_CHANGE_S bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
static ssize_t secure_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (attr == &secure_mode_attr)
		return sprintf(buf, "%d\n", hub_secure_mode);
	else
		return -EINVAL;
}
/* LGE_CHANGE_E bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */

static ssize_t hidden_reset_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (attr == &hidden_reset_attr)
		return sprintf(buf, "%d\n", hidden_reset_enabled);
	else
		return -EINVAL;
}
static ssize_t hidden_reset_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1)
		return -EINVAL;

	if (attr == &hidden_reset_attr) {
                hidden_reset_enabled = value;
	} else {
		return -EINVAL;
	}
	return n;
}
// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [END_LGE]

static struct omap_device_pm_latency *pm_lats;

static struct device *mpu_dev;
static struct device *iva_dev;
static struct device *l3_dev;
static struct device *dsp_dev;

// 20110425 prime@sdcmicro.com Patch for INTC autoidle management to make sure it is done in atomic operation with interrupt disabled [START]
#if 1

/* idle notifications late in the idle path (atomic, interrupts disabled) */
static ATOMIC_NOTIFIER_HEAD(idle_notifier);

void omap_idle_notifier_register(struct notifier_block *n)
{
	atomic_notifier_chain_register(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(omap_idle_notifier_register);

void omap_idle_notifier_unregister(struct notifier_block *n)
{
	atomic_notifier_chain_unregister(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(omap_idle_notifier_unregister);

void omap_idle_notifier_start(void)
{
	atomic_notifier_call_chain(&idle_notifier, OMAP_IDLE_START, NULL);
}

void omap_idle_notifier_end(void)
{
	atomic_notifier_call_chain(&idle_notifier, OMAP_IDLE_END, NULL);
}

#endif
// 20110425 prime@sdcmicro.com Patch for INTC autoidle management to make sure it is done in atomic operation with interrupt disabled [END]

struct device *omap2_get_mpuss_device(void)
{
	WARN_ON_ONCE(!mpu_dev);
	return mpu_dev;
}
EXPORT_SYMBOL(omap2_get_mpuss_device);

struct device *omap2_get_iva_device(void)
{
	WARN_ON_ONCE(!iva_dev);
	return iva_dev;
}
EXPORT_SYMBOL(omap2_get_iva_device);

struct device *omap2_get_l3_device(void)
{
	WARN_ON_ONCE(!l3_dev);
	return l3_dev;
}
EXPORT_SYMBOL(omap2_get_l3_device);

struct device *omap4_get_dsp_device(void)
{
	WARN_ON_ONCE(!dsp_dev);
	return dsp_dev;
}
EXPORT_SYMBOL(omap4_get_dsp_device);

#ifdef CONFIG_OMAP_PM
/* Overclock vdd sysfs interface */
#ifdef CONFIG_P970_OVERCLOCK_ENABLED
#if 0
static ssize_t overclock_vdd_show(struct kobject *, struct kobj_attribute *,
              char *);
static ssize_t overclock_vdd_store(struct kobject *k, struct kobj_attribute *,
			  const char *buf, size_t n);

static struct kobj_attribute overclock_vdd_opp0_attr =
    __ATTR(overclock_vdd_opp0, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp1_attr =
    __ATTR(overclock_vdd_opp1, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp2_attr =
    __ATTR(overclock_vdd_opp2, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp3_attr =
    __ATTR(overclock_vdd_opp3, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp4_attr =
    __ATTR(overclock_vdd_opp4, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp5_attr =
    __ATTR(overclock_vdd_opp5, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp6_attr =
    __ATTR(overclock_vdd_opp6, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp7_attr =
    __ATTR(overclock_vdd_opp7, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp8_attr =
    __ATTR(overclock_vdd_opp8, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp9_attr =
    __ATTR(overclock_vdd_opp9, 0644, overclock_vdd_show, overclock_vdd_store);
#ifdef CONFIG_P970_OPPS_ENABLED
static struct kobj_attribute overclock_vdd_opp10_attr =
    __ATTR(overclock_vdd_opp10, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp11_attr =
    __ATTR(overclock_vdd_opp11, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp12_attr =
    __ATTR(overclock_vdd_opp12, 0644, overclock_vdd_show, overclock_vdd_store);
static struct kobj_attribute overclock_vdd_opp13_attr =
    __ATTR(overclock_vdd_opp13, 0644, overclock_vdd_show, overclock_vdd_store);
#endif
#endif
#endif

/* PM stuff */
static ssize_t vdd_opp_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t vdd_opp_store(struct kobject *k, struct kobj_attribute *, const char *buf, size_t n);

static struct kobj_attribute vdd1_opp_attr =
	__ATTR(vdd1_opp, 0444, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute vdd2_opp_attr =
	__ATTR(vdd2_opp, 0444, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute vdd1_lock_attr =
	__ATTR(vdd1_lock, 0644, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute vdd2_lock_attr =
	__ATTR(vdd2_lock, 0644, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute dsp_freq_attr =
	__ATTR(dsp_freq, 0644, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute tick_control_attr =
	__ATTR(tick, 0644, vdd_opp_show, vdd_opp_store);

/* Overclock vdd sysfs interface */
#ifdef CONFIG_P970_OVERCLOCK_ENABLED
#if 0
static ssize_t overclock_vdd_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	unsigned int target_opp;
	unsigned long *current_volt = 0;
	unsigned long *temp_volt = 0;
	char *voltdm_name = "mpu";
	struct device *mpu_dev = omap2_get_mpuss_device();
	struct cpufreq_frequency_table *mpu_freq_table = *omap_pm_cpu_get_freq_table();
	struct omap_opp *temp_opp;
	struct voltagedomain *mpu_voltdm;
	struct omap_volt_data *mpu_voltdata;

	if(!mpu_dev || !mpu_freq_table)
		return -EINVAL;

	else if ( attr == &overclock_vdd_opp0_attr) {
		target_opp = 0;
	}
	else if ( attr == &overclock_vdd_opp1_attr) {
		target_opp = 1;
	}
	else if ( attr == &overclock_vdd_opp2_attr) {
		target_opp = 2;
	}
	else if ( attr == &overclock_vdd_opp3_attr) {
		target_opp = 3;
	}
	else if ( attr == &overclock_vdd_opp4_attr) {
		target_opp = 4;
	}
	else if ( attr == &overclock_vdd_opp5_attr) {
		target_opp = 5;
	}
	else if ( attr == &overclock_vdd_opp6_attr) {
		target_opp = 6;
	}
	else if ( attr == &overclock_vdd_opp7_attr) {
		target_opp = 7;
	}
	else if ( attr == &overclock_vdd_opp8_attr) {
		target_opp = 8;
	}
	else if ( attr == &overclock_vdd_opp9_attr) {
		target_opp = 9;
	}
#ifdef CONFIG_P970_OPPS_ENABLED
	else if ( attr == &overclock_vdd_opp10_attr) {
		target_opp = 10;
	}
	else if ( attr == &overclock_vdd_opp11_attr) {
		target_opp = 11;
	}
	else if ( attr == &overclock_vdd_opp12_attr) {
		target_opp = 12;
	}
	else if ( attr == &overclock_vdd_opp13_attr) {
		target_opp = 13;
	}
#endif
	temp_opp = opp_find_freq_exact(mpu_dev, mpu_freq_table[target_opp].frequency*1000, true);
	if(IS_ERR(temp_opp))
		return -EINVAL;

	//temp_volt = opp_get_voltage(temp_opp);
	//mpu_voltdm = omap_voltage_domain_get(voltdm_name);
	//mpu_voltdata = omap_voltage_get_voltdata(mpu_voltdm, temp_volt);
	//current_volt = mpu_voltdata->volt_nominal;
	current_volt = opp_get_voltage(temp_opp);

	return sprintf(buf, "%lu\n", current_volt);
}

static ssize_t overclock_vdd_store(struct kobject *k,
        struct kobj_attribute *attr, const char *buf, size_t n)
{
/*	unsigned int target_opp_nr;
	unsigned long target_volt = 0;
	unsigned long divider = 500;
	unsigned long temp_vdd = 0;
	unsigned long vdd_lower_limit = 0;
	unsigned long vdd_upper_limit = 0;
	char *voltdm_name = "mpu";
	unsigned long freq;
	struct device *mpu_dev = omap2_get_mpuss_device();
	struct cpufreq_frequency_table *mpu_freq_table = *omap_pm_cpu_get_freq_table();
	struct omap_opp *temp_opp;
	struct voltagedomain *mpu_voltdm;
	struct omap_volt_data *mpu_voltdata;

	if(!mpu_dev || !mpu_freq_table)
		return -EINVAL;

	if ( attr == &overclock_vdd_opp0_attr) {
		target_opp_nr = 0;
		vdd_lower_limit = 900000;
		vdd_upper_limit = 1200000;
	}
	if ( attr == &overclock_vdd_opp1_attr) {
		target_opp_nr = 1;
		vdd_lower_limit = 900000;
		vdd_upper_limit = 1200000;
	}
	if ( attr == &overclock_vdd_opp2_attr) {
		target_opp_nr = 2;
		vdd_lower_limit = 900000;
		vdd_upper_limit = 1200000;
	}
	if ( attr == &overclock_vdd_opp3_attr) {
		target_opp_nr = 3;
		vdd_lower_limit = 950000;
		vdd_upper_limit = 1300000;
	}
	if ( attr == &overclock_vdd_opp4_attr) {
		target_opp_nr = 4;
		vdd_lower_limit = 1000000;
		vdd_upper_limit = 1400000;
	}
	if ( attr == &overclock_vdd_opp5_attr) {
		target_opp_nr = 5;
		vdd_lower_limit = 1100000;
		vdd_upper_limit = 1500000;
	}
	if ( attr == &overclock_vdd_opp6_attr) {
		target_opp_nr = 6;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp7_attr) {
		target_opp_nr = 7;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp8_attr) {
		target_opp_nr = 8;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp9_attr) {
		target_opp_nr = 9;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
#ifdef CONFIG_P970_OPPS_ENABLED
	if ( attr == &overclock_vdd_opp10_attr) {
		target_opp_nr = 10;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp11_attr) {
		target_opp_nr = 11;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp12_attr) {
		target_opp_nr = 12;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
	if ( attr == &overclock_vdd_opp13_attr) {
		target_opp_nr = 13;
		vdd_lower_limit = 1200000;
		vdd_upper_limit = 1600000;
	}
#endif
	temp_opp = opp_find_freq_exact(mpu_dev, mpu_freq_table[target_opp_nr].frequency*1000, true);
	if(IS_ERR(temp_opp))
		return -EINVAL;

	//temp_vdd = opp_get_voltage(temp_opp);
	mpu_voltdm = omap_voltage_domain_get(voltdm_name);

	if(IS_ERR(mpu_voltdm))
		return -EINVAL;

	//mpu_voltdata = omap_voltage_get_voltdata(mpu_voltdm, temp_vdd);
	//if(IS_ERR(mpu_voltdata))
	//  	return -EINVAL;

	if (sscanf(buf, "%u", &target_volt) == 1) {
		// Make sure that the voltage to be set is a multiple of 500uV, round to the safe side if necessary
		target_volt = target_volt - (target_volt % divider);

		// Enforce limits 
		if(target_volt <= vdd_upper_limit && target_volt >= vdd_lower_limit) {

			//Handle opp
			omap_smartreflex_disable_reset_volt(mpu_voltdm);
			opp_disable(temp_opp);

			temp_opp->u_volt = target_volt;
			opp_enable(temp_opp);

			//omap_smartreflex_enable(mpu_voltdm);
			return n;
		}
	}*/
	return -EINVAL;
}
#endif
#endif

/* PM stuff */
static int vdd1_locked = 0;
static int vdd2_locked = 0;
static struct device sysfs_cpufreq_dev;
extern void tick_nohz_disable(int nohz);

static ssize_t vdd_opp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	if (attr == &vdd1_opp_attr)
		return sprintf(buf, "%hu\n", opp_find_freq_exact(mpu_dev, opp_get_rate(mpu_dev), true)->opp_id+1);
	else if (attr == &vdd2_opp_attr)
		return sprintf(buf, "%hu\n", opp_find_freq_exact(l3_dev, opp_get_rate(l3_dev), true)->opp_id+1);
	else if (attr == &vdd1_lock_attr)
		return sprintf(buf, "%hu\n", vdd1_locked);
	else if (attr == &vdd2_lock_attr)
		return sprintf(buf, "%hu\n", vdd2_locked);
	else if (attr == &dsp_freq_attr)
		return sprintf(buf, "%lu\n", opp_get_rate(iva_dev)/1000);
	else
		return -EINVAL;
}

static ssize_t vdd_opp_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n)
{
	unsigned long value;
#ifdef CONFIG_LGE_DVFS
	unsigned long lc_freq = 0;
#endif	// CONFIG_LGE_DVFS
	
	if (sscanf(buf, "%lu", &value) != 1)
		return -EINVAL;

	if (attr == &tick_control_attr) {
		if (value == 1)
			tick_nohz_disable(1);
		else if (value == 0)
			tick_nohz_disable(0);
	}
	/* Check locks */
	if (attr == &vdd1_lock_attr) {
		if (vdd1_locked) {
			/* vdd1 currently locked */
			if (value == 0) {
				if (omap_pm_set_min_mpu_freq(&sysfs_cpufreq_dev, -1)) {
					printk(KERN_ERR "%s: Failed to remove vdd1_lock\n", __func__);
				} else {
					vdd1_locked = 0;
#ifdef CONFIG_LGE_DVFS
					per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 
						per_cpu(ds_sys_status, 0).sysfs_min_cpu_op_index;
#endif	// CONFIG_LGE_DVFS
					return n;
				}
			} else {
				printk(KERN_ERR "%s: vdd1 already locked to %d\n", __func__, vdd1_locked);
				return -EINVAL;
			}
		} else {
			/* vdd1 currently unlocked */
			if (value != 0) {
				u8 i = 0;
				unsigned long freq = 0;
				struct cpufreq_frequency_table *freq_table = *omap_pm_cpu_get_freq_table();
				struct cpufreq_policy *mpu_policy = cpufreq_cpu_get(0);
				if (freq_table == NULL) {
					printk(KERN_ERR "%s: Could not get freq_table\n", __func__);
					return -ENODEV;
				}
				for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
					if (freq_table[i].index == value - 1) {
						/* Ensure that freq value is floored by cpufreq */
						freq = max(freq_table[i].frequency, mpu_policy->min);
#ifdef CONFIG_LGE_DVFS
						lc_freq = freq * 1000;
#endif	// CONFIG_LGE_DVFS
						break;
					}
				}
				if (freq_table[i].frequency == CPUFREQ_TABLE_END) {
					printk(KERN_ERR "%s: Invalid value [0..%d]\n", __func__, i-1);
					return -EINVAL;
				}
				if (omap_pm_set_min_mpu_freq(&sysfs_cpufreq_dev, freq * 1000)) {
					printk(KERN_ERR "%s: Failed to add vdd1_lock\n", __func__);
				} else {
#ifdef CONFIG_LGE_DVFS
					per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = lc_freq;
#endif	// CONFIG_LGE_DVFS
					vdd1_locked = value;
				}
			} else {
				printk(KERN_ERR "%s: vdd1 already unlocked\n", __func__);
				return -EINVAL;
			}
		}
	} else if (attr == &vdd2_lock_attr) {
		if (vdd2_locked) {
			/* vdd2 currently locked */
			if (value == 0) {
				if (omap_pm_set_min_bus_tput(&sysfs_cpufreq_dev, OCP_INITIATOR_AGENT, 0)) {
					printk(KERN_ERR "%s: Failed to remove vdd2_lock\n", __func__);
				} else {
#ifdef CONFIG_LGE_DVFS
					per_cpu(ds_sys_status, 0).locked_min_l3_freq = 0;
#endif	// CONFIG_LGE_DVFS
					vdd2_locked = 0;
					return n;
				}
			} else {
				printk(KERN_ERR "%s: vdd2 already locked to %d\n", __func__, vdd2_locked);
				return -EINVAL;
			}
		} else {
			/* vdd2 currently unlocked */
			if (value != 0) {
				unsigned long freq = 0;
				if (cpu_is_omap3630()) {
					if(value == 1) {
						freq = 100*1000*4;
#ifdef CONFIG_LGE_DVFS
						lc_freq = 100000000;
#endif	// CONFIG_LGE_DVFS
					} else if (value == 2) {
						freq = 200*1000*4;
#ifdef CONFIG_LGE_DVFS
						lc_freq = 200000000;
#endif	// CONFIG_LGE_DVFS
					} else {
						printk(KERN_ERR "%s: Invalid value [1,2]\n", __func__);
						return -EINVAL;
					}
				}
				else if (cpu_is_omap44xx()) {
					if (omap_rev() <= OMAP4430_REV_ES2_0) {
						if(value == 1) {
							freq = 100*1000*4;
						} else if (value == 2) {
							freq = 200*1000*4;
						} else {
							printk(KERN_ERR "%s: Invalid value [1,2]\n", __func__);
							return -EINVAL;
						}
					} else {
						if(value == 1) {
							freq = 98304*4;
						} else if (value == 2) {
							freq = 100*1000*4;
						} else if (value == 3) {
							freq = 200*1000*4;
						} else {
							printk(KERN_ERR "%s: Invalid value [1,2,3]\n", __func__);
							return -EINVAL;
						}
					}
				} else {
					printk(KERN_ERR "%s: Unsupported HW [OMAP3630, OMAP44XX]\n", __func__);
					return -ENODEV;
				}
				if (omap_pm_set_min_bus_tput(&sysfs_cpufreq_dev, OCP_INITIATOR_AGENT, freq)) {
					printk(KERN_ERR "%s: Failed to add vdd2_lock\n", __func__);
				} else {
#ifdef CONFIG_LGE_DVFS
					per_cpu(ds_sys_status, 0).locked_min_l3_freq = lc_freq;
#endif	// CONFIG_LGE_DVFS
					vdd2_locked = value;
				}
				return n;
			} else {
				printk(KERN_ERR "%s: vdd2 already unlocked\n", __func__);
				return -EINVAL;
			}
		}
	} else if (attr == &dsp_freq_attr) {
		u8 i, opp_id = 0;
		struct omap_opp *opp_table = omap_pm_dsp_get_opp_table();
		struct cpufreq_policy *mpu_policy = cpufreq_cpu_get(0);
		if (opp_table == NULL) {
			printk(KERN_ERR "%s: Could not get dsp opp_table\n", __func__);
			return -ENODEV;
		}
		/* Ensure that freq value is floored by cpufreq */
		for (i = mpu_policy->min_order; opp_table[i].rate; i++) {
			if (opp_table[i].rate >= value) {
				opp_id = i;
#ifdef CONFIG_LGE_DVFS
				switch(i){
					case 1:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 100000000;  // Unlocked.
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 90000000;
						break;
					case 2:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 200000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 170000000;
						break;
					case 3:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 300000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 260000000;
						break;
					case 4:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 400000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 350000000;
						break;
					case 5:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 500000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 440000000;
						break;
					case 6:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 600000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 520000000;
						break;
					case 7:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 700000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 590000000;
						break;
					case 8:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 800000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 660000000;
						break;
					case 9:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 900000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 730000000;
						break;
					case 10:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1000000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 800000000;
						break;
#ifdef CONFIG_P970_OPPS_ENABLED
					case 11:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1100000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 870000000;
						break;
					case 12:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1200000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 930000000;
						break;
					case 13:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1300000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 970000000;
						break;
					case 14:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1350000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 1000000000;
						break;
#endif
					default:
						per_cpu(ds_sys_status, 0).locked_min_cpu_op_index = 1000000000;
						per_cpu(ds_sys_status, 0).locked_min_iva_freq = 800000000;
						break;
				}
#endif	// CONFIG_LGE_DVFS
				break;
			}
		}

		if (opp_id == 0) {
			printk(KERN_ERR "%s: Invalid value\n", __func__);
			return -EINVAL;
		}
		omap_pm_dsp_set_min_opp(opp_id);

	} else if (attr == &vdd1_opp_attr) {
		printk(KERN_ERR "%s: changing vdd1_opp is not supported\n", __func__);
		return -EINVAL;
	} else if (attr == &vdd2_opp_attr) {
		printk(KERN_ERR "%s: changing vdd2_opp is not supported\n", __func__);
		return -EINVAL;
	} else {
		return -EINVAL;
	}
	return n;
}
#endif

/* static int _init_omap_device(struct omap_hwmod *oh, void *user) */
static int _init_omap_device(char *name, struct device **new_dev)
{
	struct omap_hwmod *oh;
	struct omap_device *od;

	oh = omap_hwmod_lookup(name);
	if (WARN(!oh, "%s: could not find omap_hwmod for %s\n",
		 __func__, name))
		return -ENODEV;
	od = omap_device_build(oh->name, 0, oh, NULL, 0, pm_lats, 0, false);
	if (WARN(IS_ERR(od), "%s: could not build omap_device for %s\n",
		 __func__, name))
		return -ENODEV;

	*new_dev = &od->pdev.dev;

	return 0;
}

/*
 * Build omap_devices for processors and bus.
 */
static void omap2_init_processor_devices(void)
{
	struct omap_hwmod *oh;

	_init_omap_device("mpu", &mpu_dev);

	if (cpu_is_omap34xx())
		_init_omap_device("iva", &iva_dev);
	oh = omap_hwmod_lookup("iva");
	if (oh && oh->od)
		iva_dev = &oh->od->pdev.dev;

	oh = omap_hwmod_lookup("dsp");
	if (oh && oh->od)
		dsp_dev = &oh->od->pdev.dev;

	if (cpu_is_omap44xx())
		_init_omap_device("l3_main_1", &l3_dev);
	else
		_init_omap_device("l3_main", &l3_dev);
}

static int __init omap2_common_pm_init(void)
{
// LGE_UPDATE_S : come from pm.c
#if defined(CONFIG_MACH_LGE_OMAP3)
	sleep_while_idle = 0;  // temp... should be checked..
	enable_off_mode = 1;
#endif
// LGE_UPDATE_E : come from pm.c

// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [START_LGE]
	int error = -EINVAL;
// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [END_LGE]

	omap2_init_processor_devices();
	if (cpu_is_omap34xx())
		omap3_pm_init_opp_table();
	else if (cpu_is_omap44xx())
		omap4_pm_init_opp_table();

	omap_pm_if_init();

// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [START_LGE]
	error = sysfs_create_file(power_kobj, &reset_status_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(power_kobj, &hidden_reset_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
/* LGE_CHANGE_S bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
	if (hub_secure_mode) {
		error = sysfs_create_file(power_kobj, &secure_mode_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
	}
/* LGE_CHANGE_E bae.cheolhwan@lge.com, 2011-05-11. Root permission enable. */
// 20100520 jugwan.eom@lge.com For power on cause and hidden reset [END_LGE]

#ifdef CONFIG_OMAP_PM
	{
		int error = -EINVAL;

		error = sysfs_create_file(power_kobj, &dsp_freq_attr.attr);
		if (error) {
			printk(KERN_ERR "%s: sysfs_create_file(dsp_freq) failed %d\n", __func__, error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &vdd1_opp_attr.attr);
		if (error) {
			printk(KERN_ERR "%s: sysfs_create_file(vdd1_opp) failed %d\n", __func__, error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &vdd2_opp_attr.attr);
		if (error) {
			printk(KERN_ERR "%s: sysfs_create_file(vdd2_opp) failed %d\n", __func__, error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &vdd1_lock_attr.attr);
		if (error) {
			printk(KERN_ERR "%s: sysfs_create_file(vdd1_lock) failed %d\n", __func__ ,error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &vdd2_lock_attr.attr);
		if (error) {
			printk(KERN_ERR "%s: sysfs_create_file(vdd2_lock) failed %d\n", __func__, error);
			return error;
		}
        	error = sysfs_create_file(power_kobj, &tick_control_attr.attr);
        	if (error) {
            		printk(KERN_ERR "%s: sysfs_create_file(tick_control) failed: %d\n", __func__, error);
            		return error;
	        }

		/* Overclock vdd sysfs interface */
#ifdef CONFIG_P970_OVERCLOCK_ENABLED
#if 0
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp0_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp1_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp2_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp3_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp4_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp5_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp6_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp7_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp8_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp9_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
#ifdef CONFIG_P970_OPPS_ENABLED
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp10_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp11_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp12_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
		error = sysfs_create_file(power_kobj, &overclock_vdd_opp13_attr.attr);
		if (error) {
			printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
			return error;
		}
#endif
#endif
#endif
	}
#endif

	return 0;
}
device_initcall(omap2_common_pm_init);

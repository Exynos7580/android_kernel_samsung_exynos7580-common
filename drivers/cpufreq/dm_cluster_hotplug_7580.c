#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#if defined(CONFIG_POWERSUSPEND)
#include <linux/powersuspend.h>
#endif

#include "cpu_load_metric.h"

static struct delayed_work exynos_hotplug;
static struct workqueue_struct *khotplug_wq;

enum action {
	DOWN,
	UP,
	STAY,
};

struct hotplug_hstates_usage {
	unsigned long time;
};

struct exynos_hotplug_ctrl {
	ktime_t last_time;
	ktime_t last_check_time;
	unsigned int sampling_rate;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int cpu_up_load;
	unsigned int cpu_down_load;
	int max_lock;
	int min_lock;
	int force_hstate;
	int cores_online;
	struct hotplug_hstates_usage usage[NR_CPUS];
};


#define MIN_CORES				1
#define SUSPENDED_MAX_CORES	 	2
#define WAKE_UP_CORES			NR_CPUS
#define SAMPLING_RATE 			100		// 100ms (Stock)
#define SUSPENDED_SAMPLING_RATE	SAMPLING_RATE * 5
#define DUAL_CORE_UP_LOAD		30
#define QUAD_CORE_DOWN_LOAD		15
#define QUAD_CORE_UP_LOAD		80
#define OCTA_CORE_DOWN_LOAD		30

static struct exynos_hotplug_ctrl ctrl_hotplug = {
	.sampling_rate = SAMPLING_RATE,		/* ms */
	.up_threshold = 3,
	.down_threshold = 3,
	.cpu_up_load = QUAD_CORE_UP_LOAD,
	.cpu_down_load = OCTA_CORE_DOWN_LOAD,
	.force_hstate = -1,
	.min_lock = -1,
	.max_lock = -1,
	.cores_online = NR_CPUS,
};

static DEFINE_MUTEX(hotplug_lock);
static DEFINE_SPINLOCK(hstate_status_lock);

static atomic_t freq_history[STAY] =  {ATOMIC_INIT(0), ATOMIC_INIT(0)};


static void __ref hotplug_cpu(int cores)
{
	int i, num_online, least_busy_cpu;
	unsigned int least_busy_cpu_load;

	/* Check the Online CPU supposed to be online or offline */
	for (i = 0 ; i < NR_CPUS ; i++) {
		num_online = num_online_cpus();
		
		if(num_online == cores) 
		{
			break;
		}
		
		if (cores > num_online) {
			if (!cpu_online(i))
				cpu_up(i);
		} else {
			least_busy_cpu = get_least_busy_cpu(&least_busy_cpu_load);
		
			cpu_down(least_busy_cpu);
		}
	}
}

static s64 hotplug_update_time_status(void)
{
	ktime_t curr_time, last_time;
	s64 diff;

	curr_time = ktime_get();
	last_time = ctrl_hotplug.last_time;

	diff = ktime_to_ms(ktime_sub(curr_time, last_time));

	if (diff > INT_MAX)
		diff = INT_MAX;

	ctrl_hotplug.usage[ctrl_hotplug.cores_online].time += diff;
	ctrl_hotplug.last_time = curr_time;

	return diff;
}

static void hotplug_enter_hstate(bool force, int cores)
{
	int min_cores, max_cores;

	if (power_suspend_active) {
		if (cores > SUSPENDED_MAX_CORES) 
			return;
	} else if (cores < SUSPENDED_MAX_CORES) {
		cores = SUSPENDED_MAX_CORES;
	}

	if (!force) {
		min_cores = ctrl_hotplug.min_lock;
		max_cores = ctrl_hotplug.max_lock;

		if (min_cores >= 1 && cores < min_cores)
			cores = min_cores;

		if (max_cores >= 2 && cores > max_cores)
			cores = max_cores;
	}

	if (ctrl_hotplug.cores_online == cores)
		return;

	spin_lock(&hstate_status_lock);
	hotplug_update_time_status();
	spin_unlock(&hstate_status_lock);

	hotplug_cpu(cores);

	atomic_set(&freq_history[UP], 0);
	atomic_set(&freq_history[DOWN], 0);

	spin_lock(&hstate_status_lock);
	hotplug_update_time_status();
	spin_unlock(&hstate_status_lock);

	ctrl_hotplug.cores_online = cores;
}

static int select_cores(void)
{
	unsigned int cpu_load, least_busy_cpu_load;
	int cpu, up_threshold, down_threshold, cores;

	cores = ctrl_hotplug.cores_online;
	
	if (cores < NR_CPUS / 2) {
		ctrl_hotplug.cpu_up_load = DUAL_CORE_UP_LOAD;
	} else if (cores == NR_CPUS / 2) {
		ctrl_hotplug.cpu_up_load = QUAD_CORE_UP_LOAD;
		ctrl_hotplug.cpu_down_load = QUAD_CORE_DOWN_LOAD;
	} else {
		ctrl_hotplug.cpu_down_load = OCTA_CORE_DOWN_LOAD;
	}
	
	for_each_online_cpu(cpu) {
		update_cpu_load_metric(cpu);
	}
	
	up_threshold = ctrl_hotplug.up_threshold;
	down_threshold = ctrl_hotplug.down_threshold;
	
	cpu_load = cpu_get_avg_load();
	
	if (cpu_load <= ctrl_hotplug.cpu_down_load) {
		get_least_busy_cpu(&least_busy_cpu_load);
		
		if (least_busy_cpu_load <= ctrl_hotplug.cpu_down_load) {
			atomic_inc(&freq_history[DOWN]);
			atomic_set(&freq_history[UP], 0);
		}
	} else if (cpu_load >= ctrl_hotplug.cpu_up_load) {
		atomic_inc(&freq_history[UP]);
		atomic_set(&freq_history[DOWN], 0);
	} else {
		atomic_set(&freq_history[UP], 0);
		atomic_set(&freq_history[DOWN], 0);
	}
	
	if (atomic_read(&freq_history[UP]) > up_threshold) {
		if (cores < NR_CPUS / 2)
			cores = NR_CPUS / 2;
		else
			cores = NR_CPUS;
	}
	else if (atomic_read(&freq_history[DOWN]) > down_threshold) {
		cores--;
	}
	
	if (cores < MIN_CORES)
		cores = MIN_CORES;

	return cores;
}

static void exynos_work(struct work_struct *dwork)
{
	int target_cores, num_online;
	
	num_online = num_online_cpus();

	mutex_lock(&hotplug_lock);

	target_cores = select_cores();
		
	if ((ctrl_hotplug.cores_online != num_online)
			|| (target_cores != num_online))
		hotplug_enter_hstate(false, target_cores);
	
	queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug, msecs_to_jiffies(ctrl_hotplug.sampling_rate));
	mutex_unlock(&hotplug_lock);
}

#define define_show_state_function(_name) \
static ssize_t show_##_name(struct device *dev, struct device_attribute *attr, \
			char *buf) \
{ \
	return sprintf(buf, "%d\n", ctrl_hotplug._name); \
}

#define define_store_state_function(_name) \
static ssize_t store_##_name(struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t count) \
{ \
	unsigned long value; \
	int ret; \
	ret = kstrtoul(buf, 10, &value); \
	if (ret) \
		return ret; \
	ctrl_hotplug._name = value; \
	return ret ? ret : count; \
}

define_show_state_function(up_threshold)
define_store_state_function(up_threshold)

define_show_state_function(down_threshold)
define_store_state_function(down_threshold)

define_show_state_function(sampling_rate)
define_store_state_function(sampling_rate)

define_show_state_function(cpu_up_load)
define_store_state_function(cpu_up_load)

define_show_state_function(cpu_down_load)
define_store_state_function(cpu_down_load)

define_show_state_function(min_lock)

define_show_state_function(max_lock)

define_show_state_function(cores_online)

define_show_state_function(force_hstate)

void __set_force_hstate(int target_cores)
{
	if (target_cores < 0) {
		mutex_lock(&hotplug_lock);
		ctrl_hotplug.force_hstate = -1;
		queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug,
				msecs_to_jiffies(ctrl_hotplug.sampling_rate));
	} else {
		cancel_delayed_work_sync(&exynos_hotplug);

		mutex_lock(&hotplug_lock);
		hotplug_enter_hstate(true, target_cores);
		ctrl_hotplug.force_hstate = target_cores;
	}

	mutex_unlock(&hotplug_lock);
}

static ssize_t store_force_hstate(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, target_cores;

	ret = sscanf(buf, "%d", &target_cores);
	if (ret != 1 || target_cores > NR_CPUS || target_cores < 1)
		return -EINVAL;

	__set_force_hstate(target_cores);

	return count;
}

static void __force_hstate(int target_cores, int *value)
{
	if (target_cores < 0) {
		mutex_lock(&hotplug_lock);
		*value = -1;
	} else {
		cancel_delayed_work_sync(&exynos_hotplug);

		mutex_lock(&hotplug_lock);
		hotplug_enter_hstate(true, target_cores);
		*value = target_cores;
	}

	queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug,
			msecs_to_jiffies(ctrl_hotplug.sampling_rate));

	mutex_unlock(&hotplug_lock);
}

static ssize_t store_max_lock(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int max_state;
	int state;

	int ret, target_state;

	ret = sscanf(buf, "%d", &target_state);
	if (ret != 1 || target_state > NR_CPUS || target_state < 1)
		return -EINVAL;

	max_state = target_state;
	state = target_state;

	mutex_lock(&hotplug_lock);

	if (ctrl_hotplug.force_hstate != -1) {
		mutex_unlock(&hotplug_lock);
		return count;
	}

	if (state < 0) {
		mutex_unlock(&hotplug_lock);
		goto out;
	}

	if (ctrl_hotplug.min_lock >= 1)
		state = ctrl_hotplug.min_lock;

	if (max_state >= 2 && state > max_state)
		state = max_state;

	if ((int)ctrl_hotplug.cores_online > state) {
		ctrl_hotplug.max_lock = state;
		mutex_unlock(&hotplug_lock);
		return count;
	}

	mutex_unlock(&hotplug_lock);

out:
	__force_hstate(state, &ctrl_hotplug.max_lock);

	return count;
}

static ssize_t store_min_lock(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int max_state = -1;
	int state;

	int ret, target_state;

	ret = sscanf(buf, "%d", &target_state);
	if (ret != 1 || target_state < 1 || target_state > NR_CPUS)
		return -EINVAL;

	state = target_state;

	mutex_lock(&hotplug_lock);

	if (ctrl_hotplug.force_hstate != -1) {
		mutex_unlock(&hotplug_lock);
		return count;
	}

	if (state < 0) {
		mutex_unlock(&hotplug_lock);
		goto out;
	}

	if (ctrl_hotplug.max_lock >= 2)
		max_state = ctrl_hotplug.max_lock;

	if (max_state >= 2 && state < max_state)
		state = max_state;

	if ((int)ctrl_hotplug.cores_online < state) {
		ctrl_hotplug.min_lock = state;
		mutex_unlock(&hotplug_lock);
		return count;
	}

	mutex_unlock(&hotplug_lock);

out:
	__force_hstate(state, &ctrl_hotplug.min_lock);

	return count;
}

static ssize_t show_time_in_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	spin_lock(&hstate_status_lock);
	hotplug_update_time_status();
	spin_unlock(&hstate_status_lock);

	for (i = 0; i < NR_CPUS; i++) {
		len += sprintf(buf + len, "%d %llu\n", i,
				(unsigned long long)ctrl_hotplug.usage[i].time);
	}
	return len;
}

static DEVICE_ATTR(up_threshold, S_IRUGO | S_IWUSR, show_up_threshold, store_up_threshold);
static DEVICE_ATTR(down_threshold, S_IRUGO | S_IWUSR, show_down_threshold, store_down_threshold);
static DEVICE_ATTR(sampling_rate, S_IRUGO | S_IWUSR, show_sampling_rate, store_sampling_rate);
static DEVICE_ATTR(cpu_up_load, S_IRUGO | S_IWUSR, show_cpu_up_load, store_cpu_up_load);
static DEVICE_ATTR(cpu_down_load, S_IRUGO | S_IWUSR, show_cpu_down_load, store_cpu_down_load);
static DEVICE_ATTR(force_hstate, S_IRUGO | S_IWUSR, show_force_hstate, store_force_hstate);
static DEVICE_ATTR(cores_online, S_IRUGO, show_cores_online, NULL);
static DEVICE_ATTR(min_lock, S_IRUGO | S_IWUSR, show_min_lock, store_min_lock);
static DEVICE_ATTR(max_lock, S_IRUGO | S_IWUSR, show_max_lock, store_max_lock);

static DEVICE_ATTR(time_in_state, S_IRUGO, show_time_in_state, NULL);

static struct attribute *clusterhotplug_default_attrs[] = {
	&dev_attr_up_threshold.attr,
	&dev_attr_down_threshold.attr,
	&dev_attr_sampling_rate.attr,
	&dev_attr_cpu_up_load.attr,
	&dev_attr_cpu_down_load.attr,
	&dev_attr_force_hstate.attr,
	&dev_attr_cores_online.attr,
	&dev_attr_time_in_state.attr,
	&dev_attr_min_lock.attr,
	&dev_attr_max_lock.attr,
	NULL
};

static struct attribute_group clusterhotplug_attr_group = {
	.attrs = clusterhotplug_default_attrs,
	.name = "clusterhotplug",
};

#if defined(CONFIG_POWERSUSPEND)
static void __cpuinit powersave_resume(struct power_suspend *handler)
{
	mutex_lock(&hotplug_lock);
	ctrl_hotplug.sampling_rate = SAMPLING_RATE;
	hotplug_enter_hstate(true, WAKE_UP_CORES);
	mutex_unlock(&hotplug_lock);
}

static void __cpuinit powersave_suspend(struct power_suspend *handler)
{
	mutex_lock(&hotplug_lock);
	ctrl_hotplug.sampling_rate = SUSPENDED_SAMPLING_RATE;
	hotplug_enter_hstate(false, SUSPENDED_MAX_CORES);

	atomic_set(&freq_history[UP], 0);
	atomic_set(&freq_history[DOWN], 0);
	mutex_unlock(&hotplug_lock);
}

static struct power_suspend __refdata powersave_powersuspend = {
	.suspend = powersave_suspend,
	.resume = powersave_resume,
};
#endif /* (defined(CONFIG_POWERSUSPEND)... */

static int __init dm_cluster_hotplug_init(void)
{
	int ret;

	INIT_DEFERRABLE_WORK(&exynos_hotplug, exynos_work);

	khotplug_wq = alloc_workqueue("khotplug", WQ_FREEZABLE, 0);
	if (!khotplug_wq) {
		pr_err("Failed to create khotplug workqueue\n");
		ret = -EFAULT;
		goto err_wq;
	}

	ret = sysfs_create_group(&cpu_subsys.dev_root->kobj, &clusterhotplug_attr_group);
	if (ret) {
		pr_err("Failed to create sysfs for hotplug\n");
		goto err_sys;
	}

	queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug, msecs_to_jiffies(ctrl_hotplug.sampling_rate) * 250);
	
#if defined(CONFIG_POWERSUSPEND)
	register_power_suspend(&powersave_powersuspend);
 #endif

	return 0;

err_sys:
	destroy_workqueue(khotplug_wq);
err_wq:
	return ret;
}
late_initcall(dm_cluster_hotplug_init);
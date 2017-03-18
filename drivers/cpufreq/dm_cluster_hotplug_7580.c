#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pm_qos.h>

#if defined(CONFIG_POWERSUSPEND)
#include <linux/powersuspend.h>
#endif

static struct delayed_work exynos_hotplug;
static struct delayed_work start_hotplug;
static struct workqueue_struct *khotplug_wq;

enum hstate {
	H0,
	H1,
	H2,
	H3,
	H4,
	H5,
	H6,
	MAX_HSTATE,
};

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
	unsigned int down_freq;
	unsigned int up_freq;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int up_tasks;
	unsigned int down_tasks;
	unsigned int down_freq_limit;
	int max_lock;
	int min_lock;
	int force_hstate;
	int cur_hstate;
	enum hstate old_state;
	bool suspended;
	struct hotplug_hstates_usage usage[MAX_HSTATE];
};

struct hotplug_hstate {
	char *name;
	unsigned int core_count;
	enum hstate state;
};

static struct hotplug_hstate hstate_set[] = {
	[H0] = {
		.name		= "H0",
		.core_count	= NR_CPUS,
		.state		= H0,
	},
	[H1] = {
		.name		= "H1",
		.core_count	= 7,
		.state		= H1,
	},
	[H2] = {
		.name		= "H2",
		.core_count	= 6,
		.state		= H2,
	},
	[H3] = {
		.name		= "H3",
		.core_count	= 5,
		.state		= H3,
	},
	[H4] = {
		.name		= "H4",
		.core_count	= 4,
		.state		= H4,
	},
	[H5] = {
		.name		= "H5",
		.core_count	= 3,
		.state		= H5,
	},
	[H6] = {
		.name		= "H6",
		.core_count	= 2,
		.state		= H6,
	},
};

static struct exynos_hotplug_ctrl ctrl_hotplug = {
	.sampling_rate = 100,		/* ms */
	.down_freq = 800000,		/* MHz */
	.up_freq = 1300000,		/* MHz */
	.up_threshold = 2,
	.down_threshold = 3,
	.up_tasks = 2, // 2 times online cpus (4 cores online)
	.down_tasks = 1, // 1 times online cpus (8 cores online)
	.force_hstate = -1,
	.min_lock = -1,
	.max_lock = -1,
	.cur_hstate = H0,
	.old_state = H0,
	.down_freq_limit = 100000,
};

static DEFINE_MUTEX(hotplug_lock);
static DEFINE_SPINLOCK(hstate_status_lock);

static atomic_t freq_history[STAY] =  {ATOMIC_INIT(0), ATOMIC_INIT(0)};

/*
 * If 'state' is less than "MAX_STATE"
 *	return core_count of 'state'
 * else
 *	return core count of 'H0'
 */
static int get_core_count(enum hstate state)
{
	if (state < MAX_HSTATE)
		return hstate_set[state].core_count;
	else
		return hstate_set[H0].core_count;
}

static void __ref hotplug_cpu(enum hstate state)
{
	int i, cnt_target;

	cnt_target = get_core_count(state);

	/* Check the Online CPU supposed to be online or offline */
	for (i = 0 ; i < NR_CPUS ; i++) {
		if(i < cnt_target)
		{
			if (!cpu_online(i))
				cpu_up(i);
		}
		else
		{
			if (cpu_online(i))
				cpu_down(i);
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

	ctrl_hotplug.usage[ctrl_hotplug.old_state].time += diff;
	ctrl_hotplug.last_time = curr_time;

	return diff;
}

static void hotplug_enter_hstate(bool force, enum hstate state)
{
	int min_state, max_state;

	if (ctrl_hotplug.suspended)
		return;

	if (!force) {
		min_state = ctrl_hotplug.min_lock;
		max_state = ctrl_hotplug.max_lock;

		if (min_state >= 0 && state > min_state)
			state = min_state;

		if (max_state > 0 && state < max_state)
			state = max_state;
	}

	if (ctrl_hotplug.old_state == state)
		return;

	spin_lock(&hstate_status_lock);
	hotplug_update_time_status();
	spin_unlock(&hstate_status_lock);

	hotplug_cpu(state);

	atomic_set(&freq_history[UP], 0);
	atomic_set(&freq_history[DOWN], 0);

	spin_lock(&hstate_status_lock);
	hotplug_update_time_status();
	spin_unlock(&hstate_status_lock);

	ctrl_hotplug.old_state = state;
	ctrl_hotplug.cur_hstate = state;
}

void exynos_dc_hotplug_control(int state)
{
	if (delayed_work_pending(&exynos_hotplug))
		cancel_delayed_work_sync(&exynos_hotplug);

	mutex_lock(&hotplug_lock);

	if (state == -1) {
		ctrl_hotplug.force_hstate = state;

		if (!delayed_work_pending(&exynos_hotplug))
			queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug,
				msecs_to_jiffies(ctrl_hotplug.sampling_rate));

	} else {
		if (delayed_work_pending(&exynos_hotplug))
			cancel_delayed_work_sync(&exynos_hotplug);

		if (ctrl_hotplug.old_state > state)
			hotplug_enter_hstate(true, state);
		else
			hotplug_enter_hstate(false, state);
	}

	mutex_unlock(&hotplug_lock);
}

static enum action select_up_down(void)
{
	int up_threshold, down_threshold;
	unsigned int down_freq, up_freq;
	unsigned int c0_freq, c1_freq;
	int nr, num_online;
	nr = nr_running();

#ifndef CONFIG_EXYNOS7580_QUAD
	c0_freq = cpufreq_quick_get(0);	/* 0 : first cpu number for Cluster 0 */
	c1_freq = cpufreq_quick_get(4); /* 4 : first cpu number for Cluster 1 */
#else
	c0_freq = cpufreq_quick_get(0);	/* 0 : first cpu number for Cluster 0 */
	c1_freq = c0_freq;
#endif

	up_threshold = ctrl_hotplug.up_threshold;
	down_threshold = ctrl_hotplug.down_threshold;

	/* In the case of Hotplug-outted, and thermal throttled.
		up_freq = min(ctrl_hotplug.up_freq, pm_qos_max)
		up_freq = max(up_freq, down_freq)
	*/
	if(ctrl_hotplug.cur_hstate > H0) {
		/*
			up_freq is less than ctrl_hotplug.up_freq (1.3GHz)
		*/
		up_freq = pm_qos_request(PM_QOS_CLUSTER0_FREQ_MAX);
		up_freq = (ctrl_hotplug.up_freq > up_freq) \
			? (up_freq) : (ctrl_hotplug.up_freq);

		/*
			down_freq is basically up_freq * 3 / 4
			but up_freq *3/4 > ctrl_hotplug.down_freq(800)
			    use 800MHz

			and up_freq *3/4 is more than down_freq_limit (400MHz)
		*/
		down_freq = (up_freq * 3) / 4;
		down_freq = (down_freq > ctrl_hotplug.down_freq) \
			? (ctrl_hotplug.down_freq) : (down_freq);
		down_freq = (down_freq > ctrl_hotplug.down_freq_limit)  \
			? (down_freq) : (ctrl_hotplug.down_freq_limit);
	} else {
		up_freq = ctrl_hotplug.up_freq;
		down_freq = ctrl_hotplug.down_freq;
	}

	num_online = num_online_cpus();

	if (((c1_freq <= down_freq) && (c0_freq <= down_freq)) && ((num_online * ctrl_hotplug.down_tasks) > nr))
		{		// down_tasks / 4
		atomic_inc(&freq_history[DOWN]);
		atomic_set(&freq_history[UP], 0);
	} else if (((c0_freq >= up_freq) || (c1_freq >= up_freq)) && ((num_online * ctrl_hotplug.up_tasks) < nr))
		{
		atomic_inc(&freq_history[UP]);
		atomic_set(&freq_history[DOWN], 0);
	} else /* if ((((c0_freq < up_freq) && (c0_freq > down_freq)) ||
		    ((c1_freq < up_freq && c1_freq > down_freq))) && ((num_online * ctrl_hotplug.down_tasks) >= nr)) */
		{
		atomic_set(&freq_history[UP], 0);
		atomic_set(&freq_history[DOWN], 0);
	}

	if (atomic_read(&freq_history[UP]) > up_threshold)
		return UP;
	else if (atomic_read(&freq_history[DOWN]) > down_threshold)
		return DOWN;

	return STAY;
}

static enum hstate hotplug_adjust_state(enum action move)
{
	int state;
	state = ctrl_hotplug.old_state;

	if (move == DOWN) {
		state++;
		if (state >= MAX_HSTATE)
			state = MAX_HSTATE - 1;
	} else if (move != STAY){
		state = state / 2;		// will go from 2->5->7->8 CPUs online
		if(state < 0)			// we "shouldn't" need this, but just in case
			state = 0;
	}

	return state;
}

static void start_work(struct work_struct *dwork)
{
	mutex_lock(&hotplug_lock);
	ctrl_hotplug.suspended = false;
	mutex_unlock(&hotplug_lock);

	if (ctrl_hotplug.force_hstate == -1)
		queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug, msecs_to_jiffies(ctrl_hotplug.sampling_rate));
}

static void exynos_work(struct work_struct *dwork)
{
	enum action move = select_up_down();
	enum hstate target_state;

	mutex_lock(&hotplug_lock);

	target_state = hotplug_adjust_state(move);
	if ((get_core_count(ctrl_hotplug.old_state) != num_online_cpus())
		|| (move != STAY))
		hotplug_enter_hstate(false, target_state);

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

define_show_state_function(down_freq)
define_store_state_function(down_freq)

define_show_state_function(up_freq)
define_store_state_function(up_freq)

define_show_state_function(up_tasks)
define_store_state_function(up_tasks)

define_show_state_function(down_tasks)
define_store_state_function(down_tasks)

define_show_state_function(min_lock)

define_show_state_function(max_lock)

define_show_state_function(cur_hstate)

define_show_state_function(force_hstate)
void __set_force_hstate(int target_state)
{
	if (target_state < 0) {
		mutex_lock(&hotplug_lock);
		ctrl_hotplug.force_hstate = -1;
		queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug,
				msecs_to_jiffies(ctrl_hotplug.sampling_rate));
	} else {
		cancel_delayed_work_sync(&exynos_hotplug);

		mutex_lock(&hotplug_lock);
		hotplug_enter_hstate(true, target_state);
		ctrl_hotplug.force_hstate = target_state;
	}

	mutex_unlock(&hotplug_lock);
}

static ssize_t store_force_hstate(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, target_state;

	ret = sscanf(buf, "%d", &target_state);
	if (ret != 1 || target_state >= MAX_HSTATE)
		return -EINVAL;

	__set_force_hstate(target_state);

	return count;
}

static void __force_hstate(int target_state, int *value)
{
	if (target_state < 0) {
		mutex_lock(&hotplug_lock);
		*value = -1;
	} else {
		cancel_delayed_work_sync(&exynos_hotplug);

		mutex_lock(&hotplug_lock);
		hotplug_enter_hstate(true, target_state);
		*value = target_state;
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
	if (ret != 1 || target_state >= MAX_HSTATE)
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

	if (ctrl_hotplug.min_lock >= 0)
		state = ctrl_hotplug.min_lock;

	if (max_state >= 0 && state <= max_state)
		state = max_state;

	if ((int)ctrl_hotplug.old_state > state) {
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
	if (ret != 1 || target_state >= MAX_HSTATE)
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

	if (ctrl_hotplug.max_lock >= 0)
		max_state = ctrl_hotplug.max_lock;

	if (max_state >= 0 && state <= max_state)
		state = max_state;

	if ((int)ctrl_hotplug.old_state < state) {
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

	for (i = 0; i < MAX_HSTATE; i++) {
		len += sprintf(buf + len, "%s %llu\n", hstate_set[i].name,
				(unsigned long long)ctrl_hotplug.usage[i].time);
	}
	return len;
}

#if defined(CONFIG_POWERSUSPEND)
static void powersave_resume(struct power_suspend *handler)
{
	mutex_lock(&hotplug_lock);
	ctrl_hotplug.suspended = false;
	hotplug_enter_hstate(true, H0);

	if (ctrl_hotplug.force_hstate == -1)
		queue_delayed_work_on(0, khotplug_wq, &exynos_hotplug,
				msecs_to_jiffies(ctrl_hotplug.sampling_rate));

	mutex_unlock(&hotplug_lock);
}

static void powersave_suspend(struct power_suspend *handler)
{
	mutex_lock(&hotplug_lock);
	hotplug_enter_hstate(false, H6);
	ctrl_hotplug.suspended = true;

	atomic_set(&freq_history[UP], 0);
	atomic_set(&freq_history[DOWN], 0);

	mutex_unlock(&hotplug_lock);

	cancel_delayed_work_sync(&exynos_hotplug);
}

static struct power_suspend __refdata powersave_powersuspend = {
  .suspend = powersave_suspend,
  .resume = powersave_resume,
};
#endif /* (defined(CONFIG_POWERSUSPEND)... */

void exynos_dm_hotplug_disable(void)
{

}

void exynos_dm_hotplug_enable(void)
{

}

static DEVICE_ATTR(up_threshold, S_IRUGO | S_IWUSR, show_up_threshold, store_up_threshold);
static DEVICE_ATTR(down_threshold, S_IRUGO | S_IWUSR, show_down_threshold, store_down_threshold);
static DEVICE_ATTR(sampling_rate, S_IRUGO | S_IWUSR, show_sampling_rate, store_sampling_rate);
static DEVICE_ATTR(down_freq, S_IRUGO | S_IWUSR, show_down_freq, store_down_freq);
static DEVICE_ATTR(up_freq, S_IRUGO | S_IWUSR, show_up_freq, store_up_freq);
static DEVICE_ATTR(up_tasks, S_IRUGO | S_IWUSR, show_up_tasks, store_up_tasks);
static DEVICE_ATTR(down_tasks, S_IRUGO | S_IWUSR, show_down_tasks, store_down_tasks);
static DEVICE_ATTR(force_hstate, S_IRUGO | S_IWUSR, show_force_hstate, store_force_hstate);
static DEVICE_ATTR(cur_hstate, S_IRUGO, show_cur_hstate, NULL);
static DEVICE_ATTR(min_lock, S_IRUGO | S_IWUSR, show_min_lock, store_min_lock);
static DEVICE_ATTR(max_lock, S_IRUGO | S_IWUSR, show_max_lock, store_max_lock);

static DEVICE_ATTR(time_in_state, S_IRUGO, show_time_in_state, NULL);

static struct attribute *clusterhotplug_default_attrs[] = {
	&dev_attr_up_threshold.attr,
	&dev_attr_down_threshold.attr,
	&dev_attr_sampling_rate.attr,
	&dev_attr_down_freq.attr,
	&dev_attr_up_freq.attr,
	&dev_attr_up_tasks.attr,
	&dev_attr_down_tasks.attr,
	&dev_attr_force_hstate.attr,
	&dev_attr_cur_hstate.attr,
	&dev_attr_time_in_state.attr,
	&dev_attr_min_lock.attr,
	&dev_attr_max_lock.attr,
	NULL
};

static struct attribute_group clusterhotplug_attr_group = {
	.attrs = clusterhotplug_default_attrs,
	.name = "clusterhotplug",
};

static int __init dm_cluster_hotplug_init(void)
{
	int ret;

	INIT_DEFERRABLE_WORK(&exynos_hotplug, exynos_work);
	INIT_DEFERRABLE_WORK(&start_hotplug, start_work);

	mutex_lock(&hotplug_lock);
	ctrl_hotplug.suspended = true;
	mutex_unlock(&hotplug_lock);

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

#if defined(CONFIG_POWERSUSPEND)
	register_power_suspend(&powersave_powersuspend);
#endif

	queue_delayed_work_on(0, khotplug_wq, &start_hotplug, msecs_to_jiffies(ctrl_hotplug.sampling_rate) * 250);

	return 0;

err_sys:
	destroy_workqueue(khotplug_wq);
err_wq:
	return ret;
}
late_initcall(dm_cluster_hotplug_init);

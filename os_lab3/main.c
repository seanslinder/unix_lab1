#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#define PROC_NAME "tsulab"

/*
 * Индивидуальное задание:
 * текущая разница расстояний до Земли от Voyager 1 и от Voyager 2.
 *
 * Параметр dynamic=true включает простую модель "обновления" расстояния:
 * distance(t) = distance_at_load + v_km_s * (t - load_time).
 * Это приближение (расстояние до Земли из-за орбитального движения Земли
 * не обязано расти монотонно).
 */

static unsigned long long v1_km = 25422406537ULL; // пример "текущего" значения, км
static unsigned long long v2_km = 21298188735ULL; // пример "текущего" значения, км

static unsigned int v1_km_s = 17; // приближённая скорость, км/с
static unsigned int v2_km_s = 15; // приближённая скорость, км/с

static bool dynamic = false;

module_param(v1_km, ullong, 0444);
MODULE_PARM_DESC(v1_km, "Voyager 1 distance to Earth at load time (km)");
module_param(v2_km, ullong, 0444);
MODULE_PARM_DESC(v2_km, "Voyager 2 distance to Earth at load time (km)");
module_param(v1_km_s, uint, 0444);
MODULE_PARM_DESC(v1_km_s, "Voyager 1 speed for dynamic model (km/s)");
module_param(v2_km_s, uint, 0444);
MODULE_PARM_DESC(v2_km_s, "Voyager 2 speed for dynamic model (km/s)");
module_param(dynamic, bool, 0444);
MODULE_PARM_DESC(dynamic, "Enable simple linear distance update model");

static struct proc_dir_entry *proc_entry;
static time64_t load_real_seconds;

#define AU_KM 149597870ULL // 1 AU ~= 149,597,870 km (округление)

static unsigned long long km_to_au_x1000(unsigned long long km)
{
    /* Возвращает AU * 1000 (3 знака после запятой) */
    return div64_u64(km * 1000ULL, AU_KM);
}

static int tsulab_show(struct seq_file *m, void *v)
{
    time64_t now = ktime_get_real_seconds();
    time64_t elapsed = now - load_real_seconds;

    unsigned long long d1 = v1_km;
    unsigned long long d2 = v2_km;

    if (dynamic)
    {
        d1 = d1 + (unsigned long long)v1_km_s * (unsigned long long)elapsed;
        d2 = d2 + (unsigned long long)v2_km_s * (unsigned long long)elapsed;
    }

    /* Разница: Voyager1 - Voyager2 (может быть отрицательной теоретически) */
    {
        long long diff_km = (long long)d1 - (long long)d2;

        unsigned long long d1_au_x1000 = km_to_au_x1000(d1);
        unsigned long long d2_au_x1000 = km_to_au_x1000(d2);

        unsigned long long diff_abs_km = (diff_km < 0) ? (unsigned long long)(-diff_km)
                                                       : (unsigned long long)diff_km;
        unsigned long long diff_au_x1000 = km_to_au_x1000(diff_abs_km);

        seq_printf(m, "TSU kernel module: /proc/%s\n", PROC_NAME);
        seq_printf(m, "Time since load: %lld s\n", (long long)elapsed);
        seq_printf(m, "Model: %s\n", dynamic ? "dynamic (linear)" : "static (at load time)");

        seq_printf(m, "Voyager 1 distance to Earth: %llu km (~%llu.%03llu AU)\n",
                   d1,
                   d1_au_x1000 / 1000ULL,
                   d1_au_x1000 % 1000ULL);

        seq_printf(m, "Voyager 2 distance to Earth: %llu km (~%llu.%03llu AU)\n",
                   d2,
                   d2_au_x1000 / 1000ULL,
                   d2_au_x1000 % 1000ULL);

        seq_printf(m, "Difference (V1 - V2): %s%llu km (~%s%llu.%03llu AU)\n",
                   (diff_km < 0) ? "-" : "",
                   diff_abs_km,
                   (diff_km < 0) ? "-" : "",
                   diff_au_x1000 / 1000ULL,
                   diff_au_x1000 % 1000ULL);
    }

    return 0;
}

static int tsulab_open(struct inode *inode, struct file *file)
{
    return single_open(file, tsulab_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops tsulab_proc_ops = {
    .proc_open = tsulab_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct
    file_operations tsulab_proc_ops = {
        .open = tsulab_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};
#endif

static int __init tsulab_init(void)
{
    load_real_seconds = ktime_get_real_seconds();

    /* Часть 1: сообщение при загрузке */
    pr_info("Welcome to the Tomsk State University\n");

    /* Часть 2: /proc/tsulab */
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &tsulab_proc_ops);
    if (!proc_entry)
    {
        pr_err("tsulab: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    return 0;
}

static void __exit tsulab_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);

    /* Часть 1: сообщение при выгрузке */
    pr_info("Tomsk State University forever!\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TSU OS Lab");
MODULE_DESCRIPTION("Linux kernel module lab: messages + /proc/tsulab (Voyager distance difference)");
MODULE_VERSION("1.0");

module_init(tsulab_init);
module_exit(tsulab_exit);
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/version.h>

static int x_min = 800;
static int x_max = 1200;
static int y_min = 600;
static int y_max = 1000;

module_param(x_min, int, 0644);
module_param(x_max, int, 0644);
module_param(y_min, int, 0644);
module_param(y_max, int, 0644);

static int handler_pre(struct kprobe *p, struct pt_regs *regs);

static struct kprobe kp = {
    .symbol_name = "input_event",
    .pre_handler = handler_pre,
};

static int current_slot = 0;
static int last_x[20] = {0};
static int last_y[20] = {0};

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct input_dev *dev = (struct input_dev *)regs->regs[0];
    unsigned int type = (unsigned int)regs->regs[1];
    unsigned int code = (unsigned int)regs->regs[2];
    int value = (int)regs->regs[3];

    if (!dev || !dev->name)
        return 0;

    /* 第一层过滤：只处理我们的触摸屏 */
    if (strncmp(dev->name, "NVTCapacitiveTouchScreen", 23) != 0)
        return 0;

    if (type != EV_ABS)
        return 0;

    if (code == ABS_MT_SLOT) {
        current_slot = value;
        if (current_slot < 0) current_slot = 0;
        if (current_slot >= 20) current_slot = 19;
    }
    else if (code == ABS_MT_POSITION_X) {
        last_x[current_slot] = value;
    }
    else if (code == ABS_MT_POSITION_Y) {
        last_y[current_slot] = value;
        pr_info("touch_block: MOVE slot=%d X=%d Y=%d\n",
                current_slot, last_x[current_slot], last_y[current_slot]);
    }
    else if (code == ABS_MT_TRACKING_ID && value != -1) {
        int cur_x = last_x[current_slot];
        int cur_y = last_y[current_slot];

        pr_info("touch_block: DOWN slot=%d X=%d Y=%d zoneX[%d-%d] zoneY[%d-%d]\n",
                current_slot, cur_x, cur_y, x_min, x_max, y_min, y_max);

        if (cur_x >= x_min && cur_x <= x_max &&
            cur_y >= y_min && cur_y <= y_max) {
            pr_info("touch_block: === BLOCKED slot=%d ===\n", current_slot);
            regs->regs[3] = -1;
        }
    }

    return 0;
}

static int __init touch_block_init(void)
{
    int ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("touch_block: register_kprobe failed, ret=%d\n", ret);
        return ret;
    }
    pr_info("touch_block: loaded. zone X[%d-%d] Y[%d-%d]\n",
            x_min, x_max, y_min, y_max);
    return 0;
}

static void __exit touch_block_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("touch_block: unloaded.\n");
}

module_init(touch_block_init);
module_exit(touch_block_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Touch block kprobe");

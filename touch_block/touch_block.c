#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/version.h>

// ==================== 模块参数 ====================
// 阻断区的坐标范围（可在 insmod 时动态修改）
// 例如：insmod touch_block.ko x_min=800 x_max=1200 y_min=600 y_max=1000
static int x_min = 800;
static int x_max = 1200;
static int y_min = 600;
static int y_max = 1000;

module_param(x_min, int, 0644);
MODULE_PARM_DESC(x_min, "Block zone X min");
module_param(x_max, int, 0644);
MODULE_PARM_DESC(x_max, "Block zone X max");
module_param(y_min, int, 0644);
MODULE_PARM_DESC(y_min, "Block zone Y min");
module_param(y_max, int, 0644);
MODULE_PARM_DESC(y_max, "Block zone Y max");

// ==================== Kprobe 逻辑 ====================

// 定义目标函数
static struct kprobe kp = {
    .symbol_name = "input_event",
};

// 状态变量：用于记录当前手指的槽位和坐标
// 假设最多支持 20 个触控点（10个手指+10个笔等）
static int current_slot = 0;
static int last_x[20] = {0};
static int last_y[20] = {0};

// Kprobe 前置处理函数
static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    // ARM64 架构下，函数参数依次存放在 regs->regs[0] 到 regs->regs[7]
    struct input_dev *dev = (struct input_dev *)regs->regs[0];
    unsigned int type = (unsigned int)regs->regs[1];
    unsigned int code = (unsigned int)regs->regs[2];
    int value = (int)regs->regs[3];

    // 1. 安全检查：设备名必须匹配联咏触摸屏
    if (!dev || !dev->name)
        return 0;
    if (strncmp(dev->name, "NVTCapacitiveTouchScreen", 23) != 0)
        return 0;

    // 2. 只处理绝对坐标事件 (EV_ABS)
    if (type != EV_ABS)
        return 0;

    // 3. 状态跟踪
    if (code == ABS_MT_SLOT) {
        // 记录当前操作的手指槽位
        current_slot = value;
        if (current_slot >= 20) current_slot = 19; // 防越界
    } 
    else if (code == ABS_MT_POSITION_X) {
        // 记录 X 坐标
        last_x[current_slot] = value;
    } 
    else if (code == ABS_MT_POSITION_Y) {
        // 记录 Y 坐标
        last_y[current_slot] = value;
    } 
    else if (code == ABS_MT_TRACKING_ID && value != -1) {
        // 当驱动准备激活这根手指时（TRACKING_ID != -1）
        // 检查当前手指的坐标是否在阻断区内
        int cur_x = last_x[current_slot];
        int cur_y = last_y[current_slot];

        if (cur_x >= x_min && cur_x <= x_max && cur_y >= y_min && cur_y <= y_max) {
            // 命中阻断区！强制将 TRACKING_ID 改为 -1（相当于抬起手指）
            // 这样 Android 系统就收不到这跟手指的按下事件
            regs->regs[3] = -1;
        }
    }

    return 0;
}

// ==================== 模块初始化与退出 ====================

static int __init touch_block_init(void)
{
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("touch_block: register_kprobe failed, returned %d\n", ret);
        return ret;
    }

    pr_info("touch_block: Kprobe loaded. Block zone: X[%d-%d] Y[%d-%d]\n", 
            x_min, x_max, y_min, y_max);
    return 0;
}

static void __exit touch_block_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("touch_block: Kprobe unloaded.\n");
}

module_init(touch_block_init);
module_exit(touch_block_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("Physical Dead Zone Kprobe Module for NVTCapacitiveTouchScreen");

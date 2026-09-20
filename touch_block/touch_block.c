// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/string.h>

static int enable = 1;
module_param(enable, int, 0644);
MODULE_PARM_DESC(enable, "Enable block (1=on, 0=off)");

struct block_rect {
    s64 center_raw_x;
    s64 center_raw_y;
    s64 half_len_raw;
    s64 half_wid_raw;
    s64 cos_x1e6;
    s64 sin_x1e6;
};

static const struct block_rect block_rects[] = {
    {   /* 矩形1：屏幕(2757, 1228) 长380 宽40 顺时针70° */
        .center_raw_x = 12280,
        .center_raw_y = 27570,
        .half_len_raw = 1900,
        .half_wid_raw = 200,
        .cos_x1e6     = 342020,
        .sin_x1e6     = 939693,
    },
    {   /* 矩形2：屏幕(2841, 1218) 同参数 */
        .center_raw_x = 12180,
        .center_raw_y = 28410,
        .half_len_raw = 1900,
        .half_wid_raw = 200,
        .cos_x1e6     = 342020,
        .sin_x1e6     = 939693,
    },
};
#define NUM_RECTS (sizeof(block_rects) / sizeof(block_rects[0]))

static bool point_in_any_block(int raw_x, int raw_y)
{
    int i;
    for (i = 0; i < (int)NUM_RECTS; i++) {
        const struct block_rect *r = &block_rects[i];
        s64 dsx = (s64)raw_y - r->center_raw_x;
        s64 dsy = (s64)raw_x - r->center_raw_y;
        s64 u = dsx * r->cos_x1e6 + dsy * r->sin_x1e6;
        s64 v = -dsx * r->sin_x1e6 + dsy * r->cos_x1e6;
        s64 ulim = r->half_len_raw * 1000000;
        s64 vlim = r->half_wid_raw * 1000000;
        if (u > -ulim && u < ulim && v > -vlim && v < vlim)
            return true;
    }
    return false;
}

#define MAX_SLOTS 10

static int handler_pre(struct kprobe *p, struct pt_regs *regs);
static struct kprobe kp = {
    .symbol_name = "input_event",
    .pre_handler = handler_pre,
};

static int current_slot = 0;
static int last_x[MAX_SLOTS] = {0};
static int last_y[MAX_SLOTS] = {0};
static bool slot_blocked[MAX_SLOTS] = {false};
static bool name_logged = false;   /* 只打印一次设备名，用于诊断 */

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct input_dev *dev = (struct input_dev *)regs->regs[0];
    unsigned int type = (unsigned int)regs->regs[1];
    unsigned int code = (unsigned int)regs->regs[2];
    int value = (int)regs->regs[3];

    if (!dev || !dev->name)
        return 0;

    /* 【诊断】只打印一次第一次遇到的设备名 */
    if (!name_logged) {
        name_logged = true;
        pr_info("touch_block: first dev name = [%s]\n", dev->name);
    }

    /* 用 strncmp 匹配前 23 字符，避免末尾空字符差异 */
    if (strncmp(dev->name, "NVTCapacitiveTouchScreen", 23) != 0)
        return 0;
    if (type != EV_ABS)
        return 0;
    if (!enable)
        return 0;

    if (code == ABS_MT_SLOT) {
        current_slot = value;
        if (current_slot < 0) current_slot = 0;
        if (current_slot >= MAX_SLOTS) current_slot = MAX_SLOTS - 1;
    } else if (code == ABS_MT_POSITION_X) {
        last_x[current_slot] = value;
    } else if (code == ABS_MT_POSITION_Y) {
        last_y[current_slot] = value;
    } else if (code == ABS_MT_TRACKING_ID && value != -1) {
        int cx = last_x[current_slot];
        int cy = last_y[current_slot];

        /* 【诊断】每次手指按下都打印原始坐标 */
        pr_info("touch_block: DOWN slot=%d raw(%d,%d)\n",
                current_slot, cx, cy);

        if (point_in_any_block(cx, cy)) {
            regs->regs[3] = -1;
            if (!slot_blocked[current_slot]) {
                slot_blocked[current_slot] = true;
                pr_info("touch_block: === BLOCKED slot=%d ===\n", current_slot);
            }
        } else {
            if (slot_blocked[current_slot]) {
                slot_blocked[current_slot] = false;
                pr_info("touch_block: === UNBLOCKED slot=%d ===\n", current_slot);
            }
        }
    }
    return 0;
}

static int __init touch_block_init(void)
{
    int ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("touch_block: register_kprobe failed: %d\n", ret);
        return ret;
    }
    pr_info("touch_block: loaded, rects=%d enable=%d\n",
            (int)NUM_RECTS, enable);
    return 0;
}

static void __exit touch_block_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("touch_block: unloaded\n");
}

module_init(touch_block_init);
module_exit(touch_block_exit);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * touch_block.c - 双旋转矩形物理阻断条内核模块
 *
 * 屏幕坐标基准：横屏 3040×1904
 * 原始触摸坐标：X[0,19040] Y[0,30400]
 * 实测换算（横屏）：
 *   screenX = rawY / 10
 *   screenY = (19040 - rawX) / 10
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/string.h>

/* 屏幕参数 */
#define SCREEN_W    3040
#define SCREEN_H    1904
#define RAW_X_MAX   19040   /* 竖屏自然方向宽度 × 10 */

static int enable = 1;
module_param(enable, int, 0644);
MODULE_PARM_DESC(enable, "Enable block (1=on, 0=off)");

/* 阻断矩形（屏幕坐标系） */
struct block_rect {
    s32 cx_screen;   /* 屏幕 X 中心 */
    s32 cy_screen;   /* 屏幕 Y 中心 */
    s32 half_len;    /* 半长（屏幕单位） */
    s32 half_wid;    /* 半宽（屏幕单位） */
    s32 cos_x1e6;    /* cos(顺时针角度) × 1e6 */
    s32 sin_x1e6;    /* sin(顺时针角度) × 1e6 */
};

static const struct block_rect block_rects[] = {
    {   /* 矩形1：屏幕(2757, 1228) 长380 宽40 顺时针70° */
        .cx_screen = 2757,
        .cy_screen = 1228,
        .half_len  = 190,
        .half_wid  = 20,
        .cos_x1e6  = 342020,
        .sin_x1e6  = 939693,
    },
    {   /* 矩形2：屏幕(2841, 1218) 同参数 */
        .cx_screen = 2841,
        .cy_screen = 1218,
        .half_len  = 190,
        .half_wid  = 20,
        .cos_x1e6  = 342020,
        .sin_x1e6  = 939693,
    },
};
#define NUM_RECTS (sizeof(block_rects) / sizeof(block_rects[0]))

/*
 * 判断 raw 触摸点是否落在任一旋转矩形内。
 * 思路：先把 raw 转成屏幕坐标 × 10（保持整数精度），
 *       再在屏幕坐标系下做逆旋转，判断是否在矩形内。
 */
static bool point_in_any_block(int raw_x, int raw_y)
{
    int i;
    /* raw → 屏幕坐标 × 10 */
    s64 sx10 = (s64)raw_y;                 /* 屏幕 X × 10 */
    s64 sy10 = RAW_X_MAX - (s64)raw_x;     /* 屏幕 Y × 10（反向） */

    for (i = 0; i < (int)NUM_RECTS; i++) {
        const struct block_rect *r = &block_rects[i];
        s64 cx10 = (s64)r->cx_screen * 10;
        s64 cy10 = (s64)r->cy_screen * 10;
        s64 dsx = sx10 - cx10;
        s64 dsy = sy10 - cy10;
        s64 u = dsx * r->cos_x1e6 + dsy * r->sin_x1e6;
        s64 v = -dsx * r->sin_x1e6 + dsy * r->cos_x1e6;
        s64 ulim = (s64)r->half_len * 10 * 1000000;
        s64 vlim = (s64)r->half_wid * 10 * 1000000;
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

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct input_dev *dev = (struct input_dev *)regs->regs[0];
    unsigned int type = (unsigned int)regs->regs[1];
    unsigned int code = (unsigned int)regs->regs[2];
    int value = (int)regs->regs[3];

    if (!dev || !dev->name)
        return 0;
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

        /* 【诊断】每次按下打印 raw 坐标，验证完后可删 */
        /*pr_info("touch_block: DOWN slot=%d raw(%d,%d)\n",
                current_slot, cx, cy);*/

        if (point_in_any_block(cx, cy)) {
            regs->regs[3] = -1;   /* 强制抬起 */
            if (!slot_blocked[current_slot]) {
                slot_blocked[current_slot] = true;
                pr_info("touch_block: === BLOCKED slot=%d ===\n",
                        current_slot);
            }
        } else {
            if (slot_blocked[current_slot]) {
                slot_blocked[current_slot] = false;
                pr_info("touch_block: === UNBLOCKED slot=%d ===\n",
                        current_slot);
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
MODULE_DESCRIPTION("Dual rotated-rectangle touch block kprobe");

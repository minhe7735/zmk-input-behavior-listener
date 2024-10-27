/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_behavior_tog_layer

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>

// #if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_tog_layer_config {
    uint32_t time_to_live_ms;
    int32_t require_prior_idle_ms;
    int32_t hold_trigger_key_positions_len;
    int32_t hold_trigger_key_positions[];
};

struct behavior_tog_layer_data {
    uint8_t toggle_layer;
    struct k_work_delayable toggle_layer_activate_work;
    struct k_work_delayable toggle_layer_deactivate_work;
    const struct device *dev;
};

// this keeps track of the last non-move, non-mod key tap
extern int64_t last_tapped_timestamp;
// this keeps track of the last time a move was pressed
int64_t last_move_timestamp = INT32_MIN;

static void store_last_tapped(int64_t timestamp) {
    if (timestamp > last_move_timestamp) {
        last_tapped_timestamp = timestamp;
    }
}

static bool is_quick_tap(const struct behavior_tog_layer_config *config, int64_t timestamp) {
    return (last_tapped_timestamp + config->require_prior_idle_ms) > timestamp;
}

static int keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    const struct device *dev = device_get_binding(DT_DRV_COMPAT);
    const struct behavior_tog_layer_config *config = dev->config;
    struct behavior_tog_layer_data *data = dev->data;
    if (ev->state && !is_mod(ev->usage_page, ev->keycode)) {
        store_last_tapped(ev->timestamp);
        if(is_first_other_key_pressed_trigger_key(config, ev->position)) {
            k_work_schedule(&data->toggle_layer_deactivate_work, K_MSEC(config->time_to_live_ms));
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

int behavior_move_listener(const zmk_event_t *eh) {
    if (as_zmk_keycode_state_changed(eh) != NULL) {
        return keycode_state_changed_listener(eh);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(pmw3610, behavior_move_listener);
ZMK_SUBSCRIPTION(pmw3610, zmk_keycode_state_changed);


static bool is_first_other_key_pressed_trigger_key(struct behavior_tog_layer_config *config, int32_t position_of_first_other_key_pressed) {
    for (int i = 0; i < config->hold_trigger_key_positions_len; i++) {
        if (config->hold_trigger_key_positions[i] ==
            position_of_first_other_key_pressed) {
            return true;
        }
    }
    return false;
}

// end

static void toggle_layer_deactivate_cb(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_tog_layer_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_tog_layer_data,
                                                        toggle_layer_deactivate_work);
    if (!zmk_keymap_layer_active(data->toggle_layer)) {
      return;
    }
    LOG_DBG("deactivate layer %d", data->toggle_layer);
    zmk_keymap_layer_deactivate(data->toggle_layer);
}

static void toggle_layer_activate_cb(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_tog_layer_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_tog_layer_data,
                                                        toggle_layer_activate_work);
    LOG_DBG("activate layer %d", data->toggle_layer);
    zmk_keymap_layer_activate(data->toggle_layer);
}

static int to_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_tog_layer_data *data = (struct behavior_tog_layer_data *)dev->data;
    const struct behavior_tog_layer_config *cfg = dev->config;
    data->toggle_layer = binding->param1;
    if (!zmk_keymap_layer_active(data->toggle_layer) && !is_quick_tap(cfg, event.timestamp)) {
        // LOG_DBG("schedule activate layer %d", data->toggle_layer);
        k_work_schedule(&data->toggle_layer_activate_work, K_MSEC(0));
    }
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int input_behavior_to_init(const struct device *dev) {
    struct behavior_tog_layer_data *data = dev->data;
    data->dev = dev;
    k_work_init_delayable(&data->toggle_layer_activate_work, toggle_layer_activate_cb);
    k_work_init_delayable(&data->toggle_layer_deactivate_work, toggle_layer_deactivate_cb);
    return 0;
};

static const struct behavior_driver_api behavior_tog_layer_driver_api = {
    .binding_pressed = to_keymap_binding_pressed,
};

#define KP_INST(n)                                                                      \
    static struct behavior_tog_layer_data behavior_tog_layer_data_##n = {};             \
    static struct behavior_tog_layer_config behavior_tog_layer_config_##n = {           \
        .time_to_live_ms = DT_INST_PROP(n, time_to_live_ms),                            \
        .require_prior_idle_ms = DT_PROP(DT_DRV_INST(0), require_prior_idle_ms),        \
        .hold_trigger_key_positions = DT_INST_PROP(n, hold_trigger_key_positions),                 \
        .hold_trigger_key_positions_len = DT_INST_PROP_LEN(n, hold_trigger_key_positions),         \
    };                                                                                  \
    BEHAVIOR_DT_INST_DEFINE(n, input_behavior_to_init, NULL,                            \
                            &behavior_tog_layer_data_##n,                               \
                            &behavior_tog_layer_config_##n,                             \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,           \
                            &behavior_tog_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

// #endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

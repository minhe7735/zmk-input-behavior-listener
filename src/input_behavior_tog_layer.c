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
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>

#define MAX_DEACTIVATION_POSITIONS 4

struct behavior_tog_layer_config {
    int32_t require_prior_idle_ms;
    uint32_t deactivation_positions[MAX_DEACTIVATION_POSITIONS];
    uint8_t num_positions;
};

struct behavior_tog_layer_data {
    uint8_t toggle_layer;
    bool is_active;
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

static bool is_deactivation_position(const struct behavior_tog_layer_config *config, uint32_t position) {
    for (int i = 0; i < config->num_positions; i++) {
        if (config->deactivation_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static int position_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    const struct device *dev = zmk_behavior_get_binding("TOG_LAYER")->behavior_dev;
    struct behavior_tog_layer_data *data = (struct behavior_tog_layer_data *)dev->data;
    const struct behavior_tog_layer_config *cfg = dev->config;

    if (ev->state && data->is_active) {  // Key pressed and layer is active
        if (is_deactivation_position(cfg, ev->position)) {
            LOG_DBG("deactivation key pressed at position %d", ev->position);
            data->is_active = false;
            zmk_keymap_layer_deactivate(data->toggle_layer);
        }
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

static int keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev->state && !is_mod(ev->usage_page, ev->keycode)) {
        store_last_tapped(ev->timestamp);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int to_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_tog_layer_data *data = (struct behavior_tog_layer_data *)dev->data;
    const struct behavior_tog_layer_config *cfg = dev->config;
    
    data->toggle_layer = binding->param1;
    
    if (!data->is_active && !is_quick_tap(cfg, event.timestamp)) {
        LOG_DBG("activate layer %d", data->toggle_layer);
        data->is_active = true;
        zmk_keymap_layer_activate(data->toggle_layer);
    }
    
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int input_behavior_to_init(const struct device *dev) {
    struct behavior_tog_layer_data *data = dev->data;
    data->dev = dev;
    data->is_active = false;
    return 0;
}

static const struct behavior_driver_api behavior_tog_layer_driver_api = {
    .binding_pressed = to_keymap_binding_pressed,
};

ZMK_LISTENER(behavior_tog_layer, position_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_tog_layer, zmk_position_state_changed);

#define KP_INST(n)                                                                      \
    static struct behavior_tog_layer_data behavior_tog_layer_data_##n = {};            \
    static struct behavior_tog_layer_config behavior_tog_layer_config_##n = {          \
        .require_prior_idle_ms = DT_PROP(DT_DRV_INST(0), require_prior_idle_ms),      \
        .deactivation_positions = DT_INST_PROP(n, deactivation_positions),             \
        .num_positions = DT_INST_PROP_LEN(n, deactivation_positions),                  \
    };                                                                                 \
    BEHAVIOR_DT_INST_DEFINE(n, input_behavior_to_init, NULL,                          \
                           &behavior_tog_layer_data_##n,                               \
                           &behavior_tog_layer_config_##n,                             \
                           POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,           \
                           &behavior_tog_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

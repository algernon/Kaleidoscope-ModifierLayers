#include "Kaleidoscope-ModifierLayers.h"

namespace kaleidoscope {
const ModifierLayers::overlay_t *ModifierLayers::overlays = NULL;

uint8_t ModifierLayers::mod_locked_held = 0;
uint8_t ModifierLayers::mod_locked_unheld = 0;
uint8_t ModifierLayers::mod_locked_held_next = 0;
uint8_t ModifierLayers::mod_locked_unheld_next = 0;

uint8_t ModifierLayers::mod_is_pressed_directly = 0;
uint8_t ModifierLayers::mod_was_pressed_directly = 0;

uint8_t ModifierLayers::live_unheld_required[Kaleidoscope.device().matrix_rows][Kaleidoscope.device().matrix_columns];

inline uint8_t flagsToModifierMask(uint8_t flags) {
    // TODO(nikita): it may be better to also incorporate
    // (Key_RightControl, Key_RightShift, Key_RightGui)
    uint8_t result = 0;
    result |= (flags & CTRL_HELD) ? LAYER_MODIFIER_KEY(Key_LeftControl) : 0;
    result |= (flags & LALT_HELD) ? LAYER_MODIFIER_KEY(Key_LeftAlt) : 0;
    result |= (flags & RALT_HELD) ? LAYER_MODIFIER_KEY(Key_RightAlt) : 0;
    result |= (flags & SHIFT_HELD)? LAYER_MODIFIER_KEY(Key_LeftShift) : 0;
    result |= (flags & GUI_HELD)  ? LAYER_MODIFIER_KEY(Key_LeftGui) : 0;
    return result;
}

EventHandlerResult ModifierLayers::onKeyswitchEvent(Key &mapped_key, KeyAddr key_addr, uint8_t key_state) {
    // If we are idle or have an injected key, fall through
    if ((!keyWasPressed(key_state) && !keyIsPressed(key_state)) || (key_state & INJECTED)) {
        return EventHandlerResult::OK;
    }

    if (keyIsPressed(key_state)
    && (!mapped_key.getFlags())
    && (mapped_key.getKeyCode() >= HID_KEYBOARD_FIRST_MODIFIER)
    && (mapped_key.getKeyCode() <= HID_KEYBOARD_LAST_MODIFIER)) {
        mod_is_pressed_directly |= LAYER_MODIFIER_KEY(mapped_key);
        return EventHandlerResult::OK;
    }

    // Don't handle synthetic keys
    if ((mapped_key.getFlags() & SYNTHETIC)) {
        return EventHandlerResult::OK;
    }

    // We can't support rollover between keys that require a modifier to be held
    // and keys that require the same modifier to be unheld. Whichever one of
    // the requirements gets detected first is remembered, and all keypress
    // events that contradict it will get masked out.
    // When a key is first toggled on, the unheld requirements for that key are
    // computed and stored.
    if (keyToggledOn(key_state)) {
        uint8_t layer = Layer.lookupActiveLayer(key_addr);
        uint8_t modifier_mask = 0;

        // This can be precomputed (which may be faster but will use more memory)
        for (byte index = 0; overlays[index].modifier_mask != 0; index++) {
            if (layer == overlays[index].overlay_layer && Layer.isActive(overlays[index].original_layer)) {
                modifier_mask = overlays[index].modifier_mask;
            }
        }
        live_unheld_required[key_addr.row()][key_addr.col()] = modifier_mask & ~flagsToModifierMask(mapped_key.getFlags());
    } else if (keyToggledOff(key_state)) {
        // In theory this should not be necessary, but do it just in case the
        // next keyToggledOn event for this key doesn't reach this handler
        live_unheld_required[key_addr.row()][key_addr.col()] = 0;
        return EventHandlerResult::OK;
    }

    uint8_t unheld_required = live_unheld_required[key_addr.row()][key_addr.col()];
    uint8_t held_required = ~unheld_required & mod_was_pressed_directly;

    if ((unheld_required & mod_locked_held) || (held_required & mod_locked_unheld)) {
        Kaleidoscope.device().maskKey(key_addr);
        return EventHandlerResult::EVENT_CONSUMED;
    }

    mod_locked_held |= held_required;
    mod_locked_held_next |= held_required;

    mod_locked_unheld |= unheld_required;
    mod_locked_unheld_next |= unheld_required;
    return EventHandlerResult::OK;
}

EventHandlerResult ModifierLayers::beforeReportingState() {
    // Release all modifier keys that are required to be not held
    for (byte index = 0; index < 8; index++) {
        if (mod_locked_unheld_next & ((uint8_t)1 << index)) {
            uint8_t key_code = index + HID_KEYBOARD_FIRST_MODIFIER;
            Kaleidoscope.hid().keyboard().releaseRawKey({ key_code, KEY_FLAGS });
        }
    }

    // Toggle any layers we need to
    uint32_t old_layer_state = Layer.getLayerState();
    for (byte index = 0; overlays[index].modifier_mask != 0; index++) {
        if (Layer.isActive(overlays[index].original_layer)) {
            if (mod_is_pressed_directly & overlays[index].modifier_mask) {
                Layer.activate(overlays[index].overlay_layer);
            } else {
                Layer.deactivate(overlays[index].overlay_layer);
            }
        } else {
            Layer.deactivate(overlays[index].overlay_layer);
        }
    }

    return EventHandlerResult::OK;
}

EventHandlerResult ModifierLayers::afterEachCycle() {
    mod_locked_held = mod_locked_held_next;
    mod_locked_unheld = mod_locked_unheld_next;
    mod_was_pressed_directly = mod_is_pressed_directly;
    mod_locked_held_next = 0;
    mod_locked_unheld_next = 0;
    mod_is_pressed_directly = 0;

    return EventHandlerResult::OK;
}
}

kaleidoscope::ModifierLayers ModifierLayers;

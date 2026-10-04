#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Call before storage/network initialization so recovery works without either.
void pocket_boot_guard(void);
bool pocket_recovery_available(void);
bool pocket_return_to_crosspoint(void);
// Local OTA health: initialized controls, first panel refresh, verified recovery.
bool pocket_local_boot_ready(void);
void pocket_image_begin(void);
void pocket_image_abort(void);
bool pocket_image_complete(void);
void pocket_set_status(const char* text);
// Name shown above the caption; saved across restart. Empty returns to the
// name reported by the paired Muse's identity, when it has one.
bool pocket_set_name(const char* name);
void pocket_set_frontlight(int brightness, int warmth);
// Card payloads are the JSON strings described by the registered commands.
// A rejected payload leaves the previous card in place.
bool pocket_set_watch_digest(const char* payload);
bool pocket_set_next_up(const char* payload);
void pocket_note_command(const char* command);
// The setup button, once setup is complete, steps back: previous page, list
// item, screen or settings row.
void pocket_previous(void);
void pocket_get_status(const char** connection, int* received, char* last, size_t last_size);
const char* pocket_sdk_token(void);
#ifdef __cplusplus
}
#endif

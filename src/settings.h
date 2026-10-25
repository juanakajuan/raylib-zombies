#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum SettingId {
    SETTING_VOLUME,
    SETTING_SENSITIVITY,
    SETTING_FOV,
    SETTING_COUNT,
} SettingId;

typedef struct Settings {
    float volume;
    float sensitivity;
    float fov;
} Settings;

typedef struct SettingDef {
    const char *name;
    float min;
    float max;
    float step;
    float default_value;
    int decimals;
    size_t offset;
} SettingDef;

Settings SettingsDefault(void);
const SettingDef *SettingsDef(SettingId id);
float SettingsGet(const Settings *s, SettingId id);
void SettingsAdjust(Settings *s, SettingId id, int direction);

/* Always fills *out (defaults for missing or invalid keys). Returns true when at least one known key was read. */
bool SettingsParse(const char *text, Settings *out);

/* Like snprintf: returns the length the full text needs, and writes at most size - 1 characters. */
int SettingsFormat(const Settings *s, char *buf, size_t size);

void SettingsLoad(Settings *out);
void SettingsSave(const Settings *s);

#endif

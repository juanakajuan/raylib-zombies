#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define SETTINGS_PATH "settings.cfg"
#define SETTINGS_FILE_SIZE 256

static const SettingDef DEFS[SETTING_COUNT] = {
    [SETTING_VOLUME] = {"volume", 0.0f, 1.0f, 0.05f, 0.8f, 2, offsetof(Settings, volume)},
    [SETTING_SENSITIVITY] = {"sensitivity", 0.2f, 3.0f, 0.1f, 1.0f, 1, offsetof(Settings, sensitivity)},
    [SETTING_FOV] = {"fov", 60.0f, 110.0f, 5.0f, 75.0f, 0, offsetof(Settings, fov)},
};

static float *Field(Settings *s, SettingId id)
{
    return (float *)((char *)s + DEFS[id].offset);
}

static float Clamped(const SettingDef *def, float value)
{
    if (isnan(value)) return def->default_value;
    return fminf(fmaxf(value, def->min), def->max);
}

const SettingDef *SettingsDef(SettingId id)
{
    return &DEFS[id];
}

Settings SettingsDefault(void)
{
    Settings s = {0};
    for (int i = 0; i < SETTING_COUNT; i++) *Field(&s, (SettingId)i) = DEFS[i].default_value;
    return s;
}

float SettingsGet(const Settings *s, SettingId id)
{
    return *(const float *)((const char *)s + DEFS[id].offset);
}

void SettingsAdjust(Settings *s, SettingId id, int direction)
{
    float *field = Field(s, id);
    float step = direction > 0 ? DEFS[id].step : (direction < 0 ? -DEFS[id].step : 0.0f);
    *field = Clamped(&DEFS[id], *field + step);
}

static bool ParseLine(const char *line, size_t length, Settings *out)
{
    const char *equals = memchr(line, '=', length);
    if (equals == NULL) return false;
    size_t key_length = (size_t)(equals - line);
    for (int i = 0; i < SETTING_COUNT; i++) {
        if (strlen(DEFS[i].name) != key_length || strncmp(line, DEFS[i].name, key_length) != 0) continue;
        char value[32];
        size_t value_length = length - key_length - 1;
        if (value_length == 0 || value_length >= sizeof value) return false;
        memcpy(value, equals + 1, value_length);
        value[value_length] = '\0';
        char *end;
        float parsed = strtof(value, &end);
        if (end == value || (*end != '\0' && *end != '\r' && *end != ' ') || isnan(parsed)) return false;
        *Field(out, (SettingId)i) = Clamped(&DEFS[i], parsed);
        return true;
    }
    return false;
}

bool SettingsParse(const char *text, Settings *out)
{
    if (out == NULL) return false;
    *out = SettingsDefault();
    if (text == NULL) return false;
    bool any = false;
    while (*text != '\0') {
        size_t length = strcspn(text, "\n");
        any = ParseLine(text, length, out) || any;
        text += length;
        if (*text == '\n') text++;
    }
    return any;
}

int SettingsFormat(const Settings *s, char *buf, size_t size)
{
    int total = 0;
    for (int i = 0; i < SETTING_COUNT; i++) {
        size_t room = (size_t)total < size ? size - (size_t)total : 0;
        char *at = room > 0 ? buf + total : NULL;
        int written = snprintf(at, room, "%s=%.4f\n", DEFS[i].name, (double)SettingsGet(s, (SettingId)i));
        if (written < 0) return written;
        total += written;
    }
    return total;
}

void SettingsLoad(Settings *out)
{
    char *text = LoadFileText(SETTINGS_PATH);
    SettingsParse(text, out);
    if (text != NULL) UnloadFileText(text);
}

void SettingsSave(const Settings *s)
{
    char text[SETTINGS_FILE_SIZE];
    int length = SettingsFormat(s, text, sizeof text);
    if (length > 0 && (size_t)length < sizeof text) SaveFileText(SETTINGS_PATH, text);
}

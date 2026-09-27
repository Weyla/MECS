#include "mecs_command.h"
#include <string.h>

/* Parse decimal digits with an explicit bound. scanf's unsigned conversion can
 * accept minus signs or overflow before range checking, so it is unsuitable for
 * values that can eventually operate a physical output. */
static bool number(const char *s, unsigned max, unsigned *out)
{
    if (!*s) {
        return false;
    }
    unsigned n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        unsigned digit = (unsigned)(*s - '0');
        if (n > max / 10 || (n == max / 10 && digit > max % 10)) {
            return false;
        }
        n = n * 10 + digit;
    }
    *out = n;
    return true;
}
/* Convert decimal user units to exact scaled integers. Reject excess precision
 * rather than silently rounding a duty or a long period to a different value. */
static bool fixed_number(const char *s, unsigned scale, unsigned *out)
{
    char whole[16];
    size_t length = 0;
    while (s[length] && s[length] != '.') {
        if (length + 1 >= sizeof(whole)) {
            return false;
        }
        whole[length] = s[length];
        ++length;
    }
    whole[length] = 0;
    unsigned integer;
    if (!number(whole, 131071u / scale, &integer)) {
        return false;
    }
    unsigned fraction = 0, place = scale;
    if (s[length] == '.') {
        if (scale == 1 || !s[++length]) {
            return false;
        }
        for (; s[length]; ++length) {
            if (s[length] < '0' || s[length] > '9' || place <= 1) {
                return false;
            }
            place /= 10;
            fraction += (unsigned)(s[length] - '0') * place;
        }
    }
    unsigned value = integer * scale + fraction;
    if (value > 131071u) {
        return false;
    }
    *out = value;
    return true;
}

bool mecs_command_parse(const char *line, mecs_command_t *out)
{
    if (!line || !out || strlen(line) >= 160) {
        return false;
    }
    char copy[160];
    strcpy(copy, line);
    char *parts[6];
    unsigned count = 0;
    bool start = true;
    for (char *p = copy; *p; ++p) {
        if (*p == ' ') {
            *p = 0;
            start = true;
        } else if ((unsigned char)*p < 33 || (unsigned char)*p > 126) {
            return false;
        } else if (start) {
            if (count == 6) {
                return false;
            }
            parts[count++] = p;
            start = false;
        }
    }
    if (!count) {
        return false;
    }
    *out = (mecs_command_t){0};
    if (count == 1) {
        if (!strcmp(parts[0], "discover")) {
            out->kind = MECS_COMMAND_DISCOVER;
        } else if (!strcmp(parts[0], "nodes")) {
            out->kind = MECS_COMMAND_NODES;
        } else if (!strcmp(parts[0], "help")) {
            out->kind = MECS_COMMAND_HELP;
        } else {
            return false;
        }
        return true;
    }
    unsigned node, channel = 255, value = 0;
    if (!number(parts[1], MECS_MAX_NODE_ID, &node) || !node) {
        return false;
    }
    out->node = node;
    out->kind = MECS_COMMAND_PROPERTY;
    if (count == 2 && !strcmp(parts[0], "refresh")) {
        out->kind = MECS_COMMAND_REFRESH;
        return true;
    }
    {
        if (count < 3 || !number(parts[2], 255, &channel)) {
            return false;
        }
        if (count == 3 &&
            (!strcmp(parts[0], "on") || !strcmp(parts[0], "off") || !strcmp(parts[0], "pulse"))) {
            out->property = !strcmp(parts[0], "pulse") ? MECS_PROP_TRIGGER : MECS_PROP_VALUE;
            value = strcmp(parts[0], "off") != 0;
        } else if ((count == 4 && !strcmp(parts[0], "get")) ||
                   (count == 5 && !strcmp(parts[0], "set"))) {
            unsigned property;
            for (property = 1; property < MECS_PROP_COUNT; ++property) {
                if (!strcmp(parts[3], mecs_properties[property].name)) {
                    break;
                }
            }
            if (property == MECS_PROP_COUNT) {
                return false;
            }
            out->property = property;
            if (count == 4) {
                out->property |= MECS_READ_FLAG;
            } else if (!fixed_number(parts[4], mecs_properties[property].scale, &value)) {
                return false;
            }
        } else {
            return false;
        }
    }
    unsigned p = out->property & ~MECS_READ_FLAG;
    bool global = p == MECS_PROP_REPORT || p == MECS_PROP_REPORT_MS || p == MECS_PROP_REPORT_MIN_MS;
    if (global ? channel != 255 : channel >= MECS_CHANNELS) {
        return false;
    }
    if (!(out->property & MECS_READ_FLAG) &&
        (mecs_properties[p].readonly || value < mecs_properties[p].minimum ||
         value > mecs_properties[p].maximum)) {
        return false;
    }
    out->channel = channel;
    out->value = value;
    return true;
}

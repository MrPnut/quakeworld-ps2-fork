#include "mod_menu.h"

static qboolean ps2qw_modmenu_active = false;
static char ps2qw_filtered_buf[1024];

static void PS2QW_NormalizeForMatch(const char * src, char * dst, size_t dstsize)
{
    size_t i = 0;

    while (src[i] && i < dstsize - 1)
    {
        unsigned char c = ((unsigned char)src[i]) & 0x7f;

        if (c == 0x10)
        {
            c = '[';
        }
        else if (c == 0x11)
        {
            c = ']';
        }
        else if (c >= 0x12 && c <= 0x1B)
        {
            c = '0' + (c - 0x12);
        }

        dst[i] = (char)c;
        i++;
    }

    dst[i] = '\0';
}

static int PS2QW_MatchMenuPrefix(const char * clean, char d)
{
    if (clean[0] == '[' && clean[1] == d && clean[2] == ']')
    {
        return 3;
    }

    if (clean[0] == d && clean[1] == '.' && clean[2] == '.')
    {
        return 3;
    }

    if (clean[0] == d && clean[1] == ')')
    {
        return 2;
    }

    if (clean[0] == d && clean[1] == '.' && clean[2] == ' ')
    {
        return 2;
    }

    if (clean[0] == d && clean[1] == ' ' && clean[2] == '-')
    {
        return 3;
    }

    return 0;
}

static const char * PS2QW_FormatGoldLabel(char d, const char * btn_name, char * buf, size_t bufsize)
{
    size_t out = 0;

    if (bufsize < 6)
    {
        return "";
    }

    buf[out++] = (char)0x90;
    buf[out++] = d;
    buf[out++] = '/';

    for (size_t i = 0; btn_name[i] && out < bufsize - 2; i++)
    {
        buf[out++] = (char)(btn_name[i] | 0x80);
    }

    buf[out++] = (char)0x91;
    buf[out] = '\0';

    return buf;
}

static const char * PS2QW_ButtonLabelForDigit(char d)
{
    static char label[24];

    switch (d)
    {
        case '1':
            return PS2QW_FormatGoldLabel(d, "CROSS", label, sizeof(label));
        case '2':
            return PS2QW_FormatGoldLabel(d, "CIRCLE", label, sizeof(label));
        case '3':
            return PS2QW_FormatGoldLabel(d, "SQUARE", label, sizeof(label));
        case '4':
            return PS2QW_FormatGoldLabel(d, "TRIANGLE", label, sizeof(label));
        case '5':
            return PS2QW_FormatGoldLabel(d, "L1", label, sizeof(label));
        case '6':
            return PS2QW_FormatGoldLabel(d, "R1", label, sizeof(label));
        case '7':
            return PS2QW_FormatGoldLabel(d, "L2", label, sizeof(label));
        case '8':
            return PS2QW_FormatGoldLabel(d, "R2", label, sizeof(label));
        case '9':
            return PS2QW_FormatGoldLabel(d, "UP", label, sizeof(label));
        case '0':
            return PS2QW_FormatGoldLabel(d, "DOWN", label, sizeof(label));
        default:
            return NULL;
    }
}

static int PS2QW_MapKeyToMenuDigit(int key)
{
    switch (key)
    {
        case K_ABUTTON:
            return '1';
        case K_BBUTTON:
            return '2';
        case K_XBUTTON:
            return '3';
        case K_YBUTTON:
            return '4';
        case K_LSHOULDER:
            return '5';
        case K_RSHOULDER:
            return '6';
        case K_LTRIGGER:
            return '7';
        case K_RTRIGGER:
            return '8';
        case K_UPARROW:
            return '9';
        case K_DOWNARROW:
            return '0';
        default:
            return 0;
    }
}

const char * PS2QW_FilterCenterPrint(const char * raw)
{
    char clean[1024];
    qboolean found_1 = false;
    qboolean found_2 = false;
    size_t i = 0, out = 0;

    if (!raw || !raw[0])
    {
        ps2qw_modmenu_active = false;

        return raw;
    }

    PS2QW_NormalizeForMatch(raw, clean, sizeof(clean));

    while (raw[i] && out < sizeof(ps2qw_filtered_buf) - 1)
    {
        qboolean at_boundary = (i == 0 || clean[i - 1] == '\n' || clean[i - 1] == ' ' || clean[i - 1] == '\t');
        char d = 0;

        if (at_boundary)
        {
            if (clean[i] == '[' && clean[i + 1] >= '0' && clean[i + 1] <= '9')
            {
                d = clean[i + 1];
            }

            else if (clean[i] >= '0' && clean[i] <= '9')
            {
                d = clean[i];
            }
        }

        if (d)
        {
            int tok_len = PS2QW_MatchMenuPrefix(&clean[i], d);
            if (tok_len > 0)
            {
                const char * label = PS2QW_ButtonLabelForDigit(d);
                size_t label_len = strlen(label);

                if (d == '1')
                {
                    found_1 = true;
                }

                if (d == '2')
                {
                    found_2 = true;
                }

                if (out + label_len < sizeof(ps2qw_filtered_buf) - 1)
                {
                    memcpy(&ps2qw_filtered_buf[out], label, label_len);
                    out += label_len;
                    i += tok_len;

                    continue;
                }
            }
        }

        ps2qw_filtered_buf[out++] = raw[i++];
    }

    ps2qw_filtered_buf[out] = '\0';

    if (found_1 && found_2)
    {
        ps2qw_modmenu_active = true;

        return ps2qw_filtered_buf;
    }

    ps2qw_modmenu_active = false;

    return raw;
}

void PS2QW_ClearModMenu(void)
{
    ps2qw_modmenu_active = false;
}

qboolean PS2QW_IsModMenuKey(int key)
{
    if (!ps2qw_modmenu_active || key_dest != key_game || con_forcedup)
    {
        return false;
    }

    return (PS2QW_MapKeyToMenuDigit(key) != 0);
}

qboolean PS2QW_HandleModMenuKey(int key, qboolean down)
{
    int digit;

    if (!ps2qw_modmenu_active || key_dest != key_game || con_forcedup)
    {
        return false;
    }

    digit = PS2QW_MapKeyToMenuDigit(key);
    if (!digit)
    {
        return false;
    }

    if (down)
    {
        char cmd[64];
        int impulse = (digit == '0') ? 10 : (digit - '0');

        ps2qw_modmenu_active = false;
        scr_centertime_off = 0;

        if (keybindings[digit] && keybindings[digit][0])
        {
            q_snprintf(cmd, sizeof(cmd), "%s\n", keybindings[digit]);
        }

        else
        {
            q_snprintf(cmd, sizeof(cmd), "impulse %d\n", impulse);
        }

        Cbuf_AddText(cmd);
    }

    return true;
}

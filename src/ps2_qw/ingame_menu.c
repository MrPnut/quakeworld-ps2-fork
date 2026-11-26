#include "ingame_menu.h"

#define MAX_ALIAS_NAME 32

typedef struct cmdalias_s
{
    struct cmdalias_s * next;
    char name[MAX_ALIAS_NAME];
    char * value;
} cmdalias_t;

extern cmdalias_t * cmd_alias;

static int ingame_cursor = 0;

enum
{
    IG_READY = 0,
    IG_BREAK,
    IG_JOIN,
    IG_OBSERVE,
    IG_ADD_BOT,
    IG_REMOVE_BOT,
    IG_DISCONNECT,
    IG_OPTIONS,
    IG_MAIN_MENU,
    IG_RETURN,
    IG_NUM_ITEMS
};

typedef struct
{
    int id;
    const char * label;
    qboolean gold;
    int gap_before;
} ig_item_t;

static qboolean PS2QW_AliasExists(const char * name)
{
    cmdalias_t * a;
    for (a = cmd_alias; a; a = a->next)
    {
        if (!q_strcasecmp(name, a->name))
        {
            return true;
        }
    }
    return false;
}

static qboolean PS2QW_IsFrogbotActive(void)
{
    if (cls.state != ca_active)
    {
        return false;
    }

    return (PS2QW_AliasExists("addbot") || PS2QW_AliasExists("removebot"));
}

static int PS2QW_BuildMenuItems(ig_item_t * items, int max_items)
{
    int count = 0;
    qboolean has_bots = PS2QW_IsFrogbotActive();

    items[count++] = (ig_item_t){ IG_READY, "READY", false, 0 };
    items[count++] = (ig_item_t){ IG_BREAK, "BREAK", false, 0 };
    items[count++] = (ig_item_t){ IG_JOIN, "JOIN", false, 0 };
    items[count++] = (ig_item_t){ IG_OBSERVE, "OBSERVE", false, 0 };

    if (has_bots && count + 2 < max_items)
    {
        items[count++] = (ig_item_t){ IG_ADD_BOT, "ADD BOT", false, 6 };
        items[count++] = (ig_item_t){ IG_REMOVE_BOT, "REMOVE BOT", false, 0 };
    }

    items[count++] = (ig_item_t){ IG_DISCONNECT, "DISCONNECT", false, 6 };
    items[count++] = (ig_item_t){ IG_OPTIONS, "OPTIONS", false, 8 };
    items[count++] = (ig_item_t){ IG_MAIN_MENU, "MAIN MENU", false, 0 };
    items[count++] = (ig_item_t){ IG_RETURN, "RETURN TO GAME", true, 10 };

    return count;
}

static void PS2QW_CloseToGame(void)
{
    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
}

void PS2QW_InGameMenuOpen(void)
{
    IN_Deactivate(modestate == MS_WINDOWED);
    key_dest = key_menu;
    m_state = m_ps2qw_ingame;
    m_entersound = true;
    ingame_cursor = 0;
}

void PS2QW_InGameMenuDraw(void)
{
    ig_item_t items[IG_NUM_ITEMS];
    int count = PS2QW_BuildMenuItems(items, IG_NUM_ITEMS);
    const char * title = "IN-GAME MENU";
    int y = 52;

    if (ingame_cursor >= count)
    {
        ingame_cursor = 0;
    }

    /* Gold centered header matching ezQuake */
    M_Print((320 - (int)strlen(title) * 8) / 2, y, title);
    y += 14;

    for (int i = 0; i < count; i++)
    {
        int x = (320 - (int)strlen(items[i].label) * 8) / 2;
        y += items[i].gap_before;

        if (items[i].gold)
        {
            M_Print(x, y, items[i].label);
        }
        else
        {
            M_PrintWhite(x, y, items[i].label);
        }

        if (i == ingame_cursor)
        {
            M_DrawCharacter(x - 16, y, 12 + ((int)(realtime * 4) & 1));
        }

        y += 10;
    }
}

void PS2QW_InGameMenuKey(int key)
{
    ig_item_t items[IG_NUM_ITEMS];
    int count = PS2QW_BuildMenuItems(items, IG_NUM_ITEMS);

    if (ingame_cursor >= count)
    {
        ingame_cursor = 0;
    }

    switch (key)
    {
        case K_ESCAPE:
        case K_BBUTTON:
            S_LocalSound("misc/menu3.wav");
            PS2QW_CloseToGame();
            break;

        case K_UPARROW:
            S_LocalSound("misc/menu1.wav");
            if (--ingame_cursor < 0)
            {
                ingame_cursor = count - 1;
            }
            break;

        case K_DOWNARROW:
            S_LocalSound("misc/menu1.wav");
            if (++ingame_cursor >= count)
            {
                ingame_cursor = 0;
            }
            break;

        case K_ENTER:
        case K_KP_ENTER:
        case K_ABUTTON:
            m_entersound = true;
            switch (items[ingame_cursor].id)
            {
                case IG_READY:
                    PS2QW_CloseToGame();
                    Cbuf_AddText("ready\n");
                    break;

                case IG_BREAK:
                    PS2QW_CloseToGame();
                    Cbuf_AddText("break\n");
                    break;

                case IG_JOIN:
                    PS2QW_CloseToGame();
                    if (PS2QW_AliasExists("join"))
                    {
                        Cbuf_AddText("join\n");
                    }
                    else if (cl.spectator)
                    {
                        Cbuf_AddText("disconnect; spectator 0; reconnect\n");
                    }
                    else
                    {
                        Cbuf_AddText("cmd join\n");
                    }
                    break;

                case IG_OBSERVE:
                    PS2QW_CloseToGame();
                    if (PS2QW_AliasExists("observe"))
                    {
                        Cbuf_AddText("observe\n");
                    }
                    else if (!cl.spectator)
                    {
                        Cbuf_AddText("cmd observe\n");
                    }
                    break;
                case IG_ADD_BOT:
                    S_LocalSound("misc/menu2.wav");
                    PS2QW_CloseToGame();
                    Cbuf_AddText("addbot\n");
                    break;

                case IG_REMOVE_BOT:
                    S_LocalSound("misc/menu2.wav");
                    PS2QW_CloseToGame();
                    Cbuf_AddText("removebot\n");
                    break;

                case IG_DISCONNECT:
                    PS2QW_CloseToGame();
                    Cbuf_AddText("disconnect\n");
                    break;

                case IG_OPTIONS:
                    M_Menu_Options_f();
                    break;

                case IG_MAIN_MENU:
                    M_Menu_Main_f();
                    break;

                case IG_RETURN:
                    PS2QW_CloseToGame();
                    break;
            }
            break;
    }
}

#include "ui_common.h"
#include <cstdio>

// Layout registry. Filled in by the layout DSL (docs/UI_LAYOUT_DSL.md).

bool ui_layout_exists(int slot)
{
    (void)slot;
    return false;
}

int ui_layout_last_shown(void)
{
    return -1;
}

void ui_layout_title(int slot, char* buf, int len)
{
    (void)slot;
    if (len > 0) buf[0] = '\0';
}

void ui_layout_cleanup(void)
{
}

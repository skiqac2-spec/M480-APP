/* Super Famicom / SNES (snes9x2002) */
#include "host.h"

static void apply(void)
{
    static const char *fixed[] = { "1", "2", "3" };
    core_var("snes9x2002_transparency", "enabled");
    core_var("snes9x2002_low_pass_filter", "disabled");
    core_var("snes9x2002_overclock_cycles", "disabled");
    if (cfg.frameskip == 0)
        core_var("snes9x2002_frameskip", "disabled");
    else if (cfg.frameskip == 1) {
        core_var("snes9x2002_frameskip", "auto");
        core_var("snes9x2002_frameskip_interval", "4");
    } else {
        core_var("snes9x2002_frameskip", "fixed_interval");
        core_var("snes9x2002_frameskip_interval", fixed[(cfg.frameskip - 2) % 3]);
    }
}

const struct sysdef sys = {
    .id = "snes", .name = u"슈퍼패미컴 (SNES)", .game_h = 256, .scale = SC_WIDTH, .flags = SF_FRAMESKIP,
    .btn_names = { [PB_UP] = u"방향 위", [PB_DOWN] = u"방향 아래", [PB_LEFT] = u"방향 왼쪽", [PB_RIGHT] = u"방향 오른쪽",
                   [PB_TRIANGLE] = u"X 버튼", [PB_CIRCLE] = u"A 버튼", [PB_CROSS] = u"B 버튼", [PB_SQUARE] = u"Y 버튼",
                   [PB_L1] = u"L 버튼", [PB_R1] = u"R 버튼", [PB_START] = u"START", [PB_SELECT] = u"SELECT" },
    .bar = { PB_L1, PB_SELECT, PB_START, PB_R1, -1 },
    .bar_names = { u"L", u"SEL", u"STA", u"R" },
    .pad_names = { u"X", u"A", u"B", u"Y" },
    .apply = apply,
};

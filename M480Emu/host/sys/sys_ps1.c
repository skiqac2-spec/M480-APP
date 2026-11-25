/* PlayStation (PCSX-ReARMed) */
#include "host.h"
#include "psxcommon.h"

#define ONOFF(x) ((x) ? "enabled" : "disabled")

static void apply(void)
{
    static const char *fixed[] = { "1", "2", "3" };
    int sp = cfg.speed;

    core_var("pcsx_rearmed_drc", "enabled");
    core_var("pcsx_rearmed_drc_thread", "disabled");
    core_var("pcsx_rearmed_gpu_thread_rendering", "disabled");
    core_var("pcsx_rearmed_spu_thread", "disabled");
    core_var("pcsx_rearmed_scale_hires", "enabled");
    core_var("pcsx_rearmed_dithering", "disabled");
    core_var("pcsx_rearmed_show_bios_bootlogo", "disabled");
    core_var("pcsx_rearmed_memcard2", "disabled");
    core_var("pcsx_rearmed_display_fps_v2", "disabled");
    core_var("pcsx_rearmed_vibration", "disabled");
    core_var("pcsx_rearmed_region", cfg.region == 1 ? "NTSC" : cfg.region == 2 ? "PAL" : "auto");
    core_var("pcsx_rearmed_bios", cfg.use_bios ? "auto" : "HLE");
    core_var("pcsx_rearmed_nocdaudio", ONOFF(cfg.cdda));       /* "enabled" = play */
    core_var("pcsx_rearmed_noxadecoding", ONOFF(cfg.cdda));

    if (cfg.frameskip == 0)
        core_var("pcsx_rearmed_frameskip_type", "disabled");
    else if (cfg.frameskip == 1) {
        core_var("pcsx_rearmed_frameskip_type", "auto");
        core_var("pcsx_rearmed_frameskip_interval", "4");
    } else {
        core_var("pcsx_rearmed_frameskip_type", "fixed_interval");
        core_var("pcsx_rearmed_frameskip_interval", fixed[(cfg.frameskip - 2) % 3]);
    }

    /* speed presets: 0 quality, 1 balanced, 2 fast, 3 fastest */
    core_var("pcsx_rearmed_gpu_unai_old_renderer", ONOFF(sp >= 1));
    core_var("pcsx_rearmed_gpu_unai_blending", ONOFF(sp <= 2));
    core_var("pcsx_rearmed_gpu_unai_lighting", ONOFF(sp <= 2));
    core_var("pcsx_rearmed_gpu_unai_fast_lighting", ONOFF(sp >= 1));
    core_var("pcsx_rearmed_gpu_unai_skipline", ONOFF(sp >= 3));
    core_var("pcsx_rearmed_spu_reverb", ONOFF(sp == 0));
    core_var("pcsx_rearmed_spu_interpolation", sp == 0 ? "gaussian" : sp == 1 ? "simple" : "off");
    core_var("pcsx_rearmed_psxclock", sp <= 1 ? "auto" : sp == 2 ? "50" : "40");
    core_var("pcsx_rearmed_nostalls", ONOFF(sp >= 2));
    core_var("pcsx_rearmed_nogteflags", ONOFF(sp >= 3));
    core_var("pcsx_rearmed_gteregsunneeded", ONOFF(sp >= 3));
}

static int bios_found(void)
{
    unsigned i;
    for (i = 0; i < sizeof(Config.Bios) / sizeof(Config.Bios[0]); i++)
        if (Config.Bios[i][0]) return 1;
    return 0;
}

const struct sysdef sys = {
    .id = "ps1", .name = u"플레이스테이션", .game_h = 256, .scale = SC_WIDTH,
    .flags = SF_FRAMESKIP | SF_PS1OPTS,
    .btn_names = { u"방향 위", u"방향 아래", u"방향 왼쪽", u"방향 오른쪽", u"세모", u"동그라미", u"엑스", u"네모",
                   u"L1", u"R1", u"L2", u"R2", u"START", u"SELECT" },
    .bar = { PB_L2, PB_L1, PB_SELECT, PB_START, PB_R1, PB_R2, -1 },
    .bar_names = { u"L2", u"L1", u"SEL", u"STA", u"R1", u"R2" },
    .pad_names = { u"△", u"○", u"×", u"□" },
    .apply = apply, .bios_found = bios_found,
};

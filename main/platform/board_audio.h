#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hand the board's codec / amplifier power-up to the AMY synth gem
 *
 * Registers the power callback picoruby-amy calls when it starts its I2S
 * output (Tab5: ES8388 + SPK_EN, CrowPanel: amplifier supply on GPIO 30).
 * Call once from app_main after platform_init(), before any script can
 * start AMY. A no-op on boards without AMY.
 */
void board_audio_init(void);

#ifdef __cplusplus
}
#endif

#endif // BOARD_AUDIO_H

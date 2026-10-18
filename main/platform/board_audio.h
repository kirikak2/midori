#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio outputs with a master volume of their own */
typedef enum {
    BOARD_AUDIO_SPEAKER = 0,
    BOARD_AUDIO_HEADPHONE,
    BOARD_AUDIO_OUTPUT_COUNT
} board_audio_output_t;

/**
 * @brief Hand the board's codec / amplifier power-up to the AMY synth gem
 *
 * Registers the power callback picoruby-amy calls when it starts its I2S
 * output (Tab5: ES8388 + SPK_EN, CrowPanel: amplifier supply on GPIO 30)
 * and loads the master volumes saved in NVS.
 * Call once from app_main after platform_init(), before any script can
 * start AMY. A no-op on boards without AMY.
 */
void board_audio_init(void);

/**
 * @brief Poll the headphone jack (Tab5) and mute the speaker while it is in
 *
 * Call periodically from the UI task (platform_update). Rate-limited inside.
 */
void board_audio_update(void);

/** Whether the board has this output (Tab5: both, CrowPanel: speaker only). */
bool board_audio_has_output(board_audio_output_t out);

/**
 * @brief Set an output's master volume (0-100 %, 0 = off)
 *
 * Applied at once when AMY is running, otherwise when it powers up.
 * Not saved; call board_audio_save() when the user is done changing it.
 */
void board_audio_set_volume(board_audio_output_t out, uint8_t percent);
uint8_t board_audio_get_volume(board_audio_output_t out);

/** Write the master volumes to NVS (only when they changed). */
void board_audio_save(void);

/** Whether headphones are plugged in (always false without a jack). */
bool board_audio_headphone_inserted(void);

#ifdef __cplusplus
}
#endif

#endif // BOARD_AUDIO_H

#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenSettings : public Screen {
public:
    ScreenSettings();
    ~ScreenSettings() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Backlight accessors
    void setBacklight(uint8_t level);
    uint8_t getBacklight() const { return m_backlight; }

    // One slider per row, top to bottom. Volume rows exist only on boards
    // with that output (board_audio_has_output()).
    enum SliderId {
        SLIDER_BACKLIGHT = 0,
        SLIDER_SPEAKER,
        SLIDER_HEADPHONE,
        SLIDER_COUNT
    };

private:
    bool m_isActive;
    bool m_needsRedraw;
    uint8_t m_backlight;  // 0-100
    int m_dragSlider;     // slider the current touch started on, or -1
    int m_dragTouchId;
    bool m_hpInserted;    // last headphone state drawn

    void drawBleMidiSection();
    void drawSlider(int id);
    void drawSliderPartial(int id);
    void drawVersionInfo();
    bool sliderVisible(int id) const;
    int sliderY(int id) const;
    int hitTestSlider(int x, int y) const;
    void handleSliderTouch(int id, int x);
};

// Global instance
ScreenSettings& getScreenSettings();

extern "C" {
#endif

// C API
void ui_settings_set_backlight(uint8_t level);
uint8_t ui_settings_get_backlight(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_SETTINGS_H

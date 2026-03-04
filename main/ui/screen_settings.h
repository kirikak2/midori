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
    void onTouch(int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Backlight accessors
    void setBacklight(uint8_t level);
    uint8_t getBacklight() const { return m_backlight; }

private:
    bool m_isActive;
    bool m_needsRedraw;
    uint8_t m_backlight;  // 0-100

    void drawBleMidiSection();
    void drawBacklightSlider();
    void drawVersionInfo();
    bool hitTestBacklight(int x, int y);
    void handleBacklightTouch(int x);
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

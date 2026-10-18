#ifndef SCREEN_MIDI_INFO_H
#define SCREEN_MIDI_INFO_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenMidiInfo : public Screen {
public:
    ScreenMidiInfo();
    ~ScreenMidiInfo() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    const char* getTitle() override;
    void getTileStatus(char* buf, int len) override;

private:
    bool m_isActive;
    bool m_needsRedraw;
    uint32_t m_lastDiagMs;

    void drawDeviceCard(int y, const char* icon, const char* type,
                       const char* name, bool inConnected, bool outConnected);
    void drawUsbMidiCard();
    void drawDinMidiCard();
    void drawBleMidiCard();
    void drawUsbDeviceDiag();
};

// Global instance
ScreenMidiInfo& getScreenMidiInfo();

#endif // __cplusplus

#endif // SCREEN_MIDI_INFO_H

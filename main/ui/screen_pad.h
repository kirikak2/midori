#ifndef SCREEN_PAD_H
#define SCREEN_PAD_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenPads : public Screen {
public:
    ScreenPads();
    ~ScreenPads() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int x, int y, bool pressed) override;
    const char* getTitle() override;

private:
    bool m_isActive;
    bool m_needsRedraw;
    int m_pressedPad;  // Currently pressed pad (-1 = none)

    void drawPad(int index);
    void drawAllPads();
    int hitTestPad(int x, int y);
    void handlePadPress(int padIndex, bool pressed);
    void getPadRect(int index, int& x, int& y, int& w, int& h);
};

// Global instance
ScreenPads& getScreenPads();

#endif // __cplusplus

#endif // SCREEN_PAD_H

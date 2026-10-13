#pragma once

#include <memory>
#include <string>

#include "Engine/Objects/ItemEnums.h"
#include "Engine/Pid.h"

#include "Library/Geometry/Point.h"

class GraphicsImage;

namespace Io {
class Mouse {
 public:
    inline Mouse() : cursor_img(nullptr) {
        pCursorBitmap_sysmem = nullptr;
        pCursorBitmap2_sysmem = nullptr;
    }

    Pointi position() const;
    Pointi setPosition(Pointi position);
    /**
     * Moves the mouse pointer.
     * @param position                  Position in render coordinates to move mouse pointer to.
     */
    void warpMouse(Pointi position);

    void SetCursorBitmapFromItemID(ItemId uItemID);
    void SetCurrentCursorBitmap();
    void SetCursorImage(std::string_view name);
    void ClearCursor();
    void AllocCursorSystemMem();
    void *DoAllocCursorMem();
    void Initialize();
    void DrawCursor();

    /**
     * Draws the cursor into the UI target when the UI is a layer of its own and `DrawCursor` hasn't run since the
     * last call. The main menu, party creation, Arcomage and the other screens with frame loops of their own leave
     * the cursor to the system, which a headset doesn't show. Call once per frame, before the UI is copied out.
     */
    void drawLayerCursor();
    void DrawPickedItem();

    void UI_OnMouseLeftClick(bool isDoubleClick);


    int field_8 = 0;
    bool _arrowCursor = 0;
    int bInitialized = 0;
    GraphicsImage *cursor_img = nullptr;
    uint16_t *pCursorBitmap_sysmem = nullptr;
    uint8_t *pCursorBitmap2_sysmem = nullptr;
    Pointi pickedItemOffset = {}; // Offset of the item's bitmap relative to cursor position, always non-positive.
    Pointi pCursorBitmapPos{};
    std::string cursor_name;
    Pointi _position;

    enum class MouseLookState { Disabled, Enabled, Suspended };
    using enum MouseLookState;
    void SetMouseLook(MouseLookState look);
    void ToggleMouseLook();
    void DoMouseLook(Pointi relChange);
    void RestoreMouseLook();
    MouseLookState _mouseLook = MouseLookState::Disabled;

 private:
    bool _cursorDrawn = false; // Whether `DrawCursor` ran since the last `drawLayerCursor`.
};
}  // namespace Io

extern std::shared_ptr<Io::Mouse> mouse;

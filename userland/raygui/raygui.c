// ============================
// GraceOS Minimal raygui Shim
// ============================
//
// NASA-Style Design Contract:
// - Single Responsibility: GUI widget rendering
// - Deterministic Execution: Same input = same output
// - Explicit Initialization: All locals zero-initialized
//

#include "raygui.h"

/**
 * GuiButton - Draw a clickable button.
 *
 * Inputs:
 *   bounds - Rectangle defining button position and size
 *   text   - Button label text (may be NULL)
 *
 * Outputs: Returns 1 if clicked this frame, 0 otherwise
 * Errors:  None (invalid bounds render empty button)
 * Side effects: Draws to framebuffer
 */
int GuiButton(Rectangle bounds, const char* text)
{
    // Get mouse state
    Vector2 mouse = GetMousePosition();
    
    // Calculate hover state
    int hover = 0;
    if (mouse.x >= bounds.x && mouse.x <= bounds.x + bounds.width &&
        mouse.y >= bounds.y && mouse.y <= bounds.y + bounds.height)
    {
        hover = 1;
    }

    // Draw button (hover changes appearance)
    Color bg = hover ? (Color){160, 160, 160, 255} : (Color){120, 120, 120, 255};
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, bg);
    DrawRectangleLines((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, BLACK);
    
    // Draw text if provided
    if (text != NULL)
    {
        DrawText(text, (int)bounds.x + 8, (int)bounds.y + 6, 16, BLACK);
    }

    // Check for click (mouse if available, keyboard fallback)
    if ((hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ||
        IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
    {
        return 1;
    }

    return 0;
}

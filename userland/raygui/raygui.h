// ============================
// GraceOS Minimal raygui Shim
// ============================
//
// NASA-Style Design Contract:
// - Single Responsibility: GUI widget rendering
// - Deterministic Execution: Same input = same output
// - All widgets validate inputs before drawing
//

#ifndef RAYGUI_H
#define RAYGUI_H

#include "../raylib/raylib.h"

int GuiButton(Rectangle bounds, const char* text);

#endif /* RAYGUI_H */

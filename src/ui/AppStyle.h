#pragma once

class QApplication;

/// Applies the shared Fusion style, dark palette and default font.
/// Kept separate from main() so development tools can render the real look.
void applyAppStyle(QApplication &application);

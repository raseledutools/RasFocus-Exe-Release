#ifndef TAB_AI_H
#define TAB_AI_H

#include <windows.h>
#include <gdiplus.h>

// --- Settings Management ---
void SaveAiSettings();
void LoadAiSettings();

// --- Core UI Functions for AI Filter Tab ---
// Draws the main UI of the AI Filter tab
void DrawAiFilterTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);

// Handles mouse hover events
void ProcessAiFilterMouseMove(float x, float y);

// Handles mouse click events
void ProcessAiFilterMouseClick(float x, float y);

// Handles scrolling inside the specific app views (like YouTube, TikTok)
void ProcessAiFilterMouseWheel(float x, float y, int delta);

#endif // TAB_AI_H
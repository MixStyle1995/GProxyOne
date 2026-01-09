// Copyright 2024-2025 Aidan Sun and the ImGuiTextSelect contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <limits>

#include <imgui.h>

// Manages text selection in a GUI window.
// This class only works if the window only has text.
// The window should also have the "NoMove" flag set so mouse drags can be used to select text.
class TextSelect {
    // Cursor position in the window.
    struct CursorPos {
        std::size_t x = static_cast<std::size_t>(-1); // X index of character
        std::size_t y = static_cast<std::size_t>(-1); // Y index of character

        // Checks if this position is invalid.
        bool isInvalid() const {
            // Invalid cursor positions are indicated by -1
            return x == static_cast<std::size_t>(-1) || y == static_cast<std::size_t>(-1);
        }
    };

    // Text selection in the window.
    // Y - index of _whole_ line.
    // X - character index relative to beginning of that whole line.
    struct Selection {
        std::size_t startX;
        std::size_t startY;
        std::size_t endX;
        std::size_t endY;
    };

    struct SubLine {
        std::string string;
        std::size_t wholeLineIndex; // Which whole line this subline belongs to.
        
        SubLine() : wholeLineIndex(0) {}
        SubLine(const std::string& str, std::size_t idx) : string(str), wholeLineIndex(idx) {}
        SubLine(const char* str, std::size_t idx) : string(str), wholeLineIndex(idx) {}
    };

    // Selection bounds
    // In a selection, the start and end positions may not be in order (the user can click and drag left/up which
    // reverses start and end).
    CursorPos selectStart;
    CursorPos selectEnd;

    // Accessor functions to get line information
    // This class only knows about line numbers so it must be provided with functions that give it text data.
    std::function<std::string(std::size_t)> getLineAtIdx; // Gets the string given a line number
    std::function<std::size_t()> getNumLines; // Gets the total number of lines

    // Indicates whether selection should be updated. This is needed for distinguishing mouse drags that are
    // initiated by clicking the text, or different element.
    bool shouldHandleMouseDown;

    // Indicates whether text selection with word wrapping should be enabled.
    bool enableWordWrap;

    // Gets the user selection. Start and end are guaranteed to be in order.
    Selection getSelection() const;

    // Splits all whole lines by wrap width if wrapping is enabled. Otherwise returns whole lines.
    ImVector<TextSelect::SubLine> getSubLines() const;

    // Processes mouse down (click/drag) events.
    void handleMouseDown(const ImVector<TextSelect::SubLine>& subLines, const ImVec2& cursorPosStart);

    // Processes scrolling events.
    void handleScrolling() const;

    // Draws the text selection rectangle in the window.
    void drawSelection(const ImVector<TextSelect::SubLine>& subLines, const ImVec2& cursorPosStart) const;

public:
    // Constructor with function pointers
    TextSelect(const std::function<std::string(std::size_t)>& getLineAtIdx, 
               const std::function<std::size_t()>& getNumLines, 
               bool enableWordWrap = false) 
        : getLineAtIdx(getLineAtIdx)
        , getNumLines(getNumLines)
        , enableWordWrap(enableWordWrap)
        , shouldHandleMouseDown(false)
    {
    }

    // Checks if there is an active selection in the text.
    bool hasSelection() const {
        return !selectStart.isInvalid() && !selectEnd.isInvalid();
    }

    // Copies the selected text to the clipboard.
    void copy() const;

    // Selects all text in the window.
    void selectAll();

    // Draws the text selection rectangle and handles user input.
    void update();

    // Clears the current text selection.
    void clearSelection() {
        selectStart = CursorPos();
        selectEnd = CursorPos();
    }
};

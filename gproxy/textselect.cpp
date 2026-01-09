// Copyright 2024-2025 Aidan Sun and the ImGuiTextSelect contributors
// SPDX-License-Identifier: MIT

#define IMGUI_DEFINE_MATH_OPERATORS

#include "textselect.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

// Calculates the midpoint between two numbers.
template <typename T>
T midpoint(T a, T b) {
    return a + (b - a) / 2;
}

// Checks if a string ends with the specified char suffix.
bool endsWith(const std::string& str, char suffix) {
    return !str.empty() && str.back() == suffix;
}

// Simple word boundary detection, accounts for Latin Unicode blocks only.
static bool isBoundary(ImWchar32 c) {
    struct Range {
        ImWchar32 min;
        ImWchar32 max;
    };
    
    Range ranges[] = {
        { 0x20, 0x2F },
        { 0x3A, 0x40 },
        { 0x5B, 0x60 },
        { 0x7B, 0xBF }
    };
    
    for (int i = 0; i < 4; i++) {
        if (c >= ranges[i].min && c <= ranges[i].max) {
            return true;
        }
    }
    return false;
}

// Gets the number of UTF-8 characters (not bytes) in a string.
static std::size_t utf8Length(const std::string& s) {
    return ImTextCountCharsFromUtf8(s.c_str(), s.c_str() + s.size());
}

static std::size_t utf8Length(const char* start, const char* end) {
    return ImTextCountCharsFromUtf8(start, end);
}

// Increments a string iterator by a specified number of UTF-8 characters (not bytes).
static const char* advanceUtf8(const char* text, int n) {
    const char* p = text;
    while (n > 0 && *p) {
        unsigned int c;
        p += ImTextCharFromUtf8(&c, p, nullptr);
        n--;
    }
    return p;
}

// Reads a single UTF-8 character at a given string iterator.
inline static ImWchar32 textCharFromUtf8(const char* in_text, const char* in_text_end = nullptr) {
    ImWchar32 c;
    return ImTextCharFromUtf8(&c, in_text, in_text_end) > 0 ? c : 0;
}

// Gets the display width of a substring.
static float substringSizeX(const std::string& s, std::size_t start, std::size_t length = static_cast<std::size_t>(-1)) {
    // For an empty string, data() or begin() == end()
    if (s.empty()) {
        return 0;
    }

    // Convert char-based start and length into byte-based iterators
    const char* stringStart = advanceUtf8(s.c_str(), static_cast<int>(start));

    const char* stringEnd = length == static_cast<std::size_t>(-1)
        ? s.c_str() + s.size()
        : advanceUtf8(stringStart, static_cast<int>((std::min)(utf8Length(s), length)));

    // Calculate text size between start and end
    return ImGui::CalcTextSize(stringStart, stringEnd).x;
}

// Gets the index of the character the mouse cursor is over.
static std::size_t getCharIndex(const std::string& s, float cursorPosX, std::size_t start, std::size_t end) {
    // Ignore cursor position when it is invalid
    if (cursorPosX < 0) {
        return 0;
    }

    // Check for exit conditions
    if (s.empty()) {
        return 0;
    }
    if (end < start) {
        return utf8Length(s);
    }

    // Midpoint of given string range
    std::size_t midIdx = midpoint(start, end);

    // Display width of the entire string up to the midpoint, gives the x-position where the (midIdx + 1)th char starts
    float widthToMid = substringSizeX(s, 0, midIdx + 1);

    // Same as above but exclusive, gives the x-position where the (midIdx)th char starts
    float widthToMidEx = substringSizeX(s, 0, midIdx);

    // Perform a recursive binary search to find the correct index
    // If the mouse position is between the (midIdx)th and (midIdx + 1)th character positions, the search ends
    if (cursorPosX < widthToMidEx) {
        return getCharIndex(s, cursorPosX, start, midIdx - 1);
    } else if (cursorPosX > widthToMid) {
        return getCharIndex(s, cursorPosX, midIdx + 1, end);
    } else {
        return midIdx;
    }
}

// Wrapper for getCharIndex providing the initial bounds.
static std::size_t getCharIndex(const std::string& s, float cursorPosX) {
    return getCharIndex(s, cursorPosX, 0, utf8Length(s));
}

// Gets the scroll delta for the given cursor position and window bounds.
static float getScrollDelta(float v, float min, float max) {
    const float deltaScale = 10.0f * ImGui::GetIO().DeltaTime;
    const float maxDelta = 100.0f;

    if (v < min) {
        return (std::max)(-(min - v), -maxDelta) * deltaScale;
    } else if (v > max) {
        return (std::min)(v - max, maxDelta) * deltaScale;
    }

    return 0.0f;
}

TextSelect::Selection TextSelect::getSelection() const {
    // Start and end may be out of order (ordering is based on Y position)
    bool startBeforeEnd = selectStart.y < selectEnd.y || (selectStart.y == selectEnd.y && selectStart.x < selectEnd.x);

    // Reorder X points if necessary
    std::size_t startX = startBeforeEnd ? selectStart.x : selectEnd.x;
    std::size_t endX = startBeforeEnd ? selectEnd.x : selectStart.x;

    // Get min and max Y positions for start and end
    std::size_t startY = (std::min)(selectStart.y, selectEnd.y);
    std::size_t endY = (std::max)(selectStart.y, selectEnd.y);

    Selection result;
    result.startX = startX;
    result.startY = startY;
    result.endX = endX;
    result.endY = endY;
    return result;
}

//
// Taken from imgui_draw.cpp
//
// Trim trailing space and find beginning of next line
static inline const char* CalcWordWrapNextLineStartA(const char* text, const char* text_end) {
    while (text < text_end && ImCharIsBlankA(*text)) text++;
    if (*text == '\n') text++;
    return text;
}

// Split `text` that does not fit in `wrapWidth` into multiple lines.
// result.size() is never 0.
static ImVector<std::string> wrapText(const std::string& text, float wrapWidth) {
    ImFont* font = ImGui::GetCurrentContext()->Font;
    const float size = ImGui::GetFontSize();

    ImVector<std::string> result;

    const char* textEnd = text.c_str() + text.size();
    const char* wrappedLineStart = text.c_str();
    const char* wrappedLineEnd = text.c_str();
    while (wrappedLineEnd != textEnd) {
        wrappedLineStart = wrappedLineEnd;
        wrappedLineEnd = font->CalcWordWrapPositionA(size, wrappedLineStart, textEnd, wrapWidth);

        if (wrappedLineEnd - wrappedLineStart != 0) {
            result.push_back(std::string(wrappedLineStart, wrappedLineEnd));
        }

        wrappedLineEnd = CalcWordWrapNextLineStartA(wrappedLineEnd, textEnd);
    }

    // Treat empty text as one empty line.
    if (result.size() == 0) {
        result.push_back(std::string());
    }

    return result;
}

ImVector<TextSelect::SubLine> TextSelect::getSubLines() const {
    ImVector<SubLine> result;

    std::size_t numLines = getNumLines();

    // There will be at minimum `numLines` sublines.
    result.reserve(static_cast<int>(numLines));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float wrapWidth = ImGui::CalcWrapWidthForPos(window->DC.CursorPos, 0);

    for (std::size_t i = 0; i < numLines; ++i) {
        std::string wholeLine = getLineAtIdx(i);
        if (enableWordWrap) {
            ImVector<std::string> subLinesVec = wrapText(wholeLine, wrapWidth);
            for (int j = 0; j < subLinesVec.size(); j++) {
                result.push_back(SubLine(subLinesVec[j], i));
            }
        } else {
            result.push_back(SubLine(wholeLine, i));
        }
    }
    return result;
}

void TextSelect::handleMouseDown(const ImVector<SubLine>& subLines, const ImVec2& cursorPosStart) {
    if (subLines.size() == 0) {
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float wrapWidth = ImGui::CalcWrapWidthForPos(window->DC.CursorPos, 0);
    const float textHeight = ImGui::GetTextLineHeight();

    // Position of the mouse cursor in the window
    const ImVec2 mousePos = ImGui::GetMousePos();
    const ImVec2 cursorPos = mousePos - cursorPosStart;

    // Y position of the mouse in the text
    float currentY = 0;

    // Find which line the mouse is on
    std::size_t lineIdx = 0;
    for (; lineIdx < static_cast<std::size_t>(subLines.size()); lineIdx++) {
        std::string wholeLine = getLineAtIdx(subLines[lineIdx].wholeLineIndex);
        
        const char* subLineEnd = subLines[lineIdx].string.c_str() + subLines[lineIdx].string.size();
        const char* wholeLineEnd = wholeLine.c_str() + wholeLine.size();

        float nextY = currentY + textHeight;
        if (subLineEnd == wholeLineEnd) {
            nextY += ImGui::GetStyle().ItemSpacing.y;
        }

        if (cursorPos.y >= currentY && cursorPos.y < nextY) {
            break;
        }

        currentY = nextY;
    }

    // Invalid mouse position
    if (lineIdx >= static_cast<std::size_t>(subLines.size())) {
        return;
    }

    const SubLine& currentSubLine = subLines[lineIdx];
    std::size_t wholeY = currentSubLine.wholeLineIndex;

    // Find the character under the mouse in the current line
    std::string currentWholeLine = getLineAtIdx(wholeY);
    
    const char* subLineStart = currentSubLine.string.c_str();
    const char* wholeLineStart = currentWholeLine.c_str();
    
    std::size_t subLineStartX = utf8Length(wholeLineStart, subLineStart);
    std::size_t subX = getCharIndex(currentSubLine.string, cursorPos.x);
    std::size_t wholeX = subLineStartX + subX;

    // Handle mouse events
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // Double click - select word
            bool isCurrentBoundary = isBoundary(textCharFromUtf8(advanceUtf8(currentWholeLine.c_str(), static_cast<int>(wholeX))));

            selectStart.x = wholeX;
            selectStart.y = wholeY;
            selectEnd.x = wholeX;
            selectEnd.y = wholeY;

            const char* startIt = advanceUtf8(currentWholeLine.c_str(), static_cast<int>(wholeX));
            const char* endIt = startIt;

            // Scan to left until a word boundary is reached
            for (std::size_t start = wholeX; start > 0; start--) {
                selectStart.x = start;
                if (isBoundary(textCharFromUtf8(startIt)) != isCurrentBoundary) {
                    break;
                }
                startIt = ImTextFindPreviousUtf8Codepoint(currentWholeLine.c_str(), startIt);
            }

            // Scan to right until a word boundary is reached
            for (std::size_t end = wholeX; end <= utf8Length(currentWholeLine); end++) {
                selectEnd.x = end;
                if (isBoundary(textCharFromUtf8(endIt)) != isCurrentBoundary) {
                    break;
                }
                endIt = advanceUtf8(endIt, 1);
            }
        } else if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
            // Single click with shift - select text from start to click
            // The selection starts from the beginning if no start position exists
            if (selectStart.isInvalid()) {
                selectStart.x = 0;
                selectStart.y = 0;
            }

            selectEnd.x = wholeX;
            selectEnd.y = wholeY;
        } else {
            // Single click - set start position, invalidate end position
            selectStart.x = wholeX;
            selectStart.y = wholeY;
            selectEnd.x = static_cast<std::size_t>(-1);
            selectEnd.y = static_cast<std::size_t>(-1);
        }
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        // Mouse dragging - set end position
        selectEnd.x = wholeX;
        selectEnd.y = wholeY;
    }
}

void TextSelect::handleScrolling() const {
    // Window boundaries
    ImVec2 windowMin = ImGui::GetWindowPos();
    ImVec2 windowMax = windowMin + ImGui::GetWindowSize();

    // Get current and active window information from Dear ImGui state
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const ImGuiWindow* activeWindow = GImGui->ActiveIdWindow;

    ImGuiID scrollXID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_X);
    ImGuiID scrollYID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_Y);
    ImGuiID activeID = ImGui::GetActiveID();
    bool scrollbarsActive = activeID == scrollXID || activeID == scrollYID;

    // Do not handle scrolling if:
    // - There is no active window
    // - The current window is not active
    // - The user is scrolling via the scrollbars
    if (activeWindow == nullptr || activeWindow->ID != currentWindow->ID || scrollbarsActive) {
        return;
    }

    // Get scroll deltas from mouse position
    ImVec2 mousePos = ImGui::GetMousePos();
    float scrollXDelta = getScrollDelta(mousePos.x, windowMin.x, windowMax.x);
    float scrollYDelta = getScrollDelta(mousePos.y, windowMin.y, windowMax.y);

    // If there is a nonzero delta, scroll in that direction
    if (std::abs(scrollXDelta) > 0.0f) {
        ImGui::SetScrollX(ImGui::GetScrollX() + scrollXDelta);
    }
    if (std::abs(scrollYDelta) > 0.0f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + scrollYDelta);
    }
}

static void drawSelectionRect(const ImVec2& cursorPosStart, float minX, float minY, float maxX, float maxY) {
    // Get rectangle corner points offset from the cursor's start position in the window
    ImVec2 rectMin = cursorPosStart + ImVec2(minX, minY);
    ImVec2 rectMax = cursorPosStart + ImVec2(maxX, maxY);

    // Draw the rectangle
    ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color);
}

void TextSelect::drawSelection(const ImVector<SubLine>& subLines, const ImVec2& cursorPosStart) const {
    if (!hasSelection()) {
        return;
    }

    // Start and end positions
    Selection sel = getSelection();
    std::size_t startX = sel.startX;
    std::size_t startY = sel.startY;
    std::size_t endX = sel.endX;
    std::size_t endY = sel.endY;

    std::size_t numLines = getNumLines();
    if (startY >= numLines || endY >= numLines) {
        return;
    }

    float accumulatedHeight = 0;

    ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float wrapWidth = ImGui::CalcWrapWidthForPos(window->DC.CursorPos, 0);
    const float newlineWidth = ImGui::CalcTextSize(" ").x;
    const float textHeight = context->FontSize;
    const float itemSpacing = context->Style.ItemSpacing.y;

    for (int i = 0; i < subLines.size(); i++) {
        const SubLine& subLine = subLines[i];
        std::string wholeLine = getLineAtIdx(subLine.wholeLineIndex);

        const char* wholeLineEnd = wholeLine.c_str() + wholeLine.size();

        const char* subLineStart = subLine.string.c_str();
        const char* subLineEnd = subLine.string.c_str() + subLine.string.size();

        // Indices of sub-line bounds relative to the start of the whole line.
        std::size_t subLineStartX = utf8Length(wholeLine.c_str(), subLineStart);
        std::size_t subLineEndX = utf8Length(wholeLine.c_str(), subLineEnd);

        float minY = accumulatedHeight;
        accumulatedHeight += textHeight;
        // Item spacing is not applied between sub-lines
        if (subLineEnd == wholeLineEnd) {
            // We are rendering last sub-line.
            accumulatedHeight += itemSpacing;
        }
        float maxY = accumulatedHeight;

        // Skip whole/sub lines before selection.
        if (startY > subLine.wholeLineIndex || (subLine.wholeLineIndex == startY && startX >= subLineEndX)) {
            continue;
        }
        // Skip whole/sub lines after selection.
        if (endY < subLine.wholeLineIndex || (subLine.wholeLineIndex == endY && endX < subLineStartX)) {
            break;
        }

        // The first and last rectangles should only extend to the selection boundaries
        // The middle rectangles (if any) enclose the entire line + some extra width for the newline.
        bool isStartSubLine = subLine.wholeLineIndex == startY && subLineStartX <= startX && startX <= subLineEndX;
        bool isEndSubLine = subLine.wholeLineIndex == endY && subLineStartX <= endX && endX <= subLineEndX;

        float minX = isStartSubLine ? substringSizeX(subLine.string, 0, startX - (std::min)(subLineStartX, startX)) : 0;
        float maxX = isEndSubLine ? substringSizeX(subLine.string, 0, endX - (std::min)(subLineStartX, endX))
                                  : substringSizeX(subLine.string, 0) + newlineWidth;

        drawSelectionRect(cursorPosStart, minX, minY, maxX, maxY);
    }
}

void TextSelect::copy() const {
    if (!hasSelection()) {
        return;
    }

    Selection sel = getSelection();
    std::size_t startX = sel.startX;
    std::size_t startY = sel.startY;
    std::size_t endX = sel.endX;
    std::size_t endY = sel.endY;

    // Collect selected text in a single string
    std::string selectedText;

    for (std::size_t i = startY; i <= endY; i++) {
        // Similar logic to drawing selections
        std::size_t subStart = i == startY ? startX : 0;
        std::string line = getLineAtIdx(i);

        const char* stringStart = advanceUtf8(line.c_str(), static_cast<int>(subStart));
        const char* stringEnd = i == endY ? advanceUtf8(stringStart, static_cast<int>(endX - subStart)) : line.c_str() + line.size();

        std::string lineToAdd = line.substr(stringStart - line.c_str(), stringEnd - stringStart);
        selectedText += lineToAdd;

        // If lines before the last line don't already end with newlines, add them in
        if (!endsWith(lineToAdd, '\n') && i < endY) {
            selectedText += '\n';
        }
    }

    ImGui::SetClipboardText(selectedText.c_str());
}

void TextSelect::selectAll() {
    std::size_t lastLineIdx = getNumLines() - 1;
    std::string lastLine = getLineAtIdx(lastLineIdx);

    // Set the selection range from the beginning to the end of the last line
    selectStart.x = 0;
    selectStart.y = 0;
    selectEnd.x = utf8Length(lastLine);
    selectEnd.y = lastLineIdx;
}

void TextSelect::update() {
    // ImGui::GetCursorStartPos() is in window coordinates so it is added to the window position
    ImVec2 cursorPosStart = ImGui::GetWindowPos() + ImGui::GetCursorStartPos();

    // Switch cursors if the window is hovered
    bool hovered = ImGui::IsWindowHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }

    // Split whole lines by wrap width (if enabled).
    ImVector<SubLine> subLines = getSubLines();

    // Handle mouse events
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hovered) {
            shouldHandleMouseDown = true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        shouldHandleMouseDown = false;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (shouldHandleMouseDown) {
            handleMouseDown(subLines, cursorPosStart);
        }
        if (!hovered) {
            handleScrolling();
        }
    }

    drawSelection(subLines, cursorPosStart);

    // Keyboard shortcuts
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        selectAll();
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        copy();
    }
}

/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "fileBrowser.h"

namespace fs = std::filesystem;

// Supplied by the embedding app under these exact names — see fileBrowser.h.
extern "C" _Atomic bool gReDraw;
extern "C" void get_global_gui_scaled_mouse_coord(tCoord * coord);

namespace {

struct tFileBrowserEntry {
    std::string name;
    bool        isDir;
};

// Left-hand quick-access column, built fresh each time the browser opens: a fixed Favorites
// group (Home/Desktop/Documents/Downloads/iCloud Drive, whichever exist) plus a Locations group
// with the boot volume and every currently-mounted volume under /Volumes — a real equivalent of
// NSOpenPanel's sidebar rather than a hardcoded guess at what's mounted.
struct tSidebarItem {
    std::string label;
    std::string path;    // Empty for a section header — headers aren't clickable.
    bool        isHeader;
};

struct tFileBrowserState {
    bool                 active         = false;
    tFileBrowserMode     mode           = fileBrowserModeOpenFile;
    tFileBrowserCallback callback       = nullptr;
    std::string          title;
    std::string          currentDir;
    std::vector<tFileBrowserEntry> entries;
    std::vector<tSidebarItem>      sidebar;
    int32_t              selectedIndex  = -1;
    double                scrollOffset  = 0.0;
    char                 filenameBuffer[512] = {0};
    uint32_t             filenameCursor = 0;
    bool                 overwriteArmed = false; // Save mode: first click on an existing name arms a second confirm
    tRectangle            panelRect      = {0};
    bool                 closePressed   = false;
    bool                 cancelPressed  = false;
    bool                 confirmPressed = false;
};

tFileBrowserState sState;
std::string        sLastDirectory;
tFileBrowserDirectoryChangedCallback sDirectoryChangedCallback = nullptr;

const double kRowHeight     = STANDARD_TEXT_HEIGHT + 8.0;
const int    kVisibleRows   = 12;
const double kSidebarWidth  = 150.0;
const double kPanelWidth    = 730.0;
const double kPanelHeight   = 60.0 + (kRowHeight * kVisibleRows) + 70.0;

// X origin every content-area element (path label, listing, filename field) hangs off — the
// sidebar occupies the strip to its left.
double content_x(void) {
    return sState.panelRect.coord.x + kSidebarWidth + 10.0;
}

// render_text() doesn't clip to its rectangle — a name wider than the row it's drawn in just
// overruns whatever's to its right (this is how the sidebar's longer volume/app names were
// bleeding into the file list). Shortens from the end with an ellipsis until it fits maxWidth.
std::string truncate_to_width(const std::string & text, double maxWidth) {
    if (get_text_width(text.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth) {
        return text;
    }

    std::string truncated = text;

    while (!truncated.empty()) {
        truncated.pop_back();
        std::string withEllipsis = truncated + "...";

        if (get_text_width(withEllipsis.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth) {
            return withEllipsis;
        }
    }
    return "...";
}

std::string default_start_directory(void) {
    const char * home = getenv("HOME");

    if (home == nullptr) {
        home = getenv("USERPROFILE"); // Windows
    }
    if (home != nullptr) {
        return std::string(home);
    }
    std::error_code ec;
    fs::path        cwd = fs::current_path(ec);

    return ec ? std::string(".") : cwd.string();
}

void refresh_sidebar(void) {
    sState.sidebar.clear();

    std::string home = default_start_directory();

    sState.sidebar.push_back({"Favorites", "", true});
    sState.sidebar.push_back({"Home", home, false});

    static const char * kFavoriteSubfolders[] = {"Desktop", "Documents", "Downloads"};

    for (const char * sub : kFavoriteSubfolders) {
        std::error_code ec;
        std::string     path = home + "/" + sub;

        if (fs::exists(path, ec) && !ec) {
            sState.sidebar.push_back({sub, path, false});
        }
    }

    std::error_code icloudEc;
    std::string     icloudPath = home + "/Library/Mobile Documents/com~apple~CloudDocs";

    if (fs::exists(icloudPath, icloudEc) && !icloudEc) {
        sState.sidebar.push_back({"iCloud Drive", icloudPath, false});
    }

    sState.sidebar.push_back({"Locations", "", true});
    sState.sidebar.push_back({"Macintosh HD", "/", false});

    std::error_code           volEc;
    fs::directory_iterator    volIt("/Volumes", volEc);
    std::vector<tSidebarItem> volumes;

    if (!volEc) {
        for (const auto & entry : volIt) {
            std::error_code eqEc;

            if (fs::equivalent(entry.path(), "/", eqEc) && !eqEc) {
                continue; // Already listed as "Macintosh HD" above
            }
            std::string name = entry.path().filename().string();

            if (name.empty() || (name[0] == '.')) {
                continue;
            }
            volumes.push_back({name, entry.path().string(), false});
        }
    }
    std::sort(volumes.begin(), volumes.end(), [](const tSidebarItem & a, const tSidebarItem & b) {
        return a.label < b.label;
    });

    for (const tSidebarItem & v : volumes) {
        sState.sidebar.push_back(v);
    }
}

void refresh_directory_listing(void) {
    sState.entries.clear();
    std::error_code ec;
    fs::directory_iterator it(sState.currentDir, ec);

    if (!ec) {
        for (const auto & entry : it) {
            std::error_code   isDirEc;
            bool               isDir = entry.is_directory(isDirEc);
            std::string        name  = entry.path().filename().string();

            if (isDirEc || name.empty() || name[0] == '.') {
                continue;
            }
            sState.entries.push_back({name, isDir});
        }
    }

    std::sort(sState.entries.begin(), sState.entries.end(), [](const tFileBrowserEntry & a, const tFileBrowserEntry & b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        return a.name < b.name;
    });

    sState.selectedIndex  = -1;
    sState.scrollOffset   = 0.0;
    sState.overwriteArmed = false;
}

void navigate_to(const std::string & dir) {
    sState.currentDir = dir;
    refresh_directory_listing();
    gReDraw = true;
}

void begin_browse(tFileBrowserMode mode, tFileBrowserCallback callback, const char * title, const char * defaultName) {
    sState.active   = true;
    sState.mode     = mode;
    sState.callback = callback;
    sState.title    = (title != nullptr) ? title : "";

    if (sLastDirectory.empty()) {
        sLastDirectory = default_start_directory();
    }
    sState.currentDir = sLastDirectory;

    std::memset(sState.filenameBuffer, 0, sizeof(sState.filenameBuffer));

    if (defaultName != nullptr) {
        std::strncpy(sState.filenameBuffer, defaultName, sizeof(sState.filenameBuffer) - 1);
        sState.filenameCursor = (uint32_t)std::strlen(sState.filenameBuffer);
    } else {
        sState.filenameCursor = 0;
    }

    refresh_directory_listing();
    refresh_sidebar();

    double renderW = get_render_width() / gGlobalGuiScale;
    double renderH = get_render_height() / gGlobalGuiScale;

    sState.panelRect = (tRectangle){
        {
            (renderW - kPanelWidth) / 2.0, (renderH - kPanelHeight) / 2.0
        }, {
            kPanelWidth, kPanelHeight
        }
    };

    gReDraw = true;
}

void finish_browse(const char * resultPath) {
    tFileBrowserCallback callback = sState.callback;

    if (resultPath != nullptr) {
        sLastDirectory = sState.currentDir;

        if (sDirectoryChangedCallback != nullptr) {
            sDirectoryChangedCallback(sLastDirectory.c_str());
        }
    }
    sState.active   = false;
    sState.callback = nullptr;
    gReDraw         = true;

    if (callback != nullptr) {
        callback(resultPath);
    }
}

tRectangle list_area_rect(void) {
    return (tRectangle){
        {
            content_x(), sState.panelRect.coord.y + 60.0
        }, {
            kPanelWidth - kSidebarWidth - 20.0, kRowHeight * kVisibleRows
        }
    };
}

tRectangle sidebar_rect(void) {
    return (tRectangle){
        {
            sState.panelRect.coord.x + 10.0, sState.panelRect.coord.y + 60.0
        }, {
            kSidebarWidth - 10.0, kRowHeight * kVisibleRows
        }
    };
}

tRectangle sidebar_row_rect(int index) {
    tRectangle rect = sidebar_rect();

    return (tRectangle){
        {
            rect.coord.x, rect.coord.y + ((double)index * kRowHeight)
        }, {
            rect.size.w, kRowHeight
        }
    };
}

int32_t row_count_scrolled(void) {
    return (int32_t)((sState.entries.size() > (size_t)kVisibleRows) ? (sState.entries.size() - (size_t)kVisibleRows) : 0);
}

// draw_button() sizes its label text directly off the height of the rectangle passed in (see
// utilsGraphics.cpp — there's no separate font-size parameter), so every button here uses
// STANDARD_TEXT_HEIGHT for its height, matching the text size every other button in the app
// renders at, rather than a larger "easier to click" rectangle. (G2-Edit's own defs.h has a
// STANDARD_BUTTON_TEXT_HEIGHT alias for the same 12.0 value, but that header is app-specific and
// this file can only see synthlibDefs.h.)
const double kButtonH = STANDARD_TEXT_HEIGHT;

double button_row_y(void) {
    return sState.panelRect.coord.y + kPanelHeight - 10.0 - kButtonH;
}

// fromRight counts button-widths in from the panel's right edge — 0 is rightmost. x is computed
// from the button's right edge inward so it can never extend past the panel (the original version
// of this function computed x from the left instead, which put the rightmost button outside the
// panel's own border).
tRectangle button_rect(int fromRight, double y) {
    double w         = 64.0;
    double gap       = 8.0;
    double rightEdge = sState.panelRect.coord.x + kPanelWidth - 10.0 - (double)fromRight * (w + gap);

    return (tRectangle){
        {
            rightEdge - w, y
        }, {
            w, kButtonH
        }
    };
}

tRectangle up_button_rect(void) {
    return (tRectangle){
        {
            content_x(), sState.panelRect.coord.y + 30.0
        }, {
            36.0, kButtonH
        }
    };
}

// Matches graphics.cpp's draw_panel_close_button() exactly: "Close" label, right-aligned in the
// title bar at the app's standard inset.
tRectangle close_button_rect(void) {
    double w = get_text_width("Close", kButtonH, eCache) + 4.0;

    return (tRectangle){
        {
            sState.panelRect.coord.x + kPanelWidth - w - 8.0 - BORDER_LINE_WIDTH, sState.panelRect.coord.y + 4.0
        }, {
            w, kButtonH
        }
    };
}

tRectangle filename_field_rect(void) {
    double y = sState.panelRect.coord.y + kPanelHeight - 70.0;

    return (tRectangle){
        {
            content_x(), y
        }, {
            kPanelWidth - kSidebarWidth - 20.0, kButtonH + 6.0
        }
    };
}

// relativeX is the click's X position measured from the start of the text (i.e. already offset
// past the field's left padding) — finds the character boundary closest to that click.
uint32_t filename_cursor_from_click_x(double relativeX) {
    size_t len = std::strlen(sState.filenameBuffer);

    if (relativeX <= 0.0) {
        return 0;
    }
    for (size_t i = 0; i <= len; i++) {
        std::string prefix(sState.filenameBuffer, i);

        if (get_text_width(prefix.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) >= relativeX) {
            return (uint32_t)i;
        }
    }
    return (uint32_t)len;
}

bool current_dir_confirmable(void) {
    return sState.mode == fileBrowserModeChooseFolder;
}

bool filename_confirmable(void) {
    return (sState.mode == fileBrowserModeSaveFile) && (sState.filenameBuffer[0] != '\0');
}

void insert_char_in_filename(char c) {
    size_t len = std::strlen(sState.filenameBuffer);

    if (len >= sizeof(sState.filenameBuffer) - 1) {
        return;
    }
    std::memmove(&sState.filenameBuffer[sState.filenameCursor + 1],
                 &sState.filenameBuffer[sState.filenameCursor],
                 len - sState.filenameCursor + 1);
    sState.filenameBuffer[sState.filenameCursor] = c;
    sState.filenameCursor++;
    sState.overwriteArmed = false;
}

void confirm_current(void) {
    if (sState.mode == fileBrowserModeChooseFolder) {
        finish_browse(sState.currentDir.c_str());
        return;
    }

    if (sState.mode == fileBrowserModeSaveFile) {
        if (sState.filenameBuffer[0] == '\0') {
            return;
        }
        fs::path target = fs::path(sState.currentDir) / sState.filenameBuffer;

        if (fs::exists(target) && !sState.overwriteArmed) {
            sState.overwriteArmed = true; // Confirm button becomes "Replace" — next click actually confirms
            gReDraw                = true;
            return;
        }
        finish_browse(target.string().c_str());
        return;
    }

    // fileBrowserModeOpenFile
    if ((sState.selectedIndex < 0) || (sState.selectedIndex >= (int32_t)sState.entries.size())) {
        return;
    }
    const tFileBrowserEntry & entry = sState.entries[(size_t)sState.selectedIndex];

    if (entry.isDir) {
        return;
    }
    fs::path target = fs::path(sState.currentDir) / entry.name;
    finish_browse(target.string().c_str());
}

} // namespace

extern "C" {

void open_file_browser_read(tFileBrowserCallback callback) {
    begin_browse(fileBrowserModeOpenFile, callback, "Open File", nullptr);
}

void open_file_browser_write(tFileBrowserCallback callback, const char * defaultName) {
    begin_browse(fileBrowserModeSaveFile, callback, "Save File", (defaultName != nullptr) ? defaultName : "patch.pch2");
}

void open_file_browser_folder(tFileBrowserCallback callback, const char * title) {
    begin_browse(fileBrowserModeChooseFolder, callback, (title != nullptr) ? title : "Choose Folder", nullptr);
}

void set_file_browser_start_directory(const char * path) {
    if ((path != nullptr) && (path[0] != '\0')) {
        sLastDirectory = path;
    }
}

void set_file_browser_directory_changed_callback(tFileBrowserDirectoryChangedCallback callback) {
    sDirectoryChangedCallback = callback;
}

bool file_browser_active(void) {
    return sState.active;
}

// Called on mouse-down while the browser is active so Close/Cancel/Confirm can show a pressed
// state while held — matches the rest of the app's convention (gTopbarControls[i].isPressed,
// tSettingsPanelRects.closePressed, ...) of darkening a button's fill from mouse-down to
// mouse-up rather than only reacting on click.
void handle_file_browser_mouse_down(tCoord coord) {
    if (!sState.active) {
        return;
    }
    sState.closePressed   = within_rectangle(coord, close_button_rect());
    sState.cancelPressed  = within_rectangle(coord, button_rect(1, button_row_y()));
    sState.confirmPressed = within_rectangle(coord, button_rect(0, button_row_y()));
    gReDraw = true;
}

bool handle_file_browser_click(tCoord coord) {
    if (!sState.active) {
        return false;
    }
    sState.closePressed   = false;
    sState.cancelPressed  = false;
    sState.confirmPressed = false;

    if (!within_rectangle(coord, sState.panelRect)) {
        return true; // Modal — swallow clicks outside without closing (matches other G2-Edit popups)
    }

    if (within_rectangle(coord, close_button_rect())) {
        finish_browse(nullptr);
        return true;
    }

    if (within_rectangle(coord, up_button_rect())) {
        fs::path parent = fs::path(sState.currentDir).parent_path();

        if (!parent.empty()) {
            navigate_to(parent.string());
        }
        return true;
    }

    if (within_rectangle(coord, sidebar_rect())) {
        for (size_t i = 0; i < sState.sidebar.size(); i++) {
            const tSidebarItem & item = sState.sidebar[i];

            if (item.isHeader) {
                continue;
            }
            if (within_rectangle(coord, sidebar_row_rect((int)i))) {
                navigate_to(item.path);
                break;
            }
        }
        return true;
    }

    tRectangle listRect = list_area_rect();

    if (within_rectangle(coord, listRect)) {
        int32_t rowInView = (int32_t)((coord.y - listRect.coord.y) / kRowHeight);
        int32_t index     = rowInView + (int32_t)sState.scrollOffset;

        if ((index >= 0) && (index < (int32_t)sState.entries.size())) {
            const tFileBrowserEntry & entry = sState.entries[(size_t)index];

            if (entry.isDir) {
                fs::path next = fs::path(sState.currentDir) / entry.name;
                navigate_to(next.string());
            } else if (sState.mode == fileBrowserModeSaveFile) {
                std::strncpy(sState.filenameBuffer, entry.name.c_str(), sizeof(sState.filenameBuffer) - 1);
                sState.filenameCursor = (uint32_t)std::strlen(sState.filenameBuffer);
                sState.overwriteArmed = false;
                gReDraw                = true;
            } else if (sState.mode == fileBrowserModeOpenFile) {
                sState.selectedIndex = index;
                gReDraw               = true;
            }
        }
        return true;
    }

    if ((sState.mode == fileBrowserModeSaveFile) && within_rectangle(coord, filename_field_rect())) {
        tRectangle fieldRect = filename_field_rect();

        sState.filenameCursor = filename_cursor_from_click_x(coord.x - (fieldRect.coord.x + 6.0));
        gReDraw                = true;
        return true;
    }

    bool wantsConfirm = within_rectangle(coord, button_rect(0, button_row_y()));
    bool wantsCancel  = within_rectangle(coord, button_rect(1, button_row_y()));

    if (wantsCancel) {
        finish_browse(nullptr);
        return true;
    }

    if (wantsConfirm) {
        confirm_current();
        return true;
    }

    return true;
}

void handle_file_browser_key(int key, int action) {
    if (!sState.active) {
        return;
    }

    if ((action != GLFW_PRESS) && (action != GLFW_REPEAT)) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        finish_browse(nullptr);
        return;
    }

    if ((key == GLFW_KEY_ENTER) || (key == GLFW_KEY_KP_ENTER)) {
        confirm_current();
        return;
    }

    if (sState.mode == fileBrowserModeSaveFile) {
        size_t len = std::strlen(sState.filenameBuffer);

        if (key == GLFW_KEY_BACKSPACE) {
            if (sState.filenameCursor > 0) {
                std::memmove(&sState.filenameBuffer[sState.filenameCursor - 1],
                             &sState.filenameBuffer[sState.filenameCursor],
                             len - sState.filenameCursor + 1);
                sState.filenameCursor--;
                sState.overwriteArmed = false;
            }
        } else if (key == GLFW_KEY_DELETE) {
            if (sState.filenameCursor < len) {
                std::memmove(&sState.filenameBuffer[sState.filenameCursor],
                             &sState.filenameBuffer[sState.filenameCursor + 1],
                             len - sState.filenameCursor);
                sState.overwriteArmed = false;
            }
        } else if (key == GLFW_KEY_LEFT) {
            if (sState.filenameCursor > 0) {
                sState.filenameCursor--;
            }
        } else if (key == GLFW_KEY_RIGHT) {
            if (sState.filenameCursor < len) {
                sState.filenameCursor++;
            }
        }
    }
    gReDraw = true;
}

void handle_file_browser_char(unsigned int codepoint) {
    if (!sState.active || (sState.mode != fileBrowserModeSaveFile)) {
        return;
    }

    if ((codepoint >= 0x20) && (codepoint <= 0x7e)) {
        insert_char_in_filename((char)codepoint);
        gReDraw = true;
    }
}

void handle_file_browser_scroll(double yDelta) {
    if (!sState.active) {
        return;
    }
    sState.scrollOffset -= yDelta;
    sState.scrollOffset  = std::max(0.0, std::min(sState.scrollOffset, (double)row_count_scrolled()));
    gReDraw               = true;
}

void render_file_browser(void) {
    if (!sState.active) {
        return;
    }

    tCoord mouseCoord = {0};

    get_global_gui_scaled_mouse_coord(&mouseCoord);

    // Dim background overlay — solid, not translucent, matching every other modal panel in the
    // app (draw_dialog_background_overlay() in graphics.cpp uses the same RGB_GREY_2 technique;
    // this file can't call that G2-Edit-local helper directly, so it duplicates just the color).
    set_rgb_colour((tRgb)RGB_GREY_2);
    render_rectangle(mainArea, (tRectangle){
        {0.0, 0.0}, {get_render_width() / gGlobalGuiScale, get_render_height() / gGlobalGuiScale}
    });

    // Panel chrome — replicates graphics.cpp's draw_panel_chrome()/draw_panel_close_button()
    // pixel-for-pixel (this file can't call those G2-Edit-local helpers directly): fill+border
    // the whole box first, then an inset title strip so the border stays visible all the way
    // round instead of being painted over by a full-bleed title bar.
    double titleH = 26.0;

    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle_with_border(mainArea, sState.panelRect);
    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle(mainArea, (tRectangle){
        {sState.panelRect.coord.x + BORDER_LINE_WIDTH, sState.panelRect.coord.y + BORDER_LINE_WIDTH},
        {sState.panelRect.size.w - (2.0 * BORDER_LINE_WIDTH), titleH - BORDER_LINE_WIDTH}
    });
    set_rgb_colour((tRgb)RGB_WHITE);
    render_text(mainArea, (tRectangle){
        {sState.panelRect.coord.x + 10.0, sState.panelRect.coord.y + 6.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
    }, sState.title.c_str());

    draw_button(mainArea, close_button_rect(), "Close", sState.closePressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);

    // Up button + current path
    draw_button(mainArea, up_button_rect(), "Up", (tRgb)RGB_GREY_7);
    set_rgb_colour((tRgb)RGB_BLACK);
    double pathLabelWidth = kPanelWidth - kSidebarWidth - 78.0;
    render_text(mainArea, (tRectangle){
        {up_button_rect().coord.x + 44.0, up_button_rect().coord.y + 4.0}, {pathLabelWidth, STANDARD_TEXT_HEIGHT}
    }, truncate_to_width(sState.currentDir, pathLabelWidth).c_str());

    // Sidebar — Favorites/Locations quick-access, mirroring the native panel's sidebar.
    tRectangle sidebarRect = sidebar_rect();

    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle(mainArea, sidebarRect);
    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle_with_border(mainArea, sidebarRect);

    for (size_t i = 0; i < sState.sidebar.size(); i++) {
        const tSidebarItem & item    = sState.sidebar[i];
        tRectangle            rowRect = sidebar_row_rect((int)i);

        if (rowRect.coord.y + rowRect.size.h > sidebarRect.coord.y + sidebarRect.size.h) {
            break; // Overflowed the box — no sidebar scrolling in this first pass.
        }

        if (item.isHeader) {
            set_rgb_colour((tRgb)RGB_GREY_3);
            render_text(mainArea, (tRectangle){
                {rowRect.coord.x + 4.0, rowRect.coord.y + 4.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
            }, truncate_to_width(item.label, rowRect.size.w - 8.0).c_str());
            continue;
        }

        bool isCurrent = (item.path == sState.currentDir);
        bool isHovered = within_rectangle(mouseCoord, rowRect);

        if (isCurrent) {
            set_rgb_colour((tRgb)RGB_ORANGE_2);
            render_rectangle(mainArea, rowRect);
        } else if (isHovered) {
            set_rgb_colour((tRgb)RGB_GREY_7);
            render_rectangle(mainArea, rowRect);
        }
        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {rowRect.coord.x + 10.0, rowRect.coord.y + 4.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, truncate_to_width(item.label, rowRect.size.w - 14.0).c_str());
    }

    // Directory listing
    tRectangle listRect = list_area_rect();

    set_rgb_colour((tRgb)RGB_WHITE);
    render_rectangle(mainArea, listRect);
    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle_with_border(mainArea, listRect);

    for (int row = 0; row < kVisibleRows; row++) {
        int32_t index = row + (int32_t)sState.scrollOffset;

        if ((index < 0) || (index >= (int32_t)sState.entries.size())) {
            continue;
        }
        const tFileBrowserEntry & entry = sState.entries[(size_t)index];

        tRectangle rowRect = {
            {
                listRect.coord.x, listRect.coord.y + (row * kRowHeight)
            }, {
                listRect.size.w, kRowHeight
            }
        };
        bool       selected = (sState.mode == fileBrowserModeOpenFile) && (index == sState.selectedIndex);
        bool       hovered   = within_rectangle(mouseCoord, rowRect);

        if (selected) {
            set_rgb_colour((tRgb)RGB_ORANGE_2);
            render_rectangle(mainArea, rowRect);
        } else if (hovered) {
            set_rgb_colour((tRgb)RGB_GREY_7);
            render_rectangle(mainArea, rowRect);
        }

        set_rgb_colour((tRgb)RGB_BLACK);

        std::string label = entry.name + (entry.isDir ? "/" : "");
        render_text(mainArea, (tRectangle){
            {rowRect.coord.x + 6.0, rowRect.coord.y + 4.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, truncate_to_width(label, rowRect.size.w - 12.0).c_str());
    }

    // Filename field (Save mode only)
    if (sState.mode == fileBrowserModeSaveFile) {
        tRectangle fieldRect = filename_field_rect();

        set_rgb_colour((tRgb)RGB_WHITE);
        render_rectangle(mainArea, fieldRect);
        set_rgb_colour((tRgb)RGB_GREY_5);
        render_rectangle_with_border(mainArea, fieldRect);
        set_rgb_colour((tRgb)RGB_BLACK);

        // Same "insert a literal | at the cursor position before rendering" convention the app
        // already uses for patch name/notes editing (see graphics.cpp's gPatchNameEdit handling)
        // — a visible cursor, and confirmation the field really is editable.
        std::string display(sState.filenameBuffer);
        display.insert((size_t)sState.filenameCursor, "|");
        render_text(mainArea, (tRectangle){
            {fieldRect.coord.x + 6.0, fieldRect.coord.y + 6.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, display.c_str());
    }

    // Confirm / Cancel buttons — Confirm rightmost (primary action), Cancel to its left.
    double buttonY = button_row_y();

    bool        confirmEnabled = current_dir_confirmable() || filename_confirmable() || (sState.selectedIndex >= 0);
    const char * confirmLabel  = "Open";

    if (sState.mode == fileBrowserModeSaveFile) {
        confirmLabel = sState.overwriteArmed ? "Replace" : "Save";
    } else if (sState.mode == fileBrowserModeChooseFolder) {
        confirmLabel = "Choose";
    }
    tRgb confirmColour = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (confirmEnabled ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_GREY_5);
    tRgb cancelColour  = sState.cancelPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY;

    draw_button(mainArea, button_rect(0, buttonY), confirmLabel, confirmColour);
    draw_button(mainArea, button_rect(1, buttonY), "Cancel", cancelColour);
}

} // extern "C"

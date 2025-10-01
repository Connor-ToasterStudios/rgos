#include "../include/types.h"

#define MAX_TERMINAL_LINES 30
#define MAX_LINE_LENGTH 80
#define TERMINAL_HISTORY_SIZE 10

typedef struct {
    char lines[MAX_TERMINAL_LINES][MAX_LINE_LENGTH];
    int lineCount;
    int scrollOffset;
    char inputBuffer[MAX_LINE_LENGTH];
    int inputPos;
    char history[TERMINAL_HISTORY_SIZE][MAX_LINE_LENGTH];
    int historyCount;
    int historyIndex;
} TerminalData;

typedef struct {
    int x, y, width, height;
    char title[64];
    uint32_t titleBarColor, backgroundColor;
    int visible;
    int dragging;
    int dragOffsetX, dragOffsetY;
    int lastDrawX, lastDrawY;
    int isTerminal;
    int isFocused;
    TerminalData termData;
} Window;

static Framebuffer *fb;
static Window windows[16];
static int windowCount = 0;
static int focusedWindow = -1;

// Mouse state
static int mouseX = 400;
static int mouseY = 300;
static int oldMouseX = 400;
static int oldMouseY = 300;
static int mouseButtons = 0;

// Back buffer for cursor
static uint32_t cursorBackBuffer[20 * 20];
static int cursorBackBufferValid = 0;

// Colors
#define COLOR_DESKTOP_BG    0x003366
#define COLOR_TASKBAR       0x1A1A1A
#define COLOR_WINDOW_BG     0xF0F0F0
#define COLOR_TITLEBAR_BLUE 0x0078D7
#define COLOR_TITLEBAR_GREEN 0x16C60C
#define COLOR_TITLEBAR_RED   0xE81123
#define COLOR_BORDER        0x000000
#define COLOR_WHITE         0xFFFFFF
#define COLOR_BLACK         0x000000
#define COLOR_TERMINAL_BG   0x000000
#define COLOR_TERMINAL_TEXT 0x00FF00
#define COLOR_CURSOR_NORMAL 0x00FF00
#define COLOR_CURSOR_CLICK  0xFF0000

// Port I/O
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// String functions
int strlen(const char* str) {
    int len = 0;
    while(str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while(n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if(n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void strcpy(char* dest, const char* src) {
    while(*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

void strcat(char* dest, const char* src) {
    while(*dest) dest++;
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

// Graphics functions
void DrawPixel(uint32_t x, uint32_t y, uint32_t color) {
    if(x < fb->width && y < fb->height) {
        fb->base[y * fb->pixelsPerScanLine + x] = color;
    }
}

uint32_t GetPixel(uint32_t x, uint32_t y) {
    if(x < fb->width && y < fb->height) {
        return fb->base[y * fb->pixelsPerScanLine + x];
    }
    return 0;
}

void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if(x >= fb->width || y >= fb->height) return;
    
    uint32_t maxW = (x + w > fb->width) ? fb->width - x : w;
    uint32_t maxH = (y + h > fb->height) ? fb->height - y : h;
    
    for(uint32_t dy = 0; dy < maxH; dy++) {
        uint32_t offset = (y + dy) * fb->pixelsPerScanLine + x;
        for(uint32_t dx = 0; dx < maxW; dx++) {
            fb->base[offset + dx] = color;
        }
    }
}

void DrawChar(uint32_t x, uint32_t y, char c, uint32_t color) {
    // Simple character rendering
    for(int dy = 0; dy < 8; dy++) {
        for(int dx = 0; dx < 6; dx++) {
            if(c != ' ' && (dx == 0 || dx == 5 || dy == 0 || dy == 7)) {
                DrawPixel(x + dx, y + dy, color);
            }
        }
    }
}

void DrawText(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t xPos = x;
    for(int i = 0; text[i] != '\0'; i++) {
        DrawChar(xPos, y, text[i], color);
        xPos += 8;
    }
}

// Terminal functions
void TerminalAddLine(Window* win, const char* text) {
    if(!win->isTerminal) return;
    
    TerminalData* term = &win->termData;
    if(term->lineCount < MAX_TERMINAL_LINES) {
        strcpy(term->lines[term->lineCount], text);
        term->lineCount++;
    } else {
        for(int i = 0; i < MAX_TERMINAL_LINES - 1; i++) {
            strcpy(term->lines[i], term->lines[i + 1]);
        }
        strcpy(term->lines[MAX_TERMINAL_LINES - 1], text);
    }
}

void TerminalProcessCommand(Window* win, const char* cmd) {
    TerminalData* term = &win->termData;
    
    if(term->historyCount < TERMINAL_HISTORY_SIZE) {
        strcpy(term->history[term->historyCount], cmd);
        term->historyCount++;
    }
    
    if(strcmp(cmd, "help") == 0) {
        TerminalAddLine(win, "Available commands:");
        TerminalAddLine(win, "  help   - Show this help");
        TerminalAddLine(win, "  clear  - Clear screen");
        TerminalAddLine(win, "  echo   - Echo text");
        TerminalAddLine(win, "  about  - About MyOs");
        TerminalAddLine(win, "  date   - Show date");
        TerminalAddLine(win, "  ls     - List files");
        TerminalAddLine(win, "  whoami - Show user");
    }
    else if(strcmp(cmd, "clear") == 0) {
        term->lineCount = 0;
    }
    else if(strncmp(cmd, "echo ", 5) == 0) {
        TerminalAddLine(win, cmd + 5);
    }
    else if(strcmp(cmd, "about") == 0) {
        TerminalAddLine(win, "MyOs v1.0 - Custom UEFI OS");
        TerminalAddLine(win, "My AP Computer Science Project");
    }
    else if(strcmp(cmd, "date") == 0) {
        TerminalAddLine(win, "Mon Oct 1 12:34:56 2024");
    }
    else if(strcmp(cmd, "ls") == 0) {
        TerminalAddLine(win, "documents/  pictures/  readme.txt");
    }
    else if(strcmp(cmd, "whoami") == 0) {
        TerminalAddLine(win, "user");
    }
    else if(strlen(cmd) > 0) {
        char error[MAX_LINE_LENGTH];
        strcpy(error, cmd);
        strcat(error, ": command not found");
        TerminalAddLine(win, error);
    }
}

void DrawTerminalContent(Window* win) {
    if(!win->isTerminal || !win->visible) return;
    
    TerminalData* term = &win->termData;
    int contentX = win->x + 8;
    int contentY = win->y + 38;
    int contentWidth = win->width - 16;
    int contentHeight = win->height - 46;
    
    DrawRect(contentX - 4, contentY - 4, contentWidth + 8, contentHeight + 8, COLOR_TERMINAL_BG);
    
    int lineY = contentY;
    for(int i = 0; i < term->lineCount && lineY < contentY + contentHeight - 20; i++) {
        DrawText(contentX, lineY, term->lines[i], COLOR_TERMINAL_TEXT);
        lineY += 12;
    }
    
    DrawText(contentX, lineY, "user@myos:~$ ", COLOR_TERMINAL_TEXT);
    DrawText(contentX + 13 * 8, lineY, term->inputBuffer, COLOR_TERMINAL_TEXT);
    
    int cursorX = contentX + 13 * 8 + term->inputPos * 8;
    DrawRect(cursorX, lineY, 8, 10, COLOR_TERMINAL_TEXT);
}

void SaveCursorBackground(int x, int y) {
    for(int dy = 0; dy < 20; dy++) {
        for(int dx = 0; dx < 20; dx++) {
            cursorBackBuffer[dy * 20 + dx] = GetPixel(x + dx, y + dy);
        }
    }
    cursorBackBufferValid = 1;
}

void RestoreCursorBackground(int x, int y) {
    if(!cursorBackBufferValid) return;
    for(int dy = 0; dy < 20; dy++) {
        for(int dx = 0; dx < 20; dx++) {
            DrawPixel(x + dx, y + dy, cursorBackBuffer[dy * 20 + dx]);
        }
    }
}

void DrawCursor(int x, int y, int clicked) {
    uint32_t cursorColor = clicked ? COLOR_CURSOR_CLICK : COLOR_CURSOR_NORMAL;
    for(int dy = 0; dy < 16; dy++) {
        for(int dx = 0; dx <= dy && dx < 10; dx++) {
            DrawPixel(x + dx, y + dy, cursorColor);
        }
    }
    for(int dy = 0; dy < 16; dy++) {
        DrawPixel(x, y + dy, COLOR_BORDER);
        if(dy < 10) {
            DrawPixel(x + dy, y + dy, COLOR_BORDER);
        }
    }
}

void UpdateCursor(int newX, int newY, int clicked) {
    if(cursorBackBufferValid) {
        RestoreCursorBackground(oldMouseX, oldMouseY);
    }
    SaveCursorBackground(newX, newY);
    DrawCursor(newX, newY, clicked);
}

void DrawWindow(Window* win) {
    if(!win->visible) return;
    
    uint32_t titleBarHeight = 30;
    
    if(!win->dragging) {
        DrawRect(win->x + 4, win->y + 4, win->width, win->height, 0x80000000);
    }
    
    DrawRect(win->x, win->y, win->width, win->height, COLOR_BORDER);
    
    uint32_t titleColor = win->isFocused ? win->titleBarColor : (win->titleBarColor & 0x808080);
    DrawRect(win->x + 2, win->y + 2, win->width - 4, titleBarHeight - 2, titleColor);
    DrawText(win->x + 10, win->y + 10, win->title, COLOR_WHITE);
    
    if(win->isTerminal) {
        DrawTerminalContent(win);
    } else {
        DrawRect(win->x + 2, win->y + titleBarHeight, win->width - 4, 
                 win->height - titleBarHeight - 2, win->backgroundColor);
    }
    
    DrawRect(win->x + win->width - 26, win->y + 6, 18, 18, 0xE81123);
    DrawText(win->x + win->width - 21, win->y + 11, "X", COLOR_WHITE);
    
    win->lastDrawX = win->x;
    win->lastDrawY = win->y;
}

void ClearWindowArea(int x, int y, int w, int h) {
    DrawRect(x, y, w, h, COLOR_DESKTOP_BG);
}

void DrawDesktop() {
    DrawRect(0, 0, fb->width, fb->height, COLOR_DESKTOP_BG);
    DrawRect(30, 30, 64, 64, COLOR_WHITE);
    DrawText(35, 100, "Computer", COLOR_WHITE);
    DrawRect(130, 30, 64, 64, COLOR_WHITE);
    DrawText(135, 100, "Files", COLOR_WHITE);
    DrawRect(230, 30, 64, 64, COLOR_WHITE);
    DrawText(230, 100, "Terminal", COLOR_WHITE);
}

void DrawTaskbar() {
    uint32_t taskbarHeight = 48;
    uint32_t taskbarY = fb->height - taskbarHeight;
    DrawRect(0, taskbarY, fb->width, taskbarHeight, COLOR_TASKBAR);
    DrawRect(8, taskbarY + 8, 120, 32, COLOR_TITLEBAR_BLUE);
    DrawText(20, taskbarY + 16, "Start", COLOR_WHITE);
    DrawRect(fb->width - 150, taskbarY + 8, 140, 32, 0x2D2D2D);
    DrawText(fb->width - 135, taskbarY + 16, "MyOs v1.0", COLOR_WHITE);
}

void RedrawEverything() {
    cursorBackBufferValid = 0;
    DrawDesktop();
    for(int i = 0; i < windowCount; i++) {
        if(windows[i].visible) {
            DrawWindow(&windows[i]);
        }
    }
    DrawTaskbar();
    SaveCursorBackground(mouseX, mouseY);
    DrawCursor(mouseX, mouseY, mouseButtons);
}

void UpdateDraggingWindow(Window* win) {
    if(!win->dragging || !win->visible) return;
    ClearWindowArea(win->lastDrawX, win->lastDrawY, win->width, win->height);
    for(int i = 0; i < windowCount; i++) {
        Window* other = &windows[i];
        if(other == win || !other->visible) continue;
        if(!(other->x + other->width < win->lastDrawX || 
             other->x > win->lastDrawX + win->width ||
             other->y + other->height < win->lastDrawY ||
             other->y > win->lastDrawY + win->height)) {
            DrawWindow(other);
        }
    }
    DrawWindow(win);
}

void CreateWindow(int x, int y, int width, int height, const char* title, uint32_t color, int isTerminal) {
    if(windowCount >= 16) return;
    Window* win = &windows[windowCount];
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->titleBarColor = color;
    win->backgroundColor = COLOR_WINDOW_BG;
    win->visible = 1;
    win->dragging = 0;
    win->lastDrawX = x;
    win->lastDrawY = y;
    win->isTerminal = isTerminal;
    win->isFocused = 0;
    
    if(isTerminal) {
        win->termData.lineCount = 0;
        win->termData.inputPos = 0;
        win->termData.inputBuffer[0] = '\0';
        win->termData.historyCount = 0;
        TerminalAddLine(win, "MyOs Terminal v1.0");
        TerminalAddLine(win, "Type 'help' for commands");
        TerminalAddLine(win, "");
    }
    
    strcpy(win->title, title);
    windowCount++;
}

int PointInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void HandleMouseClick(int x, int y) {
    for(int i = windowCount - 1; i >= 0; i--) {
        Window* win = &windows[i];
        if(!win->visible) continue;
        
        if(PointInRect(x, y, win->x, win->y, win->width, win->height)) {
            for(int j = 0; j < windowCount; j++) {
                windows[j].isFocused = 0;
            }
            win->isFocused = 1;
            focusedWindow = i;
            
            int closeX = win->x + win->width - 26;
            int closeY = win->y + 6;
            if(PointInRect(x, y, closeX, closeY, 18, 18)) {
                win->visible = 0;
                focusedWindow = -1;
                RedrawEverything();
                return;
            }
            
            if(PointInRect(x, y, win->x, win->y, win->width, 30)) {
                win->dragging = 1;
                win->dragOffsetX = x - win->x;
                win->dragOffsetY = y - win->y;
            }
            
            RedrawEverything();
            return;
        }
    }
    
    if(PointInRect(x, y, 230, 30, 64, 64)) {
        CreateWindow(100, 100, 700, 500, "Terminal", COLOR_TITLEBAR_BLUE, 1);
        RedrawEverything();
    }
}

void HandleMouseRelease() {
    int anyDragging = 0;
    for(int i = 0; i < windowCount; i++) {
        if(windows[i].dragging) {
            windows[i].dragging = 0;
            anyDragging = 1;
        }
    }
    if(anyDragging) {
        RedrawEverything();
    }
}

void HandleMouseMove(int x, int y) {
    for(int i = 0; i < windowCount; i++) {
        Window* win = &windows[i];
        if(win->dragging) {
            win->x = x - win->dragOffsetX;
            win->y = y - win->dragOffsetY;
            if(win->x < 0) win->x = 0;
            if(win->y < 0) win->y = 0;
            if(win->x + win->width > fb->width) win->x = fb->width - win->width;
            if(win->y + win->height > fb->height - 48) win->y = fb->height - 48 - win->height;
            UpdateDraggingWindow(win);
            return;
        }
    }
}

// Keyboard
char ScancodeToChar(unsigned char scancode) {
    static const char scancodeMap[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    
    if(scancode < sizeof(scancodeMap)) {
        return scancodeMap[scancode];
    }
    return 0;
}

void HandleKeyPress(unsigned char key) {
    if(focusedWindow < 0 || focusedWindow >= windowCount) return;
    
    Window* win = &windows[focusedWindow];
    if(!win->isTerminal || !win->visible) return;
    
    TerminalData* term = &win->termData;
    
    if(key == '\n') {
        term->inputBuffer[term->inputPos] = '\0';
        
        char cmdLine[MAX_LINE_LENGTH];
        strcpy(cmdLine, "user@myos:~$ ");
        strcat(cmdLine, term->inputBuffer);
        TerminalAddLine(win, cmdLine);
        
        TerminalProcessCommand(win, term->inputBuffer);
        
        term->inputPos = 0;
        term->inputBuffer[0] = '\0';
        
        DrawWindow(win);
    }
    else if(key == '\b') {
        if(term->inputPos > 0) {
            term->inputPos--;
            term->inputBuffer[term->inputPos] = '\0';
            DrawWindow(win);
        }
    }
    else if(key >= 32 && key <= 126) {
        if(term->inputPos < MAX_LINE_LENGTH - 1) {
            term->inputBuffer[term->inputPos] = key;
            term->inputPos++;
            term->inputBuffer[term->inputPos] = '\0';
            DrawWindow(win);
        }
    }
}

void InitMouse() {
    outb(0x64, 0xA8);
    outb(0x64, 0x20);
    uint8_t status = inb(0x60) | 2;
    outb(0x64, 0x60);
    outb(0x60, status);
    outb(0x64, 0xD4);
    outb(0x60, 0xF6);
    inb(0x60);
    outb(0x64, 0xD4);
    outb(0x60, 0xF4);
    inb(0x60);
}

void PollMouse() {
    static uint8_t mouseCycle = 0;
    static uint8_t mouseBytes[3];
    
    // Check if data is from mouse (bit 5 set)
    uint8_t status = inb(0x64);
    if(!(status & 0x01)) return;  // No data
    
    // Only read if it's mouse data
    if(!(status & 0x20)) return;  // Not mouse data
    
    uint8_t data = inb(0x60);
    mouseBytes[mouseCycle++] = data;
    
    if(mouseCycle == 3) {
        mouseCycle = 0;
        
        int8_t deltaX = mouseBytes[1];
        int8_t deltaY = -mouseBytes[2];
        
        oldMouseX = mouseX;
        oldMouseY = mouseY;
        
        mouseX += deltaX;
        mouseY += deltaY;
        
        if(mouseX < 0) mouseX = 0;
        if(mouseY < 0) mouseY = 0;
        if(mouseX >= fb->width - 20) mouseX = fb->width - 20;
        if(mouseY >= fb->height - 20) mouseY = fb->height - 20;
        
        int leftButton = mouseBytes[0] & 0x01;
        
        if(leftButton && !mouseButtons) {
            HandleMouseClick(mouseX, mouseY);
        } else if(!leftButton && mouseButtons) {
            HandleMouseRelease();
        } else if(leftButton) {
            HandleMouseMove(mouseX, mouseY);
        } else if(mouseX != oldMouseX || mouseY != oldMouseY) {
            UpdateCursor(mouseX, mouseY, leftButton);
            oldMouseX = mouseX;
            oldMouseY = mouseY;
            return;
        }
        
        mouseButtons = leftButton;
    }
}

void PollKeyboard() {
    // Check if data is available
    uint8_t status = inb(0x64);
    if(!(status & 0x01)) return;  // No data
    
    // Only read if it's keyboard data (not mouse)
    if(status & 0x20) return;  // Mouse data, skip
    
    unsigned char scancode = inb(0x60);
    
    // Only process key press (not release)
    if(scancode & 0x80) return;
    
    char key = ScancodeToChar(scancode);
    if(key) {
        HandleKeyPress(key);
    }
}

void KernelMain(Framebuffer* framebuffer) {
    fb = framebuffer;
    
    InitMouse();
    
    DrawDesktop();
    
    CreateWindow(100, 100, 700, 500, "Terminal", COLOR_TITLEBAR_BLUE, 1);
    CreateWindow(150, 150, 500, 350, "File Manager", COLOR_TITLEBAR_GREEN, 0);
    CreateWindow(200, 200, 450, 300, "About MyOs", COLOR_TITLEBAR_RED, 0);
    
    // Focus first terminal
    windows[0].isFocused = 1;
    focusedWindow = 0;
    
    for(int i = 0; i < windowCount; i++) {
        DrawWindow(&windows[i]);
    }
    
    DrawTaskbar();
    SaveCursorBackground(mouseX, mouseY);
    DrawCursor(mouseX, mouseY, 0);
    
    while(1) {
        PollMouse();
        PollKeyboard();
        for(volatile int i = 0; i < 5000; i++);
    }
}

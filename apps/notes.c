#include "../include/notes.h"

// Forward declarations for kernel functions we'll use
extern int strlen(const char* str);
extern void strcpy(char* dest, const char* src);
extern void strcat(char* dest, const char* src);
extern void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void DrawText(uint32_t x, uint32_t y, const char* text, uint32_t color);
extern void DrawChar(uint32_t x, uint32_t y, char c, uint32_t color);

// Color definitions (matching kernel)
#define COLOR_WHITE         0xFFFFFF
#define COLOR_BLACK         0x000000
#define COLOR_WINDOW_BG     0xF0F0F0
#define COLOR_TITLEBAR_BLUE 0x0078D7
#define COLOR_TITLEBAR_GREEN 0x16C60C

// Window structure definition
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
    void* termData;
    int isNotesApp;
    NotesAppData notesData;
} WindowInternal;

void NotesAppInit(void* winPtr) {
    WindowInternal* win = (WindowInternal*)winPtr;
    if(!win->isNotesApp) return;
    
    NotesAppData* notes = &win->notesData;
    notes->noteCount = 0;
    notes->currentNote = -1;
    notes->cursorPos = 0;
    notes->scrollOffset = 0;
    strcpy(notes->statusMessage, "Notes App - Press N for new note");
    
    // Create a default welcome note
    Note* note = &notes->notes[notes->noteCount];
    strcpy(note->name, "Welcome.txt");
    strcpy(note->content, "Welcome to Notes App!\n\nCommands:\nN - New note\nS - Save note\nD - Delete note\nArrows - Navigate between notes\n\nStart typing to edit...");
    note->contentLength = strlen(note->content);
    note->active = 1;
    notes->noteCount = 1;
    notes->currentNote = 0;
}

void NotesAppCreateNew(void* winPtr, const char* name) {
    WindowInternal* win = (WindowInternal*)winPtr;
    NotesAppData* notes = &win->notesData;
    
    if(notes->noteCount >= MAX_NOTES) {
        strcpy(notes->statusMessage, "Error: Maximum notes reached!");
        return;
    }
    
    Note* note = &notes->notes[notes->noteCount];
    strcpy(note->name, name);
    note->content[0] = '\0';
    note->contentLength = 0;
    note->active = 1;
    
    notes->currentNote = notes->noteCount;
    notes->noteCount++;
    notes->cursorPos = 0;
    notes->scrollOffset = 0;
    
    strcpy(notes->statusMessage, "New note created");
}

void NotesAppSave(void* winPtr) {
    WindowInternal* win = (WindowInternal*)winPtr;
    NotesAppData* notes = &win->notesData;
    
    if(notes->currentNote < 0 || notes->currentNote >= notes->noteCount) {
        strcpy(notes->statusMessage, "No note selected");
        return;
    }
    
    strcpy(notes->statusMessage, "Note saved successfully!");
}

void NotesAppDelete(void* winPtr) {
    WindowInternal* win = (WindowInternal*)winPtr;
    NotesAppData* notes = &win->notesData;
    
    if(notes->currentNote < 0 || notes->currentNote >= notes->noteCount) {
        strcpy(notes->statusMessage, "No note to delete");
        return;
    }
    
    // Remove the current note
    for(int i = notes->currentNote; i < notes->noteCount - 1; i++) {
        notes->notes[i] = notes->notes[i + 1];
    }
    notes->noteCount--;
    
    if(notes->noteCount > 0) {
        notes->currentNote = 0;
    } else {
        notes->currentNote = -1;
    }
    
    strcpy(notes->statusMessage, "Note deleted");
}

void NotesAppInsertChar(void* winPtr, char c) {
    WindowInternal* win = (WindowInternal*)winPtr;
    NotesAppData* notes = &win->notesData;
    
    if(notes->currentNote < 0 || notes->currentNote >= notes->noteCount) return;
    
    Note* note = &notes->notes[notes->currentNote];
    
    if(note->contentLength >= MAX_NOTE_CONTENT - 1) return;
    
    // Insert character at cursor position
    for(int i = note->contentLength; i > notes->cursorPos; i--) {
        note->content[i] = note->content[i - 1];
    }
    
    note->content[notes->cursorPos] = c;
    notes->cursorPos++;
    note->contentLength++;
    note->content[note->contentLength] = '\0';
}

void NotesAppBackspace(void* winPtr) {
    WindowInternal* win = (WindowInternal*)winPtr;
    NotesAppData* notes = &win->notesData;
    
    if(notes->currentNote < 0 || notes->currentNote >= notes->noteCount) return;
    if(notes->cursorPos == 0) return;
    
    Note* note = &notes->notes[notes->currentNote];
    
    // Remove character before cursor
    for(int i = notes->cursorPos - 1; i < note->contentLength; i++) {
        note->content[i] = note->content[i + 1];
    }
    
    notes->cursorPos--;
    note->contentLength--;
}

void DrawNotesApp(void* winPtr) {
    WindowInternal* win = (WindowInternal*)winPtr;
    if(!win->isNotesApp || !win->visible) return;
    
    NotesAppData* notes = &win->notesData;
    
    int sidebarWidth = 150;
    int sidebarX = win->x + 2;
    int sidebarY = win->y + 32;
    int sidebarHeight = win->height - 62;
    
    int contentX = win->x + sidebarWidth + 4;
    int contentY = win->y + 32;
    int contentWidth = win->width - sidebarWidth - 6;
    int contentHeight = win->height - 62;
    
    // Draw sidebar
    DrawRect(sidebarX, sidebarY, sidebarWidth, sidebarHeight, 0x2D2D2D);
    DrawText(sidebarX + 5, sidebarY + 5, "Notes:", COLOR_WHITE);
    
    int listY = sidebarY + 20;
    for(int i = 0; i < notes->noteCount; i++) {
        uint32_t bgColor = (i == notes->currentNote) ? COLOR_TITLEBAR_BLUE : 0x3D3D3D;
        DrawRect(sidebarX + 2, listY, sidebarWidth - 4, 18, bgColor);
        
        char displayName[20];
        int len = strlen(notes->notes[i].name);
        if(len > 18) {
            for(int j = 0; j < 15; j++) displayName[j] = notes->notes[i].name[j];
            displayName[15] = '.';
            displayName[16] = '.';
            displayName[17] = '.';
            displayName[18] = '\0';
        } else {
            strcpy(displayName, notes->notes[i].name);
        }
        
        DrawText(sidebarX + 5, listY + 5, displayName, COLOR_WHITE);
        listY += 20;
    }
    
    // Draw content area
    DrawRect(contentX, contentY, contentWidth, contentHeight, COLOR_WINDOW_BG);
    
    if(notes->currentNote >= 0 && notes->currentNote < notes->noteCount) {
        Note* note = &notes->notes[notes->currentNote];
        
        // Draw note title
        DrawRect(contentX, contentY, contentWidth, 25, COLOR_TITLEBAR_GREEN);
        DrawText(contentX + 5, contentY + 8, note->name, COLOR_WHITE);
        
        // Draw content
        int textY = contentY + 30;
        int textX = contentX + 5;
        int charIndex = 0;
        
        for(int i = 0; i < note->contentLength && textY < contentY + contentHeight - 15; i++) {
            if(charIndex >= notes->scrollOffset) {
                if(note->content[i] == '\n') {
                    textY += 12;
                    textX = contentX + 5;
                } else {
                    DrawChar(textX, textY, note->content[i], COLOR_BLACK);
                    textX += 8;
                    
                    if(textX > contentX + contentWidth - 15) {
                        textY += 12;
                        textX = contentX + 5;
                    }
                }
            }
            charIndex++;
        }
        
        // Draw cursor
        int cursorX = contentX + 5;
        int cursorY = contentY + 30;
        
        for(int i = 0; i < notes->cursorPos && i < note->contentLength; i++) {
            if(note->content[i] == '\n') {
                cursorY += 12;
                cursorX = contentX + 5;
            } else {
                cursorX += 8;
                if(cursorX > contentX + contentWidth - 15) {
                    cursorY += 12;
                    cursorX = contentX + 5;
                }
            }
        }
        
        DrawRect(cursorX, cursorY, 2, 10, COLOR_BLACK);
    }
    
    // Draw status bar
    int statusY = win->y + win->height - 28;
    DrawRect(win->x + 2, statusY, win->width - 4, 26, 0x1A1A1A);
    DrawText(win->x + 10, statusY + 9, notes->statusMessage, COLOR_WHITE);
}

void HandleNotesAppKeyPress(void* winPtr, char key) {
    WindowInternal* win = (WindowInternal*)winPtr;
    if(!win->isNotesApp) return;
    
    NotesAppData* notes = &win->notesData;
    
    // Static variables for new note name input
    static int waitingForName = 0;
    static char newNoteName[MAX_NOTE_NAME];
    static int namePos = 0;
    
    if(waitingForName) {
        if(key == '\n') {
            newNoteName[namePos] = '\0';
            if(namePos > 0) {
                NotesAppCreateNew(win, newNoteName);
            }
            waitingForName = 0;
            namePos = 0;
        } else if(key == '\b') {
            if(namePos > 0) {
                namePos--;
                newNoteName[namePos] = '\0';
                strcpy(notes->statusMessage, "New note name: ");
                strcat(notes->statusMessage, newNoteName);
            }
        } else if(key >= 32 && key <= 126 && namePos < MAX_NOTE_NAME - 1) {
            newNoteName[namePos++] = key;
            newNoteName[namePos] = '\0';
            strcpy(notes->statusMessage, "New note name: ");
            strcat(notes->statusMessage, newNoteName);
        }
        return;
    }
    
    if(key == 'n' || key == 'N') {
        waitingForName = 1;
        namePos = 0;
        newNoteName[0] = '\0';
        strcpy(notes->statusMessage, "New note name: ");
        return;
    }
    
    if(key == 's' || key == 'S') {
        NotesAppSave(win);
        return;
    }
    
    if(key == 'd' || key == 'D') {
        NotesAppDelete(win);
        return;
    }
    
    // Regular text input
    if(key == '\b') {
        NotesAppBackspace(win);
    } else if(key >= 32 && key <= 126 || key == '\n') {
        NotesAppInsertChar(win, key);
    }
}
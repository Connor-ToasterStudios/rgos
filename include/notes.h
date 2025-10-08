#ifndef NOTES_H
#define NOTES_H

#include "types.h"

#define MAX_NOTES 10
#define MAX_NOTE_CONTENT 2000
#define MAX_NOTE_NAME 32

typedef struct {
    char name[MAX_NOTE_NAME];
    char content[MAX_NOTE_CONTENT];
    int contentLength;
    int active;
} Note;

typedef struct {
    Note notes[MAX_NOTES];
    int noteCount;
    int currentNote;
    int cursorPos;
    int scrollOffset;
    char statusMessage[64];
} NotesAppData;

// Function declarations
void NotesAppInit(void* win);
void NotesAppCreateNew(void* win, const char* name);
void NotesAppSave(void* win);
void NotesAppDelete(void* win);
void NotesAppInsertChar(void* win, char c);
void NotesAppBackspace(void* win);
void DrawNotesApp(void* win);
void HandleNotesAppKeyPress(void* win, char key);

#endif
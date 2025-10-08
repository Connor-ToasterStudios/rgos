#include "../include/types.h"

#define MAX_TERMINAL_LINES 30
#define MAX_LINE_LENGTH 80
#define TERMINAL_HISTORY_SIZE 10
#define MAX_FILES 64
#define MAX_FILENAME 64

// FAT12 structures
#pragma pack(push, 1)
typedef struct {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t fatCount;
    uint16_t rootEntries;
    uint16_t totalSectors;
    uint8_t mediaType;
    uint16_t sectorsPerFat;
    uint16_t sectorsPerTrack;
    uint16_t headCount;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;
} FAT12_BPB;

typedef struct {
    char name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t createTimeTenth;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t accessDate;
    uint16_t clusterHigh;
    uint16_t modifyTime;
    uint16_t modifyDate;
    uint16_t clusterLow;
    uint32_t fileSize;
} FAT12_DirEntry;
#pragma pack(pop)

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20

typedef struct {
    char name[MAX_FILENAME];
    int isDirectory;
    uint32_t size;
    uint16_t cluster;
} FileEntry;

typedef struct {
    FileEntry files[MAX_FILES];
    int fileCount;
    int scrollOffset;
    int selectedIndex;
    char currentPath[256];
} FileBrowserData;

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

#define MAX_FILE_CONTENT 4096
typedef struct {
    char content[MAX_FILE_CONTENT];
    int contentLength;
    int cursorPos;
    int scrollLine;
    char filename[64];
    int modified;
    int editingFilename;
    int filenamePos;
} TextEditorData;

typedef struct {
    int x, y, width, height;
    char title[64];
    uint32_t titleBarColor, backgroundColor;
    int visible;
    int dragging;
    int dragOffsetX, dragOffsetY;
    int lastDrawX, lastDrawY;
    int windowType; // 0=normal, 1=terminal, 2=file browser, 3=text editor
    int isFocused;
    union {
        TerminalData termData;
        FileBrowserData browserData;
        TextEditorData editorData;
    };
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

// Keyboard state
static int ctrlPressed = 0;
static int shiftPressed = 0;

// Back buffer for cursor
static uint32_t cursorBackBuffer[20 * 20];
static int cursorBackBufferValid = 0;

// FAT12 state
static uint8_t* diskImage = NULL;
static FAT12_BPB bpb;
static uint16_t* fatTable = NULL;
static uint8_t* rootDir = NULL;

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

// 8x8 bitmap font
static const unsigned char font8x8[128][8] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    ['"'] = {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    ['$'] = {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    ['%'] = {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    ['&'] = {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    ['\''] = {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    [')'] = {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    ['*'] = {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    ['+'] = {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    ['-'] = {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    ['/'] = {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    ['0'] = {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    ['1'] = {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    ['2'] = {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    ['3'] = {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    ['4'] = {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    ['5'] = {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    ['6'] = {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    ['7'] = {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    ['8'] = {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    ['9'] = {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    [':'] = {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    [';'] = {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    ['<'] = {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    ['='] = {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    ['>'] = {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    ['?'] = {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    ['@'] = {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    ['A'] = {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    ['B'] = {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    ['C'] = {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    ['D'] = {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    ['E'] = {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    ['F'] = {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    ['G'] = {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    ['H'] = {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    ['I'] = {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    ['J'] = {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    ['K'] = {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    ['L'] = {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    ['M'] = {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    ['N'] = {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    ['O'] = {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    ['P'] = {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    ['Q'] = {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    ['R'] = {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    ['S'] = {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    ['T'] = {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    ['U'] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    ['V'] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    ['W'] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    ['X'] = {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    ['Y'] = {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    ['Z'] = {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    ['['] = {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    ['\\'] = {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    [']'] = {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    ['^'] = {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    ['`'] = {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['a'] = {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    ['b'] = {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    ['c'] = {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    ['d'] = {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},
    ['e'] = {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},
    ['f'] = {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},
    ['g'] = {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    ['h'] = {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    ['i'] = {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    ['j'] = {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    ['k'] = {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    ['l'] = {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    ['m'] = {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    ['n'] = {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    ['o'] = {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    ['p'] = {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    ['q'] = {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    ['r'] = {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    ['s'] = {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    ['t'] = {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    ['u'] = {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    ['v'] = {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    ['w'] = {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    ['x'] = {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    ['y'] = {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    ['z'] = {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    ['{'] = {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    ['|'] = {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    ['}'] = {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    ['~'] = {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

void DrawChar(uint32_t x, uint32_t y, char c, uint32_t color) {
    if(c < 0 || c >= 128) c = '?';
    
    const unsigned char* glyph = font8x8[(int)c];
    for(int row = 0; row < 8; row++) {
        unsigned char line = glyph[row];
        for(int col = 0; col < 8; col++) {
            if(line & (1 << col)) {
                DrawPixel(x + col, y + row, color);
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

// FAT12 functions
void InitFAT12() {
    diskImage = (uint8_t*)0x100000;
    
    FAT12_BPB* bpbPtr = (FAT12_BPB*)diskImage;
    bpbPtr->jump[0] = 0xEB;
    bpbPtr->jump[1] = 0x3C;
    bpbPtr->jump[2] = 0x90;
    
    for(int i = 0; i < 8; i++) bpbPtr->oem[i] = "RGOS 1.3 "[i];
    
    bpbPtr->bytesPerSector = 512;
    bpbPtr->sectorsPerCluster = 1;
    bpbPtr->reservedSectors = 1;
    bpbPtr->fatCount = 2;
    bpbPtr->rootEntries = 224;
    bpbPtr->totalSectors = 2880;
    bpbPtr->mediaType = 0xF0;
    bpbPtr->sectorsPerFat = 9;
    bpbPtr->sectorsPerTrack = 18;
    bpbPtr->headCount = 2;
    bpbPtr->hiddenSectors = 0;
    bpbPtr->totalSectors32 = 0;
    
    bpb = *bpbPtr;
    
    uint32_t fatOffset = bpb.reservedSectors * bpb.bytesPerSector;
    fatTable = (uint16_t*)(diskImage + fatOffset);
    
    fatTable[0] = 0xFF0;
    fatTable[1] = 0xFFF;
    
    uint32_t rootDirOffset = fatOffset + (bpb.fatCount * bpb.sectorsPerFat * bpb.bytesPerSector);
    rootDir = diskImage + rootDirOffset;
    
    FAT12_DirEntry* entries = (FAT12_DirEntry*)rootDir;
    
    for(int i = 0; i < 11; i++) entries[0].name[i] = "RGOS  DISK "[i];
    entries[0].attributes = ATTR_VOLUME_ID;
    
    for(int i = 0; i < 11; i++) entries[1].name[i] = "DOCUMENTS  "[i];
    entries[1].attributes = ATTR_DIRECTORY;
    entries[1].clusterLow = 2;
    
    for(int i = 0; i < 11; i++) entries[2].name[i] = "PICTURES   "[i];
    entries[2].attributes = ATTR_DIRECTORY;
    entries[2].clusterLow = 3;
    
    for(int i = 0; i < 11; i++) entries[3].name[i] = "README  TXT"[i];
    entries[3].attributes = ATTR_ARCHIVE;
    entries[3].clusterLow = 4;
    entries[3].fileSize = 256;
    
    for(int i = 0; i < 11; i++) entries[4].name[i] = "KERNEL  BIN"[i];
    entries[4].attributes = ATTR_ARCHIVE;
    entries[4].clusterLow = 5;
    entries[4].fileSize = 4096;
    
    for(int i = 0; i < 11; i++) entries[5].name[i] = "CONFIG  SYS"[i];
    entries[5].attributes = ATTR_ARCHIVE;
    entries[5].clusterLow = 6;
    entries[5].fileSize = 128;
    
    fatTable[2] = 0xFFF;
    fatTable[3] = 0xFFF;
    fatTable[4] = 0xFFF;
    fatTable[5] = 0xFFF;
    fatTable[6] = 0xFFF;
}

void FormatFAT12Name(const char* fat12Name, char* output) {
    int outputPos = 0;
    
    for(int i = 0; i < 8 && fat12Name[i] != ' '; i++) {
        output[outputPos++] = fat12Name[i];
    }
    
    int hasExt = 0;
    for(int i = 8; i < 11; i++) {
        if(fat12Name[i] != ' ') {
            hasExt = 1;
            break;
        }
    }
    
    if(hasExt) {
        output[outputPos++] = '.';
        for(int i = 8; i < 11 && fat12Name[i] != ' '; i++) {
            output[outputPos++] = fat12Name[i];
        }
    }
    
    output[outputPos] = '\0';
}

void LoadRootDirectory(FileBrowserData* fb) {
    fb->fileCount = 0;
    fb->scrollOffset = 0;
    fb->selectedIndex = 0;
    
    FAT12_DirEntry* entries = (FAT12_DirEntry*)rootDir;
    
    for(int i = 0; i < bpb.rootEntries && fb->fileCount < MAX_FILES; i++) {
        if(entries[i].name[0] == 0x00) break;
        if(entries[i].name[0] == 0xE5) continue;
        if(entries[i].attributes == ATTR_VOLUME_ID) continue;
        
        FileEntry* file = &fb->files[fb->fileCount];
        
        FormatFAT12Name(entries[i].name, file->name);
        file->isDirectory = (entries[i].attributes & ATTR_DIRECTORY) ? 1 : 0;
        file->size = entries[i].fileSize;
        file->cluster = entries[i].clusterLow;
        
        fb->fileCount++;
    }
}

uint16_t AllocateCluster() {
    for(uint16_t i = 2; i < 2880; i++) {
        if(fatTable[i] == 0) {
            fatTable[i] = 0xFFF;
            return i;
        }
    }
    return 0;
}

void WriteFileContent(uint16_t cluster, const char* content, uint32_t size) {
    if(cluster < 2) return;
    uint32_t dataOffset = (bpb.reservedSectors + bpb.fatCount * bpb.sectorsPerFat + 
                          (bpb.rootEntries * 32) / bpb.bytesPerSector) * bpb.bytesPerSector;
    uint32_t clusterOffset = dataOffset + (cluster - 2) * bpb.sectorsPerCluster * bpb.bytesPerSector;
    
    uint8_t* dest = diskImage + clusterOffset;
    for(uint32_t i = 0; i < size && i < bpb.sectorsPerCluster * bpb.bytesPerSector; i++) {
        dest[i] = content[i];
    }
}

void ReadFileContent(uint16_t cluster, char* buffer, uint32_t size) {
    if(cluster < 2) return;
    uint32_t dataOffset = (bpb.reservedSectors + bpb.fatCount * bpb.sectorsPerFat + 
                          (bpb.rootEntries * 32) / bpb.bytesPerSector) * bpb.bytesPerSector;
    uint32_t clusterOffset = dataOffset + (cluster - 2) * bpb.sectorsPerCluster * bpb.bytesPerSector;
    
    uint8_t* src = diskImage + clusterOffset;
    for(uint32_t i = 0; i < size && i < bpb.sectorsPerCluster * bpb.bytesPerSector; i++) {
        buffer[i] = src[i];
    }
}

void CreateNewFile(const char* filename, const char* content, uint32_t size) {
    FAT12_DirEntry* entries = (FAT12_DirEntry*)rootDir;
    
    int emptySlot = -1;
    for(int i = 0; i < bpb.rootEntries; i++) {
        if(entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            emptySlot = i;
            break;
        }
    }
    
    if(emptySlot == -1) return;
    
    FAT12_DirEntry* entry = &entries[emptySlot];
    
    for(int i = 0; i < 11; i++) entry->name[i] = ' ';
    
    int nameLen = 0;
    while(filename[nameLen] && filename[nameLen] != '.' && nameLen < 8) {
        entry->name[nameLen] = filename[nameLen];
        nameLen++;
    }
    
    int dotPos = 0;
    while(filename[dotPos] && filename[dotPos] != '.') dotPos++;
    
    if(filename[dotPos] == '.') {
        dotPos++;
        for(int i = 0; i < 3 && filename[dotPos]; i++, dotPos++) {
            entry->name[8 + i] = filename[dotPos];
        }
    }
    
    entry->attributes = ATTR_ARCHIVE;
    uint16_t cluster = AllocateCluster();
    entry->clusterLow = cluster;
    entry->fileSize = size;
    
    WriteFileContent(cluster, content, size);
}

void DrawFileBrowserContent(Window* win) {
    if(!win->visible) return;
    
    FileBrowserData* fb = &win->browserData;
    
    int contentX = win->x + 8;
    int contentY = win->y + 38;
    int contentWidth = win->width - 16;
    int contentHeight = win->height - 46;
    
    DrawRect(contentX - 4, contentY - 4, contentWidth + 8, contentHeight + 8, COLOR_WINDOW_BG);
    
    DrawText(contentX, contentY, "Location: /", COLOR_BLACK);
    
    int headerY = contentY + 20;
    DrawRect(contentX, headerY, contentWidth, 20, 0xE0E0E0);
    DrawText(contentX + 4, headerY + 6, "Name", COLOR_BLACK);
    DrawText(contentX + 300, headerY + 6, "Type", COLOR_BLACK);
    DrawText(contentX + 420, headerY + 6, "Size", COLOR_BLACK);
    
    int fileY = headerY + 24;
    int visibleFiles = (contentHeight - 50) / 20;
    
    for(int i = fb->scrollOffset; i < fb->fileCount && i < fb->scrollOffset + visibleFiles; i++) {
        FileEntry* file = &fb->files[i];
        
        if(i == fb->selectedIndex) {
            DrawRect(contentX, fileY, contentWidth, 18, 0x0078D7);
        }
        
        uint32_t textColor = (i == fb->selectedIndex) ? COLOR_WHITE : COLOR_BLACK;
        
        if(file->isDirectory) {
            DrawRect(contentX + 4, fileY + 2, 14, 14, 0xFFCC00);
            DrawText(contentX + 22, fileY + 4, file->name, textColor);
        } else {
            DrawRect(contentX + 4, fileY + 2, 14, 14, 0xCCCCCC);
            DrawText(contentX + 22, fileY + 4, file->name, textColor);
        }
        
        const char* typeStr = file->isDirectory ? "Folder" : "File";
        DrawText(contentX + 300, fileY + 4, typeStr, textColor);
        
        if(!file->isDirectory) {
            char sizeStr[32];
            uint32_t size = file->size;
            if(size < 1024) {
                int idx = 0;
                if(size == 0) {
                    sizeStr[idx++] = '0';
                } else {
                    char temp[16];
                    int tempIdx = 0;
                    while(size > 0) {
                        temp[tempIdx++] = '0' + (size % 10);
                        size /= 10;
                    }
                    for(int j = tempIdx - 1; j >= 0; j--) {
                        sizeStr[idx++] = temp[j];
                    }
                }
                sizeStr[idx++] = ' ';
                sizeStr[idx++] = 'B';
                sizeStr[idx] = '\0';
            } else {
                uint32_t kb = size / 1024;
                int idx = 0;
                char temp[16];
                int tempIdx = 0;
                while(kb > 0) {
                    temp[tempIdx++] = '0' + (kb % 10);
                    kb /= 10;
                }
                for(int j = tempIdx - 1; j >= 0; j--) {
                    sizeStr[idx++] = temp[j];
                }
                sizeStr[idx++] = ' ';
                sizeStr[idx++] = 'K';
                sizeStr[idx++] = 'B';
                sizeStr[idx] = '\0';
            }
            DrawText(contentX + 420, fileY + 4, sizeStr, textColor);
        }
        
        fileY += 20;
    }
    
    if(fb->fileCount > visibleFiles) {
        int scrollBarX = contentX + contentWidth - 16;
        int scrollBarY = headerY + 24;
        int scrollBarHeight = contentHeight - 50;
        
        DrawRect(scrollBarX, scrollBarY, 14, scrollBarHeight, 0xE0E0E0);
        
        int thumbHeight = (visibleFiles * scrollBarHeight) / fb->fileCount;
        if(thumbHeight < 20) thumbHeight = 20;
        
        int thumbY = scrollBarY + (fb->scrollOffset * (scrollBarHeight - thumbHeight)) / (fb->fileCount - visibleFiles);
        DrawRect(scrollBarX + 2, thumbY, 10, thumbHeight, 0x808080);
    }
}

// Terminal functions
void TerminalAddLine(Window* win, const char* text) {
    if(win->windowType != 1) return;
    
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
        TerminalAddLine(win, "  about  - About RGOS");
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
        TerminalAddLine(win, "RGOS v1.3 - Custom UEFI OS");
        TerminalAddLine(win, "With FAT12 File Browser");
    }
    else if(strcmp(cmd, "date") == 0) {
        TerminalAddLine(win, "Mon Oct 7 12:34:56 2024");
    }
    else if(strcmp(cmd, "ls") == 0) {
        TerminalAddLine(win, "DOCUMENTS/  PICTURES/  README.TXT");
        TerminalAddLine(win, "KERNEL.BIN  CONFIG.SYS");
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
    if(win->windowType != 1 || !win->visible) return;
    
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
    
    DrawText(contentX, lineY, "user@rgos:~$ ", COLOR_TERMINAL_TEXT);
    DrawText(contentX + 13 * 8, lineY, term->inputBuffer, COLOR_TERMINAL_TEXT);
    
    int cursorX = contentX + 13 * 8 + term->inputPos * 8;
    DrawRect(cursorX, lineY, 8, 10, COLOR_TERMINAL_TEXT);
}

// Text Editor functions
void DrawTextEditorContent(Window* win) {
    if(win->windowType != 3 || !win->visible) return;
    
    TextEditorData* editor = &win->editorData;
    int contentX = win->x + 8;
    int contentY = win->y + 38;
    int contentWidth = win->width - 16;
    int contentHeight = win->height - 66;
    
    // Draw white background for text area
    DrawRect(contentX - 4, contentY - 4, contentWidth + 8, contentHeight + 8, COLOR_WHITE);
    
    // Draw filename bar at the top
    DrawRect(contentX - 4, contentY - 4, contentWidth + 8, 20, 0xD0D0D0);
    
    if(editor->editingFilename) {
        // Editing filename mode
        DrawText(contentX, contentY, "Filename: ", COLOR_BLACK);
        DrawText(contentX + 10 * 8, contentY, editor->filename, COLOR_BLACK);
        
        // Draw cursor under filename
        int cursorX = contentX + 10 * 8 + editor->filenamePos * 8;
        DrawRect(cursorX, contentY + 10, 8, 2, COLOR_BLACK);
        
        DrawText(contentX + 400, contentY, "Enter: Done", 0x0078D7);
    } else {
        // Normal mode - show filename
        char titleBar[80];
        strcpy(titleBar, "File: ");
        strcat(titleBar, editor->filename);
        if(editor->modified) strcat(titleBar, " *");
        DrawText(contentX, contentY, titleBar, COLOR_BLACK);
        
        DrawText(contentX + 400, contentY, "F3: Rename", 0x0078D7);
    }
    
    // Start drawing text content below the filename bar
    int lineY = contentY + 24;
    int lineNum = 0;
    int charX = 0;
    
    // Draw each character
    for(int i = 0; i < editor->contentLength && lineY < contentY + contentHeight - 12; i++) {
        char c = editor->content[i];
        
        if(c == '\n') {
            lineNum++;
            lineY += 12;
            charX = 0;
        } else if(c >= 32 && c <= 126) {
            if(charX < 85) {  // Limit characters per line
                DrawChar(contentX + charX * 8, lineY, c, COLOR_BLACK);
                charX++;
            } else {
                // Wrap to next line
                lineNum++;
                lineY += 12;
                charX = 0;
                DrawChar(contentX + charX * 8, lineY, c, COLOR_BLACK);
                charX++;
            }
        }
    }
    
    // Draw status bar at the bottom
    int statusY = win->y + win->height - 24;
    DrawRect(contentX - 4, statusY, contentWidth + 8, 20, 0xE0E0E0);
    if(editor->editingFilename) {
        DrawText(contentX, statusY + 6, "Enter filename and press Enter", COLOR_BLACK);
    } else {
        DrawText(contentX, statusY + 6, "F2: Save  F3: Rename  Esc: Close", COLOR_BLACK);
    }
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
    
    if(win->windowType == 1) {
        DrawTerminalContent(win);
    } else if(win->windowType == 2) {
        DrawFileBrowserContent(win);
    } else if(win->windowType == 3) {
        DrawTextEditorContent(win);
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
    DrawText(fb->width - 135, taskbarY + 16, "RGOS v1.3", COLOR_WHITE);
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

void CreateWindow(int x, int y, int width, int height, const char* title, uint32_t color, int windowType) {
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
    win->windowType = windowType;
    win->isFocused = 0;
    
    if(windowType == 1) {
        win->termData.lineCount = 0;
        win->termData.inputPos = 0;
        win->termData.inputBuffer[0] = '\0';
        win->termData.historyCount = 0;
        TerminalAddLine(win, "RGOS Terminal v1.3");
        TerminalAddLine(win, "Type 'help' for commands");
        TerminalAddLine(win, "");
    } else if(windowType == 2) {
        win->browserData.currentPath[0] = '/';
        win->browserData.currentPath[1] = '\0';
        LoadRootDirectory(&win->browserData);
    } else if(windowType == 3) {
        win->editorData.contentLength = 0;
        win->editorData.cursorPos = 0;
        win->editorData.scrollLine = 0;
        win->editorData.modified = 0;
        win->editorData.content[0] = '\0';
        win->editorData.filename[0] = '\0';
    }
    
    strcpy(win->title, title);
    windowCount++;
}

int PointInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void HandleFileBrowserClick(Window* win, int x, int y) {
    FileBrowserData* fb = &win->browserData;
    
    int contentX = win->x + 8;
    int contentY = win->y + 38;
    int headerY = contentY + 20;
    int fileY = headerY + 24;
    int contentHeight = win->height - 46;
    int visibleFiles = (contentHeight - 50) / 20;
    
    if(y >= fileY && y < fileY + visibleFiles * 20) {
        int clickedIndex = fb->scrollOffset + (y - fileY) / 20;
        if(clickedIndex < fb->fileCount) {
            fb->selectedIndex = clickedIndex;
            DrawWindow(win);
        }
    }
}

void OpenFileInEditor(const char* filename, uint16_t cluster, uint32_t fileSize) {
    CreateWindow(120, 120, 700, 500, "Text Editor", COLOR_TITLEBAR_BLUE, 3);
    Window* editor = &windows[windowCount - 1];
    
    strcpy(editor->editorData.filename, filename);
    
    if(cluster >= 2 && fileSize > 0) {
        if(fileSize > MAX_FILE_CONTENT - 1) fileSize = MAX_FILE_CONTENT - 1;
        ReadFileContent(cluster, editor->editorData.content, fileSize);
        editor->editorData.contentLength = fileSize;
        editor->editorData.content[fileSize] = '\0';
    } else {
        editor->editorData.contentLength = 0;
        editor->editorData.content[0] = '\0';
    }
    
    editor->editorData.cursorPos = 0;
    editor->editorData.scrollLine = 0;
    editor->editorData.modified = 0;
    editor->editorData.editingFilename = 0;
    editor->editorData.filenamePos = strlen(filename);
}

void CreateNewFileEditor() {
    CreateWindow(120, 120, 700, 500, "Text Editor - New File", COLOR_TITLEBAR_BLUE, 3);
    Window* editor = &windows[windowCount - 1];
    
    strcpy(editor->editorData.filename, "newfile.txt");
    editor->editorData.contentLength = 0;
    editor->editorData.content[0] = '\0';
    editor->editorData.cursorPos = 0;
    editor->editorData.scrollLine = 0;
    editor->editorData.modified = 0;
    editor->editorData.editingFilename = 1;
    editor->editorData.filenamePos = strlen(editor->editorData.filename);
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
            } else if(win->windowType == 2) {
                HandleFileBrowserClick(win, x, y);
            }
            
            RedrawEverything();
            return;
        }
    }
    
    if(PointInRect(x, y, 130, 30, 64, 64)) {
        CreateWindow(100, 100, 700, 500, "File Browser", COLOR_TITLEBAR_GREEN, 2);
        RedrawEverything();
    } else if(PointInRect(x, y, 230, 30, 64, 64)) {
        CreateWindow(150, 150, 700, 500, "Terminal", COLOR_TITLEBAR_BLUE, 1);
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
    if(!win->visible) return;
    
    if(win->windowType == 1) {
        TerminalData* term = &win->termData;
        
        if(key == '\n') {
            term->inputBuffer[term->inputPos] = '\0';
            
            char cmdLine[MAX_LINE_LENGTH];
            strcpy(cmdLine, "user@rgos:~$ ");
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
    } else if(win->windowType == 2) {
        FileBrowserData* fb = &win->browserData;
        
        if(key == 'j' || key == 's') {
            if(fb->selectedIndex < fb->fileCount - 1) {
                fb->selectedIndex++;
                int visibleFiles = (win->height - 96) / 20;
                if(fb->selectedIndex >= fb->scrollOffset + visibleFiles) {
                    fb->scrollOffset++;
                }
                DrawWindow(win);
            }
        } else if(key == 'k' || key == 'w') {
            if(fb->selectedIndex > 0) {
                fb->selectedIndex--;
                if(fb->selectedIndex < fb->scrollOffset) {
                    fb->scrollOffset--;
                }
                DrawWindow(win);
            }
        } else if(key == '\n') {
            if(fb->selectedIndex >= 0 && fb->selectedIndex < fb->fileCount) {
                FileEntry* file = &fb->files[fb->selectedIndex];
                if(!file->isDirectory) {
                    OpenFileInEditor(file->name, file->cluster, file->size);
                    RedrawEverything();
                }
            }
        } else if(key == 'n') {
            CreateNewFileEditor();
            RedrawEverything();
        }
    } else if(win->windowType == 3) {
        TextEditorData* editor = &win->editorData;
        
        if(editor->editingFilename) {
            // Filename editing mode
            if(key == '\n') {
                // Finish editing filename
                editor->editingFilename = 0;
                DrawWindow(win);
            }
            else if(key == '\b') {
                if(editor->filenamePos > 0) {
                    // Remove character from filename
                    for(int i = editor->filenamePos - 1; i < 63; i++) {
                        editor->filename[i] = editor->filename[i + 1];
                    }
                    editor->filenamePos--;
                    DrawWindow(win);
                }
            }
            else if(key >= 32 && key <= 126) {
                if(editor->filenamePos < 63) {
                    // Insert character into filename
                    for(int i = 63; i > editor->filenamePos; i--) {
                        editor->filename[i] = editor->filename[i - 1];
                    }
                    editor->filename[editor->filenamePos] = key;
                    editor->filenamePos++;
                    editor->filename[63] = '\0';
                    DrawWindow(win);
                }
            }
        } else {
            // Normal text editing mode
            // F2 key for save
            if(key == 1) {
                CreateNewFile(editor->filename, editor->content, editor->contentLength);
                editor->modified = 0;
                
                for(int i = 0; i < windowCount; i++) {
                    if(windows[i].windowType == 2 && windows[i].visible) {
                        LoadRootDirectory(&windows[i].browserData);
                    }
                }
                
                DrawWindow(win);
            }
            // F3 key for rename
            else if(key == 2) {
                editor->editingFilename = 1;
                editor->filenamePos = strlen(editor->filename);
                DrawWindow(win);
            }
            else if(key == 27) {  // Esc
                win->visible = 0;
                RedrawEverything();
            }
            else if(key == '\b') {
                if(editor->contentLength > 0) {
                    editor->contentLength--;
                    editor->content[editor->contentLength] = '\0';
                    editor->modified = 1;
                    DrawWindow(win);
                }
            }
            else if(key >= 32 && key <= 126 || key == '\n') {
                if(editor->contentLength < MAX_FILE_CONTENT - 1) {
                    editor->content[editor->contentLength] = key;
                    editor->contentLength++;
                    editor->content[editor->contentLength] = '\0';
                    editor->modified = 1;
                    DrawWindow(win);
                }
            }
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
    
    uint8_t status = inb(0x64);
    if(!(status & 0x01)) return;
    if(!(status & 0x20)) return;
    
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
    uint8_t status = inb(0x64);
    if(!(status & 0x01)) return;
    if(status & 0x20) return;
    
    unsigned char scancode = inb(0x60);
    
    // Check for key release (high bit set)
    if(scancode & 0x80) {
        scancode &= 0x7F;
        // Handle ctrl/shift release
        if(scancode == 29) ctrlPressed = 0;  // Ctrl release
        if(scancode == 42 || scancode == 54) shiftPressed = 0;  // Shift release
        return;
    }
    
    // Handle ctrl/shift press
    if(scancode == 29) {
        ctrlPressed = 1;
        return;
    }
    if(scancode == 42 || scancode == 54) {
        shiftPressed = 1;
        return;
    }
    
    // F2 key for save
    if(scancode == 60) {
        HandleKeyPress(1);  // Special code for F2
        return;
    }
    
    // F3 key for rename
    if(scancode == 61) {
        HandleKeyPress(2);  // Special code for F3
        return;
    }
    
    // Escape key
    if(scancode == 1) {
        HandleKeyPress(27);
        return;
    }
    
    char key = ScancodeToChar(scancode);
    if(key) {
        HandleKeyPress(key);
    }
}

void KernelMain(Framebuffer* framebuffer) {
    fb = framebuffer;
    
    InitFAT12();
    InitMouse();
    
    DrawDesktop();
    
    CreateWindow(100, 100, 700, 500, "File Browser", COLOR_TITLEBAR_GREEN, 2);
    CreateWindow(150, 150, 700, 500, "Terminal", COLOR_TITLEBAR_BLUE, 1);
    CreateWindow(200, 200, 450, 300, "Test", COLOR_TITLEBAR_RED, 0);
    
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
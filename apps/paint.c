#ifndef PAINT_APP_C
#define PAINT_APP_C

#define PAINT_CANVAS_WIDTH 400
#define PAINT_CANVAS_HEIGHT 300
#define PAINT_TOOLBAR_HEIGHT 40

typedef struct {
    uint32_t canvas[PAINT_CANVAS_HEIGHT][PAINT_CANVAS_WIDTH];
    uint32_t currentColor;
    int brushSize;
    int tool; // 0=brush, 1=eraser, 2=fill, 3=line, 4=rectangle, 5=circle
    int isDrawing;
    int lastX;
    int lastY;
    int startX; // For shapes
    int startY;
    int modified;
} PaintData;

// Available colors
static const uint32_t paintColors[] = {
    0x000000, // Black
    0xFFFFFF, // White
    0xFF0000, // Red
    0x00FF00, // Green
    0x0000FF, // Blue
    0xFFFF00, // Yellow
    0xFF00FF, // Magenta
    0x00FFFF, // Cyan
    0xFFA500, // Orange
    0x800080, // Purple
    0xFFC0CB, // Pink
    0x8B4513, // Brown
};

// Forward declarations from kernel
extern void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void DrawPixel(uint32_t x, uint32_t y, uint32_t color);
extern void DrawText(uint32_t x, uint32_t y, const char* text, uint32_t color);
extern uint32_t GetPixel(uint32_t x, uint32_t y);

void PaintInit(PaintData* paint) {
    // Clear canvas to white
    for(int y = 0; y < PAINT_CANVAS_HEIGHT; y++) {
        for(int x = 0; x < PAINT_CANVAS_WIDTH; x++) {
            paint->canvas[y][x] = 0xFFFFFF;
        }
    }
    
    paint->currentColor = 0x000000; // Black
    paint->brushSize = 3;
    paint->tool = 0; // Brush
    paint->isDrawing = 0;
    paint->lastX = -1;
    paint->lastY = -1;
    paint->startX = -1;
    paint->startY = -1;
    paint->modified = 0;
}

void PaintDrawPixelOnCanvas(PaintData* paint, int x, int y, uint32_t color) {
    if(x >= 0 && x < PAINT_CANVAS_WIDTH && y >= 0 && y < PAINT_CANVAS_HEIGHT) {
        paint->canvas[y][x] = color;
        paint->modified = 1;
    }
}

void PaintDrawBrush(PaintData* paint, int x, int y) {
    int halfSize = paint->brushSize / 2;
    for(int dy = -halfSize; dy <= halfSize; dy++) {
        for(int dx = -halfSize; dx <= halfSize; dx++) {
            // Draw circle brush
            if(dx * dx + dy * dy <= halfSize * halfSize) {
                PaintDrawPixelOnCanvas(paint, x + dx, y + dy, paint->currentColor);
            }
        }
    }
}

void PaintDrawLine(PaintData* paint, int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's line algorithm
    int dx = x1 - x0;
    int dy = y1 - y0;
    if(dx < 0) dx = -dx;
    if(dy < 0) dy = -dy;
    
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while(1) {
        PaintDrawPixelOnCanvas(paint, x0, y0, color);
        
        if(x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if(e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if(e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void PaintDrawRectangle(PaintData* paint, int x0, int y0, int x1, int y1, uint32_t color) {
    // Draw rectangle outline
    PaintDrawLine(paint, x0, y0, x1, y0, color);
    PaintDrawLine(paint, x1, y0, x1, y1, color);
    PaintDrawLine(paint, x1, y1, x0, y1, color);
    PaintDrawLine(paint, x0, y1, x0, y0, color);
}

void PaintDrawCircle(PaintData* paint, int cx, int cy, int radius, uint32_t color) {
    // Midpoint circle algorithm
    int x = radius;
    int y = 0;
    int err = 0;
    
    while(x >= y) {
        PaintDrawPixelOnCanvas(paint, cx + x, cy + y, color);
        PaintDrawPixelOnCanvas(paint, cx + y, cy + x, color);
        PaintDrawPixelOnCanvas(paint, cx - y, cy + x, color);
        PaintDrawPixelOnCanvas(paint, cx - x, cy + y, color);
        PaintDrawPixelOnCanvas(paint, cx - x, cy - y, color);
        PaintDrawPixelOnCanvas(paint, cx - y, cy - x, color);
        PaintDrawPixelOnCanvas(paint, cx + y, cy - x, color);
        PaintDrawPixelOnCanvas(paint, cx + x, cy - y, color);
        
        if(err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if(err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void PaintFloodFill(PaintData* paint, int x, int y, uint32_t targetColor, uint32_t fillColor) {
    if(x < 0 || x >= PAINT_CANVAS_WIDTH || y < 0 || y >= PAINT_CANVAS_HEIGHT) return;
    if(paint->canvas[y][x] != targetColor) return;
    if(targetColor == fillColor) return;
    
    // Simple recursive flood fill (stack-based would be better for large areas)
    paint->canvas[y][x] = fillColor;
    
    PaintFloodFill(paint, x + 1, y, targetColor, fillColor);
    PaintFloodFill(paint, x - 1, y, targetColor, fillColor);
    PaintFloodFill(paint, x, y + 1, targetColor, fillColor);
    PaintFloodFill(paint, x, y - 1, targetColor, fillColor);
}

void DrawPaintApp(void* win_ptr, PaintData* paint) {
    typedef struct {
        int x, y, width, height;
        char title[64];
        uint32_t titleBarColor, backgroundColor;
        int visible;
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    if(!win->visible) return;
    
    int canvasX = win->x + 10;
    int canvasY = win->y + 40 + PAINT_TOOLBAR_HEIGHT;
    
    // Clear background
    DrawRect(win->x + 2, win->y + 30, win->width - 4, win->height - 32, 0xCCCCCC);
    
    // Draw toolbar background
    DrawRect(canvasX, win->y + 40, PAINT_CANVAS_WIDTH, PAINT_TOOLBAR_HEIGHT, 0x333333);
    
    // Draw tool buttons
    const char* tools[] = {"Brush", "Erase", "Fill", "Line", "Rect", "Circle"};
    for(int i = 0; i < 6; i++) {
        int btnX = canvasX + 5 + i * 65;
        int btnY = win->y + 45;
        
        // Highlight selected tool
        uint32_t btnColor = (paint->tool == i) ? 0x0078D7 : 0x555555;
        DrawRect(btnX, btnY, 60, 30, btnColor);
        DrawText(btnX + 5, btnY + 11, tools[i], 0xFFFFFF);
    }
    
    // Draw color palette
    int paletteX = canvasX;
    int paletteY = canvasY + PAINT_CANVAS_HEIGHT + 5;
    
    for(int i = 0; i < 12; i++) {
        int colorX = paletteX + i * 33;
        uint32_t borderColor = (paintColors[i] == paint->currentColor) ? 0xFF0000 : 0x000000;
        DrawRect(colorX - 2, paletteY - 2, 34, 34, borderColor);
        DrawRect(colorX, paletteY, 30, 30, paintColors[i]);
    }
    
    // Draw brush size indicator
    DrawText(paletteX, paletteY + 40, "Size: ", 0x000000);
    char sizeStr[4];
    if(paint->brushSize == 1) sizeStr[0] = '1';
    else if(paint->brushSize == 3) sizeStr[0] = '3';
    else if(paint->brushSize == 5) sizeStr[0] = '5';
    else sizeStr[0] = '7';
    sizeStr[1] = '\0';
    DrawText(paletteX + 48, paletteY + 40, sizeStr, 0x000000);
    
    // Draw size buttons
    DrawRect(paletteX + 80, paletteY + 38, 20, 16, 0x555555);
    DrawText(paletteX + 86, paletteY + 41, "-", 0xFFFFFF);
    DrawRect(paletteX + 105, paletteY + 38, 20, 16, 0x555555);
    DrawText(paletteX + 110, paletteY + 41, "+", 0xFFFFFF);
    
    // Draw canvas border
    DrawRect(canvasX - 2, canvasY - 2, PAINT_CANVAS_WIDTH + 4, PAINT_CANVAS_HEIGHT + 4, 0x000000);
    
    // Draw canvas
    for(int y = 0; y < PAINT_CANVAS_HEIGHT; y++) {
        for(int x = 0; x < PAINT_CANVAS_WIDTH; x++) {
            DrawPixel(canvasX + x, canvasY + y, paint->canvas[y][x]);
        }
    }
}

void HandlePaintMouseDown(void* win_ptr, PaintData* paint, int mouseX, int mouseY) {
    typedef struct {
        int x, y, width, height;
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    
    int canvasX = win->x + 10;
    int canvasY = win->y + 40 + PAINT_TOOLBAR_HEIGHT;
    
    // Check toolbar buttons
    if(mouseY >= win->y + 45 && mouseY <= win->y + 75) {
        for(int i = 0; i < 6; i++) {
            int btnX = canvasX + 5 + i * 65;
            if(mouseX >= btnX && mouseX < btnX + 60) {
                paint->tool = i;
                DrawPaintApp(win_ptr, paint);
                return;
            }
        }
    }
    
    // Check color palette
    int paletteY = canvasY + PAINT_CANVAS_HEIGHT + 5;
    if(mouseY >= paletteY && mouseY < paletteY + 30) {
        for(int i = 0; i < 12; i++) {
            int colorX = canvasX + i * 33;
            if(mouseX >= colorX && mouseX < colorX + 30) {
                paint->currentColor = paintColors[i];
                DrawPaintApp(win_ptr, paint);
                return;
            }
        }
    }
    
    // Check brush size buttons
    if(mouseY >= paletteY + 38 && mouseY <= paletteY + 54) {
        // Decrease size
        if(mouseX >= canvasX + 80 && mouseX < canvasX + 100) {
            if(paint->brushSize > 1) paint->brushSize -= 2;
            DrawPaintApp(win_ptr, paint);
            return;
        }
        // Increase size
        if(mouseX >= canvasX + 105 && mouseX < canvasX + 125) {
            if(paint->brushSize < 9) paint->brushSize += 2;
            DrawPaintApp(win_ptr, paint);
            return;
        }
    }
    
    // Check canvas area
    if(mouseX >= canvasX && mouseX < canvasX + PAINT_CANVAS_WIDTH &&
       mouseY >= canvasY && mouseY < canvasY + PAINT_CANVAS_HEIGHT) {
        
        int localX = mouseX - canvasX;
        int localY = mouseY - canvasY;
        
        paint->isDrawing = 1;
        paint->lastX = localX;
        paint->lastY = localY;
        paint->startX = localX;
        paint->startY = localY;
        
        if(paint->tool == 0) { // Brush
            PaintDrawBrush(paint, localX, localY);
            DrawPaintApp(win_ptr, paint);
        } else if(paint->tool == 1) { // Eraser
            uint32_t oldColor = paint->currentColor;
            paint->currentColor = 0xFFFFFF;
            PaintDrawBrush(paint, localX, localY);
            paint->currentColor = oldColor;
            DrawPaintApp(win_ptr, paint);
        } else if(paint->tool == 2) { // Fill
            uint32_t targetColor = paint->canvas[localY][localX];
            PaintFloodFill(paint, localX, localY, targetColor, paint->currentColor);
            paint->modified = 1;
            DrawPaintApp(win_ptr, paint);
        }
    }
}

void HandlePaintMouseMove(void* win_ptr, PaintData* paint, int mouseX, int mouseY) {
    if(!paint->isDrawing) return;
    
    typedef struct {
        int x, y, width, height;
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    
    int canvasX = win->x + 10;
    int canvasY = win->y + 40 + PAINT_TOOLBAR_HEIGHT;
    
    if(mouseX >= canvasX && mouseX < canvasX + PAINT_CANVAS_WIDTH &&
       mouseY >= canvasY && mouseY < canvasY + PAINT_CANVAS_HEIGHT) {
        
        int localX = mouseX - canvasX;
        int localY = mouseY - canvasY;
        
        if(paint->tool == 0) { // Brush - draw line from last position
            PaintDrawLine(paint, paint->lastX, paint->lastY, localX, localY, paint->currentColor);
            PaintDrawBrush(paint, localX, localY);
            paint->lastX = localX;
            paint->lastY = localY;
            DrawPaintApp(win_ptr, paint);
        } else if(paint->tool == 1) { // Eraser
            uint32_t oldColor = paint->currentColor;
            paint->currentColor = 0xFFFFFF;
            PaintDrawLine(paint, paint->lastX, paint->lastY, localX, localY, 0xFFFFFF);
            PaintDrawBrush(paint, localX, localY);
            paint->currentColor = oldColor;
            paint->lastX = localX;
            paint->lastY = localY;
            DrawPaintApp(win_ptr, paint);
        }
    }
}

void HandlePaintMouseUp(void* win_ptr, PaintData* paint, int mouseX, int mouseY) {
    if(!paint->isDrawing) return;
    
    typedef struct {
        int x, y, width, height;
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    
    int canvasX = win->x + 10;
    int canvasY = win->y + 40 + PAINT_TOOLBAR_HEIGHT;
    
    if(mouseX >= canvasX && mouseX < canvasX + PAINT_CANVAS_WIDTH &&
       mouseY >= canvasY && mouseY < canvasY + PAINT_CANVAS_HEIGHT) {
        
        int localX = mouseX - canvasX;
        int localY = mouseY - canvasY;
        
        if(paint->tool == 3) { // Line
            PaintDrawLine(paint, paint->startX, paint->startY, localX, localY, paint->currentColor);
            DrawPaintApp(win_ptr, paint);
        } else if(paint->tool == 4) { // Rectangle
            PaintDrawRectangle(paint, paint->startX, paint->startY, localX, localY, paint->currentColor);
            DrawPaintApp(win_ptr, paint);
        } else if(paint->tool == 5) { // Circle
            int dx = localX - paint->startX;
            int dy = localY - paint->startY;
            int radius = 0;
            // Calculate radius
            while(radius * radius < dx * dx + dy * dy) radius++;
            PaintDrawCircle(paint, paint->startX, paint->startY, radius, paint->currentColor);
            DrawPaintApp(win_ptr, paint);
        }
    }
    
    paint->isDrawing = 0;
}

void HandlePaintKeyPress(void* win_ptr, PaintData* paint, unsigned char key) {
    // Clear canvas with 'c'
    if(key == 'c') {
        for(int y = 0; y < PAINT_CANVAS_HEIGHT; y++) {
            for(int x = 0; x < PAINT_CANVAS_WIDTH; x++) {
                paint->canvas[y][x] = 0xFFFFFF;
            }
        }
        paint->modified = 1;
        DrawPaintApp(win_ptr, paint);
    }
    // Toggle tools with number keys
    else if(key >= '1' && key <= '6') {
        paint->tool = key - '1';
        DrawPaintApp(win_ptr, paint);
    }
}

#endif // PAINT_APP_C
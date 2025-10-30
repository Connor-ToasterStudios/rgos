// Standalone Tetris game application

#ifndef TETRIS_APP_C
#define TETRIS_APP_C

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define BLOCK_SIZE 20

// Tetromino shapes (I, O, T, S, Z, J, L)
static const int tetrominoes[7][4][4][4] = {
    // I piece
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    // O piece
    {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    // T piece
    {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // S piece
    {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // Z piece
    {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    // J piece
    {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    // L piece
    {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
     {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}}
};

static const uint32_t tetrominoColors[7] = {
    0x00FFFF, // I - Cyan
    0xFFFF00, // O - Yellow
    0x800080, // T - Purple
    0x00FF00, // S - Green
    0xFF0000, // Z - Red
    0x0000FF, // J - Blue
    0xFFA500  // L - Orange
};

typedef struct {
    int board[BOARD_HEIGHT][BOARD_WIDTH];
    int currentPiece;
    int currentRotation;
    int currentX;
    int currentY;
    int nextPiece;
    int score;
    int lines;
    int level;
    int gameOver;
    int paused;
    int dropCounter;
    int dropSpeed;
    int clearing;
} TetrisGame;

// Forward declarations - these come from kernel
extern void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void DrawText(uint32_t x, uint32_t y, const char* text, uint32_t color);
extern void IntToStr(int num, char* str);
extern int Random(int max);

// Tetris-specific functions
int TetrisCheckCollision(TetrisGame* game, int piece, int rotation, int x, int y) {
    for(int row = 0; row < 4; row++) {
        for(int col = 0; col < 4; col++) {
            if(tetrominoes[piece][rotation][row][col]) {
                int boardX = x + col;
                int boardY = y + row;
                if(boardX < 0 || boardX >= BOARD_WIDTH || boardY >= BOARD_HEIGHT) return 1;
                if(boardY >= 0 && game->board[boardY][boardX]) return 1;
            }
        }
    }
    return 0;
}

void TetrisLockPiece(TetrisGame* game) {
    for(int row = 0; row < 4; row++) {
        for(int col = 0; col < 4; col++) {
            if(tetrominoes[game->currentPiece][game->currentRotation][row][col]) {
                int boardY = game->currentY + row;
                int boardX = game->currentX + col;
                if(boardY >= 0 && boardY < BOARD_HEIGHT && 
                   boardX >= 0 && boardX < BOARD_WIDTH) {
                    game->board[boardY][boardX] = game->currentPiece + 1;
                }
            }
        }
    }
}

int TetrisClearLines(TetrisGame* game) {
    int linesCleared = 0;
    
    // First pass: identify which lines are full
    int linesToClear[BOARD_HEIGHT];
    for(int i = 0; i < BOARD_HEIGHT; i++) {
        linesToClear[i] = 0;
    }
    
    for(int row = 0; row < BOARD_HEIGHT; row++) {
        int full = 1;
        for(int col = 0; col < BOARD_WIDTH; col++) {
            if(game->board[row][col] == 0) {
                full = 0;
                break;
            }
        }
        if(full) {
            linesToClear[row] = 1;
            linesCleared++;
        }
    }
    
    // Second pass: collapse the board
    if(linesCleared > 0) {
        int writeRow = BOARD_HEIGHT - 1;
        for(int readRow = BOARD_HEIGHT - 1; readRow >= 0; readRow--) {
            if(!linesToClear[readRow]) {
                if(writeRow != readRow) {
                    for(int col = 0; col < BOARD_WIDTH; col++) {
                        game->board[writeRow][col] = game->board[readRow][col];
                    }
                }
                writeRow--;
            }
        }
        // Clear the top rows
        for(int row = 0; row <= writeRow; row++) {
            for(int col = 0; col < BOARD_WIDTH; col++) {
                game->board[row][col] = 0;
            }
        }
    }
    
    return linesCleared;
}

void TetrisSpawnPiece(TetrisGame* game) {
    game->currentPiece = game->nextPiece;
    game->nextPiece = Random(7);
    game->currentRotation = 0;
    game->currentX = BOARD_WIDTH / 2 - 2;
    game->currentY = -1;
    
    if(TetrisCheckCollision(game, game->currentPiece, game->currentRotation, 
                            game->currentX, game->currentY)) {
        game->gameOver = 1;
    }
}

void TetrisInit(TetrisGame* game) {
    for(int row = 0; row < BOARD_HEIGHT; row++) {
        for(int col = 0; col < BOARD_WIDTH; col++) {
            game->board[row][col] = 0;
        }
    }
    game->score = 0;
    game->lines = 0;
    game->level = 1;
    game->gameOver = 0;
    game->paused = 0;
    game->dropCounter = 0;
    game->dropSpeed = 30;
    game->clearing = 0;
    game->nextPiece = Random(7);
    TetrisSpawnPiece(game);
}

void TetrisMovePiece(TetrisGame* game, int dx, int dy) {
    if(!TetrisCheckCollision(game, game->currentPiece, game->currentRotation,
                             game->currentX + dx, game->currentY + dy)) {
        game->currentX += dx;
        game->currentY += dy;
    }
}

void TetrisRotatePiece(TetrisGame* game) {
    int newRotation = (game->currentRotation + 1) % 4;
    if(!TetrisCheckCollision(game, game->currentPiece, newRotation,
                             game->currentX, game->currentY)) {
        game->currentRotation = newRotation;
    }
}

void TetrisDropPiece(TetrisGame* game) {
    while(!TetrisCheckCollision(game, game->currentPiece, game->currentRotation,
                                 game->currentX, game->currentY + 1)) {
        game->currentY++;
        game->score += 2;
    }
}

void TetrisUpdate(TetrisGame* game) {
    if(game->gameOver || game->paused) return;
    
    game->dropCounter++;
    if(game->dropCounter >= game->dropSpeed) {
        game->dropCounter = 0;
        
        if(!TetrisCheckCollision(game, game->currentPiece, game->currentRotation,
                                 game->currentX, game->currentY + 1)) {
            game->currentY++;
        } else {
            game->clearing = 1;
            TetrisLockPiece(game);
            
            int cleared = TetrisClearLines(game);
            
            if(cleared > 0) {
                game->lines += cleared;
                int lineScore = cleared * 100 * game->level;
                if(cleared == 2) lineScore = 300 * game->level;
                if(cleared == 3) lineScore = 500 * game->level;
                if(cleared == 4) lineScore = 800 * game->level;
                game->score += lineScore;
                
                game->level = (game->lines / 10) + 1;
                game->dropSpeed = 30 - (game->level * 2);
                if(game->dropSpeed < 5) game->dropSpeed = 5;
            }
            
            game->clearing = 0;
            TetrisSpawnPiece(game);
        }
    }
}

void DrawTetrisBlock(int x, int y, uint32_t color) {
    DrawRect(x, y, BLOCK_SIZE - 1, BLOCK_SIZE - 1, color);
}

void DrawTetrisBoard(void* win_ptr, TetrisGame* game) {
    // Cast to Window pointer (defined in kernel)
    typedef struct {
        int x, y, width, height;
        char title[64];
        uint32_t titleBarColor, backgroundColor;
        int visible;
        int dragging;
        int dragOffsetX, dragOffsetY;
        int lastDrawX, lastDrawY;
        int windowType;
        int isFocused;
        // ... other fields we don't need to access
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    
    if(!win->visible || !game || game->clearing) return;
    
    int boardX = win->x + 20;
    int boardY = win->y + 50;
    
    // Draw background
    DrawRect(win->x + 2, win->y + 30, win->width - 4, win->height - 32, 0x000000);
    
    // Draw board border
    DrawRect(boardX - 2, boardY - 2, BOARD_WIDTH * BLOCK_SIZE + 4, 
             BOARD_HEIGHT * BLOCK_SIZE + 4, 0xFFFFFF);
    DrawRect(boardX, boardY, BOARD_WIDTH * BLOCK_SIZE, BOARD_HEIGHT * BLOCK_SIZE, 0x000000);
    
    // Draw locked pieces
    for(int row = 0; row < BOARD_HEIGHT; row++) {
        for(int col = 0; col < BOARD_WIDTH; col++) {
            int cellValue = game->board[row][col];
            if(cellValue > 0 && cellValue <= 7) {
                DrawTetrisBlock(boardX + col * BLOCK_SIZE, boardY + row * BLOCK_SIZE,
                              tetrominoColors[cellValue - 1]);
            }
        }
    }
    
    // Draw current falling piece
    if(!game->gameOver && !game->paused && game->currentPiece >= 0 && game->currentPiece < 7) {
        for(int row = 0; row < 4; row++) {
            for(int col = 0; col < 4; col++) {
                if(tetrominoes[game->currentPiece][game->currentRotation][row][col]) {
                    int drawY = game->currentY + row;
                    if(drawY >= 0 && drawY < BOARD_HEIGHT) {
                        int drawX = game->currentX + col;
                        if(drawX >= 0 && drawX < BOARD_WIDTH) {
                            DrawTetrisBlock(boardX + drawX * BLOCK_SIZE,
                                          boardY + drawY * BLOCK_SIZE,
                                          tetrominoColors[game->currentPiece]);
                        }
                    }
                }
            }
        }
    }
    
    // Info panel
    int infoX = boardX + BOARD_WIDTH * BLOCK_SIZE + 30;
    int infoY = boardY;
    
    DrawText(infoX, infoY, "NEXT:", 0xFFFFFF);
    infoY += 20;
    
    // Draw next piece
    if(game->nextPiece >= 0 && game->nextPiece < 7) {
        for(int row = 0; row < 4; row++) {
            for(int col = 0; col < 4; col++) {
                if(tetrominoes[game->nextPiece][0][row][col]) {
                    DrawTetrisBlock(infoX + col * BLOCK_SIZE, infoY + row * BLOCK_SIZE,
                                  tetrominoColors[game->nextPiece]);
                }
            }
        }
    }
    
    infoY += 100;
    DrawText(infoX, infoY, "SCORE:", 0xFFFFFF);
    infoY += 15;
    char scoreStr[16];
    IntToStr(game->score, scoreStr);
    DrawText(infoX, infoY, scoreStr, 0xFFFFFF);
    
    infoY += 30;
    DrawText(infoX, infoY, "LINES:", 0xFFFFFF);
    infoY += 15;
    char linesStr[16];
    IntToStr(game->lines, linesStr);
    DrawText(infoX, infoY, linesStr, 0xFFFFFF);
    
    infoY += 30;
    DrawText(infoX, infoY, "LEVEL:", 0xFFFFFF);
    infoY += 15;
    char levelStr[16];
    IntToStr(game->level, levelStr);
    DrawText(infoX, infoY, levelStr, 0xFFFFFF);
    
    infoY += 50;
    DrawText(infoX, infoY, "CONTROLS:", 0xFFFFFF);
    infoY += 15;
    DrawText(infoX, infoY, "A - Left", 0xCCCCCC);
    infoY += 12;
    DrawText(infoX, infoY, "D - Right", 0xCCCCCC);
    infoY += 12;
    DrawText(infoX, infoY, "S - Down", 0xCCCCCC);
    infoY += 12;
    DrawText(infoX, infoY, "W - Rotate", 0xCCCCCC);
    infoY += 12;
    DrawText(infoX, infoY, "Space-Drop", 0xCCCCCC);
    infoY += 12;
    DrawText(infoX, infoY, "P - Pause", 0xCCCCCC);
    
    if(game->gameOver) {
        int msgX = boardX + (BOARD_WIDTH * BLOCK_SIZE - 80) / 2;
        int msgY = boardY + (BOARD_HEIGHT * BLOCK_SIZE - 40) / 2;
        DrawRect(msgX - 10, msgY - 10, 100, 60, 0xCC0000);
        DrawText(msgX, msgY, "GAME OVER!", 0xFFFFFF);
        DrawText(msgX, msgY + 20, "Press R", 0xFFFFFF);
        DrawText(msgX, msgY + 32, "to restart", 0xFFFFFF);
    }
    
    if(game->paused) {
        int msgX = boardX + (BOARD_WIDTH * BLOCK_SIZE - 56) / 2;
        int msgY = boardY + (BOARD_HEIGHT * BLOCK_SIZE - 20) / 2;
        DrawRect(msgX - 10, msgY - 10, 76, 40, 0x0078D7);
        DrawText(msgX, msgY, "PAUSED", 0xFFFFFF);
        DrawText(msgX, msgY + 15, "Press P", 0xFFFFFF);
    }
}

void HandleTetrisKeyPress(void* win_ptr, TetrisGame* game, unsigned char key) {
    typedef struct {
        int x, y, width, height;
        char title[64];
        uint32_t titleBarColor, backgroundColor;
        int visible;
    } WindowHeader;
    
    WindowHeader* win = (WindowHeader*)win_ptr;
    if(!win->visible) return;
    
    if(key == 'r' && game->gameOver) {
        TetrisInit(game);
        DrawTetrisBoard(win_ptr, game);
        return;
    }
    
    if(game->gameOver) return;
    
    if(key == 'p') {
        game->paused = !game->paused;
        DrawTetrisBoard(win_ptr, game);
        return;
    }
    
    if(game->paused) return;
    
    if(key == 'a') {
        TetrisMovePiece(game, -1, 0);
        DrawTetrisBoard(win_ptr, game);
    } else if(key == 'd') {
        TetrisMovePiece(game, 1, 0);
        DrawTetrisBoard(win_ptr, game);
    } else if(key == 's') {
        TetrisMovePiece(game, 0, 1);
        game->score += 1;
        DrawTetrisBoard(win_ptr, game);
    } else if(key == 'w') {
        TetrisRotatePiece(game);
        DrawTetrisBoard(win_ptr, game);
    } else if(key == ' ') {
        TetrisDropPiece(game);
        DrawTetrisBoard(win_ptr, game);
    }
}

#endif // TETRIS_APP_C
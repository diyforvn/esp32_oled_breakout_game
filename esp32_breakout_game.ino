#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>      // Thư viện đồ họa Adafruit
#include <Adafruit_SH110X.h>   // Thư viện driver SH110X

// --- Cấu hình chân (Giống như trong mã Rust) ---
const int VRX_PIN = 27;   // ADC cho joystick X
const int VRY_PIN = 25;   // ADC cho joystick Y
const int BTN_PIN = 35;   // Nút bấm reset

// --- Cấu hình Adafruit SH110X ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // Địa chỉ I2C, có thể là 0x3D
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Player 
const int PLAYER_WIDTH = 40;
const int PLAYER_HEIGHT = 5;
const int PLAYER_VELOCITY = 5;
const uint8_t PLAYER_START_LIVES = 3;

// Block 
const int BLOCK_WIDTH = 20;
const int BLOCK_HEIGHT = 3;
const int MAX_BLOCKS = 50; 

// Ball 
const int BALL_WIDTH = 4;
const int BALL_HEIGHT = 4;
const int BALL_SPEED = 2;
const int MAX_BALLS = 5; 
// --- Trạng thái trò chơi  ---
enum PlayerDirection { IDLE, LEFT, RIGHT };
enum GameState { MENU, PLAYING, LEVEL_COMPLETED, DEAD };

volatile PlayerDirection g_playerDirection = IDLE;
volatile bool g_resetGame = false;
GameState g_gameState = MENU;
unsigned int g_score = 0;

// --- Định nghĩa lớp (Class) ---

// Cấu trúc Rect để xử lý va chạm
struct Rect {
    float x, y;
    int w, h;
};

class Ball {
public:
    float x, y;
    int velX, velY;
    bool isActive;
    
    Ball() : isActive(false) {} // Constructor mặc định

    void init(float startX, float startY) {
        x = startX;
        y = startY;
        velX = (random(0, 2) == 0) ? -1 : 1;
        velY = 1; // Luôn bắt đầu đi xuống
        isActive = true;
    }

    void update() {
        if (!isActive) return;

        x += velX * BALL_SPEED;
        y += velY * BALL_SPEED;

        // Va chạm tường
        if (x < 0) {
            x = 0; velX = 1;
        }
        if (x + BALL_WIDTH > SCREEN_WIDTH) {
            x = SCREEN_WIDTH - BALL_WIDTH; velX = -1;
        }
        if (y < 0) {
            y = 0; velY = 1;
        }
    }

    void draw(Adafruit_SH110X &disp) {
        if (isActive) {
            disp.fillRect(x, y, BALL_WIDTH, BALL_HEIGHT, SH110X_WHITE);
        }
    }
};

class Player {
public:
    int x, y;
    uint8_t lives;

    Player() {} // Constructor mặc định

    void init(int startX, int startY) {
        x = startX;
        y = startY;
        lives = PLAYER_START_LIVES;
    }

    void draw(Adafruit_SH110X &disp) {
        disp.fillRect(x, y, PLAYER_WIDTH, PLAYER_HEIGHT, SH110X_WHITE);
    }

    void update(PlayerDirection dir) {
        if (dir == LEFT) {
            x -= PLAYER_VELOCITY;
            if (x < 0) x = 0;
        } else if (dir == RIGHT) {
            x += PLAYER_VELOCITY;
            if (x + PLAYER_WIDTH > SCREEN_WIDTH) {
                x = SCREEN_WIDTH - PLAYER_WIDTH;
            }
        }
    }
    
    Rect getRect() {
        return { (float)x, (float)y, PLAYER_WIDTH, PLAYER_HEIGHT };
    }
};

class Block {
public:
    int x, y;
    int lives;

    Block() {} // Constructor mặc định

    void init(int startX, int startY) {
        x = startX;
        y = startY;
        lives = 2; 
    }

    void draw(Adafruit_SH110X &disp) {
        if (lives > 0) {
            disp.fillRect(x, y, BLOCK_WIDTH, BLOCK_HEIGHT, SH110X_WHITE);
        }
    }
    
    Rect getRect() {
        return { (float)x, (float)y, BLOCK_WIDTH, BLOCK_HEIGHT };
    }
};

// --- Các đối tượng trò chơi toàn cục ---
Player g_player;
Block g_blocks[MAX_BLOCKS];
Ball g_balls[MAX_BALLS];

// 8x8 pixel, định dạng XBM (hoạt động với drawBitmap của Adafruit)
const unsigned char heart_sprite_8x8[] PROGMEM = {
    0x00, 0x6e, 0xff, 0xef, 0x7e, 0x3c, 0x18, 0x00
};

// --- Khởi tạo và Reset Trò chơi ---

void init_blocks() {
    int cols = 6;
    int rows = 5;
    int padding = 1;
    int totalBlockWidth = BLOCK_WIDTH + padding;
    int totalBlockHeight = BLOCK_HEIGHT + padding;
    
    // Chiều cao của văn bản điểm số (Font 5x8 mặc định của GFX)
    int textHeight = 8; 
    int boardStartY = textHeight + 2; // Bắt đầu bên dưới điểm
    int boardStartX = 1;
    
    for (int i = 0; i < cols * rows; i++) {
        int blockX = (i % cols) * totalBlockWidth;
        int blockY = (i / cols) * totalBlockHeight;
        g_blocks[i].init(boardStartX + blockX, boardStartY + blockY);
    }
    for (int i = cols * rows; i < MAX_BLOCKS; i++) {
        g_blocks[i].lives = 0;
    }
}

void init_balls() {
    for (int i = 0; i < MAX_BALLS; i++) {
        g_balls[i].isActive = false;
    }
    g_balls[0].init(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}

void spawn_player() {
    int playerX = (SCREEN_WIDTH / 2) - (PLAYER_WIDTH / 2);
    int playerY = SCREEN_HEIGHT - PLAYER_HEIGHT - 1; // Gần đáy
    g_player.init(playerX, playerY);
}

void reset_game() {
    g_score = 0;
    spawn_player();
    init_blocks();
    init_balls();
}

// --- Logic xử lý va chạm ---

bool resolveCollision(Ball &ball, const Rect &obstacle) {
   
    float ballLeft = ball.x;
    float ballRight = ball.x + BALL_WIDTH;
    float ballTop = ball.y;
    float ballBottom = ball.y + BALL_HEIGHT;
    
    float obsLeft = obstacle.x;
    float obsRight = obstacle.x + obstacle.w;
    float obsTop = obstacle.y;
    float obsBottom = obstacle.y + obstacle.h;

    if (ballRight < obsLeft || ballLeft > obsRight || ballBottom < obsTop || ballTop > obsBottom) {
        return false; // Không va chạm
    }

    // Đã va chạm.
    float overlapX = min(ballRight, obsRight) - max(ballLeft, obsLeft);
    float overlapY = min(ballBottom, obsBottom) - max(ballTop, obsTop);

    float ballCenterX = ball.x + BALL_WIDTH / 2.0;
    float ballCenterY = ball.y + BALL_HEIGHT / 2.0;
    float obsCenterX = obstacle.x + obstacle.w / 2.0;
    float obsCenterY = obstacle.y + obstacle.h / 2.0;

    float toX = obsCenterX - ballCenterX;
    float toY = obsCenterY - ballCenterY;

    int signX = (toX > 0) ? 1 : -1;
    int signY = (toY > 0) ? 1 : -1;

    if (overlapX > overlapY) { // Va chạm dọc
        ball.y -= signY * overlapY;
        if (signY > 0) ball.velY = -abs(ball.velY);
        else ball.velY = abs(ball.velY);
    } else { // Va chạm ngang
        ball.x -= signX * overlapX;
        if (signX > 0) ball.velX = -abs(ball.velX);
        else ball.velX = abs(ball.velX);
    }
    
    return true;
}

void handleCollisions() {
    Rect playerRect = g_player.getRect();
    
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!g_balls[i].isActive) continue;
        
        resolveCollision(g_balls[i], playerRect);

        for (int j = 0; j < MAX_BLOCKS; j++) {
            if (g_blocks[j].lives <= 0) continue;
            
            Rect blockRect = g_blocks[j].getRect();
            if (resolveCollision(g_balls[i], blockRect)) {
                g_blocks[j].lives--;
                if (g_blocks[j].lives <= 0) {
                    g_score += 10;
                }
            }
        }
    }
}

// --- Logic cập nhật trò chơi ---

void moveBalls() {
    for (int i = 0; i < MAX_BALLS; i++) {
        g_balls[i].update();
    }
}

void removeBalls() {
    bool ballWasRemoved = false;
    int activeBalls = 0;

    for (int i = 0; i < MAX_BALLS; i++) {
        if (g_balls[i].isActive) {
            if (g_balls[i].y > SCREEN_HEIGHT) {
                g_balls[i].isActive = false;
                ballWasRemoved = true;
            } else {
                activeBalls++;
            }
        }
    }

    if (ballWasRemoved && activeBalls == 0) {
        g_player.lives--;
        if (g_player.lives == 0) {
            g_gameState = DEAD;
            g_resetGame = false;
        } else {
            for (int i = 0; i < MAX_BALLS; i++) {
                if (!g_balls[i].isActive) {
                    g_balls[i].init(g_player.x + (PLAYER_WIDTH / 2) - (BALL_WIDTH / 2), g_player.y - BALL_HEIGHT - 2);
                    break;
                }
            }
        }
    }
}

bool checkLevelCompleted() {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (g_blocks[i].lives > 0) {
            return false;
        }
    }
    return true;
}

// --- Các hàm đọc Input ---

void readJoystick() {
    int adcValue = analogRead(VRY_PIN);
    if (adcValue > 3000) {
        g_playerDirection = LEFT;
    } else if (adcValue < 1500) {
        g_playerDirection = RIGHT;
    } else {
        g_playerDirection = IDLE;
    }
}

void readResetButton() {
    if (digitalRead(BTN_PIN) == LOW) {
        g_resetGame = true;
    }
}

// --- Hàm Update chính (Trạng thái máy) ---

void updateGame() {
    switch (g_gameState) {
        case MENU:
            if (g_resetGame) {
                g_resetGame = false;
                reset_game();
                g_gameState = PLAYING;
            }
            break;
            
        case PLAYING:
            g_player.update(g_playerDirection);
            moveBalls();
            handleCollisions();
            removeBalls();
            
            if (checkLevelCompleted()) {
                g_gameState = LEVEL_COMPLETED;
                g_resetGame = false;
            }
            break;
            
        case LEVEL_COMPLETED:
        case DEAD:
            if (g_resetGame) {
                g_resetGame = false;
                g_gameState = MENU;
            }
            break;
    }
}


void drawTitleText(const char* title) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    int textWidth = strlen(title) * 6; // 5px chiều rộng + 1px khoảng cách
    int textHeight = 8;
    
    int x = (SCREEN_WIDTH - textWidth) / 2;
    int y = (SCREEN_HEIGHT - textHeight) / 2;
    
    display.setCursor(x, y);
    display.print(title);
}

void printScore() {
    char scoreText[16];
    sprintf(scoreText, "Score: %u", g_score);
    
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    // Vẽ ở góc trên bên trái
    display.setCursor(0, 0); 
    display.print(scoreText);
}

void printLives() {
    int imgWidth = 8;
    int x = (SCREEN_WIDTH - (imgWidth * g_player.lives)) - imgWidth; // Căn phải
    
    for (int i = 0; i < g_player.lives; i++) {
        display.drawBitmap(x + (i * imgWidth), 0, heart_sprite_8x8, imgWidth, 8, SH110X_WHITE);
    }
}

void drawBlocks() {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        g_blocks[i].draw(display);
    }
}

void drawBalls() {
    for (int i = 0; i < MAX_BALLS; i++) {
        g_balls[i].draw(display);
    }
}

void drawPlaying() {
    g_player.draw(display);
    drawBlocks();
    drawBalls();
    printScore();
    printLives();
}

void drawScreen() {
    display.clearDisplay(); 
    
    char buffer[64];
    switch (g_gameState) {
        case MENU:
            drawTitleText("Press to start...");
            break;
        case PLAYING:
            drawPlaying();
            break;
        case LEVEL_COMPLETED:
            sprintf(buffer, "You win! Score: %u", g_score);
            drawTitleText(buffer);
            break;
        case DEAD:
            sprintf(buffer, "You died! Score: %u", g_score);
            drawTitleText(buffer);
            break;
    }
    
    display.display(); 
}

// --- Setup và Loop (Arduino) ---

void setup() {
    Serial.begin(115200);
    
    // Khởi tạo I2C cho màn hình
    //Wire.begin(SDA_PIN, SCL_PIN);
    
    // Khởi tạo màn hình Adafruit SH110X
    if (!display.begin(SCREEN_ADDRESS, true)) { // true = reset
        Serial.println(F("SH110X allocation failed"));
        for (;;); // Loop forever
    }
    
    display.clearDisplay();
    display.display(); // Hiển thị màn hình trống
    
    // Cấu hình chân input
    pinMode(BTN_PIN, INPUT_PULLUP);
    analogReadResolution(12);
    
    // Khởi tạo bộ sinh số ngẫu nhiên
    randomSeed(millis()); 

    Serial.println("Game setup complete. Starting...");
}

// Biến cho vòng lặp game
unsigned long lastLoopTime = 0;
const int LOOP_DELAY = 10; // 10ms

void loop() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastLoopTime < LOOP_DELAY) {
        return; 
    }
    lastLoopTime = currentTime;

    // 1. Đọc Inputs
    readJoystick();
    readResetButton();

    // 2. Cập nhật logic trò chơi
    updateGame();

    // 3. Vẽ lên màn hình
    drawScreen();
}
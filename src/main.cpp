#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>
#include <qrcode.h>

#define SDA_PIN     5
#define SCL_PIN     6
#define BUTTON_PIN  9
#define LED_PIN     8

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;

enum AppMode {
    MENU = 0,
    INSTRUCTIONS,
    STOPWATCH,
    TIMER,
    POMODORO,
    DICE,
    MAGIC_8BALL,
    COIN_FLIPPER,
    BREATHING,
    TALLY_COUNTER,
    FLASHLIGHT,
    SNAKE,
    FLAPPY,
    PONG,
    SPACE_DEFENDER,
    DINO_RUNNER,
    ABOUT
};

AppMode currentApp = MENU;
uint8_t menuIndex = 0;

const uint8_t TOTAL_MENU_ITEMS = 16;
const char* menuTitles[] = {
    "Instructions: Tap=Next Hold=Enter", "Stopwatch", "Timer", "Pomodoro", "Dice", "Magic 8-Ball",
    "Coin Flipper", "Breathing", "Tally Counter", "Flashlight", "Snake",
    "Flappy Bird", "Pong", "Space Defender", "Dino Runner", "About"
};

const AppMode menuToApp[] = {
    INSTRUCTIONS, STOPWATCH, TIMER, POMODORO, DICE, MAGIC_8BALL, COIN_FLIPPER,
    BREATHING, TALLY_COUNTER, FLASHLIGHT, SNAKE, FLAPPY, PONG,
    SPACE_DEFENDER, DINO_RUNNER, ABOUT
};

// System Timing Parameters (700ms Hold for Global Enter/Exit)
const unsigned long HOLD_TIME = 700;
const unsigned long TIMER_START_HOLD = 3000;
// Dino Runner needs its own thresholds: duck is a hold gesture, so it needs
// more room than the normal exit-hold before it's read as "leave the app".
const unsigned long DINO_JUMP_MAX_PRESS = 250;   // taps shorter than this = jump
const unsigned long DINO_DUCK_EXIT_HOLD = 2000;  // hold longer than this while ducking = exit to menu
bool lastButtonState = HIGH, currentButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 30;
unsigned long pressStart = 0;
bool buttonPressed = false;
bool timerStartHoldConsumed = false;
unsigned long lastActivity = 0;
bool autoDim = false;
bool screensaverWakeConsumed = false;
bool displayDirty = true;
unsigned long lastDisplayUpdate = 0;
const unsigned long MIN_DISPLAY_UPDATE = 33;

// Double tap tracking
unsigned long lastTapTime = 0;
bool isDoubleTap = false;

// Deferred single-tap resolution (only used by apps that need real double-tap)
const unsigned long TAP_WINDOW = 350;
bool tapPending = false;
unsigned long tapPendingSince = 0;
AppMode tapPendingApp = MENU;

// Stopwatch
bool swRunning = false;
unsigned long swStartTime = 0, swElapsed = 0;

// Countdown Timer
enum TimerState { TMR_STOPPED, TMR_RUNNING, TMR_ALARM };
TimerState tmrState = TMR_STOPPED;
uint8_t tmrPresetIdx = 0; // Default 1 min
const uint32_t tmrPresets[] = {60000, 120000, 300000, 600000, 900000, 1800000, 2700000, 3600000};
const uint8_t TOTAL_TMR_PRESETS = 8;
unsigned long tmrRemaining = 60000;
unsigned long tmrLastTick = 0;
unsigned long tmrAlarmBlink = 0;
bool tmrAlarmState = false;

// Pomodoro
enum PomState { POM_IDLE, POM_WORK, POM_BREAK };
PomState pomState = POM_IDLE;
unsigned long pomDuration = 25 * 60000, pomBreak = 5 * 60000;
unsigned long pomRemaining = 0, pomLastTick = 0;
int pomSessions = 0;

// Dice (plain d6 only)
int diceValue = 1;
bool diceRolling = false;
unsigned long diceRollStart = 0;

// Magic 8-Ball
bool magic8Shaking = false;
unsigned long magic8ShakeStart = 0;
int magic8AnswerIdx = -1;
unsigned long magic8ScrollTimer = 0;
const char* magic8Answers[] = {
    "YES!",
    "ABSOLUTELY",
    "MOST LIKELY",
    "OUTLOOK GOOD",
    "SIGNS SAY YES",
    "ASK AGAIN",
    "MAYBE...",
    "REPLY HAZY",
    "CANNOT TELL",
    "TRY AGAIN",
    "NO WAY",
    "DONT COUNT IT",
    "SOURCES SAY NO",
    "VERY DOUBTFUL"
};
const uint8_t TOTAL_MAGIC_ANSWERS = 14;

// Coin Flipper
bool coinFlipping = false;
unsigned long coinFlipStart = 0;
bool coinIsHeads = true;

// Breathing Coach — single method (Box Breathing), smoothed animation
struct BreathMethod { const char* name; uint8_t inhale; uint8_t hold1; uint8_t exhale; uint8_t hold2; };
const BreathMethod boxBreath = {"BOX", 4, 4, 4, 4};
unsigned long breathTimer = 0;
bool breathActive = false;

float smoothstep(float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3.0f - 2.0f * t);
}

// Tally Counter
int tallyCount = 0;

// Flashlight & SOS
enum FlashlightMode { FLASHLIGHT_SOLID, FLASHLIGHT_STROBE, FLASHLIGHT_SOS };
FlashlightMode torchMode = FLASHLIGHT_SOLID;
unsigned long torchTimer = 0;
bool torchState = true;
int sosIndex = 0;
const int sosPattern[] = {200,200, 200,200, 200,600, 600,200, 600,200, 600,600, 200,200, 200,200, 200,1000};
const int TOTAL_SOS_STEPS = 18;

// Snake — grid height trimmed to 8 rows so the score strip has clear space.
// for the score HUD and never overlaps gameplay (fixes food spawning "in" the score).
enum SnakeDir { SNK_UP, SNK_RIGHT, SNK_DOWN, SNK_LEFT };
#define SNAKE_GRID_W 18
#define SNAKE_GRID_H 8
#define SNAKE_MAX_LEN 60
uint8_t snakeX[SNAKE_MAX_LEN], snakeY[SNAKE_MAX_LEN];
int snakeLen = 3;
SnakeDir snakeDir = SNK_RIGHT;
uint8_t foodX, foodY;
bool snakeGameOver = false;
int snakeScore = 0, snakeHighScore = 0;
unsigned long lastSnakeMove = 0;
const unsigned long snakeIntervals[] = {250, 180, 120};
uint8_t snakeDifficulty = 0;

// Flappy
float birdY = 20, birdVelocity = 0;
int pipeX = 72, gapY = 15;
int gapSize = 30;
const int flappySpeed = 1;
int flappyScore = 0, flappyHighScore = 0;
bool flappyGameOver = false;
unsigned long lastFlappyFrame = 0;

// Pong Game
float paddleX = 27;
const float paddleW = 18;
float paddleVX = 1.0;
float ballX = 36, ballY = 10;
float ballVX = 0.8, ballVY = 0.9;
bool pongGameOver = false;
int pongScore = 0, pongHighScore = 0;
unsigned long lastPongFrame = 0;

// Space Defender
float shipX = 32;
float shipVX = 1.0;
const int shipY = 34;
int bullets[5][2];
struct Alien { float x; float y; bool alive; };
Alien aliens[3];
int stars[5][2];
int explosionX = -1, explosionY = -1, explosionTimer = 0;
bool spaceGameOver = false;
int spaceScore = 0, spaceHighScore = 0;
uint8_t spaceLives = 3;
unsigned long spaceInvulnerableUntil = 0;
unsigned long lastSpaceFrame = 0, lastEnemyMove = 0, lastShot = 0;

// Dino Runner
float dinoY = 32;
float dinoVelocity = 0;
bool dinoJumping = false;
bool dinoDucking = false;
int groundX = 0;

enum ObstacleType { OBS_CACTUS, OBS_BIRD };
ObstacleType obsType = OBS_CACTUS;
int obsX = 72;
int cactusH = 8;
int pteroY = 18;
bool pteroWing = false;
unsigned long lastWingFlap = 0;

bool dinoGameOver = false;
int dinoScore = 0, dinoHighScore = 0;
unsigned long lastDinoFrame = 0;
bool dinoNight = false;

// Screensaver
bool inScreensaver = false;
const unsigned long SCREENSAVER_DELAY = 15000;
float ssX = 10, ssY = 10;
float ssVX = 1.2, ssVY = 0.9;
unsigned long lastSsFrame = 0;

// Menu scroll
unsigned long menuScrollTimer = 0;
uint8_t lastMenuIndexForScroll = 255;
unsigned long timerHelpScrollStart = 0;
unsigned long stopwatchHelpScrollStart = 0;
unsigned long pomodoroHelpScrollStart = 0;
unsigned long tallyHelpScrollStart = 0;
unsigned long aboutScrollStart = 0;

// LED
unsigned long ledSecondTick = 0;
bool ledSecondState = false;
unsigned long ledGameOverBlink = 0;
bool ledGameOverState = false;

// Forward Declarations
void drawMenu();
void drawInstructions();
void drawStopwatch();
void drawScrollingFooter(const char* text, unsigned long started);
void drawTimer();
void drawPomodoro();
void drawDice();
void drawMagic8Ball();
void drawCoinFlipper();
void drawBreathing();
void drawTallyCounter();
void drawFlashlight();
void drawSnake();
void drawFlappy();
void drawPong();
void drawSpaceDefender();
void drawDinoRunner();
void drawAbout();
void drawScreensaver();
void updateScreensaver();
void resetSnake();
void resetFlappy();
void resetPong();
void resetSpaceDefender();
void resetDinoRunner();
void updateSnake();
void updateFlappy();
void updatePong();
void updateSpaceDefender();
void updateDinoRunner();
void updateLED();
void initApp(AppMode app);
float getInternalTemp();
void handleTap(bool isDouble);
bool appUsesDoubleTap(AppMode app);

float getInternalTemp() {
    float t = temperatureRead();
    if (isnan(t) || t < -40.0f || t > 125.0f) {
        return 0.0f;
    }
    return t;
}

void updateLED() {
    if (currentApp == FLASHLIGHT && torchMode == FLASHLIGHT_SOS) {
        digitalWrite(LED_PIN, torchState ? LOW : HIGH);
    } else if ((currentApp == STOPWATCH && swRunning) || (currentApp == TIMER && tmrState == TMR_RUNNING)) {
        if (millis() - ledSecondTick >= 1000) {
            ledSecondTick = millis();
            ledSecondState = !ledSecondState;
            digitalWrite(LED_PIN, ledSecondState ? LOW : HIGH);
        }
    } else if (currentApp == TIMER && tmrState == TMR_ALARM) {
        if (millis() - ledGameOverBlink >= 150) {
            ledGameOverBlink = millis();
            ledGameOverState = !ledGameOverState;
            digitalWrite(LED_PIN, ledGameOverState ? LOW : HIGH);
        }
    } else if ((currentApp == SNAKE && snakeGameOver) ||
               (currentApp == FLAPPY && flappyGameOver) ||
               (currentApp == PONG && pongGameOver) ||
               (currentApp == SPACE_DEFENDER && spaceGameOver) ||
               (currentApp == DINO_RUNNER && dinoGameOver)) {
        if (millis() - ledGameOverBlink >= 200) {
            ledGameOverBlink = millis();
            ledGameOverState = !ledGameOverState;
            digitalWrite(LED_PIN, ledGameOverState ? LOW : HIGH);
        }
    } else {
        digitalWrite(LED_PIN, HIGH);
    }
}

// Game Resets
void resetSnake() {
    snakeLen = 3;
    snakeX[0] = 9; snakeY[0] = 5;
    snakeX[1] = 8; snakeY[1] = 5;
    snakeX[2] = 7; snakeY[2] = 5;
    snakeDir = SNK_RIGHT;
    snakeGameOver = false; snakeScore = 0;
    foodX = random(SNAKE_GRID_W); foodY = random(SNAKE_GRID_H);
    lastSnakeMove = millis();
}

void rotateSnake() {
    switch (snakeDir) {
        case SNK_UP: snakeDir = SNK_RIGHT; break;
        case SNK_RIGHT: snakeDir = SNK_DOWN; break;
        case SNK_DOWN: snakeDir = SNK_LEFT; break;
        case SNK_LEFT: snakeDir = SNK_UP; break;
    }
}

void updateSnake() {
    if (snakeGameOver || millis() - lastSnakeMove < snakeIntervals[snakeDifficulty]) return;
    lastSnakeMove = millis();
    int nx = snakeX[0], ny = snakeY[0];
    switch (snakeDir) {
        case SNK_UP: ny--; break;
        case SNK_DOWN: ny++; break;
        case SNK_LEFT: nx--; break;
        case SNK_RIGHT: nx++; break;
    }
    if (nx < 0 || nx >= SNAKE_GRID_W || ny < 0 || ny >= SNAKE_GRID_H) {
        snakeGameOver = true;
        if (snakeScore > snakeHighScore) { snakeHighScore = snakeScore; prefs.putInt("snk_hi", snakeHighScore); }
        return;
    }
    for (int i = 0; i < snakeLen; i++) {
        if (snakeX[i] == nx && snakeY[i] == ny) {
            snakeGameOver = true;
            if (snakeScore > snakeHighScore) { snakeHighScore = snakeScore; prefs.putInt("snk_hi", snakeHighScore); }
            return;
        }
    }
    bool ate = (nx == foodX && ny == foodY);
    int newLen = ate && snakeLen < SNAKE_MAX_LEN ? snakeLen + 1 : snakeLen;
    for (int i = newLen - 1; i > 0; i--) { snakeX[i] = snakeX[i - 1]; snakeY[i] = snakeY[i - 1]; }
    snakeX[0] = nx; snakeY[0] = ny;
    snakeLen = newLen;
    if (ate) {
        snakeScore++;
        foodX = random(SNAKE_GRID_W); foodY = random(SNAKE_GRID_H);
        for (int i = 0; i < snakeLen; i++) {
            if (snakeX[i] == foodX && snakeY[i] == foodY) {
                foodX = random(SNAKE_GRID_W); foodY = random(SNAKE_GRID_H); i = -1;
            }
        }
    }
}

void resetFlappy() {
    birdY = 20; birdVelocity = 0; pipeX = 72; gapY = random(8, 11);
    flappyScore = 0; flappyGameOver = false;
}

void updateFlappy() {
    if (flappyGameOver || millis() - lastFlappyFrame < 40) return;
    lastFlappyFrame = millis();
    birdVelocity += 0.28; birdY += birdVelocity;
    if (birdY < 0) birdY = 0;
    // The bottom is a safe floor, not an instant failure.
    if (birdY > 36) { birdY = 36; birdVelocity = 0; }
    pipeX -= flappySpeed;
    if (pipeX < -8) {
        pipeX = 72; gapY = random(8, 11); flappyScore++;
        if (flappyScore > flappyHighScore) { flappyHighScore = flappyScore; prefs.putInt("flp_hi", flappyHighScore); }
    }
    if (pipeX >= 8 && pipeX <= 16) {
        int bt = (int)birdY - 2, bb = (int)birdY + 2;
        if (bt < gapY || bb > gapY + gapSize) {
            flappyGameOver = true;
            if (flappyScore > flappyHighScore) { flappyHighScore = flappyScore; prefs.putInt("flp_hi", flappyHighScore); }
        }
    }
}

void resetPong() {
    paddleX = 27; paddleVX = 1.0;
    ballX = 36; ballY = 10;
    ballVX = (random(0, 2) == 0 ? 0.8 : -0.8);
    ballVY = 0.9;
    pongScore = 0; pongGameOver = false;
}

void updatePong() {
    if (pongGameOver || millis() - lastPongFrame < 33) return;
    lastPongFrame = millis();

    paddleX += paddleVX;
    if (paddleX <= 0) { paddleX = 0; paddleVX = -paddleVX; }
    if (paddleX >= 72 - paddleW) { paddleX = 72 - paddleW; paddleVX = -paddleVX; }

    ballX += ballVX;
    ballY += ballVY;

    if (ballX <= 0) { ballX = 0; ballVX = -ballVX; }
    if (ballX >= 70) { ballX = 70; ballVX = -ballVX; }
    if (ballY <= 0) { ballY = 0; ballVY = -ballVY; }

    if (ballY >= 33 && ballY <= 36 && ballX >= paddleX - 2 && ballX <= paddleX + paddleW) {
        ballY = 33;
        ballVY = -abs(ballVY);
        pongScore++;
        if (abs(ballVY) < 2.0) ballVY *= 1.03;
    }

    if (ballY > 38) {
        pongGameOver = true;
        if (pongScore > pongHighScore) {
            pongHighScore = pongScore;
            prefs.putInt("png_hi", pongHighScore);
        }
    }
}

void resetSpaceDefender() {
    shipX = 32; shipVX = 1.0;
    spaceGameOver = false; spaceScore = 0;
    explosionTimer = 0;
    for (int i = 0; i < 5; i++) { bullets[i][0] = -1; bullets[i][1] = -1; }
    for (int i = 0; i < 3; i++) {
        aliens[i].x = random(5, 60);
        aliens[i].y = 2 + i * 8;
        aliens[i].alive = true;
    }
    for (int i = 0; i < 5; i++) {
        stars[i][0] = random(0, 72);
        stars[i][1] = random(0, 40);
    }
}

void updateSpaceDefender() {
    if (spaceGameOver || millis() - lastSpaceFrame < 33) return;
    lastSpaceFrame = millis();

    shipX += shipVX;
    if (shipX <= 2) { shipX = 2; shipVX = -shipVX; }
    if (shipX >= 64) { shipX = 64; shipVX = -shipVX; }

    for (int i = 0; i < 5; i++) {
        stars[i][1] = (stars[i][1] + 1) % 40;
    }

    if (explosionTimer > 0) explosionTimer--;

    if (millis() - lastEnemyMove > 400) {
        lastEnemyMove = millis();
        for (int i = 0; i < 3; i++) {
            if (aliens[i].alive) {
                aliens[i].y += 1.5;
                if (aliens[i].y >= 30) spaceGameOver = true;
            }
        }
    }

    for (int i = 0; i < 5; i++) {
        if (bullets[i][1] >= 0) {
            bullets[i][1] -= 3;
            if (bullets[i][1] < 0) bullets[i][1] = -1;
            else {
                for (int a = 0; a < 3; a++) {
                    if (aliens[a].alive &&
                        bullets[i][0] >= aliens[a].x - 1 && bullets[i][0] <= aliens[a].x + 6 &&
                        bullets[i][1] >= aliens[a].y && bullets[i][1] <= aliens[a].y + 5) {
                        aliens[a].alive = false;
                        bullets[i][1] = -1;
                        spaceScore += 20;
                        explosionX = (int)aliens[a].x + 2;
                        explosionY = (int)aliens[a].y + 2;
                        explosionTimer = 5;

                        aliens[a].x = random(4, 62);
                        aliens[a].y = 2;
                        aliens[a].alive = true;
                        break;
                    }
                }
            }
        }
    }

    if (spaceGameOver && spaceScore > spaceHighScore) {
        spaceHighScore = spaceScore; prefs.putInt("spc_hi", spaceHighScore);
    }
}

void resetDinoRunner() {
    dinoY = 32; dinoVelocity = 0; dinoJumping = false; dinoDucking = false;
    groundX = 0; obsX = 72; obsType = OBS_CACTUS; cactusH = 6 + random(4); pteroY = 18;
    dinoGameOver = false; dinoScore = 0;
}

void updateDinoRunner() {
    if (dinoGameOver || millis() - lastDinoFrame < 30) return;
    lastDinoFrame = millis();

    if (millis() - lastWingFlap > 150) {
        lastWingFlap = millis();
        pteroWing = !pteroWing;
    }

    if (dinoJumping) {
        dinoVelocity += 0.7;
        dinoY += dinoVelocity;
        if (dinoY >= 32) { dinoY = 32; dinoVelocity = 0; dinoJumping = false; }
    }

    groundX = (groundX + 2) % 8;
    obsX -= 2;

    if (obsX < -8) {
        obsX = 72;
        dinoScore++;
        if (random(0, 10) < 4) {
            obsType = OBS_BIRD;
            pteroY = 18 + random(0, 4);
        } else {
            obsType = OBS_CACTUS;
            cactusH = 5 + random(4);
        }
    }

    if (obsType == OBS_CACTUS) {
        if (obsX >= 8 && obsX <= 16 && dinoY > 32 - cactusH) {
            dinoGameOver = true;
            if (dinoScore > dinoHighScore) { dinoHighScore = dinoScore; prefs.putInt("dno_hi", dinoHighScore); }
        }
    } else {
        int dinoHeadY = (dinoDucking && !dinoJumping) ? 28 : (int)dinoY - 6;
        if (obsX >= 6 && obsX <= 16) {
            if (dinoHeadY <= pteroY + 4 && (int)dinoY >= pteroY) {
                dinoGameOver = true;
                if (dinoScore > dinoHighScore) { dinoHighScore = dinoScore; prefs.putInt("dno_hi", dinoHighScore); }
            }
        }
    }
}

// Screensaver
void updateScreensaver() {
    if (millis() - lastSsFrame < 40) return;
    lastSsFrame = millis();
    ssX += ssVX; ssY += ssVY;
    if (ssX <= 0 || ssX >= 42) ssVX = -ssVX;
    if (ssY <= 0 || ssY >= 30) ssVY = -ssVY;
    ssX = constrain(ssX, 0, 42); ssY = constrain(ssY, 0, 30);
}

// Drawing routines
void drawMenu() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    if (menuIndex != lastMenuIndexForScroll) {
        menuScrollTimer = millis();
        lastMenuIndexForScroll = menuIndex;
    }
    int top = (menuIndex / 3) * 3;
    for (int i = 0; i < 3; i++) {
        int idx = top + i;
        if (idx >= TOTAL_MENU_ITEMS) break;
        int y = 12 + i * 12;
        const char* t = menuTitles[idx];
        int tw = u8g2.getStrWidth(t);
        if (idx == menuIndex) {
            // Keep a dedicated left gutter for the scroll indicator.
            u8g2.drawBox(8, y - 9, 64, 11);
            u8g2.setDrawColor(0);
            if (tw > 62) {
                unsigned long el = millis() - menuScrollTimer;
                int range = tw - 62, pause = 800, speed = 25;
                unsigned long tm = el % (pause + range*speed + pause);
                int off=(tm<pause)?0:min(range,(int)((tm-pause)/speed));
                u8g2.setClipWindow(9,y-9,72,y+2);
                u8g2.setCursor(9-off,y);u8g2.print(t);
                u8g2.setMaxClipWindow();
            } else {u8g2.setCursor(9,y);u8g2.print(t);}
            u8g2.setDrawColor(1);
        } else {
            String label(t);
            if (tw > 62) {
                while (label.length() && u8g2.getStrWidth((label + "..").c_str()) > 62)
                    label.remove(label.length() - 1);
                label += "..";
            }
            u8g2.setCursor(9,y);
            u8g2.print(label);
        }
    }
    // A scroll rail, page-position thumb, and arrows make remaining menu
    // items obvious while keeping the text area completely separate.
    const int pageCount = (TOTAL_MENU_ITEMS + 2) / 3;
    const int page = top / 3;
    u8g2.drawFrame(0, 9, 5, 22);
    int thumbY = 10 + (page * 17) / (pageCount - 1);
    u8g2.drawBox(1, thumbY, 3, 3);
    if (page > 0) u8g2.drawTriangle(2, 1, 0, 5, 4, 5);
    if (page < pageCount - 1) u8g2.drawTriangle(2, 39, 0, 35, 4, 35);
    u8g2.sendBuffer();
}

void drawInstructions() {
    // Version 2 comfortably stores the current URL and fits this display at
    // one pixel per module, including a quiet border for reliable scanning.
    static QRCode qr;
    static bool qrReady = false;
    // Version 1 is 21x21 modules.  The uppercase URL uses QR alphanumeric
    // encoding, allowing larger modules on this small display.
    static uint8_t qrData[56];
    if (!qrReady) {
        qrcode_initText(&qr, qrData, 1, ECC_LOW, "HTTPS://GOOGLE.COM");
        qrReady = true;
    }

    u8g2.clearBuffer();
    // The QR region spans the complete display height.  Four quiet modules
    // around the code preserve reliable scanner detection.
    const int qrX = 12;
    const int qrPixels = 40;
    const int quietModules = 4;
    const int gridModules = qr.size + quietModules * 2;

    u8g2.setFont(u8g2_font_4x6_tr);
    const char* label = "SCANQR";
    for (uint8_t i = 0; label[i]; i++) {
        u8g2.setCursor(2, 5 + i * 6);
        u8g2.print(label[i]);
    }
    // QR readers expect dark modules on a light, quiet background.
    u8g2.drawBox(qrX, 0, qrPixels, qrPixels);
    u8g2.setDrawColor(0);
    for (uint8_t row = 0; row < qr.size; row++) {
        for (uint8_t col = 0; col < qr.size; col++) {
            if (qrcode_getModule(&qr, col, row)) {
                int x0 = qrX + ((quietModules + col) * qrPixels) / gridModules;
                int y0 = ((quietModules + row) * qrPixels) / gridModules;
                int x1 = qrX + ((quietModules + col + 1) * qrPixels) / gridModules;
                int y1 = ((quietModules + row + 1) * qrPixels) / gridModules;
                u8g2.drawBox(x0, y0, x1 - x0, y1 - y0);
            }
        }
    }
    u8g2.setDrawColor(1);
    u8g2.sendBuffer();
}

void drawStopwatch() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(10, 7); u8g2.print("STOPWATCH");

    u8g2.setFont(u8g2_font_logisoso16_tr);
    unsigned long t = swElapsed;
    if (swRunning) t += millis() - swStartTime;
    int m = t / 60000, s = (t % 60000) / 1000, ms = (t % 1000) / 100;
    char buf[9]; snprintf(buf, 9, "%02d:%02d.%01d", m, s, ms);
    int w = u8g2.getStrWidth(buf); u8g2.setCursor((72 - w) / 2, 26); u8g2.print(buf);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawScrollingFooter("Tap: Start/Stop | 2x Tap: Reset", stopwatchHelpScrollStart);
    u8g2.sendBuffer();
}

void drawScrollingFooter(const char* text, unsigned long started) {
    int textWidth = u8g2.getStrWidth(text);
    int offset = 0;
    if (textWidth > 72) {
        const unsigned long pause = 800;
        const unsigned long speed = 30;
        unsigned long phase = (millis() - started) % (pause + textWidth * speed + pause);
        if (phase >= pause) offset = min(textWidth, (int)((phase - pause) / speed));
    }
    u8g2.setClipWindow(0, 31, 72, 40);
    u8g2.setCursor(-offset, 39);
    u8g2.print(text);
    u8g2.setMaxClipWindow();
}

void drawHoldProgress(unsigned long requiredMs) {
    if (!buttonPressed || currentButtonState != LOW || requiredMs == 0) return;
    unsigned long held = millis() - pressStart;
    int progress = min(70, (int)((min(held, requiredMs) * 70) / requiredMs));
    u8g2.drawFrame(0, 28, 72, 3);
    if (progress > 0) u8g2.drawBox(1, 29, progress, 1);
}

void drawTimer() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 7); u8g2.print("TIMER");

    if (tmrState == TMR_ALARM) {
        if ((millis() / 250) % 2 == 0) {
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.setCursor(14, 24); u8g2.print("TIME UP!");
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(8, 38); u8g2.print("Tap to Stop");
    } else {
        u8g2.setFont(u8g2_font_logisoso16_tr);
        unsigned long rem = tmrRemaining;
        if (tmrState == TMR_RUNNING) {
            unsigned long el = millis() - tmrLastTick;
            rem = (el < rem) ? rem - el : 0;
        }
        int m = rem / 60000, s = (rem % 60000) / 1000;
        char buf[6]; snprintf(buf, 6, "%02d:%02d", m, s);
        int w = u8g2.getStrWidth(buf); u8g2.setCursor((72 - w) / 2, 26); u8g2.print(buf);

        u8g2.setFont(u8g2_font_5x7_tr);
        if (tmrState == TMR_STOPPED) {
            if (buttonPressed && currentButtonState == LOW) {
                char holding[28];
                snprintf(holding, sizeof(holding), "Holding: %lus / 3s Start", (millis() - pressStart) / 1000);
                drawScrollingFooter(holding, timerHelpScrollStart);
                drawHoldProgress(TIMER_START_HOLD);
            } else {
                drawScrollingFooter("Tap: Next | Hold 3s: Start | 2x Tap: Reset", timerHelpScrollStart);
            }
        } else {
            drawScrollingFooter("Tap: Stop | 2x Tap: Reset", timerHelpScrollStart);
        }
    }
    u8g2.sendBuffer();
}

void drawPomodoro() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_logisoso16_tr);
    unsigned long rem = pomRemaining;
    if (pomState != POM_IDLE) {
        unsigned long el = millis() - pomLastTick;
        rem = (el < rem) ? rem - el : 0;
    }
    int m = rem / 60000, s = (rem % 60000) / 1000;
    char buf[6]; snprintf(buf, 6, "%02d:%02d", m, s);
    int w = u8g2.getStrWidth(buf); u8g2.setCursor((72 - w) / 2, 26); u8g2.print(buf);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 7);
    if (pomState == POM_WORK) u8g2.print("WORK");
    else if (pomState == POM_BREAK) u8g2.print("BREAK");
    else u8g2.print("IDLE");
    u8g2.setCursor(45, 7); u8g2.print("S:"); u8g2.print(pomSessions);

    if (pomState == POM_IDLE) {
        drawScrollingFooter("Tap: Start | Hold: Menu", pomodoroHelpScrollStart);
    } else if (buttonPressed && currentButtonState == LOW) {
        char holding[28];
        snprintf(holding, sizeof(holding), "Holding: %lums / Stop", millis() - pressStart);
        drawScrollingFooter(holding, pomodoroHelpScrollStart);
        drawHoldProgress(HOLD_TIME);
    } else {
        drawScrollingFooter("Hold 1s: Stop", pomodoroHelpScrollStart);
    }
    u8g2.sendBuffer();
}

void drawDice() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(24, 7); u8g2.print("DICE");
    u8g2.drawFrame(22, 9, 28, 24);
    auto drawDots = [&](int v) {
        if (v % 2 == 1) u8g2.drawDisc(36, 21, 2);
        if (v >= 2) { u8g2.drawDisc(28, 14, 2); u8g2.drawDisc(44, 28, 2); }
        if (v >= 4) { u8g2.drawDisc(44, 14, 2); u8g2.drawDisc(28, 28, 2); }
        if (v == 6) { u8g2.drawDisc(28, 21, 2); u8g2.drawDisc(44, 21, 2); }
    };
    if (diceRolling) drawDots(random(1, 7)); else drawDots(diceValue);
    u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(14, 39); u8g2.print("Tap to Roll");
    u8g2.sendBuffer();
}

void drawMagic8Ball() {
    u8g2.clearBuffer();
    if (magic8Shaking) {
        if (millis() - magic8ShakeStart > 800) {
            magic8Shaking = false;
            magic8AnswerIdx = random(0, TOTAL_MAGIC_ANSWERS);
            magic8ScrollTimer = millis();
        } else {
            int rx = 36 + random(-3, 4);
            int ry = 18 + random(-2, 3);
            u8g2.drawDisc(rx, ry, 14);
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.setDrawColor(0);
            u8g2.setCursor(rx - 3, ry + 3); u8g2.print("8");
            u8g2.setDrawColor(1);
        }
    } else {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 7); u8g2.print("MAGIC 8-BALL");
        u8g2.drawHLine(0, 9, 72);

        if (magic8AnswerIdx < 0) {
            u8g2.drawDisc(36, 23, 10);
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.setDrawColor(0);
            u8g2.setCursor(33, 26); u8g2.print("8");
            u8g2.setDrawColor(1);
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.setCursor(8, 39); u8g2.print("Tap to Ask");
        } else {
            const char* ans = magic8Answers[magic8AnswerIdx];
            u8g2.setFont(u8g2_font_6x10_tf);
            int tw = u8g2.getStrWidth(ans);

            if (tw > 68) {
                unsigned long el = millis() - magic8ScrollTimer;
                int range = tw - 68;
                int speed = 30, pause = 600;
                unsigned long cycle = pause + range * speed + pause;
                unsigned long tm = el % cycle;
                int off = 0;
                if (tm >= (unsigned long)pause && tm < (unsigned long)pause + range * speed) off = (tm - pause) / speed;
                else if (tm >= (unsigned long)pause + range * speed) off = range;
                u8g2.setClipWindow(2, 14, 70, 26);
                u8g2.setCursor(2 - off, 24);
                u8g2.print(ans);
                u8g2.setMaxClipWindow();
            } else {
                u8g2.setCursor((72 - tw) / 2, 24);
                u8g2.print(ans);
            }

            u8g2.setFont(u8g2_font_5x7_tr);
            // Moved left so it never gets clipped off the right edge
            u8g2.setCursor(1, 38); u8g2.print("Tap:Ask Again");
        }
    }
    u8g2.sendBuffer();
}

void drawCoinFlipper() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 7); u8g2.print("COIN FLIPPER");
    u8g2.drawHLine(0, 9, 72);

    if (coinFlipping) {
        unsigned long el = millis() - coinFlipStart;
        if (el > 700) {
            coinFlipping = false;
            coinIsHeads = (random(0, 2) == 0);
        } else {
            // Simulate a coin spinning on its vertical axis: the ellipse's
            // width squashes and stretches each half-cycle, flipping the
            // visible face right as it passes through "edge-on".
            unsigned long cyclePos = el % 160;
            float t = (float)cyclePos / 160.0f;
            float squish = fabs(cosf(t * PI));
            int rx = 3 + (int)(squish * 15);
            bool showHeads = ((el / 160) % 2 == 0);
            u8g2.drawEllipse(36, 24, rx, 15);
            if (rx > 8) {
                u8g2.setFont(u8g2_font_5x7_tr);
                const char* face = showHeads ? "H" : "T";
                int fw = u8g2.getStrWidth(face);
                u8g2.setCursor(36 - fw / 2, 27);
                u8g2.print(face);
            }
        }
    } else {
        u8g2.setFont(u8g2_font_6x10_tf);
        if (coinIsHeads) {
            u8g2.setCursor(18, 24); u8g2.print("HEADS");
            u8g2.drawDisc(10, 20, 4);
        } else {
            u8g2.setCursor(18, 24); u8g2.print("TAILS");
            u8g2.drawCircle(10, 20, 4);
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(10, 38); u8g2.print("Tap to Flip");
    }
    u8g2.sendBuffer();
}

void drawBreathing() {
    u8g2.clearBuffer();
    if (breathActive) {
        const BreathMethod& method = boxBreath;
        unsigned long total = (method.inhale + method.hold1 + method.exhale + method.hold2) * 1000UL;
        unsigned long el = (millis() - breathTimer) % total;

        const char* label;
        float r;
        uint8_t secondsLeft;
        const float minR = 4.0f, maxR = 15.0f;

        if (el < method.inhale * 1000UL) {
            label = "INHALE";
            float t = (float)el / (method.inhale * 1000.0f);
            r = minR + smoothstep(t) * (maxR - minR);
            secondsLeft = method.inhale - el / 1000;
        } else if (el < (method.inhale + method.hold1) * 1000UL) {
            label = "HOLD";
            r = maxR;
            unsigned long ph = el - method.inhale * 1000UL;
            secondsLeft = method.hold1 - ph / 1000;
        } else if (el < (method.inhale + method.hold1 + method.exhale) * 1000UL) {
            label = "EXHALE";
            unsigned long ph = el - (method.inhale + method.hold1) * 1000UL;
            float t = (float)ph / (method.exhale * 1000.0f);
            r = maxR - smoothstep(t) * (maxR - minR);
            secondsLeft = method.exhale - ph / 1000;
        } else {
            label = "HOLD";
            r = minR;
            unsigned long ph = el - (method.inhale + method.hold1 + method.exhale) * 1000UL;
            secondsLeft = method.hold2 - ph / 1000;
        }

        // Phase label lives at the top (out of the circle's way)
        u8g2.setFont(u8g2_font_6x10_tf);
        int lw = u8g2.getStrWidth(label);
        u8g2.setCursor((72 - lw) / 2, 9);
        u8g2.print(label);

        int ir = (int)(r + 0.5f);
        u8g2.drawCircle(36, 27, ir);
        u8g2.drawCircle(36, 27, max(1, ir - 2));

        u8g2.setFont(u8g2_font_5x7_tr);
        char buf[3]; snprintf(buf, 3, "%d", secondsLeft);
        int nw = u8g2.getStrWidth(buf);
        u8g2.setCursor(36 - nw / 2, 30);
        u8g2.print(buf);
    } else {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 12); u8g2.print("BREATHING");
        u8g2.setFont(u8g2_font_6x10_tf);
        int w = u8g2.getStrWidth(boxBreath.name);
        u8g2.setCursor((72 - w) / 2, 25); u8g2.print(boxBreath.name);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(6, 39); u8g2.print("Tap to Start");
    }
    u8g2.sendBuffer();
}

void drawTallyCounter() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 7); u8g2.print("TALLY COUNTER");
    u8g2.drawHLine(0, 9, 72);

    u8g2.setFont(u8g2_font_logisoso16_tr);
    char buf[7]; snprintf(buf, 7, "%04d", tallyCount);
    int w = u8g2.getStrWidth(buf);
    u8g2.setCursor((72 - w) / 2, 28);
    u8g2.print(buf);

    u8g2.setFont(u8g2_font_5x7_tr);
    drawScrollingFooter("Tap: +1 | 2x Tap: Reset", tallyHelpScrollStart);
    u8g2.sendBuffer();
}

void drawFlashlight() {
    u8g2.clearBuffer();
    if (torchMode == FLASHLIGHT_SOLID) {
        u8g2.setContrast(255);
        u8g2.drawBox(0, 0, 72, 40);
    } else if (torchMode == FLASHLIGHT_STROBE) {
        if ((millis() / 50) % 2 == 0) {
            u8g2.setContrast(255);
            u8g2.drawBox(0, 0, 72, 40);
        } else {
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.setCursor(14, 22); u8g2.print("STROBE");
        }
    } else if (torchMode == FLASHLIGHT_SOS) {
        if (millis() - torchTimer > (unsigned long)sosPattern[sosIndex]) {
            torchTimer = millis();
            sosIndex = (sosIndex + 1) % TOTAL_SOS_STEPS;
            torchState = (sosIndex % 2 == 0);
        }
        if (torchState) {
            u8g2.setContrast(255);
            u8g2.drawBox(0, 0, 72, 40);
        } else {
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.setCursor(24, 24); u8g2.print("S O S");
        }
    }
    u8g2.sendBuffer();
}

void drawSnake() {
    u8g2.clearBuffer();
    if (snakeGameOver) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.setCursor(8, 14); u8g2.print("GAME OVER");
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(4, 24);
        u8g2.print("Score:"); u8g2.print(snakeScore); u8g2.print(" Hi:"); u8g2.print(snakeHighScore);
        u8g2.setCursor(10, 36); u8g2.print("Tap=Retry");
    } else {
        for (int i = 0; i < snakeLen; i++) u8g2.drawBox(snakeX[i] * 4, snakeY[i] * 4, i ? 3 : 4, i ? 3 : 4);
        // Food is filled + blinking so it never blends into the grid
        if ((millis() / 200) % 2 == 0) u8g2.drawBox(foodX * 4, foodY * 4, 4, 4);
        else u8g2.drawFrame(foodX * 4, foodY * 4, 4, 4);
        // Grid ends at y=31; the divider stays above the score glyphs.
        u8g2.drawHLine(0, 32, 72);
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(2, 39); u8g2.print("Score: "); u8g2.print(snakeScore);
    }
    u8g2.sendBuffer();
}

void drawFlappy() {
    u8g2.clearBuffer();
    if (flappyGameOver) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.setCursor(8, 14); u8g2.print("GAME OVER");
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(4, 24);
        u8g2.print("Score:"); u8g2.print(flappyScore); u8g2.print(" Hi:"); u8g2.print(flappyHighScore);
        u8g2.setCursor(10, 36); u8g2.print("Tap=Retry");
    } else {
        u8g2.drawBox(8, (int)birdY - 2, 4, 4);
        if (gapY > 0) u8g2.drawBox(pipeX, 0, 8, gapY);
        if (gapY + gapSize < 40) u8g2.drawBox(pipeX, gapY + gapSize, 8, 40 - gapY - gapSize);
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(58, 7); u8g2.print(flappyScore);
    }
    u8g2.sendBuffer();
}

void drawPong() {
    u8g2.clearBuffer();
    if (pongGameOver) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.setCursor(8, 14); u8g2.print("GAME OVER");
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(4, 24);
        u8g2.print("Score:"); u8g2.print(pongScore); u8g2.print(" Hi:"); u8g2.print(pongHighScore);
        u8g2.setCursor(10, 36); u8g2.print("Tap=Retry");
    } else {
        u8g2.drawBox((int)paddleX, 36, (int)paddleW, 3);
        u8g2.drawBox((int)ballX, (int)ballY, 3, 3);
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(58, 7); u8g2.print(pongScore);
    }
    u8g2.sendBuffer();
}

void drawSpaceDefender() {
    u8g2.clearBuffer();
    if (spaceGameOver) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.setCursor(8, 14); u8g2.print("GAME OVER");
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(4, 24);
        u8g2.print("Score:"); u8g2.print(spaceScore); u8g2.print(" Hi:"); u8g2.print(spaceHighScore);
        u8g2.setCursor(10, 36); u8g2.print("Tap=Retry");
    } else {
        for (int i = 0; i < 5; i++) {
            u8g2.drawPixel(stars[i][0], stars[i][1]);
        }

        int sx = (int)shipX;
        u8g2.drawBox(sx + 2, shipY, 3, 4);
        u8g2.drawBox(sx, shipY + 3, 7, 2);

        for (int i = 0; i < 3; i++) {
            if (aliens[i].alive) {
                int ax = (int)aliens[i].x, ay = (int)aliens[i].y;
                u8g2.drawBox(ax + 1, ay, 4, 3);
                u8g2.drawPixel(ax, ay + 3);
                u8g2.drawPixel(ax + 5, ay + 3);
            }
        }

        for (int i = 0; i < 5; i++) {
            if (bullets[i][1] >= 0) {
                u8g2.drawVLine(bullets[i][0], bullets[i][1], 3);
            }
        }

        if (explosionTimer > 0 && explosionX >= 0) {
            u8g2.drawPixel(explosionX - 2, explosionY - 2);
            u8g2.drawPixel(explosionX + 2, explosionY - 2);
            u8g2.drawPixel(explosionX - 2, explosionY + 2);
            u8g2.drawPixel(explosionX + 2, explosionY + 2);
        }

        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(56, 7); u8g2.print(spaceScore);
    }
    u8g2.sendBuffer();
}

void drawDinoRunner() {
    u8g2.clearBuffer();
    if (dinoGameOver) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.setCursor(8, 14); u8g2.print("GAME OVER");
        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(4, 24);
        u8g2.print("Score:"); u8g2.print(dinoScore); u8g2.print(" Hi:"); u8g2.print(dinoHighScore);
        u8g2.setCursor(10, 36); u8g2.print("Tap=Retry");
    } else {
        u8g2.drawHLine(0, 33, 72);
        for (int i = 0; i < 72; i += 8) { int x = (i - groundX + 72) % 72; u8g2.drawPixel(x, 35); }

        if (obsType == OBS_CACTUS) {
            u8g2.drawBox(obsX, 33 - cactusH, 4, cactusH);
            u8g2.drawPixel(obsX - 1, 33 - cactusH + 2);
            u8g2.drawPixel(obsX + 4, 33 - cactusH + 1);
        } else {
            u8g2.drawBox(obsX, pteroY, 6, 3);
            u8g2.drawPixel(obsX - 1, pteroY + 1);
            if (pteroWing) {
                u8g2.drawVLine(obsX + 2, pteroY - 3, 3);
            } else {
                u8g2.drawVLine(obsX + 2, pteroY + 3, 3);
            }
        }

        int dy = (int)dinoY;
        if (dinoDucking && !dinoJumping) {
            u8g2.drawBox(6, dy - 3, 10, 3);
            u8g2.drawBox(14, dy - 4, 3, 2);
            u8g2.drawBox(8, dy, 2, 2);
            u8g2.drawBox(12, dy, 2, 2);
        } else {
            u8g2.drawBox(10, dy - 6, 4, 6);
            u8g2.drawBox(12, dy - 8, 3, 3);
            if (!dinoJumping) {
                u8g2.drawBox(8, dy, 2, 2);
                u8g2.drawBox(12, dy - 2, 2, 2);
            } else {
                u8g2.drawBox(12, dy - 4, 2, 2);
            }
        }

        u8g2.setFont(u8g2_font_5x7_tr); u8g2.setCursor(58, 7); u8g2.print(dinoScore);
    }
    u8g2.sendBuffer();
}

void drawAbout() {
    const char* msg="DEV AVALANCHE --- Never gonna give you up.. Never gonna let you down.. Never gonna run around and desert you.";
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(0,8);u8g2.print("MIROS v1.0");
    u8g2.setCursor(0,18);u8g2.print("Developed by");
    int w=u8g2.getStrWidth(msg);
    int off=0;
    // Each entry starts with the beginning of the message visible.
    if(w>72){off=((millis()-aboutScrollStart)/40)%(w+72);}
    u8g2.setClipWindow(0,20,72,40);
    u8g2.setCursor(-off,32);u8g2.print(msg);
    u8g2.setMaxClipWindow();
    u8g2.sendBuffer();
}


void drawScreensaver() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor((int)ssX, (int)ssY + 10); u8g2.print("MIROS");
    u8g2.sendBuffer();
}

// App Initialization
void initApp(AppMode app) {
    switch (app) {
        case SNAKE: resetSnake(); break;
        case FLAPPY: resetFlappy(); break;
        case PONG: resetPong(); break;
        case SPACE_DEFENDER: resetSpaceDefender(); break;
        case DINO_RUNNER: resetDinoRunner(); break;
        case MAGIC_8BALL: magic8Shaking = false; magic8AnswerIdx = -1; break;
        case COIN_FLIPPER: coinFlipping = false; break;
        case BREATHING: breathActive = true; breathTimer = millis(); break;
        case FLASHLIGHT: torchMode = FLASHLIGHT_SOLID; sosIndex = 0; torchTimer = millis(); break;
        case STOPWATCH: swRunning = false; swElapsed = 0; stopwatchHelpScrollStart = millis(); break;
        case TIMER: tmrState = TMR_STOPPED; tmrRemaining = tmrPresets[tmrPresetIdx]; timerHelpScrollStart = millis(); break;
        case POMODORO: pomState = POM_IDLE; pomRemaining = 0; pomSessions = 0; pomodoroHelpScrollStart = millis(); break;
        case TALLY_COUNTER: tallyHelpScrollStart = millis(); break;
        case ABOUT: aboutScrollStart = millis(); break;
        case DICE: diceValue = 1; diceRolling = false; break;
        default: break;
    }
}

// Which apps need real double-tap disambiguation (adds a short deferral window)
bool appUsesDoubleTap(AppMode app) {
    switch (app) {
        case STOPWATCH:
        case TIMER:
        case TALLY_COUNTER:
            return true;
        default:
            return false;
    }
}

// Single place where every tap action is resolved — called on release
// (immediately for most apps, or after the tap window for double-tap apps).
void handleTap(bool isDouble) {
    switch (currentApp) {
        case MENU:
            menuIndex = (menuIndex + 1) % TOTAL_MENU_ITEMS;
            break;
        case STOPWATCH:
            if (isDouble) { swRunning = false; swElapsed = 0; }
            else if (swRunning) { swElapsed += millis() - swStartTime; swRunning = false; }
            else { swRunning = true; swStartTime = millis(); }
            break;
        case TALLY_COUNTER:
            if (isDouble) { tallyCount = 0; prefs.putInt("tally_cnt", 0); }
            else { tallyCount++; prefs.putInt("tally_cnt", tallyCount); }
            break;
        case FLASHLIGHT:
            if (torchMode == FLASHLIGHT_SOLID) torchMode = FLASHLIGHT_STROBE;
            else if (torchMode == FLASHLIGHT_STROBE) torchMode = FLASHLIGHT_SOS;
            else torchMode = FLASHLIGHT_SOLID;
            sosIndex = 0; torchTimer = millis();
            break;
        case TIMER:
            if (isDouble) {
                tmrState = TMR_STOPPED;
                tmrRemaining = tmrPresets[tmrPresetIdx];
                timerHelpScrollStart = millis();
            } else if (tmrState == TMR_STOPPED) {
                tmrPresetIdx = (tmrPresetIdx + 1) % TOTAL_TMR_PRESETS;
                tmrRemaining = tmrPresets[tmrPresetIdx];
                timerHelpScrollStart = millis();
            } else if (tmrState == TMR_RUNNING) {
                unsigned long el = millis() - tmrLastTick;
                tmrRemaining = (el < tmrRemaining) ? tmrRemaining - el : 0;
                tmrState = TMR_STOPPED;
                timerHelpScrollStart = millis();
            } else if (tmrState == TMR_ALARM) {
                tmrState = TMR_STOPPED;
                tmrRemaining = tmrPresets[tmrPresetIdx];
            }
            break;
        case POMODORO:
            if (pomState == POM_IDLE) { pomState = POM_WORK; pomRemaining = pomDuration; pomLastTick = millis(); }
            break;
        case DICE:
            diceRolling = true; diceRollStart = millis();
            break;
        case MAGIC_8BALL:
            magic8Shaking = true; magic8ShakeStart = millis();
            break;
        case COIN_FLIPPER:
            coinFlipping = true; coinFlipStart = millis();
            break;
        case BREATHING:
            breathActive = !breathActive;
            if (breathActive) breathTimer = millis();
            break;
        case SNAKE:
            if (snakeGameOver) resetSnake(); else rotateSnake();
            break;
        case FLAPPY:
            if (flappyGameOver) resetFlappy(); else birdVelocity = -2.8;
            break;
        case PONG:
            if (pongGameOver) resetPong(); else paddleVX = -paddleVX;
            break;
        case SPACE_DEFENDER:
            if (spaceGameOver) resetSpaceDefender();
            else if (millis() - lastShot > 200) {
                lastShot = millis();
                for (int i = 0; i < 5; i++) if (bullets[i][1] < 0) { bullets[i][0] = (int)shipX + 3; bullets[i][1] = shipY; break; }
            }
            break;
        case DINO_RUNNER:
            // Only handles the game-over retry tap here. The live jump needs
            // press duration (quick tap vs. hold-to-duck), so it's resolved
            // directly in the button release block instead.
            if (dinoGameOver) resetDinoRunner();
            break;
        default: break;
    }
    displayDirty = true;
}

// Setup
void setup() {
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    Wire.begin(SDA_PIN, SCL_PIN);
    u8g2.begin(); u8g2.setContrast(255);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    prefs.begin("scores", false);
    snakeHighScore = prefs.getInt("snk_hi", 0);
    flappyHighScore = prefs.getInt("flp_hi", 0);
    pongHighScore = prefs.getInt("png_hi", 0);
    spaceHighScore = prefs.getInt("spc_hi", 0);
    dinoHighScore = prefs.getInt("dno_hi", 0);
    tallyCount = prefs.getInt("tally_cnt", 0);

    lastActivity = millis();
    randomSeed(analogRead(0));
}

// Main Loop
void loop() {
    // Countdown Timer tick
    if (tmrState == TMR_RUNNING) {
        if (millis() - tmrLastTick >= tmrRemaining) {
            tmrState = TMR_ALARM;
            tmrRemaining = 0;
        }
    }

    updateLED();

    // Button Debounce & Standardized Gesture Machine
    bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastButtonState) lastDebounceTime = millis();
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != currentButtonState) {
            currentButtonState = reading;
            if (currentButtonState == LOW) { // Pressed Down
                pressStart = millis(); buttonPressed = true; lastActivity = millis();
                timerStartHoldConsumed = false;
                if (autoDim) { u8g2.setContrast(255); autoDim = false; }
                if (inScreensaver) { inScreensaver = false; screensaverWakeConsumed = true; }
                else {
                    screensaverWakeConsumed = false; displayDirty = true;

                    // The ONLY press-down action left is Dino's duck — a genuine
                    // hold gesture. Every other app resolves its action on
                    // release via handleTap(), so long-press-to-exit never
                    // "leaks" an action anymore.
                    if (currentApp == DINO_RUNNER && !dinoGameOver && !dinoJumping) {
                        dinoDucking = true;
                    }
                }
            } else { // Released
                if (currentApp == DINO_RUNNER) {
                    dinoDucking = false;
                }

                if (buttonPressed && !screensaverWakeConsumed) {
                    unsigned long pressTime = millis() - pressStart;

                    if (pressTime < 300 && (millis() - lastTapTime < 350)) {
                        isDoubleTap = true;
                        lastTapTime = 0;
                    } else {
                        isDoubleTap = false;
                        if (pressTime < 300) lastTapTime = millis();
                    }

                    // Dino Runner (while alive) gets a longer exit-hold, since
                    // ducking through an obstacle is itself a hold gesture and
                    // shouldn't be misread as "hold to exit".
                    unsigned long effectiveHoldTime = HOLD_TIME;
                    if (currentApp == DINO_RUNNER && !dinoGameOver) effectiveHoldTime = DINO_DUCK_EXIT_HOLD;

                    // A Timer start is performed while held, so releasing the
                    // button afterwards cannot be mistaken for a menu exit.
                    if (timerStartHoldConsumed) {
                        timerStartHoldConsumed = false;
                    // App-specific holds take priority over the global menu hold.
                    // This prevents Timer/Pomodoro controls from exiting the app.
                    } else if (currentApp == TIMER && tmrState == TMR_STOPPED && pressTime >= TIMER_START_HOLD) {
                        tmrState = TMR_RUNNING;
                        tmrLastTick = millis();
                        timerHelpScrollStart = millis();
                    } else if (currentApp == POMODORO && pomState != POM_IDLE && pressTime >= HOLD_TIME) {
                        pomState = POM_IDLE;
                        pomRemaining = 0;
                    // LONG PRESS -> UNIFIED GLOBAL ENTER / EXIT RULE
                    } else if (pressTime >= effectiveHoldTime) {
                        tapPending = false; // discard any stale deferred tap from the app we're leaving
                        if (currentApp == MENU) {
                            currentApp = menuToApp[menuIndex];
                            initApp(currentApp);
                        } else {
                            if (currentApp == STOPWATCH && swRunning) { swElapsed += millis() - swStartTime; swRunning = false; }
                            else if (currentApp == POMODORO && pomState != POM_IDLE) { pomState = POM_IDLE; pomRemaining = 0; }
                            else if (currentApp == TIMER && tmrState == TMR_RUNNING) { tmrState = TMR_STOPPED; }
                            currentApp = MENU;
                        }
                    } else if (currentApp == DINO_RUNNER && !dinoGameOver) {
                        // Quick tap = jump. Anything held longer (but under the
                        // exit threshold) was just a duck — release ends it
                        // with no further action, no menu exit.
                        if (pressTime < DINO_JUMP_MAX_PRESS && !dinoJumping) {
                            dinoJumping = true; dinoVelocity = -5.0;
                        }
                    } else { // SHORT PRESS (< 400ms) or DOUBLE TAP
                        tapPending = false;
                        if (appUsesDoubleTap(currentApp)) {
                            if (isDoubleTap) {
                                handleTap(true);
                            } else {
                                tapPending = true;
                                tapPendingSince = millis();
                                tapPendingApp = currentApp;
                            }
                        } else {
                            handleTap(false);
                        }
                    }
                }
                buttonPressed = false;
            }
        }
    }
    lastButtonState = reading;

    // Start the countdown at the three-second mark, without waiting for
    // release.  The consumed flag keeps that release inside the Timer.
    if (buttonPressed && currentButtonState == LOW && !timerStartHoldConsumed &&
        currentApp == TIMER && tmrState == TMR_STOPPED &&
        millis() - pressStart >= TIMER_START_HOLD) {
        tmrState = TMR_RUNNING;
        tmrLastTick = millis();
        timerHelpScrollStart = millis();
        timerStartHoldConsumed = true;
        displayDirty = true;
    }

    // Resolve a deferred single tap once the double-tap window has safely passed
    if (tapPending && (millis() - tapPendingSince) > TAP_WINDOW) {
        tapPending = false;
        if (tapPendingApp == currentApp) handleTap(false);
    }

    // Game & App Updates
    if (currentApp == SNAKE) updateSnake();
    if (currentApp == FLAPPY) updateFlappy();
    if (currentApp == PONG) updatePong();
    if (currentApp == SPACE_DEFENDER) updateSpaceDefender();
    if (currentApp == DINO_RUNNER) updateDinoRunner();

    if (diceRolling && millis() - diceRollStart > 500) { diceRolling = false; diceValue = random(1, 7); }

    if (pomState != POM_IDLE && millis() - pomLastTick >= pomRemaining) {
        if (pomState == POM_WORK) { pomSessions++; pomState = POM_BREAK; pomRemaining = pomBreak; }
        else { pomState = POM_IDLE; pomRemaining = 0; }
        pomLastTick = millis();
    }

    // Auto-dim & Screensaver
    if (millis() - lastActivity > 30000 && !autoDim) { u8g2.setContrast(30); autoDim = true; }
    if (autoDim && !inScreensaver && millis() - lastActivity > 30000 + SCREENSAVER_DELAY) {
        inScreensaver = true; ssX = random(0, 42); ssY = random(0, 30);
    }

    // Display Rendering
    unsigned long now = millis();
    if (displayDirty || (now - lastDisplayUpdate > MIN_DISPLAY_UPDATE)) {
        lastDisplayUpdate = now; displayDirty = false;
        if (inScreensaver) { updateScreensaver(); drawScreensaver(); }
        else {
            switch (currentApp) {
                case MENU: drawMenu(); break;
                case INSTRUCTIONS: drawInstructions(); break;
                case STOPWATCH: drawStopwatch(); break;
                case TIMER: drawTimer(); break;
                case POMODORO: drawPomodoro(); break;
                case DICE: drawDice(); break;
                case MAGIC_8BALL: drawMagic8Ball(); break;
                case COIN_FLIPPER: drawCoinFlipper(); break;
                case BREATHING: drawBreathing(); break;
                case TALLY_COUNTER: drawTallyCounter(); break;
                case FLASHLIGHT: drawFlashlight(); break;
                case SNAKE: drawSnake(); break;
                case FLAPPY: drawFlappy(); break;
                case PONG: drawPong(); break;
                case SPACE_DEFENDER: drawSpaceDefender(); break;
                case DINO_RUNNER: drawDinoRunner(); break;
                case ABOUT: drawAbout(); break;
            }
        }
    }
    delay(1);
}

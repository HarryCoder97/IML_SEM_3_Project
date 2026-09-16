#include <iostream>
#include <string>
#include <winsock2.h>

#include <windows.h>
#include <gl/GL.h>
#include <cmath>
#include <random>
#include <chrono>
#include <vector>
#include <fstream>
#include <iomanip>

constexpr int WIDTH = 1100;
constexpr int HEIGHT = 700;
constexpr float ARENA = 12.0f;
constexpr float SPEED = 0.24f;
constexpr float GRAVITY = 0.014f;
constexpr float JUMP_SPEED = 0.198f;
constexpr float MOVE_ACCELERATION = 0.18f;
constexpr float MOVE_DRAG = 0.82f;
constexpr float MAX_SPEED = 0.12f;
constexpr size_t MAX_QUANTUM_CUBES = 1;
constexpr int TAGGER_SPAWN_INTERVAL = 45;
constexpr double SESSION_SECONDS = 30.0;
constexpr float CUBE_SIZE = 1.4f;
constexpr float WALL_THICKNESS = 0.7f;
constexpr float ENTITY_LIMIT = ARENA - WALL_THICKNESS / 2.0f - CUBE_SIZE / 2.0f;

struct Cube {
    float x;
    float z;
    float y = 0.0f;
    float velocityX = 0.0f;
    float velocityZ = 0.0f;
    float velocityY = 0.0f;
    bool grounded = true;
};

struct Tagger {
    Cube body;
    float originX;
    float originZ;
};

struct PlayerPolicy {
    float fleeWeight = 2.0f;
    float wallWeight = 2.5f;
    float jitter = 0.35f;
    int directionMinFrames = 8;
    int directionMaxFrames = 24;
    int attempts = 0;
};

SOCKET gameSocket = INVALID_SOCKET;
HDC deviceContext = nullptr;
HGLRC renderContext = nullptr;
bool running = true;
std::vector<Tagger> taggers;
std::string actionReceiveBuffer;
std::vector<int> lastActions;
PlayerPolicy playerPolicy;

float loadPolicyValue(const std::string& text, const std::string& key, float fallback)
{
    const size_t marker = text.find("\"" + key + "\":");
    if (marker == std::string::npos) return fallback;
    return static_cast<float>(std::atof(text.c_str() + marker + key.size() + 3));
}

void loadPlayerPolicy()
{
    std::ifstream policyFile("player_policy.json");
    const std::string text((std::istreambuf_iterator<char>(policyFile)), std::istreambuf_iterator<char>());
    playerPolicy.fleeWeight = loadPolicyValue(text, "flee_weight", playerPolicy.fleeWeight);
    playerPolicy.wallWeight = loadPolicyValue(text, "wall_weight", playerPolicy.wallWeight);
    playerPolicy.jitter = loadPolicyValue(text, "jitter", playerPolicy.jitter);
    playerPolicy.directionMinFrames = static_cast<int>(loadPolicyValue(text, "direction_min_frames", static_cast<float>(playerPolicy.directionMinFrames)));
    playerPolicy.directionMaxFrames = static_cast<int>(loadPolicyValue(text, "direction_max_frames", static_cast<float>(playerPolicy.directionMaxFrames)));
    playerPolicy.attempts = static_cast<int>(loadPolicyValue(text, "attempts", 0.0f));
}

void savePlayerPolicy(bool caught)
{
    ++playerPolicy.attempts;
    if (caught) {
        playerPolicy.fleeWeight = (std::min)(4.0f, playerPolicy.fleeWeight + 0.12f);
        playerPolicy.wallWeight = (std::min)(4.0f, playerPolicy.wallWeight + 0.15f);
        playerPolicy.jitter = (std::max)(0.12f, playerPolicy.jitter - 0.02f);
    } else {
        playerPolicy.fleeWeight = (std::max)(1.4f, playerPolicy.fleeWeight - 0.03f);
        playerPolicy.wallWeight = (std::max)(1.8f, playerPolicy.wallWeight - 0.03f);
        playerPolicy.jitter = (std::min)(0.6f, playerPolicy.jitter + 0.01f);
    }
    std::ofstream policyFile("player_policy.json", std::ios::trunc);
    policyFile << "{\n"
        << "  \"flee_weight\": " << playerPolicy.fleeWeight << ",\n"
        << "  \"wall_weight\": " << playerPolicy.wallWeight << ",\n"
        << "  \"jitter\": " << playerPolicy.jitter << ",\n"
        << "  \"direction_min_frames\": " << playerPolicy.directionMinFrames << ",\n"
        << "  \"direction_max_frames\": " << playerPolicy.directionMaxFrames << ",\n"
        << "  \"attempts\": " << playerPolicy.attempts << ",\n"
        << "  \"last_result\": \"" << (caught ? "caught" : "survived") << "\"\n"
        << "}\n";
}

void drawCube(float x, float y, float z, float size, float red, float green, float blue)
{
    const float h = size / 2.0f;
    glBegin(GL_QUADS);
    glColor3f(red * 1.08f, green * 1.08f, blue * 1.08f);
    glVertex3f(x - h, y - h, z + h); glVertex3f(x + h, y - h, z + h); glVertex3f(x + h, y + h, z + h); glVertex3f(x - h, y + h, z + h);
    glColor3f(red * 0.68f, green * 0.68f, blue * 0.68f);
    glVertex3f(x + h, y - h, z - h); glVertex3f(x - h, y - h, z - h); glVertex3f(x - h, y + h, z - h); glVertex3f(x + h, y + h, z - h);
    glColor3f(red * 0.82f, green * 0.82f, blue * 0.82f);
    glVertex3f(x - h, y - h, z - h); glVertex3f(x - h, y - h, z + h); glVertex3f(x - h, y + h, z + h); glVertex3f(x - h, y + h, z - h);
    glColor3f(red * 0.92f, green * 0.92f, blue * 0.92f);
    glVertex3f(x + h, y - h, z + h); glVertex3f(x + h, y - h, z - h); glVertex3f(x + h, y + h, z - h); glVertex3f(x + h, y + h, z + h);
    glColor3f(red * 1.22f, green * 1.22f, blue * 1.22f);
    glVertex3f(x - h, y + h, z + h); glVertex3f(x + h, y + h, z + h); glVertex3f(x + h, y + h, z - h); glVertex3f(x - h, y + h, z - h);
    glColor3f(red * 0.55f, green * 0.55f, blue * 0.55f);
    glVertex3f(x - h, y - h, z - h); glVertex3f(x + h, y - h, z - h); glVertex3f(x + h, y - h, z + h); glVertex3f(x - h, y - h, z + h);
    glEnd();
}

void appendSessionReport(int attempt, double sessionTime, bool caught, char mode)
{
    std::ifstream existing("training_report.csv");
    std::string header;
    std::getline(existing, header);
    const bool needsHeader = !existing.good() || header.empty();
    const bool needsModeColumn = header.find(",mode") == std::string::npos;
    std::vector<std::string> oldRows;
    std::string oldRow;
    if (needsModeColumn && !needsHeader) {
        while (std::getline(existing, oldRow)) {
            if (!oldRow.empty()) oldRows.push_back(oldRow + ",a");
        }
    }
    existing.close();
    if (needsModeColumn && !needsHeader) {
        std::ofstream migrated("training_report.csv", std::ios::trunc);
        migrated << "attempt_no,session_time,caught,time_taken,mode\n";
        for (const std::string& row : oldRows) migrated << row << '\n';
        migrated.close();
    }
    std::ofstream report("training_report.csv", std::ios::app);
    if (needsHeader) report << "attempt_no,session_time,caught,time_taken,mode\n";
    report << attempt << ',' << std::fixed << std::setprecision(3) << sessionTime << ','
           << (caught ? "yes" : "no") << ',' << std::fixed << std::setprecision(3)
           << (caught ? sessionTime : SESSION_SECONDS) << ',' << mode << '\n';
}

void appendPlayerReport(int attempt, double sessionTime, bool caught)
{
    std::ifstream existing("player_training_report.csv");
    const bool needsHeader = !existing.good() || existing.peek() == std::ifstream::traits_type::eof();
    existing.close();
    std::ofstream report("player_training_report.csv", std::ios::app);
    if (needsHeader) report << "attempt_no,session_time,caught,time_taken,mode\n";
    report << attempt << ',' << std::fixed << std::setprecision(3) << sessionTime << ','
           << (caught ? "yes" : "no") << ',' << std::fixed << std::setprecision(3)
           << (caught ? sessionTime : SESSION_SECONDS) << ",p\n";
}

int loadNextPlayerAttempt()
{
    std::ifstream report("player_training_report.csv");
    std::string line;
    int highestAttempt = 0;
    while (std::getline(report, line)) {
        if (line.empty() || line.rfind("attempt_no", 0) == 0) continue;
        highestAttempt = (std::max)(highestAttempt, std::atoi(line.c_str()));
    }
    return highestAttempt + 1;
}

int loadNextAttempt()
{
    std::ifstream history("training_report.csv");
    std::string line;
    int highestAttempt = 0;
    while (std::getline(history, line)) {
        if (line.empty() || line.rfind("attempt_no", 0) == 0) continue;
        const int savedAttempt = std::atoi(line.c_str());
        highestAttempt = (std::max)(highestAttempt, savedAttempt);
    }
    return highestAttempt + 1;
}

void drawBlock(float x, float y, float z, float width, float height, float depth, float red, float green, float blue)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(width, height, depth);
    drawCube(0, 0, 0, 1.0f, red, green, blue);
    glPopMatrix();
}

void drawArena()
{
    glColor3f(0.12f, 0.16f, 0.2f);
    glBegin(GL_QUADS);
    glVertex3f(-ARENA, 0, -ARENA); glVertex3f(ARENA, 0, -ARENA); glVertex3f(ARENA, 0, ARENA); glVertex3f(-ARENA, 0, ARENA);
    glEnd();
    drawBlock(0, 1.5f, -ARENA, ARENA * 2.0f, 3.0f, 0.7f, 0.24f, 0.28f, 0.34f);
    drawBlock(-ARENA, 1.5f, 0, 0.7f, 3.0f, ARENA * 2.0f, 0.24f, 0.28f, 0.34f);
    drawBlock(ARENA, 1.5f, 0, 0.7f, 3.0f, ARENA * 2.0f, 0.24f, 0.28f, 0.34f);
    drawBlock(-ARENA, 3.0f, ARENA, 0.9f, 6.0f, 0.9f, 0.18f, 0.22f, 0.28f);
    drawBlock(ARENA, 3.0f, ARENA, 0.9f, 6.0f, 0.9f, 0.18f, 0.22f, 0.28f);
    for (int i = -8; i <= 8; i += 2) {
        drawCube(static_cast<float>(i), 0.03f, 0, 0.03f, 0.3f, 0.34f, 0.4f);
        drawCube(0, 0.03f, static_cast<float>(i), 0.03f, 0.3f, 0.34f, 0.4f);
    }
}

void drawDigitalDigit(int digit, float x, float y, float z)
{
    static const bool segments[10][7] = {
        {true, true, true, true, true, true, false},
        {false, true, true, false, false, false, false},
        {true, true, false, true, true, false, true},
        {true, true, true, true, false, false, true},
        {false, true, true, false, false, true, true},
        {true, false, true, true, false, true, true},
        {true, false, true, true, true, true, true},
        {true, true, true, false, false, false, false},
        {true, true, true, true, true, true, true},
        {true, true, true, true, false, true, true}
    };
    const float width = 0.72f;
    const float height = 1.25f;
    const float thickness = 0.12f;
    const float halfWidth = width / 2.0f;
    const float halfHeight = height / 2.0f;
    const float verticalHeight = (height - thickness) / 2.0f;
    const float verticalOffset = (height + thickness) / 4.0f;
    const float colorRed = 0.95f;
    const float colorGreen = 0.18f;
    const float colorBlue = 0.04f;
    if (segments[digit][0]) drawBlock(x, y + halfHeight, z, width, thickness, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][1]) drawBlock(x + halfWidth, y + verticalOffset, z, thickness, verticalHeight, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][2]) drawBlock(x + halfWidth, y - verticalOffset, z, thickness, verticalHeight, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][3]) drawBlock(x, y - halfHeight, z, width, thickness, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][4]) drawBlock(x - halfWidth, y - verticalOffset, z, thickness, verticalHeight, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][5]) drawBlock(x - halfWidth, y + verticalOffset, z, thickness, verticalHeight, 0.06f, colorRed, colorGreen, colorBlue);
    if (segments[digit][6]) drawBlock(x, y, z, width, thickness, 0.06f, colorRed, colorGreen, colorBlue);
}

void drawTimerScreen(int secondsLeft)
{
    const float screenZ = -ARENA + 0.42f;
    drawBlock(0, 5.2f, screenZ, 7.0f, 2.4f, 0.22f, 0.02f, 0.04f, 0.05f);
    const int tens = secondsLeft / 10;
    const int ones = secondsLeft % 10;
    drawDigitalDigit(tens, -0.62f, 5.2f, screenZ + 0.14f);
    drawDigitalDigit(ones, 0.62f, 5.2f, screenZ + 0.14f);
}

void addTagger(float x, float z, const Cube& player)
{
    if (taggers.size() >= MAX_QUANTUM_CUBES) {
        auto furthest = taggers.begin();
        float furthestDistance = -1.0f;
        for (auto candidate = taggers.begin(); candidate != taggers.end(); ++candidate) {
            const float distance = std::hypot(candidate->body.x - player.x, candidate->body.z - player.z);
            if (distance > furthestDistance) {
                furthestDistance = distance;
                furthest = candidate;
            }
        }
        taggers.erase(furthest);
    }
    taggers.push_back(Tagger{Cube{x, z, 0.0f}, x, z});
}

bool sendLine(const std::string& line)
{
    return send(gameSocket, line.c_str(), static_cast<int>(line.size()), 0) != SOCKET_ERROR;
}

void moveTagger(Cube& tagger, int action, const Cube& player)
{
    float targetX = 0.0f;
    float targetZ = 0.0f;
    if (action == 1) targetZ = -MAX_SPEED;
    if (action == 2) targetZ = MAX_SPEED;
    if (action == 3) targetX = -MAX_SPEED;
    if (action == 4) targetX = MAX_SPEED;
    if (action == 5 && tagger.grounded) {
        tagger.velocityY = JUMP_SPEED;
        tagger.grounded = false;
    }
    if (action == 5) {
        targetX = tagger.velocityX;
        targetZ = tagger.velocityZ;
    }
    if (tagger.x >= ENTITY_LIMIT - 0.08f && targetX > 0.0f) targetX = -MAX_SPEED;
    if (tagger.x <= -ENTITY_LIMIT + 0.08f && targetX < 0.0f) targetX = MAX_SPEED;
    if (tagger.z >= ENTITY_LIMIT - 0.08f && targetZ > 0.0f) targetZ = -MAX_SPEED;
    if (tagger.z <= -ENTITY_LIMIT + 0.08f && targetZ < 0.0f) targetZ = MAX_SPEED;
    const float chaseX = player.x - tagger.x;
    const float chaseZ = player.z - tagger.z;
    const float chaseDistance = std::max(0.001f, std::hypot(chaseX, chaseZ));
    if (action != 5) {
        const float chaseWeight = chaseDistance < 3.5f ? 0.8f : 0.55f;
        targetX = targetX * (1.0f - chaseWeight) + (chaseX / chaseDistance * MAX_SPEED) * chaseWeight;
        targetZ = targetZ * (1.0f - chaseWeight) + (chaseZ / chaseDistance * MAX_SPEED) * chaseWeight;
    }
    tagger.velocityX += (targetX - tagger.velocityX) * 0.18f;
    tagger.velocityZ += (targetZ - tagger.velocityZ) * 0.18f;
    if (action == 0) {
        tagger.velocityX *= MOVE_DRAG;
        tagger.velocityZ *= MOVE_DRAG;
    }
    if (std::abs(tagger.velocityX) < 0.002f) tagger.velocityX = 0.0f;
    if (std::abs(tagger.velocityZ) < 0.002f) tagger.velocityZ = 0.0f;
    tagger.x += tagger.velocityX;
    tagger.z += tagger.velocityZ;
    if (!tagger.grounded) {
        tagger.y += tagger.velocityY;
        tagger.velocityY -= GRAVITY;
        if (tagger.y <= 0.0f) {
            tagger.y = 0.0f;
            tagger.velocityY = 0.0f;
            tagger.grounded = true;
        }
    }
    if (tagger.x < -ENTITY_LIMIT) { tagger.x = -ENTITY_LIMIT; if (tagger.velocityX < 0.0f) tagger.velocityX = -tagger.velocityX; }
    if (tagger.x > ENTITY_LIMIT) { tagger.x = ENTITY_LIMIT; if (tagger.velocityX > 0.0f) tagger.velocityX = -tagger.velocityX; }
    if (tagger.z < -ENTITY_LIMIT) { tagger.z = -ENTITY_LIMIT; if (tagger.velocityZ < 0.0f) tagger.velocityZ = -tagger.velocityZ; }
    if (tagger.z > ENTITY_LIMIT) { tagger.z = ENTITY_LIMIT; if (tagger.velocityZ > 0.0f) tagger.velocityZ = -tagger.velocityZ; }
}

void movePlayer(Cube& player, const std::vector<Tagger>& activeTaggers, bool autoPlayer)
{
    float inputX = 0.0f;
    float inputZ = 0.0f;
    if (autoPlayer) {
        static std::mt19937 randomEngine(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        static std::uniform_real_distribution<float> randomAngle(0.0f, 6.2831853f);
        static float directionX = 1.0f;
        static float directionZ = 0.0f;
        static int framesLeft = 0;
        std::uniform_real_distribution<float> randomJitter(-playerPolicy.jitter, playerPolicy.jitter);
        if (framesLeft-- <= 0) {
            const float angle = randomAngle(randomEngine);
            directionX = std::cos(angle);
            directionZ = std::sin(angle);
            std::uniform_int_distribution<int> randomChangeFrames(playerPolicy.directionMinFrames, playerPolicy.directionMaxFrames);
            framesLeft = randomChangeFrames(randomEngine);
        }
        inputX = directionX + randomJitter(randomEngine);
        inputZ = directionZ + randomJitter(randomEngine);

        const float distanceFromCenter = std::hypot(player.x, player.z);
        if (distanceFromCenter > 11.0f) {
            inputX = -player.x / distanceFromCenter;
            inputZ = -player.z / distanceFromCenter;
        } else if (!activeTaggers.empty()) {
            const Tagger* nearestTagger = &activeTaggers.front();
            float nearestDistance = std::hypot(
                player.x - nearestTagger->body.x,
                player.z - nearestTagger->body.z);
            for (const Tagger& activeTagger : activeTaggers) {
                const float distance = std::hypot(
                    player.x - activeTagger.body.x,
                    player.z - activeTagger.body.z);
                if (distance < nearestDistance) {
                    nearestTagger = &activeTagger;
                    nearestDistance = distance;
                }
            }
            if (nearestDistance < 5.0f) {
                const float awayX = player.x - nearestTagger->body.x;
                const float awayZ = player.z - nearestTagger->body.z;
                const float awayDistance = std::max(0.001f, std::hypot(awayX, awayZ));
                inputX = awayX / awayDistance;
                inputZ = awayZ / awayDistance;
            }
        }
    } else {
        if (GetAsyncKeyState('W') & 0x8000) inputZ -= 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) inputZ += 1.0f;
        if (GetAsyncKeyState('A') & 0x8000) inputX -= 1.0f;
        if (GetAsyncKeyState('D') & 0x8000) inputX += 1.0f;
    }
    const float inputLength = std::hypot(inputX, inputZ);
    if (inputLength > 0.0f) {
        inputX = inputX / inputLength * MAX_SPEED;
        inputZ = inputZ / inputLength * MAX_SPEED;
    }
    player.velocityX += (inputX - player.velocityX) * MOVE_ACCELERATION;
    player.velocityZ += (inputZ - player.velocityZ) * MOVE_ACCELERATION;
    if (inputLength == 0.0f) {
        player.velocityX *= MOVE_DRAG;
        player.velocityZ *= MOVE_DRAG;
    }
    player.x += player.velocityX;
    player.z += player.velocityZ;
    if (player.x < -ENTITY_LIMIT) { player.x = -ENTITY_LIMIT; if (player.velocityX < 0.0f) player.velocityX = -player.velocityX; }
    if (player.x > ENTITY_LIMIT) { player.x = ENTITY_LIMIT; if (player.velocityX > 0.0f) player.velocityX = -player.velocityX; }
    if (player.z < -ENTITY_LIMIT) { player.z = -ENTITY_LIMIT; if (player.velocityZ < 0.0f) player.velocityZ = -player.velocityZ; }
    if (player.z > ENTITY_LIMIT) { player.z = ENTITY_LIMIT; if (player.velocityZ > 0.0f) player.velocityZ = -player.velocityZ; }
    static bool spaceWasDown = false;
    const bool spaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (spaceDown && !spaceWasDown && player.grounded) {
        player.velocityY = JUMP_SPEED;
        player.grounded = false;
    }
    static DWORD lastAutoJump = 0;
    if (autoPlayer && player.grounded && GetTickCount() - lastAutoJump > 4500) {
        player.velocityY = JUMP_SPEED;
        player.grounded = false;
        lastAutoJump = GetTickCount();
    }
    spaceWasDown = spaceDown;
    if (!player.grounded) {
        player.y += player.velocityY;
        player.velocityY -= GRAVITY;
        if (player.y <= 0.0f) {
            player.y = 0.0f;
            player.velocityY = 0.0f;
            player.grounded = true;
        }
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE || message == WM_DESTROY) { running = false; PostQuitMessage(0); return 0; }
    return DefWindowProc(window, message, wParam, lParam);
}

bool createWindow(HINSTANCE instance, HWND& window)
{
    WNDCLASSA windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = "TagArenaWindow";
    windowClass.style = CS_OWNDC;
    if (!RegisterClassA(&windowClass)) return false;
    window = CreateWindowA(windowClass.lpszClassName, "Cube Tag Arena - WASD to escape", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 40, WIDTH, HEIGHT, nullptr, nullptr, instance, nullptr);
    if (!window) return false;
    deviceContext = GetDC(window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format); format.nVersion = 1; format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32; format.cDepthBits = 24;
    const int pixelFormat = ChoosePixelFormat(deviceContext, &format);
    SetPixelFormat(deviceContext, pixelFormat, &format);
    renderContext = wglCreateContext(deviceContext);
    wglMakeCurrent(deviceContext, renderContext);
    glEnable(GL_DEPTH_TEST);
    return true;
}

void render(const Cube& player, const std::vector<Tagger>& activeTaggers, int attempt, int secondsLeft)
{
    glClearColor(0.04f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    const float aspect = static_cast<float>(WIDTH) / HEIGHT;
    const float top = std::tan(45.0f * 3.14159265f / 360.0f) * 0.1f;
    glFrustum(-top * aspect, top * aspect, -top, top, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(-player.x, -4.8f - player.y * 0.25f, -22.0f - player.z);
    glRotatef(14.0f, 1, 0, 0);
    drawArena();
    drawTimerScreen(secondsLeft);
    for (const Tagger& tagger : activeTaggers) {
        drawCube(tagger.body.x, 0.75f + tagger.body.y, tagger.body.z, 1.4f, 1.0f, 0.2f, 0.18f);
        drawCube(tagger.body.x, 1.47f + tagger.body.y, tagger.body.z, 0.42f, 1.0f, 0.35f, 0.22f);
    }
    drawCube(player.x, 0.75f + player.y, player.z, 1.4f, 0.1f, 0.65f, 1.0f);
    drawCube(player.x, 1.47f + player.y, player.z, 0.42f, 0.2f, 0.85f, 1.0f);
    static HFONT hudFont = CreateFontA(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    const HFONT previousFont = static_cast<HFONT>(SelectObject(deviceContext, hudFont));
    SetBkMode(deviceContext, TRANSPARENT);
    RECT hudBackground{0, 0, WIDTH, 64};
    HBRUSH hudBrush = CreateSolidBrush(RGB(8, 18, 30));
    FillRect(deviceContext, &hudBackground, hudBrush);
    DeleteObject(hudBrush);
    SetTextColor(deviceContext, RGB(240, 248, 255));
    const std::string hud = "ATTEMPT  " + std::to_string(attempt) + "     TIME LEFT  " + std::to_string(secondsLeft) + "s";
    TextOutA(deviceContext, 28, 18, hud.c_str(), static_cast<int>(hud.size()));
    SelectObject(deviceContext, previousFont);
    SwapBuffers(deviceContext);
}

std::vector<int> receiveActions(size_t count)
{
    if (lastActions.size() != count) lastActions.assign(count, 1);
    char buffer[1024] = {};
    while (true) {
        const int received = recv(gameSocket, buffer, sizeof(buffer) - 1, 0);
        if (received == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
            return lastActions;
        }
        if (received == 0) return lastActions;
        actionReceiveBuffer.append(buffer, received);
    }
    size_t newline = actionReceiveBuffer.find('\n');
    while (newline != std::string::npos) {
        const std::string response = actionReceiveBuffer.substr(0, newline);
        actionReceiveBuffer.erase(0, newline + 1);
        const size_t field = response.find("\"actions\"");
        const size_t start = field == std::string::npos ? std::string::npos : response.find('[', field);
        if (start != std::string::npos) {
            size_t position = start + 1;
            for (size_t index = 0; index < count && position < response.size(); ++index) {
                while (position < response.size() && (response[position] == ' ' || response[position] == ',')) ++position;
                lastActions[index] = std::atoi(response.c_str() + position);
                while (position < response.size() && response[position] != ',') ++position;
            }
        }
        newline = actionReceiveBuffer.find('\n');
    }
    return lastActions;
}
int main(int argc, char* argv[])
{
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    gameSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(5000); address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (gameSocket == INVALID_SOCKET || connect(gameSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) { std::cerr << "Start train.py first.\n"; return 1; }
    u_long nonBlocking = 1;
    ioctlsocket(gameSocket, FIONBIO, &nonBlocking);
    HWND window = nullptr;
    if (!createWindow(GetModuleHandle(nullptr), window)) return 1;
    const bool autoPlayer = argc > 1 && std::string(argv[1]) == "--autoplay";
    loadPlayerPolicy();
    Cube player{5.5f, 5.5f};
    addTagger(-5.5f, -5.5f, player);
    int spawnTimer = 0;
    int attempt = autoPlayer ? loadNextAttempt() : loadNextPlayerAttempt();
    const auto sessionStart = std::chrono::steady_clock::now();
    auto attemptStart = sessionStart;
    bool attemptReported = false;
    MSG message{};
    while (running) {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessage(&message); }
        movePlayer(player, taggers, autoPlayer);
        bool tagged = false;
        std::string state = "{\"px\":" + std::to_string(player.x) + ",\"pz\":" + std::to_string(player.z) + ",\"py\":" + std::to_string(player.y) + ",\"tagged\":false,\"taggers\":[";
        for (size_t index = 0; index < taggers.size(); ++index) {
            const Cube& tagger = taggers[index].body;
            const float xGap = std::abs(player.x - tagger.x);
            const float zGap = std::abs(player.z - tagger.z);
            const float yGap = std::abs(player.y - tagger.y);
            const bool cubesTouch = xGap <= CUBE_SIZE && zGap <= CUBE_SIZE && yGap <= CUBE_SIZE;
            tagged = tagged || cubesTouch;
            if (index > 0) state += ",";
            state += "{\"x\":" + std::to_string(tagger.x) + ",\"z\":" + std::to_string(tagger.z) + ",\"y\":" + std::to_string(tagger.y) + "}";
        }
        state += "]}\n";
        state.replace(state.find("false"), 5, tagged ? "true" : "false");
        sendLine(state);
        const std::vector<int> actions = receiveActions(taggers.size());
        for (size_t index = 0; index < taggers.size(); ++index) moveTagger(taggers[index].body, actions[index], player);
        if (++spawnTimer >= TAGGER_SPAWN_INTERVAL) {
            spawnTimer = 0;
            const Cube& origin = taggers.front().body;
            addTagger(origin.x + 0.9f, origin.z, player);
        }
        const auto now = std::chrono::steady_clock::now();
        const double sessionTime = std::chrono::duration<double>(now - attemptStart).count();
        const int secondsLeft = (std::max)(0, static_cast<int>(std::ceil(SESSION_SECONDS - sessionTime)));
        const std::string title = "Cube Tag Arena - Attempt " + std::to_string(attempt) + " - " + std::to_string(secondsLeft) + "s - WASD / Space";
        SetWindowTextA(window, title.c_str());
        render(player, taggers, attempt, secondsLeft);
        if (tagged || sessionTime >= SESSION_SECONDS) {
            appendSessionReport(attempt, tagged ? sessionTime : SESSION_SECONDS, tagged, autoPlayer ? 'a' : 'p');
            appendPlayerReport(attempt, tagged ? sessionTime : SESSION_SECONDS, tagged);
            savePlayerPolicy(tagged);
            attemptReported = true;
            ++attempt;
            player = {5.5f, 5.5f};
            taggers.clear();
            addTagger(-5.5f, -5.5f, player);
            spawnTimer = 0;
            attemptStart = now;
            attemptReported = false;
        }
        Sleep(16);
    }
    if (!autoPlayer && !attemptReported) {
        const double unfinishedTime = (std::min)(SESSION_SECONDS, std::chrono::duration<double>(std::chrono::steady_clock::now() - attemptStart).count());
        appendSessionReport(attempt, unfinishedTime, false, 'p');
        appendPlayerReport(attempt, unfinishedTime, false);
    }
    if (renderContext) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(renderContext); }
    if (window) ReleaseDC(window, deviceContext);
    closesocket(gameSocket); WSACleanup();
    return 0;
}
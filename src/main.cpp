// #include "player.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>  // For event handling
#include <algorithm> // For std::max/min
#include <cmath>     // For std::abs
#include <cstdint>   // For uint32_t

// --- Animation Constants ---
const int FRAME_WIDTH = 64;               // Frame width in pixels
const int FRAME_HEIGHT = 64;              // Frame height in pixels
const int FRAMES_PER_ROW = 8;             // Number of frames per row in sprite sheet
const float STATE_CHANGE_COOLDOWN = 0.1f; // Cooldown time between state changes (Optional)

// Animation state definitions
enum class PlayerAnimState
{
    Stand, // Stand animation
    Walk,  // Walk animation
    Jump,  // Jump and Fall animation
    Stance // Combat stance (currently unused)
};

// Animation data structure (start index, frame count, time per frame)
struct AnimationData
{
    int startFrameIndex; // Starting frame index in the sheet (0-based)
    int frameCount;      // Total number of frames for this animation
    float timePerFrame;  // Time per frame in seconds
};

// Player input state structure (Removed up/down as they are not used for rope)
struct PlayerInputState
{
    bool up = false;   // Removed
    bool down = false; // Removed
    bool left = false;
    bool right = false;
    bool jump = false; // Using Space for jump
};

// Packet type enumeration
enum class PacketType : uint8_t
{ // Explicit underlying type
    Welcome,
    PlayerState,
    PlayerInput,
    PlayerJoined,
    PlayerLeft,
    MapData
};

// Packet
sf::Packet &operator<<(sf::Packet &packet, const PlayerInputState &input)
{
    return packet << input.up << input.down << input.left << input.right << input.jump;
}

sf::Packet &operator>>(sf::Packet &packet, PlayerInputState &input)
{
    return packet >> input.up >> input.down >> input.left >> input.right >> input.jump;
}

sf::Packet &operator<<(sf::Packet &packet, PacketType type)
{
    return packet << static_cast<uint8_t>(type);
}

sf::Packet &operator>>(sf::Packet &packet, PacketType &type)
{
    uint8_t value;
    packet >> value;
    type = static_cast<PacketType>(value);
    return packet;
}

int main()
{
    // --- 초기화 ---
    // 윈도우 생성
    sf::RenderWindow window(sf::VideoMode({800, 600}), "Client");
    window.setFramerateLimit(60);

    // Map data variables
    std::vector<std::vector<int>> clientTileMap;
    int clientMapWidth = 0;
    int clientMapHeight = 0;
    const float CLIENT_TILE_SIZE = 40.f;
    std::vector<sf::RectangleShape> mapShapes;
    bool mapLoaded = false;

    // Animation data initialization (Corrected Stand index)
    std::map<PlayerAnimState, AnimationData> animData = {
        {PlayerAnimState::Stand, {64, 1, 0.18f}}, // Stand: start at 64, 1 frame
        {PlayerAnimState::Walk, {32, 8, 0.1f}},   // Walk: start at 32, 8 frames
        {PlayerAnimState::Jump, {42, 6, 0.1f}},   // Jump and Fall: start at 42, 6 frames
        {PlayerAnimState::Stance, {0, 4, 0.18f}}  // Stance: start at 0, 4 frames (unused for now)
    };

    // Player variables
    sf::Texture playerTexture;
    if (!playerTexture.loadFromFile("assets/platformer_sprites_pixelized.png"))
    { // Use the correct filename
        std::cerr << "Failed to load player texture!" << std::endl;
        return -1;
    }

    sf::Sprite playerSprite(playerTexture);
    playerSprite.setOrigin({FRAME_WIDTH / 2.f, FRAME_HEIGHT / 2.f}); // Bottom-center origin
    playerSprite.setPosition({400.f, 300.f});                        // Initial position

    sf::View gameView;                // Create view
    gameView.setSize({800.f, 600.f}); // Set size

    PlayerAnimState currentAnimState = PlayerAnimState::Stand;
    bool myIsOnGround = true; // Assume starting on ground
    float previousX = playerSprite.getPosition().x;
    bool facingRight = true;
    int currentFrame = 0;
    sf::Time animTimer = sf::Time::Zero;
    sf::Time stateChangeCooldownTimer = sf::Time::Zero; // Renamed for clarity

    // Other players
    uint32_t myPlayerId = static_cast<uint32_t>(-1); // Use uint32_t, init to invalid ID
    std::map<uint32_t, std::shared_ptr<sf::Sprite>> otherPlayers;
    std::map<uint32_t, PlayerAnimState> otherPlayersAnimState;
    std::map<uint32_t, int> otherPlayersCurrentFrame;
    std::map<uint32_t, sf::Time> otherPlayersAnimTimer;
    std::map<uint32_t, bool> otherPlayersFacingRight;

    sf::TcpSocket socket;
    sf::IpAddress serverIp = sf::IpAddress::LocalHost;
    unsigned short serverPort = 53000;
    bool connected = false;

    // Game loop
    sf::Clock clock;
    while (window.isOpen())
    {
        sf::Time dt = clock.restart();
        animTimer += dt;
        if (stateChangeCooldownTimer > sf::Time::Zero)
        {
            stateChangeCooldownTimer -= dt;
        }
        // Update timers for other players
        for (auto it = otherPlayersAnimTimer.begin(); it != otherPlayersAnimTimer.end(); ++it)
        {
            it->second += dt;
        }

        while (const std::optional event = window.pollEvent())
        {
            // "close requested" event: we close the window
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        if (!connected)
        {
            if (socket.connect(serverIp, serverPort, sf::seconds(1)) == sf::Socket::Status::Done)
            {
                std::cout << "Connected to server!" << std::endl;
                connected = true;
                socket.setBlocking(false); // Set non-blocking after connection
            }
            else
            {
            }
        }

        PlayerInputState currentInput; // Create fresh input state each frame
        if (window.hasFocus())
        {
            currentInput.left = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left);
            currentInput.right = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right);
            currentInput.up = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up);
            currentInput.down = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);
            currentInput.jump = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space); // Use Space for jump

            if (currentInput.right)
                facingRight = true;
            else if (currentInput.left)
                facingRight = false;
        }

        if (connected)
        {
            sf::Packet inputPacket;
            inputPacket << PacketType::PlayerInput << currentInput;
            socket.send(inputPacket); // TCP send doesn't need address/port here
        }
        if (connected)
        {
            sf::Packet packet;
            while (socket.receive(packet) == sf::Socket::Status::Done)
            { // Loop for TCP stream
                PacketType type;
                if (!(packet >> type))
                {
                    std::cerr << "Failed to read packet type" << std::endl;
                    continue;
                }

                switch (type)
                {
                case PacketType::Welcome:
                {
                    uint32_t receivedId; // Use different variable name
                    if (!(packet >> receivedId))
                    { /* Error */
                        continue;
                    }
                    myPlayerId = receivedId; // Store the received ID
                    std::cout << "Welcome! Your player ID is: " << myPlayerId << std::endl;
                    break;
                }
                case PacketType::PlayerState:
                {
                    uint32_t id;
                    float x, y;
                    bool isOnGround; // Only receive needed flags
                    if (!(packet >> id >> x >> y >> isOnGround))
                    {
                        std::cerr << "Failed to read player state" << std::endl;
                        continue;
                    }

                    if (id == myPlayerId)
                    {
                        myIsOnGround = isOnGround; // Update ground state
                        playerSprite.setPosition({x, y});
                    }
                    else
                    {
                        // 플레이어가 맵에 존재하는지 확인 및 없으면 생성
                        if (otherPlayers.find(id) == otherPlayers.end())
                        {
                            otherPlayers[id] = std::make_shared<sf::Sprite>(playerTexture);
                            // Set the origin for other players here, same as the main player
                            otherPlayers[id]->setOrigin({FRAME_WIDTH / 2.f, FRAME_HEIGHT / 2.f});
                            otherPlayersAnimState[id] = PlayerAnimState::Stand;
                            otherPlayersCurrentFrame[id] = 0;
                            otherPlayersAnimTimer[id] = sf::Time::Zero;
                            otherPlayersFacingRight[id] = true; // Default direction
                            std::cout << "Created other player sprite: " << id << std::endl;
                        }

                        // 이전 X 좌표 저장 (방향 비교용)
                        float otherPrevX = otherPlayers[id]->getPosition().x;

                        // 위치 업데이트
                        otherPlayers[id]->setPosition({x, y});

                        // 방향(FacingRight) 업데이트
                        if (x > otherPrevX)
                        {
                            otherPlayersFacingRight[id] = true; // 오른쪽으로 이동
                        }
                        else if (x < otherPrevX)
                        {
                            otherPlayersFacingRight[id] = false; // 왼쪽으로 이동
                        }

                        // 5. 애니메이션 상태 업데이트 (기존 로직)
                        bool &otherGroundState = myIsOnGround; // FIX: 다른 플레이어별 isOnGround 상태 저장 필요!
                        // 예: std::map<uint32_t, bool> otherPlayersOnGround; 사용 필요
                        // otherGroundState = otherPlayersOnGround[id];
                        // otherPlayersOnGround[id] = isOnGround;

                        PlayerAnimState targetOtherState = otherPlayersAnimState[id]; // 현재 상태 유지 기본값
                        if (!isOnGround)
                        { // FIX: 다른 플레이어의 isOnGround 값 사용해야 함!
                            targetOtherState = PlayerAnimState::Jump;
                        }
                        else if (std::abs(x - otherPrevX) > 0.1f)
                        { // 이동 감지 시 Walk (임계값 조정 가능)
                            targetOtherState = PlayerAnimState::Walk;
                        }
                        else
                        { // 땅에 있고 움직임 없으면 Stand
                            targetOtherState = PlayerAnimState::Stand;
                        }

                        // 상태 변경 시 프레임 리셋
                        if (otherPlayersAnimState[id] != targetOtherState)
                        {
                            otherPlayersCurrentFrame[id] = 0;
                            otherPlayersAnimTimer[id] = sf::Time::Zero;
                        }
                        otherPlayersAnimState[id] = targetOtherState;
                    }
                    break;
                }
                case PacketType::PlayerJoined:
                {
                    uint32_t id;
                    float x, y;
                    bool onGround; // Assume server sends initial state too
                    // FIX: Adjust parsing if server sends less/more data on join
                    if (!(packet >> id >> x >> y >> onGround))
                    { /* Error */
                        continue;
                    }
                    if (id != myPlayerId && otherPlayers.find(id) == otherPlayers.end())
                    { // Add check if already exists
                        otherPlayers[id] = std::make_shared<sf::Sprite>(playerTexture);
                        // Set the origin for other players here, same as the main player
                        otherPlayers[id]->setOrigin({FRAME_WIDTH / 2.f, FRAME_HEIGHT / 2.f});
                        otherPlayers[id]->setPosition({x, y});
                        otherPlayersAnimState[id] = onGround ? PlayerAnimState::Stand : PlayerAnimState::Jump; // Set initial state
                        otherPlayersCurrentFrame[id] = 0;
                        otherPlayersAnimTimer[id] = sf::Time::Zero;
                        otherPlayersFacingRight[id] = true;
                        std::cout << "Player " << id << " joined." << std::endl;
                    }
                    break;
                }
                case PacketType::PlayerLeft:
                {
                    uint32_t id;
                    if (!(packet >> id))
                    { /* Error */
                        continue;
                    }
                    if (id != myPlayerId)
                    {
                        if (otherPlayers.erase(id) > 0)
                        { // Remove and check if successful
                            otherPlayersAnimState.erase(id);
                            otherPlayersCurrentFrame.erase(id);
                            otherPlayersAnimTimer.erase(id);
                            otherPlayersFacingRight.erase(id);
                            std::cout << "Player " << id << " left." << std::endl;
                        }
                    }
                    break;
                }
                case PacketType::MapData:
                {

                    uint32_t width, height;
                    if (packet >> width >> height)
                    {
                        clientMapWidth = static_cast<int>(width);
                        clientMapHeight = static_cast<int>(height);
                        clientTileMap.resize(clientMapHeight, std::vector<int>(clientMapWidth));
                        mapShapes.clear(); // 이전 맵 데이터 클리어
                        bool parseOk = true;
                        for (int y = 0; y < clientMapHeight; ++y)
                        {
                            for (int x = 0; x < clientMapWidth; ++x)
                            {
                                int tileValue;
                                if (packet >> tileValue)
                                {
                                    clientTileMap[y][x] = tileValue;
                                    // 렌더링할 타일 Shape 생성 (벽만 그리기)
                                    if (clientTileMap[y][x] == 1)
                                    {
                                        sf::RectangleShape tileShape({CLIENT_TILE_SIZE, CLIENT_TILE_SIZE});
                                        tileShape.setPosition({x * CLIENT_TILE_SIZE, y * CLIENT_TILE_SIZE});
                                        tileShape.setFillColor(sf::Color::White); // 벽 색상
                                        mapShapes.push_back(tileShape);
                                    }
                                }
                                else
                                {
                                    parseOk = false;
                                    break;
                                }
                            }
                            if (!parseOk)
                                break;
                        }
                        if (parseOk)
                        {

                            mapLoaded = true;

                            std::cout << "Map data loaded (" << clientMapWidth << "x" << clientMapHeight << ")" << std::endl;
                        }
                        else
                        {

                            std::cerr << "Error: Could not parse map data content" << std::endl;
                        }
                    }
                    else
                    {
                        std::cerr << "Error: Could not parse map dimensions" << std::endl;
                    }

                    break;

                } // End MapData case
                default:
                    std::cerr << "Unknown packet type: " << static_cast<int>(type) << std::endl;
                    break;
                }
            } // End while receive

            // Handle TCP disconnection more explicitly if needed
            if (!socket.getRemoteAddress())
            { // FIX: Removed sf::IpAddress::None
                std::cerr << "Disconnected from server." << std::endl;
                connected = false;
                window.close(); // Or handle reconnection
            }
        }

        float currentX = playerSprite.getPosition().x; // Use sprite position
        PlayerAnimState targetState = currentAnimState;

        // Determine target state based on flags and movement
        if (!myIsOnGround)
        {
            targetState = PlayerAnimState::Jump;
        }
        else if (currentInput.left || currentInput.right)
        { // Using input for Walk state
            targetState = PlayerAnimState::Walk;
        }
        else
        {
            targetState = PlayerAnimState::Stand;
        }

        if (currentAnimState != targetState && stateChangeCooldownTimer <= sf::Time::Zero)
        {
            currentFrame = 0;
            animTimer = sf::Time::Zero;
            currentAnimState = targetState;
            stateChangeCooldownTimer = sf::seconds(STATE_CHANGE_COOLDOWN);

            // Update texture rect immediately on state change
            const AnimationData &newData = animData[currentAnimState];      // Use current state here
            int frameOverallIndex = newData.startFrameIndex + currentFrame; // Use currentFrame (0)
            int frameCol = frameOverallIndex % FRAMES_PER_ROW;
            int frameRow = frameOverallIndex / FRAMES_PER_ROW;
            playerSprite.setTextureRect(sf::IntRect({frameCol * FRAME_WIDTH, frameRow * FRAME_HEIGHT}, {FRAME_WIDTH, FRAME_HEIGHT}));
        }
        // previousX = currentX; // Move this to end of loop

        if (animData.count(currentAnimState))
        { // Check if state exists in map
            const AnimationData &currentData = animData.at(currentAnimState);
            if (animTimer >= sf::seconds(currentData.timePerFrame))
            {
                animTimer -= sf::seconds(currentData.timePerFrame);
                currentFrame = (currentFrame + 1) % currentData.frameCount;

                int frameOverallIndex = currentData.startFrameIndex + currentFrame;
                int frameCol = frameOverallIndex % FRAMES_PER_ROW;
                int frameRow = frameOverallIndex / FRAMES_PER_ROW;
                playerSprite.setTextureRect(sf::IntRect({frameCol * FRAME_WIDTH, frameRow * FRAME_HEIGHT}, {FRAME_WIDTH, FRAME_HEIGHT}));
            }
        }

        playerSprite.setScale({facingRight ? 1.f : -1.f, 1.f});

        for (auto &[id, spritePtr] : otherPlayers)
        {
            if (!spritePtr)
                continue;                    // Safety check
            sf::Sprite &sprite = *spritePtr; // Use reference for convenience
            if (otherPlayersAnimState.count(id) && animData.count(otherPlayersAnimState.at(id)))
            { // Check state exists
                PlayerAnimState otherState = otherPlayersAnimState.at(id);
                int &otherFrame = otherPlayersCurrentFrame.at(id);   // Use reference
                sf::Time &otherTimer = otherPlayersAnimTimer.at(id); // Use reference
                const AnimationData &otherData = animData.at(otherState);

                if (otherTimer >= sf::seconds(otherData.timePerFrame))
                {
                    otherTimer -= sf::seconds(otherData.timePerFrame);
                    otherFrame = (otherFrame + 1) % otherData.frameCount;

                    int frameOverallIndex = otherData.startFrameIndex + otherFrame;
                    int frameCol = frameOverallIndex % FRAMES_PER_ROW;
                    int frameRow = frameOverallIndex / FRAMES_PER_ROW;
                    sprite.setTextureRect(sf::IntRect({frameCol * FRAME_WIDTH, frameRow * FRAME_HEIGHT}, {FRAME_WIDTH, FRAME_HEIGHT}));
                }
                if (otherPlayersFacingRight.count(id))
                {
                    sprite.setScale({otherPlayersFacingRight.at(id) ? 1.f : -1.f, 1.f});
                }
            }
        }

        sf::Vector2f playerPos = playerSprite.getPosition();
        gameView.setCenter(playerPos);
        // (Optional camera clamping)
        if (mapLoaded)
        {
            float viewHalfWidth = gameView.getSize().x / 2.0f;
            float viewHalfHeight = gameView.getSize().y / 2.0f;
            float mapWidthPixels = clientMapWidth * CLIENT_TILE_SIZE;
            float mapHeightPixels = clientMapHeight * CLIENT_TILE_SIZE;
            float clampedX = std::max(viewHalfWidth, std::min(playerPos.x, mapWidthPixels - viewHalfWidth));
            float clampedY = std::max(viewHalfHeight, std::min(playerPos.y, mapHeightPixels - viewHalfHeight));
            gameView.setCenter({clampedX, clampedY});
        }

        window.clear(sf::Color::Black);
        window.setView(gameView); // Apply game view

        if (mapLoaded)
        {
            for (const auto &shape : mapShapes)
            {
                window.draw(shape);
            }
        }
        for (const auto &[id, spritePtr] : otherPlayers)
        {
            if (spritePtr)
                window.draw(*spritePtr);
        }
        window.draw(playerSprite);

        window.display();

        // Update previousX at the very end
        previousX = playerSprite.getPosition().x;

    } // End main game loop

    return 0;
}

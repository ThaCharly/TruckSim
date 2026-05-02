#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>
#include "Powertrain.hpp"
#include "Vehicle.hpp"
#include "HPatternShifter.hpp"
#include "SDLHapticDevice.hpp"
#include "AirBrakes.hpp"

using namespace TruckCore;

// Función auxiliar para dibujar gráfico DYNO (Curva de Potencia en vivo)
void drawDynoGraph(sf::RenderWindow& window, sf::Font& font, const EngineSpec& spec, float currentRPM, float currentHP, float maxAvailHP, float targetHP) {
    float x = 450.0f;
    float y = 290.0f;
    float width = 220.0f;
    float height = 150.0f;

    // Fondo del gráfico
    sf::RectangleShape bg(sf::Vector2f(width, height));
    bg.setPosition(x, y);
    bg.setFillColor(sf::Color(30, 30, 30));
    bg.setOutlineThickness(1.0f);
    bg.setOutlineColor(sf::Color(100, 100, 100));
    window.draw(bg);

    // Límites auto-escalables
    float maxRPM_plot = std::max(3000.0f, spec.cutoffRPM);
    float maxHP_plot = std::max(500.0f, targetHP * 1.5f);

    // Interpolar curva base para pintar la línea teórica ideal
    auto getBaseTorque = [&](float rpm) {
        if (spec.torqueCurve.empty()) return 1.0f;
        if (rpm <= spec.torqueCurve.front().first) return spec.torqueCurve.front().second;
        if (rpm >= spec.torqueCurve.back().first) return spec.torqueCurve.back().second;
        for (size_t i = 0; i < spec.torqueCurve.size() - 1; ++i) {
            if (rpm >= spec.torqueCurve[i].first && rpm <= spec.torqueCurve[i + 1].first) {
                float t = (rpm - spec.torqueCurve[i].first) / (spec.torqueCurve[i + 1].first - spec.torqueCurve[i].first);
                return spec.torqueCurve[i].second + t * (spec.torqueCurve[i + 1].second - spec.torqueCurve[i].second);
            }
        }
        return 0.0f;
    };

    // Línea verde de Potencia Máxima Teórica (100% Boost)
    sf::VertexArray curve(sf::LineStrip, 50);
    for (int i = 0; i < 50; ++i) {
        float rpm = (i / 49.0f) * maxRPM_plot;
        float maxTq = spec.maxTorque * getBaseTorque(rpm); 
        float hp = (maxTq * rpm) / 7120.8f;
        
        float px = x + (rpm / maxRPM_plot) * width;
        float py = y + height - std::clamp(hp / maxHP_plot, 0.0f, 1.0f) * height;
        curve[i].position = sf::Vector2f(px, py);
        curve[i].color = sf::Color(100, 200, 100, 150); 
    }
    window.draw(curve);

    // Punto de HP Disponible (varía por lag del turbo) -> Magenta
    float px = x + std::clamp(currentRPM / maxRPM_plot, 0.0f, 1.0f) * width;
    float pyAvail = y + height - std::clamp(maxAvailHP / maxHP_plot, 0.0f, 1.0f) * height;
    sf::CircleShape availDot(4.0f);
    availDot.setOrigin(4.0f, 4.0f);
    availDot.setPosition(px, pyAvail);
    availDot.setFillColor(sf::Color::Magenta);
    window.draw(availDot);

    // Punto de HP Entregado Real (varía por acelerador/carga) -> Cyan
    float pyActual = y + height - std::clamp(currentHP / maxHP_plot, 0.0f, 1.0f) * height;
    sf::CircleShape actualDot(3.0f);
    actualDot.setOrigin(3.0f, 3.0f);
    actualDot.setPosition(px, pyActual);
    actualDot.setFillColor(sf::Color::Cyan);
    window.draw(actualDot);

    // Títulos
    sf::Text title;
    title.setFont(font);
    title.setString("DYNO: HP Teo (Verde), Disp (Mag), Real (Cian)");
    title.setCharacterSize(10);
    title.setFillColor(sf::Color(180, 180, 180));
    title.setPosition(x, y - 15.0f);
    window.draw(title);
}

// Función auxiliar para dibujar barras
void drawBar(sf::RenderWindow& window, sf::Font& font, const std::string& label, float x, float y, float value, float maxVal, sf::Color color) {
    sf::Text text;
    text.setFont(font);
    text.setString(label);
    text.setCharacterSize(14);
    text.setFillColor(sf::Color::White);
    text.setPosition(x, y);
    window.draw(text);

    sf::RectangleShape bgBar(sf::Vector2f(250.0f, 15.0f));
    bgBar.setPosition(x + 120.0f, y + 2.0f);
    bgBar.setFillColor(sf::Color(50, 50, 50));
    window.draw(bgBar);

    float fillPct = std::clamp(value / maxVal, 0.0f, 1.0f);
    sf::RectangleShape fgBar(sf::Vector2f(250.0f * fillPct, 15.0f));
    fgBar.setPosition(x + 120.0f, y + 2.0f);
    fgBar.setFillColor(color);
    window.draw(fgBar);

    sf::Text valText;
    valText.setFont(font);
    valText.setString(std::to_string((int)value));
    valText.setCharacterSize(14);
    valText.setFillColor(sf::Color::White);
    valText.setPosition(x + 380.0f, y);
    window.draw(valText);
}

int main() {
    PowertrainSolver powertrain;
    VehiclePhysics vehicle;
    AirBrakeSystem airSystem;
    DriverInput driver; 
    HPatternShifter shifter; 
    
    // Levantamos el motor de vibración y lo inyectamos a la caja
    SDLHapticDevice haptics(0);
    shifter.setHapticDevice(&haptics);

    // --- CARGA DE MOTOR DESDE ARCHIVO JSON ---
    std::ifstream engineFile("../json/engines.json");
    if (!engineFile.is_open()) {
        std::cerr << "Error: No se encontro engines.json." << std::endl;
        return -1;
    }
    json j;
    engineFile >> j;
    
    int motorElegido = 1; // 1 = Scania V12 1800HP
    EngineTarget target;
    target.name = j[motorElegido]["name"];
    target.type = parseEngineType(j[motorElegido]["type"]);
    target.displacement = j[motorElegido]["displacement"];
    target.cylinders = j[motorElegido]["cylinders"];
    target.targetHP = j[motorElegido]["targetHP"];
    target.targetRPM = j[motorElegido]["targetRPM"];

    EngineSpec spec = EngineFactory::generateFromTarget(target);
    powertrain.swapEngine(spec);

    // --- CARGA DE CAJA DESDE ARCHIVO JSON ---
    std::ifstream gearboxFile("../json/gearboxes.json");
    if (!gearboxFile.is_open()) {
        std::cerr << "Error: No se encontro gearboxes.json." << std::endl;
        return -1;
    }
    json g;
    gearboxFile >> g;
    
    int cajaElegida = 0; // 0 = Eaton 12-speed
    GearboxTarget gTarget;
    gTarget.name = g[cajaElegida]["name"];
    for(auto& ratio : g[cajaElegida]["ratios"]) {
        gTarget.ratios.push_back(ratio.get<float>());
    }

    GearboxSpec gSpec = GearboxFactory::generateFromTarget(gTarget);
    powertrain.swapGearbox(gSpec);

    // --- CARGA DE DIFERENCIAL DESDE ARCHIVO JSON ---
    std::ifstream diffFile("../json/differentials.json");
    if (!diffFile.is_open()) {
        std::cerr << "Error: No se encontro differentials.json." << std::endl;
        return -1;
    }
    json d;
    diffFile >> d;
    
    int diffElegido = 1; // 1 = Eaton Open 3.08
    DifferentialTarget dTarget;
    dTarget.name = d[diffElegido]["name"];
    dTarget.type = parseDiffType(d[diffElegido]["type"]);
    dTarget.ratio = d[diffElegido]["ratio"];

    DifferentialSpec dSpec = DifferentialFactory::generateFromTarget(dTarget);
    powertrain.swapDifferential(dSpec);
    
    std::cout << "\n--- MOTOR CARGADO: " << target.name << " ---\n";
    std::cout << "--- CAJA CARGADA: " << gTarget.name << " ---\n";
    std::cout << "--- DIFF CARGADO: " << dTarget.name << " (Ratio: " << dTarget.ratio << " Eficiencia: " << dSpec.efficiency * 100 << "%) ---\n";
    std::cout << "Estado Físico: " 
              << (spec.feedback == EngineFeedback::Realista ? "REALISTA" : 
                 (spec.feedback == EngineFeedback::Exigido ? "EXIGIDO (Térmica alta)" : "EXPERIMENTAL (Fricción extrema)")) 
              << "\n\n";

    // Agrandamos un poco la ventana para que entre todo
    sf::RenderWindow window(sf::VideoMode(700, 650), "TruckSim - Telemetria Termodinamica");
    window.setFramerateLimit(144); 

    sf::Font font;
    if (!font.loadFromFile("../fonts/jetbrains_mono.ttf")) {
        std::cerr << "Error: Faltan las fuentes, loco. Tirá el jetbrains_mono.ttf en la carpeta." << std::endl;
        return -1;
    }

    sf::Clock dtClock;
    float physicsAccumulator = 0.0f;
    const float PHYSICS_STEP = 0.01f; 

    bool lastJoyUp = false;
    bool lastJoyDown = false;
    bool lastDpadUp = false;
    bool lastDpadDown = false;
    bool lastDpadRight = false;
    bool lastDpadLeft = false;
    
    // Toggle para Gatillo Derecho
    bool rtIsBrake = false;
    bool lastToggleBtn = false;

    // Memoria para el Force Feedback
    bool wasGrinding = false;
    bool wasMoneyShift = false;

    std::cout << "CONTROLES TECLADO:\n";
    std::cout << "[W] Acelerar | [S] Freno | [ESPACIO] Embrague | [FLECHAS UP/DOWN] Marchas | [K] Arranque\n";
    std::cout << "[E]/[D] Subir/Bajar Retardador | [R]/[F] Subir/Bajar Jake Brake\n";
    std::cout << "[T] o [Boton RB en Mando] Alternar Gatillo Derecho (Acelerador/Freno)\n\n";

    while (window.isOpen()) {
        float frameTime = dtClock.restart().asSeconds();
        if (frameTime > 0.25f) frameTime = 0.25f; 
        physicsAccumulator += frameTime;

        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
            }
            if (event.type == sf::Event::KeyPressed) {
                // Marchas dinámicas leyendo de la caja actual
                if (event.key.code == sf::Keyboard::Up && driver.shiftRequest < powertrain.getMaxGear()) driver.shiftRequest++;
                if (event.key.code == sf::Keyboard::Down && driver.shiftRequest > 0) driver.shiftRequest--;
                
                // Retardador (E/D)
                if (event.key.code == sf::Keyboard::E && driver.retarderLevel < 5) driver.retarderLevel++;
                if (event.key.code == sf::Keyboard::D && driver.retarderLevel > 0) driver.retarderLevel--;
                
                // Jake Brake (R/F)
                if (event.key.code == sf::Keyboard::R && driver.jakeBrakeLevel < 3) driver.jakeBrakeLevel++;
                if (event.key.code == sf::Keyboard::F && driver.jakeBrakeLevel > 0) driver.jakeBrakeLevel--;
            }
        }

        driver.throttle = sf::Keyboard::isKeyPressed(sf::Keyboard::W) ? 1.0f : 0.0f;
        driver.brake = sf::Keyboard::isKeyPressed(sf::Keyboard::S) ? 1.0f : 0.0f;
        driver.clutch = sf::Keyboard::isKeyPressed(sf::Keyboard::Space) ? 1.0f : 0.0f;
        driver.ignitionKey = sf::Keyboard::isKeyPressed(sf::Keyboard::K);

        // --- XBOX CONTROLLER ---
        if (sf::Joystick::isConnected(0)) {
            // Lógica de toggle
            bool toggleBtn = sf::Joystick::isButtonPressed(0, 5) || sf::Keyboard::isKeyPressed(sf::Keyboard::T); // RB o tecla T
            if (toggleBtn && !lastToggleBtn) {
                rtIsBrake = !rtIsBrake;
            }
            lastToggleBtn = toggleBtn;

            float lt = sf::Joystick::getAxisPosition(0, sf::Joystick::Z);
            float rt = sf::Joystick::getAxisPosition(0, sf::Joystick::R);

            if (lt > -99.0f || rt > -99.0f) {
                float joyClutch = std::clamp((lt + 100.0f) / 200.0f, 0.0f, 1.0f);
                float joyRT = std::clamp((rt + 100.0f) / 200.0f, 0.0f, 1.0f);
                
                if (joyClutch > 0.01f) driver.clutch = joyClutch;
                
                if (rtIsBrake) {
                    if (joyRT > 0.01f) driver.brake = joyRT;
                } else {
                    if (joyRT > 0.01f) driver.throttle = joyRT;
                }
            }

            if (sf::Joystick::isButtonPressed(0, 3)) driver.ignitionKey = true; // Y para arranque
            if (sf::Joystick::isButtonPressed(0, 2)) driver.brake = 1.0f;       // X para freno (si no hay gatillos/pedales)

            // === SELECTOR H-PATTERN ===
            // En SFML (Linux/Windows), el stick derecho suele usar los ejes U (Horiz) y V (Vert)
            float stickU = sf::Joystick::getAxisPosition(0, sf::Joystick::U) / 100.0f;
            float stickV = sf::Joystick::getAxisPosition(0, sf::Joystick::V) / 100.0f;
            
            bool r3Pressed = sf::Joystick::isButtonPressed(0, 9); // R3 (Stick Der. Click) para Alta/Baja
            bool neutralBtn = sf::Joystick::isButtonPressed(0, 4); // LB (Botón 4) para forzar Neutro

            shifter.update(stickU, stickV, r3Pressed, neutralBtn, driver.clutch);
            driver.shiftRequest = shifter.getRequestedGear();
            driver.fsmGrinding = shifter.isGrinding();

            // D-Pad para frenos
            float povY = sf::Joystick::getAxisPosition(0, sf::Joystick::PovY); // Retardador
            float povX = sf::Joystick::getAxisPosition(0, sf::Joystick::PovX); // Jake Brake
            
            bool dpadUp = (povY > 50.0f);
            bool dpadDown = (povY < -50.0f);
            bool dpadRight = (povX > 50.0f);
            bool dpadLeft = (povX < -50.0f);

            if (dpadUp && !lastDpadUp && driver.retarderLevel < 5) driver.retarderLevel++;
            if (dpadDown && !lastDpadDown && driver.retarderLevel > 0) driver.retarderLevel--;
            if (dpadRight && !lastDpadRight && driver.jakeBrakeLevel < 3) driver.jakeBrakeLevel++;
            if (dpadLeft && !lastDpadLeft && driver.jakeBrakeLevel > 0) driver.jakeBrakeLevel--;

            lastDpadUp = dpadUp;
            lastDpadDown = dpadDown;
            lastDpadRight = dpadRight;
            lastDpadLeft = dpadLeft;
        }

        // --- FÍSICA ---
        while (physicsAccumulator >= PHYSICS_STEP) {
            // 1. Sistema de aire (consume presión y roba torque al motor)
            airSystem.update(PHYSICS_STEP, powertrain.getState().engineRPM, driver.brake);

            // 2. El vehículo suma la fuerza de freno neumático a las cargas resistivas
            vehicle.update(PHYSICS_STEP, powertrain.getState(), airSystem.getBrakingFactor());

            // 3. Inyectamos las cargas físicas y el peso del compresor a la caja
            powertrain.update(PHYSICS_STEP, driver, vehicle.getLoadTorque(), vehicle.getEquivalentInertia(), airSystem.getCompressorTorque());
            
            physicsAccumulator -= PHYSICS_STEP;
        }

        const TruckState& state = powertrain.getState();

        // --- FEEDBACK HÁPTICO CENTRALIZADO ---
        if (state.isMoneyShift && !wasMoneyShift) {
            // Rebaje asesino (Flanco de subida) -> Terremoto en el control
            haptics.rumble(1.0f, 1.0f, 800); 
        } else if (state.isGrinding && !wasGrinding && !state.isMoneyShift) {
            // Raspado normal (Flanco de subida) -> Pulso largo continuo
            haptics.rumble(0.6f, 1.0f, 10000); 
        } else if (!state.isGrinding && wasGrinding) {
            // Zafó del raspado (Flanco de bajada) -> Corta la vibración
            haptics.rumble(0.0f, 0.0f, 0); 
        }
        
        wasGrinding = state.isGrinding;
        wasMoneyShift = state.isMoneyShift;

        // --- DIBUJADO ---
        window.clear(sf::Color(20, 20, 20));

        sf::Text header;
        header.setFont(font);
        
        std::string actualGearStr = (state.currentGear == 0 ? "N" : (state.currentGear == -1 ? "R" : std::to_string(state.currentGear)));
        std::string targetGearStr = (driver.shiftRequest == 0 ? "N" : (driver.shiftRequest == -1 ? "R" : std::to_string(driver.shiftRequest)));
        std::string rangeStr = shifter.isHighRange() ? " [HIGH RANGE]" : " [LOW RANGE]";
        
        if (state.isMoneyShift) {
            header.setString("MARCHA: " + actualGearStr + rangeStr + " [¡REBAJE DESTRUCTIVO!]");
            header.setFillColor(sf::Color::Magenta);
        } else if (state.isGrinding) {
            header.setString("MARCHA: " + actualGearStr + rangeStr + " [¡RASCANDO CAJA!]");
            header.setFillColor(sf::Color::Red);
        } else if (state.currentGear != driver.shiftRequest) {
            header.setString("MARCHA: " + actualGearStr + " [Sincronizando " + targetGearStr + "]");
            header.setFillColor(sf::Color(255, 165, 0));
        } else {
            header.setString("MARCHA: " + actualGearStr);
            header.setFillColor(sf::Color::Yellow);
        }
        
        header.setCharacterSize(24);
        header.setPosition(40, 20);
        window.draw(header);

        if (state.engineRPM < 10.0f) {
            sf::Text stallText;
            stallText.setFont(font);
            stallText.setString("MOTOR CALADO - MANTENE [Y] PARA ARRANQUE");
            stallText.setCharacterSize(16);
            stallText.setFillColor(sf::Color::Red);
            stallText.setPosition(40, 50);
            window.draw(stallText);
        }

        // --- BARRAS ---
        // Escala dinámica del tacómetro según el motor
        drawBar(window, font, "RPM Motor:", 40, 80, state.engineRPM, std::max(3000.0f, spec.cutoffRPM), sf::Color(0, 150, 255));
        drawBar(window, font, "Turbo (Bar):", 40, 110, state.boostPressure * 100.0f, 250.0f, sf::Color(255, 100, 0));
        drawBar(window, font, "Temp Motor:", 40, 140, state.engineTemp, 120.0f, sf::Color(220, 20, 60)); 
        drawBar(window, font, "Temp Caja:", 40, 170, state.transmissionTemp, 150.0f, sf::Color(139, 69, 19)); 
        drawBar(window, font, "Salida RPM:", 40, 200, state.outputRPM, 1000.0f, sf::Color(150, 150, 150));
        drawBar(window, font, "Integridad:", 40, 230, state.gearboxHealth * 100.0f, 100.0f, sf::Color::Green);
        
        // La frutilla de la torta
        drawBar(window, font, "Velocidad (km/h):", 40, 260, vehicle.getSpeedKmh(), 160.0f, sf::Color(0, 255, 128));

        // --- NUEVA TELEMETRÍA DE FUERZAS ---
        drawBar(window, font, "Torque Max (Nm):", 40, 290, state.maxAvailableTorque, std::max(3000.0f, spec.maxTorque * 1.5f), sf::Color(200, 200, 50));
        drawBar(window, font, "Torque Carga(Nm):", 40, 320, state.loadTorqueAtEngine, std::max(3000.0f, spec.maxTorque * 1.5f), sf::Color(200, 100, 200));
        drawBar(window, font, "HP Disp. Max:", 40, 350, state.maxAvailableHP, std::max(500.0f, target.targetHP * 1.5f), sf::Color(255, 50, 255));
        drawBar(window, font, "HP Entregados:", 40, 380, state.engineHP, std::max(500.0f, target.targetHP * 1.5f), sf::Color(50, 255, 255));

        // --- SISTEMA DE AIRE Y TERMODINÁMICA DE FRENOS ---
        drawBar(window, font, "Presion Aire(PSI):", 40, 410, airSystem.getPressurePSI(), 120.0f, sf::Color(100, 200, 255));
        
        sf::Color brakeColor = vehicle.areBrakesDeformed() ? sf::Color::Magenta : sf::Color(255, 69, 0);
        drawBar(window, font, "Temp Frenos (C):", 40, 440, vehicle.getBrakeTemp(), 700.0f, brakeColor);

        // --- STATUS FRENOS AUXILIARES ---
        sf::Text brakeText;
        brakeText.setFont(font);
        brakeText.setCharacterSize(16);
        brakeText.setFillColor(sf::Color::Cyan);
        brakeText.setString("RETARDADOR: Nivel " + std::to_string(driver.retarderLevel) + "/5  |  JAKE BRAKE: Nivel " + std::to_string(driver.jakeBrakeLevel) + "/3");
        brakeText.setPosition(40, 470); 
        window.draw(brakeText);

        // --- STATUS PEDALES Y ALERTAS ---
        sf::Text inputsText;
        inputsText.setFont(font);
        std::string modoRT = rtIsBrake ? "[RT = FRENO]" : "[RT = ACEL]";
        inputsText.setString("Acel: " + std::to_string((int)(driver.throttle * 100)) + "% | Freno: " + std::to_string((int)(driver.brake * 100)) + "% | Embrg: " + std::to_string((int)(driver.clutch * 100)) + "%  " + modoRT);
        inputsText.setCharacterSize(16);
        inputsText.setFillColor(sf::Color::White);
        inputsText.setPosition(40, 500); 
        window.draw(inputsText);

        if (airSystem.isSpringBrakeEngaged()) {
            sf::Text warningText;
            warningText.setFont(font);
            warningText.setString("!!! ALERTA: SPRING BRAKES BLOQUEADOS (<60 PSI) !!!");
            warningText.setCharacterSize(18);
            warningText.setFillColor(sf::Color::Red);
            warningText.setPosition(40, 530);
            window.draw(warningText);
        }
        
        // Alerta dinámica: si estás por encima de 450 pero no llegaste a deformar
        if (vehicle.getBrakeTemp() > 450.0f && !vehicle.areBrakesDeformed()) {
            sf::Text glazeText;
            glazeText.setFont(font);
            glazeText.setString("! FRENOS CRISTALIZADOS (FADING EXTREMO) - ENFRIAR !");
            glazeText.setCharacterSize(18);
            glazeText.setFillColor(sf::Color(255, 140, 0)); // Naranja
            glazeText.setPosition(40, 560);
            window.draw(glazeText);
        } else if (vehicle.areBrakesDeformed()) {
            sf::Text deadText;
            deadText.setFont(font);
            deadText.setString("!!! FRENOS DEFORMADOS/DESTRUIDOS (PERDIDA TOTAL) !!!");
            deadText.setCharacterSize(18);
            deadText.setFillColor(sf::Color::Magenta);
            deadText.setPosition(40, 560);
            window.draw(deadText);
        }

        // --- GRÁFICO DYNO EN VIVO ---
        drawDynoGraph(window, font, spec, state.engineRPM, state.engineHP, state.maxAvailableHP, target.targetHP);

        window.display();
    }

    return 0;
}
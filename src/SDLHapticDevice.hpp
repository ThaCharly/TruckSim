#pragma once
#include "HPatternShifter.hpp"
#include <SDL2/SDL.h>
#include <iostream>

namespace TruckCore {

class SDLHapticDevice : public IHapticDevice {
private:
    SDL_GameController* controller = nullptr;
    bool isInitialized = false;

public:
    SDLHapticDevice(int deviceIndex = 0) {
        // Inicializamos SOLO el subsistema de GameController de SDL
        // SFML sigue manejando todo el resto.
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
            std::cerr << "Error crítico SDL: No se pudo levantar el subsistema - " << SDL_GetError() << "\n";
            return;
        }

        // Abrimos el primer joystick que encuentre
        controller = SDL_GameControllerOpen(deviceIndex);
        if (!controller) {
            std::cerr << "Advertencia SDL: No se pudo enganchar el control para vibración - " << SDL_GetError() << "\n";
            return;
        }

        isInitialized = true;
        std::cout << "SDL Haptics: Motores listos para romper todo en el dispositivo " << deviceIndex << "\n";
    }

    ~SDLHapticDevice() {
        if (controller) {
            SDL_GameControllerClose(controller);
        }
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }

    void rumble(float lowFreq, float highFreq, int durationMs) override {
        if (!isInitialized || !controller) return;

        // SDL espera valores de 16 bits sin signo (0 a 65535) para el PWM de los motores.
        // Mapeamos nuestro float [0.0, 1.0] a ese rango de hardware.
        Uint16 low_pwm = static_cast<Uint16>(std::clamp(lowFreq, 0.0f, 1.0f) * 65535.0f);
        Uint16 high_pwm = static_cast<Uint16>(std::clamp(highFreq, 0.0f, 1.0f) * 65535.0f);

        // Le pegamos directo a los motores
        SDL_GameControllerRumble(controller, low_pwm, high_pwm, durationMs);
    }
};

} // namespace TruckCore
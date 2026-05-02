#pragma once
#include <cmath>

namespace TruckCore {

class IHapticDevice {
public:
    virtual ~IHapticDevice() = default;
    virtual void rumble(float lowFreq, float highFreq, int durationMs) = 0;
};

class HPatternShifter {
private:
    bool highRange = false;
    bool stickInCenter = true;
    int currentSlot = 0; // 0 = Neutro, 1-6 = Posiciones físicas
    int requestedGear = 0; 
    bool isGrindingState = false;
    
    float clutchThreshold = 0.65f; 
    IHapticDevice* haptics = nullptr;
    bool lastR3 = false;
    bool lastNeutralBtn = false;

    // FSM: Mapeo espacial del stick al H-Pattern (Ignorando reversa por ahora)
    int getZone(float u, float v) {
        // Zona central (Volviendo al medio por el resorte del joystick)
        if (std::abs(u) < 0.3f && std::abs(v) < 0.3f) return 0;

        if (u < -0.4f) { // Columna Izquierda
            if (v < -0.5f) return 1; // 1ra (Arriba-Izq)
            if (v > 0.5f) return 2;  // 2da (Abajo-Izq)
        } else if (u > 0.4f) { // Columna Derecha
            if (v < -0.5f) return 5; // 5ta (Arriba-Der)
            if (v > 0.5f) return 6;  // 6ta (Abajo-Der)
        } else if (std::abs(u) < 0.3f) { // Columna Centro
            if (v < -0.5f) return 3; // 3ra (Arriba-Medio)
            if (v > 0.5f) return 4;  // 4ta (Abajo-Medio)
        }
        
        return -99; // Limbo (bordes o diagonales sucias)
    }

public:
    void setHapticDevice(IHapticDevice* device) { haptics = device; }

    void update(float joyU, float joyV, bool r3Pressed, bool neutralBtn, float clutchInput) {
        int currentZone = getZone(joyU, joyV);
        isGrindingState = false;

        // Toggle de rango HIGH/LOW con R3
        if (r3Pressed && !lastR3) {
            if (currentSlot == 0 || clutchInput >= clutchThreshold) {
                highRange = !highRange;
            }
        }
        lastR3 = r3Pressed;

        // Botón de Neutro manual (necesario porque el stick vuelve solo al centro)
        if (neutralBtn && !lastNeutralBtn) {
            currentSlot = 0;
            requestedGear = 0;
        }
        lastNeutralBtn = neutralBtn;

        if (currentZone == 0) {
            // El stick volvió al centro físico. 
            // Marcamos que pasó por el medio, pero NO desengranamos la marcha actual.
            stickInCenter = true;
        } else if (currentZone != -99) {
            // El usuario empujó el stick a una marcha física
            if (currentZone != currentSlot) {
                
                // Solo dejamos que entre si vino desde el centro limpio
                if (stickInCenter) {
                    if (clutchInput >= clutchThreshold) {
                        // Cambio exitoso
                        currentSlot = currentZone;
                        requestedGear = currentZone + (highRange ? 6 : 0);
                        stickInCenter = false; // Ya no está en el centro
                    } else {
                        // Rascada por no pisar embrague
                        isGrindingState = true;
                        requestedGear = 0;
                        currentSlot = 0; // La caja escupe la marcha
                    }
                } else {
                    // Rascada por cruzar la palanca de un cambio a otro sin pasar por el medio
                    isGrindingState = true;
                    requestedGear = 0;
                    currentSlot = 0;
                }
            }
        }

        }

    [[nodiscard]] int getRequestedGear() const { return requestedGear; }
    [[nodiscard]] bool isGrinding() const { return isGrindingState; }
    [[nodiscard]] bool isHighRange() const { return highRange; }
};

} // namespace TruckCore
#pragma once
#include <algorithm>
#include <cmath>

namespace TruckCore {

class AirBrakeSystem {
private:
    float pressurePSI = 120.0f;       
    float maxPressurePSI = 120.0f;    
    float minSafePressurePSI = 60.0f; 
    bool springBrakesEngaged = false; 
    
    float currentCompressorTorque = 0.0f;
    float currentBrakingFactor = 0.0f;

public:
    // dt: Delta time, engineRPM: RPM actuales del motor, brakeInput: Eje de freno (0.0 a 1.0)
    void update(float dt, float engineRPM, float brakeInput) {
        // 1. Compresor acoplado mecánicamente
        float compressorEfficiency = std::clamp(engineRPM / 1200.0f, 0.0f, 1.0f);
        
        // El gobernador corta el compresor si el tanque está lleno (deja de robar HP)
        if (pressurePSI >= maxPressurePSI) {
            currentCompressorTorque = 0.0f;
            pressurePSI = maxPressurePSI;
        } else {
            // Roba torque en base a qué tan rápido gira
            currentCompressorTorque = 12.0f * compressorEfficiency;
            float airFillRate = 1.5f * compressorEfficiency; // PSI/s (carga lenta y realista)
            pressurePSI += airFillRate * dt;
        }

        // 2. Consumo de aire por purga (pedal)
        if (brakeInput > 0.05f) {
            // Se gasta aire más rápido de lo que carga, pero sin vaciarse al instante
            float airConsumption = brakeInput * 5.0f; // PSI/s
            pressurePSI -= airConsumption * dt;
        }

        // 3. Falla de seguridad (Spring Brakes) con ciclo de histéresis
        if (pressurePSI < minSafePressurePSI) {
            springBrakesEngaged = true;
        } else if (pressurePSI > minSafePressurePSI + 10.0f) {
            // Requiere sobrepasar el umbral por 10 PSI para destrabar mecánicamente
            springBrakesEngaged = false; 
        }

        // 4. Salida de fuerza física
        if (springBrakesEngaged) {
            currentBrakingFactor = 1.0f; // Bloqueo mecánico total
        } else {
            // Si te estás quedando sin aire (ej. 70 PSI), el freno pierde mordiente
            float brakeEfficiency = std::clamp(pressurePSI / maxPressurePSI, 0.0f, 1.0f);
            currentBrakingFactor = brakeInput * brakeEfficiency;
        }
    }

    [[nodiscard]] float getPressurePSI() const { return std::max(0.0f, pressurePSI); }
    [[nodiscard]] bool isSpringBrakeEngaged() const { return springBrakesEngaged; }
    [[nodiscard]] float getCompressorTorque() const { return currentCompressorTorque; }
    [[nodiscard]] float getBrakingFactor() const { return currentBrakingFactor; }
};

} // namespace TruckCore
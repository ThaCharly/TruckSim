#pragma once
#include <cmath>
#include <algorithm>
#include "Powertrain.hpp" 

namespace TruckCore {

class VehiclePhysics {
public:
    // --- PARÁMETROS FÍSICOS DEL CAMIÓN ---
    float mass = 20000.0f;           // kg (40 toneladas: tractora + semi cargado)
    float wheelRadius = 0.54f;       // metros (medida estándar de un 315/80R22.5)
    float dragCoefficient = 0.55f;   // Cd típico de un camión chato
    float frontalArea = 9.0f;       // m^2 (área brutal de choque frontal)
    float airDensity = 1.225f;       // kg/m^3 (a nivel del mar, ~20°C)
    float rollingResistance = 0.006f;// Asfalto en buen estado
    float gravity = 9.81f;           // m/s^2
    float slopeAngle = 0.0f;         // Radianes (positivo = repecho/subida)
    
    // Inercia rotacional base de las ruedas (pesan una tonelada en total aprox)
    float staticWheelsInertia = 150.0f; 
    
    private:
    float currentSpeed = 0.0f; // m/s
    float brakeTemp = 15.0f;   // Temperatura de los frenos (°C)
    bool brakesDeformed = false; // Punto de no retorno absoluto (>650°C)
    float calculatedLoadTorque = 0.0f;
    float calculatedInertia = 0.0f;

public:
    VehiclePhysics() {}

    void update(float dt, const TruckState& pwtState, float brakingFactor) {
        // 1. Cinemática: PowertrainSolver ya nos da las RPM de la Rueda (incluye diferencial)
        float wheelOmega = pwtState.outputRPM * (2.0f * PI) / 60.0f;
        
        currentSpeed = wheelOmega * wheelRadius;

        // Clip de velocidad para evitar cálculos espurios si retrocede por error de coma flotante
        if (currentSpeed < 0.01f) currentSpeed = 0.0f;

        // 2. Fuerzas Resistivas
        float fAero = 0.5f * airDensity * dragCoefficient * frontalArea * (currentSpeed * currentSpeed);

        // Transición suave pero con resistencia desde 0 para evitar que vibre y permitir enganche
        float fRoll = rollingResistance * mass * gravity * std::cos(slopeAngle);
        float lowSpeedBoost = 1.0f - std::exp(-currentSpeed * 5.0f);
        fRoll *= (0.3f + 0.7f * lowSpeedBoost);

        float fGrad = mass * gravity * std::sin(slopeAngle);

        // Fuerza de frenado neumático (capacidad de ~0.2G de deceleración respecto a la masa)
        float maxBrakeForce = mass * gravity * 0.2f; 
        float attemptedBrakeForce = brakingFactor * maxBrakeForce;

        // --- TERMODINÁMICA DE FRENOS (P = F * v) ---
        float brakePower = attemptedBrakeForce * currentSpeed; 
        
        // Inercia térmica: el metal frío absorbe energía sin subir tanto la temperatura superficial.
        // Superados los 150°C, el calentamiento se vuelve mucho más agresivo.
        float heatFactor = (brakeTemp < 150.0f) ? 0.000015f : 0.00003f;
        brakeTemp += (brakePower * heatFactor) * dt; 

        float coolingFactor = 0.005f + (currentSpeed * 0.0002f); 
        brakeTemp -= (brakeTemp - 20.0f) * coolingFactor * dt;

        // Punto de no retorno REAL: Deformación del disco
        if (brakeTemp > 700.0f) {
            brakesDeformed = true;
        }

        // --- CURVA DINÁMICA DE FADING ---
        float fadeMultiplier = 1.0f;

        if (brakesDeformed) {
            // Discos alabados y pastillas desintegradas. Daño permanente.
            fadeMultiplier = 0.05f; // 5% de fuerza residual
        } else {
            if (brakeTemp > 450.0f) {
                // Glazing severo (450°C - 650°C). Cae de 40% a 10%. 
                // Es dinámico: si dejás que baje de 450°C, recuperás algo de freno.
                float t = (brakeTemp - 450.0f) / 200.0f; // Normalizado 0..1 en este tramo
                fadeMultiplier = std::clamp(0.40f - (0.30f * t), 0.10f, 0.40f);
            } else if (brakeTemp > 250.0f) {
                // Fading fuerte (250°C - 450°C). Cae de 100% a 40%.
                float t = (brakeTemp - 250.0f) / 200.0f;
                fadeMultiplier = std::clamp(1.0f - (0.60f * t), 0.40f, 1.0f);
            }
            // Debajo de 250°C el multiplicador queda intacto en 1.0f.
        }

        float actualBrakeForce = attemptedBrakeForce * fadeMultiplier;

        // El freno térmicamente afectado se suma a la resistencia
        float totalResistiveForce = fAero + fRoll + fGrad + actualBrakeForce;

        // 3. Torque Resultante en las Ruedas
        calculatedLoadTorque = totalResistiveForce * wheelRadius;

        // 4. Inercia Equivalente (I = m*r^2) reducida al 30% por compliance del chasis/ruedas
        calculatedInertia = (mass * wheelRadius * wheelRadius) * 0.5f + staticWheelsInertia;
    }

    // Getters limpios para el solver principal
    [[nodiscard]] float getLoadTorque() const { return calculatedLoadTorque; }
    [[nodiscard]] float getEquivalentInertia() const { return calculatedInertia; }
    [[nodiscard]] float getSpeedKmh() const { return currentSpeed * 3.6f; }
    [[nodiscard]] float getSpeedMs() const { return currentSpeed; }
    [[nodiscard]] float getBrakeTemp() const { return brakeTemp; }
    [[nodiscard]] bool areBrakesDeformed() const { return brakesDeformed; }
};

} // namespace TruckCore
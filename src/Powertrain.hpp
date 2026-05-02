#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include "Gearbox.hpp"
#include "Differential.hpp"
#include "json.hpp"
#include <fstream>


using json = nlohmann::json;

namespace TruckCore {

constexpr float PI = 3.14159265358979323846f;

// ---------------------------------------------------------
// 1. DATA-DRIVEN ENGINE SPECIFICATIONS
// ---------------------------------------------------------
enum class EngineFeedback {
    Realista,
    Exigido,
    Experimental
};

struct TurboSpec {
    bool enabled = false;
    float maxBoost = 2.5f;           // Bar
    float inertia = 0.001f;
    float spoolRate = 5.0f;
    float maxTurbineOmega = 10500.0f; // rad/s (~100k RPM)
    float kExhaust = 24.0f;
    float kCompressor = 9.0e-8f;
};

struct EngineSpec {
    EngineFeedback feedback = EngineFeedback::Realista;
    float maxTorque = 2100.0f;       // Nm
    float maxRPM = 2500.0f;
    float idleRPM = 600.0f;
    float cutoffRPM = 2600.0f;
    float engineInertia = 4.5f;      // kg*m^2
    float displacement = 13.0f;      // Liters
    int cylinderCount = 6;
    
    // Fricción: base estática + drag lineal + drag aerodinámico interno
    float frictionBase = 30.0f;
    float frictionLinear = 0.03f;
    float frictionQuadratic = 0.000015f;
    
    // Curva de torque normalizada [RPM, Factor 0..1]
    std::vector<std::pair<float, float>> torqueCurve; 
    TurboSpec turbo;
};

// ---------------------------------------------------------
// 1.5. PROCEDURAL ENGINE FACTORY
// ---------------------------------------------------------
enum class EngineType { HeavyDiesel, LightDiesel, GasolineNA, GasolineTurbo };

inline EngineType parseEngineType(const std::string& typeStr) {
    if (typeStr == "heavy_diesel" || typeStr == "diesel") return EngineType::HeavyDiesel;
    if (typeStr == "light_diesel") return EngineType::LightDiesel;
    if (typeStr == "gasoline_na") return EngineType::GasolineNA;
    return EngineType::GasolineTurbo;
}

struct EngineTarget {
    std::string name;
    EngineType type;
    float displacement;
    int cylinders;
    float targetHP;
    float targetRPM;
};

namespace EngineFactory {

inline EngineSpec generateFromTarget(const EngineTarget& target) {
    EngineSpec s;
    s.displacement = target.displacement;
    s.cylinderCount = target.cylinders;

   // 1. Relación física P = T * w. Despejando Nm para el HP deseado:
        float torqueAtTargetRPM = (target.targetHP * 7120.8f) / target.targetRPM;

        // El torque pico SIEMPRE es mayor que el torque a potencia máxima.
        float torqueMultiplier = (target.type == EngineType::HeavyDiesel) ? 1.30f : 
                                 (target.type == EngineType::LightDiesel) ? 1.20f : 1.15f;
        s.maxTorque = torqueAtTargetRPM * torqueMultiplier;
        
        // RPMs: en lugar de escalar porcentualmente, damos un margen fijo (headroom).
        if (target.type == EngineType::HeavyDiesel) {
            s.maxRPM = target.targetRPM + 200.0f;
            s.cutoffRPM = s.maxRPM + 150.0f; // Corte brusco muy cerquita
            s.idleRPM = 600.0f;
        } else if (target.type == EngineType::LightDiesel) {
            s.maxRPM = target.targetRPM + 400.0f;
            s.cutoffRPM = s.maxRPM + 200.0f;
            s.idleRPM = 800.0f;
        } else { // Nafteros
            s.maxRPM = target.targetRPM + 500.0f;
            s.cutoffRPM = s.maxRPM + 250.0f;
            s.idleRPM = 850.0f;
        }

        // 2. Validación de eficiencia volumétrica (Torque Específico: Nm/Litro)
        float specificTorque = s.maxTorque / s.displacement;
        
        // Límite físico de la vida real para un atmosférico
        float baseSpecificTorque = (target.type == EngineType::HeavyDiesel) ? 120.0f : 
                                   (target.type == EngineType::LightDiesel) ? 100.0f : 105.0f; 
    
    float requiredBoost = 0.0f;
    if (specificTorque > baseSpecificTorque) {
        // 1 bar extra de presión duplica la masa de aire.
        requiredBoost = (specificTorque / baseSpecificTorque) - 1.0f;
    }

    // Determinar feedback físico
    if (requiredBoost <= 0.0f && target.type == EngineType::GasolineNA) {
        s.feedback = EngineFeedback::Realista;
        s.turbo.enabled = false;
    } else if (requiredBoost <= 1.5f) {
        s.feedback = EngineFeedback::Realista;
        s.turbo.enabled = true;
    } else if (requiredBoost <= 3.0f) {
        s.feedback = EngineFeedback::Exigido;
        s.turbo.enabled = true;
    } else {
        s.feedback = EngineFeedback::Experimental; // Genera fricción y calor masivo
        s.turbo.enabled = true;
    }

    // Ajuste dinámico del hardware del Turbo
    if (s.turbo.enabled) {
        s.turbo.maxBoost = std::max(requiredBoost, 0.5f);
        s.turbo.inertia = 0.001f * (1.0f + requiredBoost * 0.8f); // Más presión = caracol más grande = más lag
        s.turbo.spoolRate = std::max(1.0f, 6.0f - requiredBoost); 
        s.turbo.maxTurbineOmega = 12000.0f + (requiredBoost * 2000.0f);
    }

    // 3. Fricción y Térmica (Castigamos a los motores absurdos)
    float stressFactor = (s.feedback == EngineFeedback::Experimental) ? 2.5f : 
                         (s.feedback == EngineFeedback::Exigido) ? 1.5f : 1.0f;

    s.frictionBase = s.displacement * 2.0f * stressFactor;
    s.frictionLinear = 0.02f * stressFactor;
    s.frictionQuadratic = 0.00001f * stressFactor;

    float inertiaFactor = (target.type == EngineType::HeavyDiesel) ? 1.6f : 
                              (target.type == EngineType::LightDiesel) ? 1.1f : 0.8f;
        if (s.feedback != EngineFeedback::Realista) inertiaFactor *= 0.8f; // Volante alivianado
        s.engineInertia = s.displacement * 0.3f * inertiaFactor;

        // 4. Curva de torque anclada a las matemáticas
        float peakTorqueRPM = target.targetRPM * ((target.type == EngineType::HeavyDiesel || target.type == EngineType::LightDiesel) ? 0.6f : 0.75f);
    float torqueRatioAtTarget = 1.0f / torqueMultiplier; // Caída exacta para pegar en el HP blanco

    s.torqueCurve = {
        { s.idleRPM * 0.8f, 0.40f },
        { peakTorqueRPM * 0.7f, 0.85f },
        { peakTorqueRPM, 1.00f },       // Pico de torque
        { target.targetRPM, torqueRatioAtTarget }, // Intersección matemática del HP
        { s.maxRPM, torqueRatioAtTarget * 0.8f },
        { s.cutoffRPM, 0.20f }
    };

    return s;
}

inline std::vector<EngineTarget> loadEnginesFromJson(const std::string& filepath) {
    std::vector<EngineTarget> engines;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: No se pudo abrir " << filepath << "\n";
        return engines;
    }

    json j;
    file >> j;

    for (const auto& item : j) {
        EngineTarget t;
        t.name = item.value("name", "Unknown Engine");
        t.type = parseEngineType(item.value("type", "diesel"));
        t.displacement = item.value("displacement", 2.0f);
        t.cylinders = item.value("cylinders", 4);
        t.targetHP = item.value("targetHP", 100.0f);
        t.targetRPM = item.value("targetRPM", 3000.0f);
        engines.push_back(t);
    }
    return engines;
}

} // namespace EngineFactory

namespace EnginePresets {
    inline EngineSpec getHeavyDiesel() {
        EngineSpec s;
        s.maxTorque = 2100.0f;
        s.maxRPM = 2500.0f;
        s.idleRPM = 600.0f;
        s.cutoffRPM = 2600.0f;
        s.engineInertia = 4.5f; 
        s.displacement = 13.0f;
        s.cylinderCount = 6;
        
        s.torqueCurve = {
            {400.0f,  0.4f},
            {800.0f,  0.8f},
            {1100.0f, 1.0f},
            {1500.0f, 1.0f},
            {2000.0f, 0.7f},
            {2600.0f, 0.4f}
        };
        
        s.turbo.enabled = true;
        s.turbo.maxBoost = 2.5f;
        s.turbo.inertia = 0.001f;
        s.turbo.spoolRate = 5.0f;
        return s;
    }

    inline EngineSpec getHighRevGas() {
        EngineSpec s;
        s.maxTorque = 450.0f; 
        s.maxRPM = 9000.0f;
        s.idleRPM = 900.0f;
        s.cutoffRPM = 9200.0f;
        s.engineInertia = 0.3f; // Volante de inercia súper ligero
        s.displacement = 3.0f;
        s.cylinderCount = 8;
        
        s.frictionBase = 15.0f;
        s.frictionLinear = 0.01f;
        s.frictionQuadratic = 0.000005f;
        
        s.torqueCurve = {
            {800.0f,  0.3f},
            {3000.0f, 0.6f},
            {6000.0f, 1.0f},
            {8500.0f, 0.9f},
            {9200.0f, 0.2f}
        };
        
        s.turbo.enabled = false; // Aspirado Natural
        return s;
    }

    inline EngineSpec getExtremeEngine() {
        EngineSpec s;
        s.maxTorque = 100000.0f; // Brutalidad pura, te parte el cardán
        s.maxRPM = 5000.0f;
        s.idleRPM = 800.0f;
        s.cutoffRPM = 5200.0f;
        s.engineInertia = 8.0f; // Necesita masa para no saltar a infinito en 1 frame
        s.displacement = 50.0f; 
        s.cylinderCount = 24;
        
        s.torqueCurve = {
            {400.0f,  0.5f},
            {2000.0f, 1.0f},
            {4000.0f, 1.0f},
            {5200.0f, 0.8f}
        };
        
        s.turbo.enabled = true;
        s.turbo.maxBoost = 8.0f; 
        s.turbo.spoolRate = 10.0f;
        s.turbo.maxTurbineOmega = 25000.0f;
        return s;
    }
}

// ---------------------------------------------------------
// 2. MODULAR ENGINE MODEL
// ---------------------------------------------------------
class EngineModel {
private:
    EngineSpec spec;
    float engineOmega = 0.0f;
    float turbineOmega = 0.0f;
    float boostPressure = 0.0f;
    float filteredTargetBoost = 0.0f;
    float engineTemp = 20.0f;

    [[nodiscard]] float interpolateTorqueCurve(float rpm) const {
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
    }

public:
    EngineModel() {
        spec = EnginePresets::getHeavyDiesel();
        engineOmega = spec.idleRPM * (2.0f * PI) / 60.0f;
    }

    void setSpec(const EngineSpec& newSpec) { 
        spec = newSpec; 
        engineOmega = spec.idleRPM * (2.0f * PI) / 60.0f;
    }
    
    [[nodiscard]] const EngineSpec& getSpec() const { return spec; }

    // Calcula y devuelve el torque neto (Generado - Fricción - Bombeo - Freno Motor)
    // No integra su propia velocidad si está lockeado, eso lo hace el Drivetrain.
    [[nodiscard]] float computeNetTorque(float dt, float throttle, float loadTorqueScale, float jakeBrakeLevel, bool ignitionKey) {
        float rpm = engineOmega * 60.0f / (2.0f * PI);
        
        // Lógica de Stall (Calado)
        if (rpm < 400.0f && !ignitionKey) {
            return -(spec.frictionBase + (engineOmega * spec.frictionLinear)); // Solo arrastra fricción mecánica pura
        }

        // Gobernador Anti-Calado
        float idleThrottle = 0.0f;
        if (rpm < spec.idleRPM) {
            idleThrottle = std::clamp(1.2f * (1.0f - rpm / spec.idleRPM), 0.0f, 1.0f);
        }
        
        // Mapeo Drive-by-Wire
        float mappedThrottle = std::pow(throttle, 1.5f);
        float effectiveThrottle = std::max(mappedThrottle, idleThrottle);

        // Dinámica del Turbo
        if (spec.turbo.enabled) {
            float airAvailable = 0.5f + (boostPressure / spec.turbo.maxBoost) * 0.5f; 
            float lowRPMFactor = std::clamp((1500.0f - rpm) / 1000.0f, 0.0f, 1.0f);
            float smokeOverride = 1.0f + 0.8f * lowRPMFactor;
            effectiveThrottle = std::min(effectiveThrottle, airAvailable * smokeOverride);

            float normalizedRPM = std::clamp(rpm / spec.cutoffRPM, 0.0f, 1.0f);
            float loadFactor = 0.2f + 0.8f * loadTorqueScale; 

            float exhaustEnergy = (0.55f * effectiveThrottle + 0.45f * loadFactor) * (0.3f + 0.7f * normalizedRPM);
            float exhaustTorque = spec.turbo.kExhaust * exhaustEnergy; 
            exhaustTorque *= (1.0f - (boostPressure / spec.turbo.maxBoost) * 0.15f);

            float wastegateFactor = std::clamp((boostPressure - spec.turbo.maxBoost * 0.9f) / (spec.turbo.maxBoost * 0.1f), 0.0f, 1.0f);
            exhaustTorque *= (1.0f - wastegateFactor * 0.5f);

            float compressorTorque = spec.turbo.kCompressor * turbineOmega * turbineOmega * (1.0f + boostPressure * 0.5f);
            float turboFriction = turbineOmega * 0.00015f;

            float turboAlpha = (exhaustTorque - compressorTorque - turboFriction) / spec.turbo.inertia;
            turbineOmega += turboAlpha * dt;
            turbineOmega = std::clamp(turbineOmega, 0.0f, spec.turbo.maxTurbineOmega);

            float baseTargetBoost = spec.turbo.maxBoost * (turbineOmega / spec.turbo.maxTurbineOmega) * (turbineOmega / spec.turbo.maxTurbineOmega);
            
            float engineFactor = std::clamp(rpm / 1500.0f, 0.0f, 1.0f);
            engineFactor = std::pow(engineFactor, 1.5f); 
            float rawTargetBoost = std::max(0.0f, (baseTargetBoost * engineFactor) - 0.05f);
            
            filteredTargetBoost += (rawTargetBoost - filteredTargetBoost) * 2.5f * dt;
            
            if (effectiveThrottle < 0.05f) {
                float passiveFlow = 0.4f * normalizedRPM;
                float decay = boostPressure * std::max(0.05f, 0.4f - passiveFlow);
                boostPressure -= decay * dt;
            } else {
                float lagFactor = std::clamp(rpm / 1200.0f, 0.2f, 1.0f);
                boostPressure += (filteredTargetBoost - boostPressure) * spec.turbo.spoolRate * lagFactor * dt; 
            }
            if (boostPressure < 0.0f) boostPressure = 0.0f;
        } else {
            boostPressure = 0.0f;
            turbineOmega = 0.0f;
        }

        // Generación de Torque
        float curveShape = interpolateTorqueCurve(rpm);
        float baseTorque = spec.maxTorque * curveShape; 
        
        float intakeTemp = 40.0f + (engineTemp - 20.0f) * 0.5f;
        float tempFactor = std::max(0.0f, (intakeTemp - 60.0f) * 0.015f); 
        float airDensityBoost = boostPressure / (1.0f + tempFactor);
        
        float generatedTorque = 0.0f;
        if (rpm >= 400.0f && rpm <= spec.cutoffRPM) {
            float boostEfficiency = spec.turbo.enabled ? (0.4f + 0.6f * std::clamp(airDensityBoost / spec.turbo.maxBoost, 0.0f, 1.0f)) : 1.0f;
            generatedTorque = baseTorque * boostEfficiency * effectiveThrottle;
        } else if (rpm > spec.cutoffRPM) {
            generatedTorque = -500.0f; // Corte brusco
        }
        
        // Pérdidas y Freno Motor
        float pumpingLosses = (1.0f - effectiveThrottle) * (engineOmega * 0.15f);
        float turboBackpressure = boostPressure * 5.0f; 
        float engineFriction = spec.frictionBase + (engineOmega * spec.frictionLinear) + (engineOmega * engineOmega * spec.frictionQuadratic) + pumpingLosses + turboBackpressure;
        
        float jakeTorque = 0.0f;
        if (effectiveThrottle < 0.05f && jakeBrakeLevel > 0) {
            jakeTorque = jakeBrakeLevel * (1.5f * engineOmega + 0.004f * engineOmega * engineOmega);
        }
        
        return generatedTorque - engineFriction - jakeTorque;
    }

    // Integración de cinemática (solo si el embrague patina y el motor está libre de la caja)
    void integrateFreeOmega(float dt, float netTorque, float externalClutchDrag, bool ignitionKey) {
        float alpha = (netTorque - externalClutchDrag) / spec.engineInertia;
        if (ignitionKey) {
            alpha += 1000.0f / spec.engineInertia; // Torque del burro
        }
        engineOmega += alpha * dt;
        if (engineOmega < 0.0f) engineOmega = 0.0f;
    }

    // Setters/Getters para el solver del Powertrain
    void forceOmega(float w) { engineOmega = std::max(w, 0.0f); }
    [[nodiscard]] float getOmega() const { return engineOmega; }
    [[nodiscard]] float getInertia() const { return spec.engineInertia; }
    
    // Telemetría
    [[nodiscard]] float getRPM() const { return engineOmega * 60.0f / (2.0f * PI); }
    [[nodiscard]] float getBoost() const { return boostPressure; }
    [[nodiscard]] float getTurbineRPM() const { return turbineOmega * 60.0f / (2.0f * PI); }
    [[nodiscard]] float getTemp() const { return engineTemp; }
    
    [[nodiscard]] float getMaxAvailableTorque() const {
        float rpm = getRPM();
        float curveShape = interpolateTorqueCurve(rpm);
        float baseTorque = spec.maxTorque * curveShape; 
        float intakeTemp = 40.0f + (engineTemp - 20.0f) * 0.5f;
        float tempFactor = std::max(0.0f, (intakeTemp - 60.0f) * 0.015f); 
        float airDensityBoost = boostPressure / (1.0f + tempFactor);
        
        // El spec.maxTorque YA ES el techo (100% boost). Si no hay presión, el motor rinde menos (40% base).
        float boostEfficiency = spec.turbo.enabled ? (0.4f + 0.6f * std::clamp(airDensityBoost / spec.turbo.maxBoost, 0.0f, 1.0f)) : 1.0f;
        return baseTorque * boostEfficiency;
    }
    
    void addHeat(float dt, float powerDissipated) { engineTemp += powerDissipated * dt; }
    void updateCooling(float dt) { engineTemp -= (engineTemp - 20.0f) * 0.01f * dt; }
};


// ---------------------------------------------------------
// 3. ESTRUCTURAS DEL POWERTRAIN
// ---------------------------------------------------------
struct TruckState {
    float engineRPM = 0.0f;
    float outputRPM = 0.0f;
    float clutchSlip = 0.0f;        
    float transmittedTorque = 0.0f;   
    float internalShaftTorque = 0.0f; 
    float wheelTorque = 0.0f;         
    bool wasLockedLastFrame = false;  
    
    float turbineRPM = 0.0f;        
    float boostPressure = 0.0f;     
    
    int currentGear = 0;            
    float gearboxHealth = 1.0f;     
    float engineTemp = 20.0f;       
    float transmissionTemp = 40.0f; 
    bool isGrinding = false;        
    bool isMoneyShift = false;      

    float engineHP = 0.0f;
    float maxAvailableTorque = 0.0f;
    float maxAvailableHP = 0.0f;
    float loadTorqueAtEngine = 0.0f;
};

struct DriverInput {
    float throttle = 0.0f;          
    float brake = 0.0f;             // Nuevo pedal de freno
    float clutch = 0.0f;            
    int shiftRequest = 0;           
    bool fsmGrinding = false;       
    bool ignitionKey = false;       
    int jakeBrakeLevel = 0;         
    int retarderLevel = 0;          
};


// ---------------------------------------------------------
// 4. EL SOLVER POWERTRAIN (AHORA DESACOPLADO)
// ---------------------------------------------------------
class PowertrainSolver {
private:
    TruckState state;
    EngineModel engine; 
    Gearbox gearbox;
    Differential differential; // Nuevo integrante

    static constexpr float INERTIA_INPUT_SHAFT = 0.05f;  
    static constexpr float MAX_CLUTCH_TORQUE = 3500.0f;  

    float inputShaftOmega = 0.0f;
    float outputShaftOmega = 0.0f; 

    [[nodiscard]] float getRatio(int gearIndex) const {
        return gearbox.getRatio(gearIndex);
    }

public:
    PowertrainSolver() {}

    void swapEngine(const EngineSpec& spec) { engine.setSpec(spec); }
    void swapGearbox(const GearboxSpec& spec) { gearbox.setSpec(spec); }
    void swapDifferential(const DifferentialSpec& spec) { differential.setSpec(spec); } // Inyector

    [[nodiscard]] int getMaxGear() const { return gearbox.getMaxGear(); }

    // Añadimos el accessoryLoadTorque para inyectarle el peso del compresor
    void update(float dt, const DriverInput& input, float externalLoadTorque, float equivalentVehicleInertia, float accessoryLoadTorque = 0.0f) {
        float finalDriveRatio = differential.getRatio();
        
        // Retardador
        float retarderTorque = 0.0f;
        if (input.retarderLevel > 0) {
            float rawRetarder = input.retarderLevel * (50.0f * outputShaftOmega + 0.3f * outputShaftOmega * outputShaftOmega);
            float maxRetarder = 800.0f * input.retarderLevel; 
            retarderTorque = std::min(rawRetarder, maxRetarder);
            
            float tempFactor = std::clamp(1.0f - (state.transmissionTemp - 120.0f) / 100.0f, 0.3f, 1.0f);
            retarderTorque *= tempFactor;
            
            float retarderPower = std::abs(retarderTorque * outputShaftOmega);
            state.transmissionTemp += (retarderPower * 0.000008f) * dt;
        }
        float effectiveLoadTorque = externalLoadTorque + retarderTorque;

        if (state.gearboxHealth <= 0.0f) state.currentGear = 0;

        float baseRatio = getRatio(state.currentGear);
        float currentRatio = baseRatio > 0.0f ? baseRatio * finalDriveRatio : 0.0f;

        // Embrague
        float rawClutch = std::clamp(input.clutch, 0.0f, 1.0f);
        float clutchEngagement = 0.0f; 
        if (rawClutch < 0.15f) {
            clutchEngagement = 1.0f; 
        } else if (rawClutch < 0.70f) {
            float bite = 1.0f - ((rawClutch - 0.15f) / 0.55f);
            clutchEngagement = std::pow(bite, 3.0f);
        } else {
            clutchEngagement = 0.0f; 
        }
        float maxTransmissibleTorque = MAX_CLUTCH_TORQUE * clutchEngagement;

        // Caja y Sincronizadores
        state.isGrinding = input.fsmGrinding;
        state.isMoneyShift = false;
        
        if (input.fsmGrinding) {
            state.currentGear = 0; // Te escupe la marcha a la fuerza
            state.gearboxHealth -= 0.015f * dt; // Castigo MUY reducido por errar el cambio con el stick
            if (state.gearboxHealth < 0.0f) state.gearboxHealth = 0.0f;
        } else if (input.shiftRequest == 0) {
            state.currentGear = 0;
        } else if (input.shiftRequest != state.currentGear && state.gearboxHealth > 0.0f) {
            float targetBaseRatio = getRatio(input.shiftRequest);
            float targetRatio = targetBaseRatio > 0.0f ? targetBaseRatio * finalDriveRatio : 0.0f;
            float targetInputOmega = outputShaftOmega * targetRatio;
            
            // Verificación de "Money Shift" (Rebaje destructivo masivo)
            float targetRPM = targetInputOmega * 60.0f / (2.0f * PI);
            float maxSafeRPM = engine.getSpec().cutoffRPM + 400.0f; // Margen de 400 RPM antes de explotar

            if (targetRPM > maxSafeRPM) {
                state.isGrinding = true;
                state.isMoneyShift = true; // Flag para telemetría y vibración
                state.currentGear = 0; // La caja no lo deja entrar
                state.gearboxHealth -= 0.40f * dt; // 40% de vida por segundo. Te la parte en 2.5 segundos.
                if (state.gearboxHealth < 0.0f) state.gearboxHealth = 0.0f;
            } else {
                float omegaDiff = inputShaftOmega - targetInputOmega;
                float clutchDragTorque = maxTransmissibleTorque;
                float inertiaTorque = std::abs((INERTIA_INPUT_SHAFT * omegaDiff) / dt);
                float requiredTorque = inertiaTorque + clutchDragTorque;

                // Sincronizador normal
                if (requiredTorque > 2500.0f) { // Ajustado a un valor físico realista (vos le habías puesto 1e9f)
                    state.isGrinding = true;
                    state.currentGear = 0; 
                    float damageRate = (requiredTorque - 2500.0f) * 0.000001f; // Daño bajísimo por forzar sincros
                    state.gearboxHealth -= damageRate * dt; 
                    if (state.gearboxHealth < 0.0f) state.gearboxHealth = 0.0f;
                } else {
                    state.currentGear = input.shiftRequest;
                    inputShaftOmega = targetInputOmega; 
                }
            }
        }
        
        baseRatio = getRatio(state.currentGear);
        currentRatio = baseRatio > 0.0f ? baseRatio * finalDriveRatio : 0.0f;

        // Sumamos la inercia del diferencial al vehículo para la física rotacional
        float totalEquivalentInertia = equivalentVehicleInertia + differential.getInertia();

        // Update del Engine
        float engineLoadScale = std::clamp(std::abs(state.internalShaftTorque) / engine.getSpec().maxTorque, 0.0f, 1.0f);
        // Restamos el torque que roba el compresor de aire directamente en el cigüeñal
        float netEngineTorque = engine.computeNetTorque(dt, input.throttle, engineLoadScale, input.jakeBrakeLevel, input.ignitionKey) - accessoryLoadTorque;
        
        float engineOmega = engine.getOmega();
        float engineInertia = engine.getInertia();

        float transmittedTorque = 0.0f;
        float omegaDiff = engineOmega - inputShaftOmega;
        state.clutchSlip = std::abs(omegaDiff);

        float reducedInertiaLock = INERTIA_INPUT_SHAFT;
        float reducedInertiaSlip = (engineInertia * INERTIA_INPUT_SHAFT) / (engineInertia + INERTIA_INPUT_SHAFT);
        
        if (currentRatio > 0.0f) {
            float reflectedVehicleInertia = std::max(totalEquivalentInertia, 0.001f) / (currentRatio * currentRatio);
            reducedInertiaLock = (engineInertia * reflectedVehicleInertia) / (engineInertia + reflectedVehicleInertia);
        } else {
            reducedInertiaLock = reducedInertiaSlip;
        }
        
        float requiredLockTorque = (omegaDiff * reducedInertiaLock) / dt;
        bool isLocked = false;
        
        if (maxTransmissibleTorque > 0.1f) {
            float lockThreshold = maxTransmissibleTorque * 0.9f;
            float unlockThreshold = maxTransmissibleTorque * 1.1f;
            if (state.wasLockedLastFrame) isLocked = (std::abs(requiredLockTorque) < unlockThreshold);
            else isLocked = (std::abs(requiredLockTorque) < lockThreshold);
        }

        float internalTorque = 0.0f;

        if (isLocked) {
            state.clutchSlip = 0.0f;
            if (currentRatio > 0.0f) {
                float reflectedVehicleInertia = std::max(totalEquivalentInertia, 0.001f) / (currentRatio * currentRatio);
                float totalSystemInertia = engineInertia + reflectedVehicleInertia;
                
                if (!state.wasLockedLastFrame) {
                    float systemMomentum = (engineInertia * engineOmega) + (reflectedVehicleInertia * inputShaftOmega);
                    float commonOmega = systemMomentum / totalSystemInertia;
                    engine.forceOmega(commonOmega); 
                    inputShaftOmega = commonOmega;
                    outputShaftOmega = commonOmega / currentRatio; 
                } else {
                    inputShaftOmega = engineOmega;
                    outputShaftOmega = engineOmega / currentRatio;
                }
                
                float reflectedLoadTorque = effectiveLoadTorque / currentRatio;
                float systemAlpha = (netEngineTorque - reflectedLoadTorque) / totalSystemInertia;
                
                engine.forceOmega(engine.getOmega() + systemAlpha * dt);
                inputShaftOmega = engine.getOmega();
                outputShaftOmega = engine.getOmega() / currentRatio;
                
                internalTorque = (netEngineTorque * reflectedVehicleInertia + reflectedLoadTorque * engineInertia) / totalSystemInertia;
                transmittedTorque = reflectedLoadTorque + (reflectedVehicleInertia * systemAlpha);
            } else {
                transmittedTorque = 0.0f; 
                internalTorque = 0.0f;
                float combinedInertia = engineInertia + INERTIA_INPUT_SHAFT;
                
                if (!state.wasLockedLastFrame) {
                    float commonOmega = (engineInertia * engineOmega + INERTIA_INPUT_SHAFT * inputShaftOmega) / combinedInertia;
                    engine.forceOmega(commonOmega);
                    inputShaftOmega = commonOmega;
                } else {
                    inputShaftOmega = engineOmega;
                }
                
                float alpha = netEngineTorque / combinedInertia;
                engine.forceOmega(engine.getOmega() + alpha * dt);
                inputShaftOmega = engine.getOmega();
            }
            
            if (std::abs(internalTorque) > maxTransmissibleTorque) isLocked = false;
        } else {
            float slipAbs = std::abs(omegaDiff);
            float signSlip = (omegaDiff > 0.0f) ? 1.0f : ((omegaDiff < 0.0f) ? -1.0f : 0.0f);
            float frictionFactor = 1.0f - std::exp(-slipAbs * 5.0f); 
            
            transmittedTorque = maxTransmissibleTorque * frictionFactor * signSlip;
            internalTorque = transmittedTorque;
            
            engine.integrateFreeOmega(dt, netEngineTorque, transmittedTorque, input.ignitionKey);
            
            if (currentRatio == 0.0f) {
                float passiveDrag = 0.02f; 
                float inputDragTorque = inputShaftOmega * passiveDrag;
                float inputAlpha = (transmittedTorque - inputDragTorque) / INERTIA_INPUT_SHAFT;
                inputShaftOmega += inputAlpha * dt;
            } else {
                inputShaftOmega = outputShaftOmega * currentRatio; 
            }
            
            float heatPower = std::abs(transmittedTorque * omegaDiff);
            engine.addHeat(dt, heatPower * 0.00005f); 
        }
        
        state.wasLockedLastFrame = isLocked;

        // La eficiencia ahora depende de la caja (fija en 0.95 por ahora) y del diferencial físico
        float drivetrainEfficiency = 0.95f * differential.getEfficiency(); 
        float torqueToWheels = transmittedTorque * currentRatio * drivetrainEfficiency;
        
        float safeVehicleInertia = std::max(totalEquivalentInertia, 0.001f);
        float vehicleAlpha = (torqueToWheels - effectiveLoadTorque) / safeVehicleInertia;
        
        if (!isLocked || currentRatio == 0.0f) {
            outputShaftOmega += vehicleAlpha * dt;
            if (outputShaftOmega < 0.0f) outputShaftOmega = 0.0f; 
        }

        engine.updateCooling(dt);
        state.transmissionTemp -= (state.transmissionTemp - 40.0f) * 0.005f * dt; 

        state.engineRPM = engine.getRPM();
        state.outputRPM = outputShaftOmega * 60.0f / (2.0f * PI);
        state.boostPressure = engine.getBoost();
        state.turbineRPM = engine.getTurbineRPM();
        state.engineTemp = engine.getTemp();
        
        state.transmittedTorque = transmittedTorque;
        state.internalShaftTorque = internalTorque;
        state.wheelTorque = torqueToWheels;

        state.maxAvailableTorque = engine.getMaxAvailableTorque();
        state.maxAvailableHP = (state.maxAvailableTorque * state.engineRPM) / 7120.8f;
        state.engineHP = (std::max(0.0f, internalTorque) * state.engineRPM) / 7120.8f;
        state.loadTorqueAtEngine = currentRatio > 0.0f ? (effectiveLoadTorque / currentRatio) : 0.0f;
    }

    [[nodiscard]] const TruckState& getState() const { return state; }
};

} // namespace TruckCore
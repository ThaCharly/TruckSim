#pragma once
#include <string>

namespace TruckCore {

enum class DiffType { Open, Locked, Torsen };

inline DiffType parseDiffType(const std::string& typeStr) {
    if (typeStr == "locked") return DiffType::Locked;
    if (typeStr == "torsen" || typeStr == "lsd") return DiffType::Torsen;
    return DiffType::Open; // Default
}

struct DifferentialTarget {
    std::string name;
    DiffType type;
    float ratio;
};

struct DifferentialSpec {
    std::string name;
    DiffType type;
    float ratio = 3.08f;
    float efficiency = 0.96f; // Pérdida por fricción de los engranajes hipoidales
    float inertia = 0.5f;     // Inercia rotacional base
};

namespace DifferentialFactory {
    inline DifferentialSpec generateFromTarget(const DifferentialTarget& target) {
        DifferentialSpec spec;
        spec.name = target.name;
        spec.type = target.type;
        spec.ratio = target.ratio;
        
        // Ajuste físico según el tipo de diferencial
        switch (target.type) {
            case DiffType::Open:
                spec.efficiency = 0.96f; // Gira libre, menos fricción
                spec.inertia = 0.4f;     // Engranajes estándar
                break;
            case DiffType::Torsen:
                spec.efficiency = 0.94f; // Los engranajes helicoidales internos generan fricción extra
                spec.inertia = 0.6f;     // Más masa rotante
                break;
            case DiffType::Locked:
                spec.efficiency = 0.93f; // Eje rígido pesado (Heavy Duty)
                spec.inertia = 0.9f;     // Engranajes masivos para soportar torque extremo
                break;
        }
        return spec;
    }
}

class Differential {
private:
    DifferentialSpec spec;

public:
    Differential() {
        // Fallback default
        spec.name = "Fallback Open 3.08";
        spec.type = DiffType::Open;
        spec.ratio = 3.08f;
    }

    void setSpec(const DifferentialSpec& newSpec) { spec = newSpec; }
    
    [[nodiscard]] float getRatio() const { return spec.ratio; }
    [[nodiscard]] float getEfficiency() const { return spec.efficiency; }
    [[nodiscard]] float getInertia() const { return spec.inertia; }
    [[nodiscard]] const std::string& getName() const { return spec.name; }
};

} // namespace TruckCore
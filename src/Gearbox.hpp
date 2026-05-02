#pragma once
#include <vector>
#include <string>

namespace TruckCore {

struct GearboxTarget {
    std::string name;
    std::vector<float> ratios; 
};

struct GearboxSpec {
    std::string name;
    std::vector<float> ratios;
};

namespace GearboxFactory {
    inline GearboxSpec generateFromTarget(const GearboxTarget& target) {
        GearboxSpec spec;
        spec.name = target.name;
        spec.ratios = target.ratios;
        
        if (spec.ratios.empty() || spec.ratios[0] != 0.0f) {
            spec.ratios.insert(spec.ratios.begin(), 0.0f);
        }
        return spec;
    }
}

class Gearbox {
private:
    GearboxSpec spec;

public:
    Gearbox() {
        spec.name = "Fallback Default";
        spec.ratios = {0.0f, 12.0f, 9.0f, 6.5f, 4.8f, 3.6f, 2.7f, 2.0f, 1.5f, 1.25f, 1.0f, 0.86f, 0.70f};
    }

    void setSpec(const GearboxSpec& newSpec) { spec = newSpec; }

    [[nodiscard]] float getRatio(int gearIndex) const {
        if (gearIndex <= 0 || gearIndex >= static_cast<int>(spec.ratios.size())) return 0.0f;
        return spec.ratios[gearIndex];
    }

    [[nodiscard]] int getMaxGear() const {
        return static_cast<int>(spec.ratios.size()) - 1;
    }
    
    [[nodiscard]] const std::string& getName() const { return spec.name; }
};

} // namespace TruckCore
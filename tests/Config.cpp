#include "../LootConfig.hpp"
#include <stdexcept>

void require(bool condition) {
    if (!condition) throw std::runtime_error("Loot config contract failed");
}

int main() {
    const auto config = loot::Config::parse(R"({"schemaVersion":1,"enabled":true,
      "objects":{"BondPile":{"occurrenceFactor":2.5,"maxOnePerRoom":false}},
      "mapCounts":[{"map":"*","objectType":"BondPile","count":8}],
      "presets":[{"vault":false,"security":1,"pointType":"Default","weights":{"BondPile":3}}],
      "points":[{"map":"Hardsell","originalSelection":"Weighted",
        "candidates":[{"objectType":"BondPile","weight":4}]}]})");
    require(config.enabled && config.objects.at("BondPile").occurrence_factor == 2.5f);
    require(config.map_counts[0].count == 8 && config.presets[0].weights.at("BondPile") == 3);
    require(config.points[0].candidates[0].weight == 4);
    const auto rejected = [](const char *source) {
        try { (void)loot::Config::parse(source); return false; }
        catch (const std::exception &) { return true; }
    };
    require(rejected(R"({"schemaVersion":1,"enabled":true,"mapCounts":[{"map":"*","objectType":"BondPile","count":-1}]})"));
    require(rejected(R"({"schemaVersion":1,"enabled":true,"points":[{"candidates":[]}]})"));
    require(rejected(R"({"schemaVersion":1,"enabled":true,"objects":{"BondPile":{"score":-1}}})"));
    require(rejected(R"({"schemaVersion":1,"enabled":true,"mapCount":[]})"));
}

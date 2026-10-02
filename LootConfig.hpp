#pragma once

#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace loot {
using Json = nlohmann::json;

struct Object {
    std::optional<float> occurrence_factor, score;
    std::optional<bool> max_one_per_room, can_fill_room, private_lobbies, spawn_on_start;
    std::optional<std::int32_t> priority;
};
struct Count {
    std::string map, object_type;
    std::uint32_t count{};
};
struct Preset {
    bool vault{};
    std::uint8_t security{};
    std::string point_type;
    std::unordered_map<std::string, float> weights;
};
struct Candidate {
    std::string object_type;
    float weight{};
};
struct Point {
    std::string map, path, implementation, point_type, original_selection, original_object_type;
    std::optional<bool> vault;
    std::optional<std::uint8_t> security;
    std::vector<Candidate> candidates;
};
struct Config {
    bool enabled{};
    std::unordered_map<std::string, Object> objects;
    std::vector<Count> map_counts;
    std::vector<Preset> presets;
    std::vector<Point> points;

    static Config parse(const std::string &text) {
        if (text.size() > 256 * 1024) throw std::runtime_error("Loot config is too large");
        const auto root = Json::parse(text);
        if (!root.is_object() || root.value("schemaVersion", 0) != 1 ||
            !root.contains("enabled") || !root.at("enabled").is_boolean())
            throw std::runtime_error("Invalid loot config schema");
        if (root.size() != 2 + root.contains("objects") + root.contains("mapCounts") +
                           root.contains("presets") + root.contains("points"))
            throw std::runtime_error("Unknown loot config field");
        Config result;
        result.enabled = root.at("enabled").get<bool>();
        const auto finite = [](const Json &value, bool zero_allowed = true) {
            if (!value.is_number()) throw std::runtime_error("Loot weight must be numeric");
            const auto number = value.get<double>();
            if (!std::isfinite(number) || number < 0 || (!zero_allowed && number == 0) ||
                number > 100000 || number > std::numeric_limits<float>::max())
                throw std::runtime_error("Loot weight is outside the supported range");
            return static_cast<float>(number);
        };
        const auto name = [](const Json &value) {
            if (!value.is_string()) throw std::runtime_error("Loot selector must be text");
            auto text = value.get<std::string>();
            if (text.empty() || text.size() > 1024 || text.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid loot selector");
            return text;
        };
        if (root.contains("objects")) {
            const auto &items = root.at("objects");
            if (!items.is_object() || items.size() > 256) throw std::runtime_error("Invalid loot objects");
            for (auto it = items.begin(); it != items.end(); ++it) {
                const auto key = name(it.key());
                const auto &row = it.value();
                if (!row.is_object()) throw std::runtime_error("Invalid loot object override");
                Object entry;
                if (row.contains("occurrenceFactor")) entry.occurrence_factor = finite(row.at("occurrenceFactor"));
                if (row.contains("score")) entry.score = finite(row.at("score"));
                if (row.contains("maxOnePerRoom")) entry.max_one_per_room = row.at("maxOnePerRoom").get<bool>();
                if (row.contains("canFillRoom")) entry.can_fill_room = row.at("canFillRoom").get<bool>();
                if (row.contains("privateLobbies")) entry.private_lobbies = row.at("privateLobbies").get<bool>();
                if (row.contains("spawnOnStart")) entry.spawn_on_start = row.at("spawnOnStart").get<bool>();
                if (row.contains("priority")) entry.priority = row.at("priority").get<std::int32_t>();
                if (row.empty() || row.size() != static_cast<std::size_t>(entry.occurrence_factor.has_value() +
                        entry.score.has_value() + entry.max_one_per_room.has_value() +
                        entry.can_fill_room.has_value() + entry.private_lobbies.has_value() +
                        entry.spawn_on_start.has_value() + entry.priority.has_value()))
                    throw std::runtime_error("Unknown or empty loot object override");
                result.objects.emplace(key, entry);
            }
        }
        if (root.contains("mapCounts")) {
            const auto &items = root.at("mapCounts");
            if (!items.is_array() || items.size() > 1024) throw std::runtime_error("Invalid map count overrides");
            for (const auto &row : items) {
                if (!row.is_object() || row.size() != 3 || !row.contains("map") ||
                    !row.contains("objectType") || !row.contains("count"))
                    throw std::runtime_error("Invalid map count override");
                const auto number = row.at("count").get<std::uint64_t>();
                if (number > 10000) throw std::runtime_error("Loot count exceeds 10000");
                result.map_counts.push_back({name(row.at("map")), name(row.at("objectType")),
                                             static_cast<std::uint32_t>(number)});
            }
        }
        if (root.contains("presets")) {
            const auto &items = root.at("presets");
            if (!items.is_array() || items.size() > 256) throw std::runtime_error("Invalid preset overrides");
            for (const auto &row : items) {
                if (!row.is_object() || row.size() != 4 || !row.at("vault").is_boolean() ||
                    !row.at("weights").is_object() || row.at("weights").empty() ||
                    row.at("weights").size() > 64)
                    throw std::runtime_error("Invalid preset override");
                const auto level = row.at("security").get<int>();
                if (level < 0 || level > 255) throw std::runtime_error("Invalid security level");
                Preset entry{row.at("vault").get<bool>(), static_cast<std::uint8_t>(level),
                             name(row.at("pointType")), {}};
                for (auto it = row.at("weights").begin(); it != row.at("weights").end(); ++it)
                    entry.weights.emplace(name(it.key()), finite(it.value()));
                result.presets.push_back(std::move(entry));
            }
        }
        if (root.contains("points")) {
            const auto &items = root.at("points");
            if (!items.is_array() || items.size() > 1024) throw std::runtime_error("Invalid point rules");
            for (const auto &row : items) {
                if (!row.is_object() || !row.contains("candidates") ||
                    !row.at("candidates").is_array() || row.at("candidates").empty() ||
                    row.at("candidates").size() > 64)
                    throw std::runtime_error("Invalid point candidates");
                Point entry;
                if (row.contains("map")) entry.map = name(row.at("map"));
                if (row.contains("path")) entry.path = name(row.at("path"));
                if (row.contains("implementation")) entry.implementation = name(row.at("implementation"));
                if (row.contains("pointType")) entry.point_type = name(row.at("pointType"));
                if (row.contains("originalSelection")) entry.original_selection = name(row.at("originalSelection"));
                if (row.contains("originalObjectType")) entry.original_object_type = name(row.at("originalObjectType"));
                if (row.contains("vault")) entry.vault = row.at("vault").get<bool>();
                if (row.contains("security")) {
                    const auto level = row.at("security").get<int>();
                    if (level < 0 || level > 255) throw std::runtime_error("Invalid point security level");
                    entry.security = static_cast<std::uint8_t>(level);
                }
                if (entry.implementation != "" && entry.implementation != "Actor" &&
                    entry.implementation != "Component") throw std::runtime_error("Invalid point implementation");
                if (entry.original_selection != "" && entry.original_selection != "Preset" &&
                    entry.original_selection != "Fixed" && entry.original_selection != "Weighted")
                    throw std::runtime_error("Invalid original selection");
                if (row.size() != 1 + !entry.map.empty() + !entry.path.empty() +
                    !entry.implementation.empty() + !entry.point_type.empty() +
                    !entry.original_selection.empty() + !entry.original_object_type.empty() +
                    entry.vault.has_value() + entry.security.has_value())
                    throw std::runtime_error("Unknown point rule field");
                for (const auto &candidate : row.at("candidates")) {
                    if (!candidate.is_object() || candidate.size() != 2)
                        throw std::runtime_error("Invalid replacement candidate");
                    entry.candidates.push_back({name(candidate.at("objectType")),
                                                finite(candidate.at("weight"), false)});
                }
                result.points.push_back(std::move(entry));
            }
        }
        return result;
    }
};
}

#include "../LootConfig.hpp"
#include "ModVersion.hpp"

#include <Briefcase/DeceiveInc/LootDistribution.hpp>
#include <Briefcase/DeceiveInc/Paths.hpp>
#include <DynamicOutput/Output.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace {
using namespace RC;
using namespace RC::Unreal;
using briefcase::deceive::LootCandidate;
using briefcase::deceive::LootDistribution;
using briefcase::deceive::LootObject;

constexpr auto manager_setup = STR("/Script/DeceiveInc.ObjectSpawningManager:HandleAllRoomsSetup");
constexpr auto actor_update = STR("/Script/DeceiveInc.ObjectSpawn:UpdateConnectedRoomsReference");
constexpr auto component_update = STR("/Script/DeceiveInc.ObjectSpawnComponent:UpdateConnectedRoomsReference");

extern "C" __declspec(dllexport) CppUserModBase *start_mod();

std::filesystem::path mod_file(const char *name) {
    return briefcase::deceive::data_file(reinterpret_cast<const void *>(&start_mod), name);
}

loot::Config load_config() {
    std::ifstream input(mod_file("config.json"), std::ios::binary);
    if (!input) throw std::runtime_error("Missing Data/config.json");
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    return loot::Config::parse(text);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
bool match(const std::string &filter, const std::string &actual) {
    return filter.empty() || filter == "*" || lower(actual).find(lower(filter)) != std::string::npos;
}

class LootControlUe4ss final : public CppUserModBase {
  public:
    LootControlUe4ss() : config_(load_config()) {
        ModName = STR("Briefcase.LootControl");
        ModVersion = briefcase_mod_version;
        ModDescription = STR("Configurable Deceive Inc server loot distribution");
        ModAuthors = STR("EnoPM");
    }
    ~LootControlUe4ss() override {
        for (const auto &hook : hooks_) UObjectGlobals::UnregisterHook(hook.first, hook.second);
    }
    void on_unreal_init() override { try_install(); }
    void on_update() override {
        if (hooks_.size() == 3 || std::chrono::steady_clock::now() < next_attempt_) return;
        next_attempt_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        try_install();
    }

  private:
    loot::Config config_;
    std::vector<std::pair<UFunction *, std::pair<int, int>>> hooks_;
    std::unordered_set<UFunction *> installed_;
    std::unordered_set<UObject *> changed_points_;
    std::unordered_set<UObject *> cataloged_points_;
    std::unordered_map<std::uint32_t, briefcase::deceive::LootRoom> rooms_;
    std::unordered_set<std::string> known_types_;
    std::string current_map_;
    loot::Json catalog_;
    UObject *catalog_manager_{};
    std::chrono::steady_clock::time_point next_attempt_{};
    std::size_t changes_{};
    bool catalog_written_{};
    bool crc_failed_{};

    void install(const wchar_t *path, bool before, void (LootControlUe4ss::*handler)(UObject *),
                 bool capture_after = false) {
        auto *function = UObjectGlobals::StaticFindObject<UFunction *>(nullptr, nullptr, path);
        if (!function || installed_.contains(function)) return;
        const auto callback = [this, handler](UnrealScriptFunctionCallableContext &context, void *) {
            try { (this->*handler)(context.Context); }
            catch (const std::exception &error) {
                Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] hook rejected: {}\n"),
                                                RC::to_wstring(error.what()));
            } catch (...) {
                Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] hook rejected\n"));
            }
        };
        const auto after = [this, capture_after](UnrealScriptFunctionCallableContext &context, void *) {
            if (!capture_after) return;
            try { after_setup(context.Context); }
            catch (...) { Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] post-setup failed\n")); }
        };
        auto ids = before ? UObjectGlobals::RegisterHook(function, callback, after, nullptr)
                          : UObjectGlobals::RegisterHook(function,
                        [](UnrealScriptFunctionCallableContext &, void *) {}, callback, nullptr);
        hooks_.push_back({function, ids});
        installed_.insert(function);
        Output::send(STR("[Briefcase.LootControl] hook installed: {}\n"), path);
    }
    void try_install() noexcept {
        try {
            install(manager_setup, true, &LootControlUe4ss::before_setup, true);
            install(actor_update, true, &LootControlUe4ss::on_point);
            install(component_update, true, &LootControlUe4ss::on_point);
            if (hooks_.size() == 3)
                Output::send(STR("[Briefcase.LootControl] ready: enabled={}, object rules={}, count rules={}, "
                                 "preset rules={}, point rules={}\n"),
                             config_.enabled, config_.objects.size(), config_.map_counts.size(),
                             config_.presets.size(), config_.points.size());
        } catch (const std::exception &error) {
            Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] initialization failed: {}\n"),
                                            RC::to_wstring(error.what()));
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] initialization failed\n"));
        }
    }

    void before_setup(UObject *manager) {
        if (!LootDistribution::is_manager(manager)) return;
        if (manager == catalog_manager_ && current_map_ == RC::to_string(manager->GetPathName()))
            return;
        current_map_ = RC::to_string(manager->GetPathName());
        catalog_manager_ = manager;
        catalog_written_ = false;
        crc_failed_ = false;
        changed_points_.clear();
        cataloged_points_.clear();
        changes_ = 0;
        known_types_.clear();
        rooms_.clear();
        for (const auto &room : LootDistribution::rooms()) rooms_.insert_or_assign(room.crc, room);
        const auto objects = LootDistribution::objects(manager);
        const auto counts = LootDistribution::counts(manager);
        catalog_ = {{"schemaVersion", 1}, {"map", current_map_},
                    {"objects", loot::Json::array()}, {"counts", loot::Json::array()},
                    {"points", loot::Json::array()}};
        for (const auto &object : objects) {
            known_types_.insert(object.object_type);
            catalog_["objects"].push_back({{"objectType", object.object_type},
                {"occurrenceFactor", object.occurrence_factor}, {"score", object.score},
                {"maxOnePerRoom", object.max_one_per_room}, {"canFillRoom", object.can_fill_room},
                {"privateLobbies", object.private_lobbies}, {"spawnOnStart", object.spawn_on_start},
                {"priority", object.priority}});
        }
        for (const auto &count : counts)
            catalog_["counts"].push_back({{"objectType", count.object_type}, {"count", count.count}});
        if (!config_.enabled) { capture_loaded_points(); return; }
        for (auto object : objects) {
            const auto it = config_.objects.find(object.object_type);
            if (it == config_.objects.end()) continue;
            const auto &override = it->second;
            if (override.occurrence_factor) object.occurrence_factor = *override.occurrence_factor;
            if (override.score) object.score = *override.score;
            if (override.max_one_per_room) object.max_one_per_room = *override.max_one_per_room;
            if (override.can_fill_room) object.can_fill_room = *override.can_fill_room;
            if (override.private_lobbies) object.private_lobbies = *override.private_lobbies;
            if (override.spawn_on_start) object.spawn_on_start = *override.spawn_on_start;
            if (override.priority) object.priority = *override.priority;
            if (LootDistribution::update_object(manager, object)) ++changes_;
        }
        for (const auto &rule : config_.map_counts) {
            if (!match(rule.map, current_map_)) continue;
            if (LootDistribution::update_count(manager, {rule.object_type, rule.count})) {
                const auto readback = LootDistribution::counts(manager);
                const auto verified = std::find_if(readback.begin(), readback.end(),
                    [&](const auto &row) { return row.object_type == rule.object_type; });
                if (verified == readback.end() || verified->count != rule.count)
                    throw std::runtime_error("Loot count readback mismatch");
                ++changes_;
            }
            else Output::send<LogLevel::Warning>(
                STR("[Briefcase.LootControl] count object not found: {}\n"),
                RC::to_wstring(rule.object_type));
        }
        capture_loaded_points();
        Output::send(STR("[Briefcase.LootControl] manager prepared: {}; points={}, changes={}\n"),
                     RC::to_wstring(current_map_), catalog_["points"].size(), changes_);
    }

    void capture_loaded_points() {
        auto *actor = UObjectGlobals::StaticFindObject<UClass *>(
            nullptr, nullptr, STR("/Script/DeceiveInc.ObjectSpawn"));
        auto *component = UObjectGlobals::StaticFindObject<UClass *>(
            nullptr, nullptr, STR("/Script/DeceiveInc.ObjectSpawnComponent"));
        if (!actor || !component) throw std::runtime_error("Loot point classes are unavailable");
        std::size_t attempted{}, failed{};
        UObjectGlobals::ForEachUObject([&](UObject *object, std::int32_t, std::int32_t) {
            if (object && !object->IsUnreachable() && (object->IsA(actor) || object->IsA(component))) {
                ++attempted;
                try { on_point(object); }
                catch (const std::exception &error) {
                    ++failed;
                    if (failed <= 3) Output::send<LogLevel::Warning>(
                        STR("[Briefcase.LootControl] point inspection failed: {}\n"),
                        RC::to_wstring(error.what()));
                }
            }
            return LoopAction::Continue;
        });
        Output::send(STR("[Briefcase.LootControl] inspected {} loaded points; failed={}\n"),
                     attempted, failed);
    }

    bool permitted(const std::vector<LootCandidate> &candidates) const {
        return std::all_of(candidates.begin(), candidates.end(), [&](const LootCandidate &candidate) {
            return known_types_.contains(candidate.object_type) && candidate.weight > 0;
        });
    }
    void on_point(UObject *object) {
        if (!LootDistribution::is_point(object) || changed_points_.contains(object)) return;
        if (!catalog_manager_) {
            UObject *manager{};
            UObjectGlobals::ForEachUObject([&](UObject *candidate, std::int32_t, std::int32_t) {
                if (LootDistribution::is_manager(candidate)) manager = candidate;
                return manager ? LoopAction::Break : LoopAction::Continue;
            });
            if (manager) before_setup(manager);
        }
        const auto point = LootDistribution::point(object);
        if (!point) return;
        if (catalog_manager_ && cataloged_points_.insert(object).second &&
            catalog_["points"].size() < 10000) {
            loot::Json row = {{"path", point->path}, {"implementation", point->implementation},
                              {"roomCrc", point->room_crc}, {"pointType", point->point_type},
                              {"originalSelection", point->selection}, {"disabled", point->disabled},
                              {"candidates", loot::Json::array()}};
            if (auto room = rooms_.find(point->room_crc); room != rooms_.end()) {
                row["security"] = room->second.security;
                row["vault"] = room->second.vault;
            }
            for (const auto &item : point->candidates)
                row["candidates"].push_back({{"objectType", item.object_type},
                                              {"weight", item.weight}, {"crc", item.crc}});
            catalog_["points"].push_back(std::move(row));
        }
        if (!config_.enabled || point->disabled || !catalog_manager_ || crc_failed_) return;
        for (const auto &candidate : point->candidates) {
            if (candidate.crc != 0xffffffffu && candidate.crc !=
                    LootDistribution::object_type_crc(candidate.object_type)) {
                crc_failed_ = true;
                Output::send<LogLevel::Warning>(
                    STR("[Briefcase.LootControl] Unreal CRC differs; point changes disabled\n"));
                return;
            }
        }
        std::vector<LootCandidate> replacement;
        for (const auto &rule : config_.points) {
            if (!match(rule.map, current_map_) || !match(rule.path, point->path) ||
                !match(rule.implementation, point->implementation) ||
                !match(rule.point_type, point->point_type) ||
                !match(rule.original_selection, point->selection)) continue;
            if (!rule.original_object_type.empty() &&
                std::none_of(point->candidates.begin(), point->candidates.end(),
                    [&](const LootCandidate &candidate) {
                        return candidate.object_type == rule.original_object_type;
                    })) continue;
            const auto room = rooms_.find(point->room_crc);
            if (rule.vault && (room == rooms_.end() || room->second.vault != *rule.vault)) continue;
            if (rule.security && (room == rooms_.end() || room->second.security != *rule.security)) continue;
            for (const auto &candidate : rule.candidates)
                replacement.push_back({candidate.object_type, candidate.weight, 0});
            break;
        }
        if (replacement.empty() && point->selection == "Preset") {
            const auto room = rooms_.find(point->room_crc);
            if (room != rooms_.end()) for (const auto &rule : config_.presets) {
                if (rule.vault != room->second.vault || rule.security != room->second.security ||
                    !match(rule.point_type, point->point_type)) continue;
                for (const auto &[name, weight] : rule.weights)
                    replacement.push_back({name, weight, 0});
                break;
            }
        }
        if (replacement.empty()) return;
        if (!permitted(replacement)) {
            Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] unknown or invalid candidate at {}\n"),
                                            RC::to_wstring(point->path));
            return;
        }
        LootDistribution::replace_candidates(object, replacement);
        changed_points_.insert(object);
        ++changes_;
        if (changes_ <= 12)
            Output::send(STR("[Briefcase.LootControl] point changed: {}\n"), RC::to_wstring(point->path));
    }

    void after_setup(UObject *manager) {
        if (!LootDistribution::is_manager(manager) || manager != catalog_manager_ || catalog_written_) return;
        try {
            const auto serialized = catalog_.dump(2) + '\n';
            const auto write = [&](const std::filesystem::path &path) {
                std::filesystem::create_directories(path.parent_path());
                const auto temporary = path.string() + ".tmp";
                { std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                  if (!output) throw std::runtime_error("Cannot write catalog temporary file");
                  output << serialized; }
                std::filesystem::rename(temporary, path);
            };
            write(mod_file("catalog.json"));
            if (auto start = current_map_.find("LVL_"); start != std::string::npos) {
                auto end = start;
                while (end < current_map_.size() && end - start < 64 &&
                       (std::isalnum(static_cast<unsigned char>(current_map_[end])) ||
                        current_map_[end] == '_')) ++end;
                write(mod_file(("catalogs/" + current_map_.substr(start, end - start) + ".json").c_str()));
            }
            catalog_written_ = true;
            Output::send(STR("[Briefcase.LootControl] catalog written: {} objects, {} counts, "
                             "{} points; {} changes\n"), catalog_["objects"].size(),
                         catalog_["counts"].size(), catalog_["points"].size(), changes_);
        } catch (const std::exception &error) {
            Output::send<LogLevel::Warning>(STR("[Briefcase.LootControl] catalog failed: {}\n"),
                                            RC::to_wstring(error.what()));
        }
    }
};
} // namespace

extern "C" __declspec(dllexport) RC::CppUserModBase *start_mod() {
    try { return new LootControlUe4ss(); }
    catch (const std::exception &error) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[Briefcase.LootControl] load failed: {}\n"),
                                                RC::to_wstring(error.what()));
        return nullptr;
    } catch (...) { return nullptr; }
}
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase *mod) { delete mod; }

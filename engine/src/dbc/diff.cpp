#include "studio/dbc.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace studio {
namespace {

template <typename Element, typename KeyFunction>
void match_unique_keys(const std::vector<Element>& previous,
                       const std::vector<Element>& current,
                       std::vector<std::optional<std::size_t>>& previous_to_current,
                       std::vector<std::optional<std::size_t>>& current_to_previous,
                       KeyFunction key_function) {
    using Key = decltype(key_function(previous.front()));
    std::unordered_map<Key, std::vector<std::size_t>> previous_by_key;
    std::unordered_map<Key, std::vector<std::size_t>> current_by_key;
    for (std::size_t index = 0; index < previous.size(); ++index) {
        previous_by_key[key_function(previous[index])].push_back(index);
    }
    for (std::size_t index = 0; index < current.size(); ++index) {
        current_by_key[key_function(current[index])].push_back(index);
    }

    for (std::size_t previous_index = 0; previous_index < previous.size(); ++previous_index) {
        if (previous_to_current[previous_index]) continue;
        const auto key = key_function(previous[previous_index]);
        const auto old_matches = previous_by_key.find(key);
        const auto new_matches = current_by_key.find(key);
        if (old_matches == previous_by_key.end() || old_matches->second.size() != 1 ||
            new_matches == current_by_key.end() || new_matches->second.size() != 1) {
            continue;
        }
        const auto current_index = new_matches->second.front();
        if (current_to_previous[current_index]) continue;
        previous_to_current[previous_index] = current_index;
        current_to_previous[current_index] = previous_index;
    }
}

std::vector<std::string> semantic_receivers(const std::vector<std::string>& receivers) {
    auto result = receivers;
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<DbcSignalChange> diff_signals(const std::vector<DbcSignal>& previous,
                                          const std::vector<DbcSignal>& current) {
    std::vector<std::optional<std::size_t>> previous_to_current(previous.size());
    std::vector<std::optional<std::size_t>> current_to_previous(current.size());

    match_unique_keys(previous, current, previous_to_current, current_to_previous,
                      [](const DbcSignal& signal) { return signal.name; });
    match_unique_keys(previous, current, previous_to_current, current_to_previous,
                      [](const DbcSignal& signal) { return signal.start_bit; });

    std::vector<DbcSignalChange> changes;
    changes.reserve(previous.size() + current.size());
    for (std::size_t previous_index = 0; previous_index < previous.size(); ++previous_index) {
        const auto& old_signal = previous[previous_index];
        if (!previous_to_current[previous_index]) {
            changes.push_back({DbcChangeKind::removed, old_signal.name, {}});
            continue;
        }

        const auto& new_signal = current[*previous_to_current[previous_index]];
        DbcSignalChange change{DbcChangeKind::unchanged, old_signal.name, {}};
        if (old_signal.name != new_signal.name) change.changed_fields.emplace_back("name");
        if (old_signal.start_bit != new_signal.start_bit) change.changed_fields.emplace_back("startBit");
        if (old_signal.bit_length != new_signal.bit_length) change.changed_fields.emplace_back("bitLength");
        if (old_signal.little_endian != new_signal.little_endian) change.changed_fields.emplace_back("byteOrder");
        if (old_signal.signed_value != new_signal.signed_value) change.changed_fields.emplace_back("signed");
        if (old_signal.factor != new_signal.factor) change.changed_fields.emplace_back("factor");
        if (old_signal.offset != new_signal.offset) change.changed_fields.emplace_back("offset");
        if (old_signal.minimum != new_signal.minimum) change.changed_fields.emplace_back("minimum");
        if (old_signal.maximum != new_signal.maximum) change.changed_fields.emplace_back("maximum");
        if (old_signal.unit != new_signal.unit) change.changed_fields.emplace_back("unit");
        if (old_signal.multiplex != new_signal.multiplex) change.changed_fields.emplace_back("multiplex");
        if (semantic_receivers(old_signal.receivers) != semantic_receivers(new_signal.receivers)) {
            change.changed_fields.emplace_back("receivers");
        }
        if (!change.changed_fields.empty()) change.kind = DbcChangeKind::modified;
        changes.push_back(std::move(change));
    }

    for (std::size_t current_index = 0; current_index < current.size(); ++current_index) {
        if (!current_to_previous[current_index]) {
            changes.push_back({DbcChangeKind::added, current[current_index].name, {}});
        }
    }
    return changes;
}

DbcMessageChange make_message_change(const DbcMessage& previous,
                                    const DbcMessage& current) {
    DbcMessageChange change;
    change.message_name = previous.name;
    change.match_basis = previous.name == current.name ? "name" : "frameId";
    change.previous_frame_id = previous.frame_id;
    change.current_frame_id = current.frame_id;
    change.signal_changes = diff_signals(previous.signals, current.signals);

    if (previous.frame_id != current.frame_id) change.changed_fields.emplace_back("frameId");
    if (previous.name != current.name) {
        change.message_name = current.name;
        change.changed_fields.emplace_back("name");
    }
    if (previous.dlc_bytes != current.dlc_bytes) change.changed_fields.emplace_back("dlcBytes");
    if (previous.transmitter != current.transmitter) change.changed_fields.emplace_back("transmitter");
    if (std::any_of(change.signal_changes.begin(), change.signal_changes.end(),
                    [](const DbcSignalChange& signal) {
                        return signal.kind != DbcChangeKind::unchanged;
                    })) {
        change.changed_fields.emplace_back("signals");
    }
    change.kind = change.changed_fields.empty()
        ? DbcChangeKind::unchanged
        : DbcChangeKind::modified;
    return change;
}

Json optional_id_json(const std::optional<std::uint32_t>& value) {
    return value ? Json(static_cast<double>(*value)) : Json(nullptr);
}

} // namespace

std::vector<DbcBatchFinding> analyze_dbc_batch(
    const std::vector<DbcBatchSource>& sources) {
    std::vector<DbcBatchFinding> findings;
    const auto collect_candidates = [&findings, &sources](const std::string& identity,
                                                          auto key_function) {
        std::map<std::string, std::set<std::size_t>> sources_by_key;
        for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
            for (const auto& message : sources[source_index].parsed.messages) {
                sources_by_key[key_function(message)].insert(source_index);
            }
        }
        for (const auto& [key, source_indices] : sources_by_key) {
            if (source_indices.size() < 2) continue;
            findings.push_back({
                identity,
                key,
                {source_indices.begin(), source_indices.end()},
            });
        }
    };

    collect_candidates("frameId", [](const DbcMessage& message) {
        return std::to_string(message.frame_id);
    });
    collect_candidates("messageName", [](const DbcMessage& message) {
        return message.name;
    });
    return findings;
}

const char* to_string(DbcChangeKind kind) {
    switch (kind) {
    case DbcChangeKind::added: return "added";
    case DbcChangeKind::removed: return "removed";
    case DbcChangeKind::modified: return "modified";
    case DbcChangeKind::unchanged: return "unchanged";
    }
    return "unchanged";
}

DbcDiffResult diff_dbc(const DbcParseResult& previous, const DbcParseResult& current) {
    std::vector<std::optional<std::size_t>> previous_to_current(previous.messages.size());
    std::vector<std::optional<std::size_t>> current_to_previous(current.messages.size());

    match_unique_keys(previous.messages, current.messages, previous_to_current,
                      current_to_previous,
                      [](const DbcMessage& message) { return message.name; });
    match_unique_keys(previous.messages, current.messages, previous_to_current,
                      current_to_previous,
                      [](const DbcMessage& message) { return message.frame_id; });

    DbcDiffResult result;
    result.messages.reserve(previous.messages.size() + current.messages.size());
    for (std::size_t previous_index = 0;
         previous_index < previous.messages.size(); ++previous_index) {
        const auto& old_message = previous.messages[previous_index];
        if (!previous_to_current[previous_index]) {
            result.messages.push_back({
                DbcChangeKind::removed,
                old_message.name,
                "none",
                old_message.frame_id,
                std::nullopt,
                {},
                diff_signals(old_message.signals, {}),
            });
            continue;
        }
        result.messages.push_back(make_message_change(
            old_message, current.messages[*previous_to_current[previous_index]]));
    }

    for (std::size_t current_index = 0; current_index < current.messages.size(); ++current_index) {
        if (!current_to_previous[current_index]) {
            const auto& message = current.messages[current_index];
            std::vector<DbcSignalChange> signals;
            signals.reserve(message.signals.size());
            for (const auto& signal : message.signals) {
                signals.push_back({DbcChangeKind::added, signal.name, {}});
            }
            result.messages.push_back({
                DbcChangeKind::added,
                message.name,
                "none",
                std::nullopt,
                message.frame_id,
                {},
                std::move(signals),
            });
        }
    }
    return result;
}

Json to_json(const DbcDiffResult& result) {
    Json::Array messages;
    messages.reserve(result.messages.size());
    for (const auto& message : result.messages) {
        Json::Array changed_fields;
        for (const auto& field : message.changed_fields) changed_fields.emplace_back(field);

        Json::Array signal_changes;
        for (const auto& signal : message.signal_changes) {
            Json::Array signal_fields;
            for (const auto& field : signal.changed_fields) signal_fields.emplace_back(field);
            signal_changes.emplace_back(Json::Object{
                {"changedFields", Json(std::move(signal_fields))},
                {"kind", to_string(signal.kind)},
                {"name", signal.name},
            });
        }

        messages.emplace_back(Json::Object{
            {"changedFields", Json(std::move(changed_fields))},
            {"currentFrameId", optional_id_json(message.current_frame_id)},
            {"kind", to_string(message.kind)},
            {"matchBasis", message.match_basis},
            {"name", message.message_name},
            {"previousFrameId", optional_id_json(message.previous_frame_id)},
            {"signals", Json(std::move(signal_changes))},
        });
    }
    return Json::Object{{"messages", Json(std::move(messages))}};
}

} // namespace studio

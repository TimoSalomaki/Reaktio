#include "reaktio/tools/InspectorPanel.hpp"

#include <sstream>
#include <utility>

namespace reaktio::tools {

void push_row(
    InspectorPanel& panel,
    std::string_view label,
    std::string value,
    InspectorRowSeverity severity) {
    InspectorRow row{};
    row.label = std::string(label);
    row.value = std::move(value);
    row.severity = severity;
    panel.rows.push_back(std::move(row));
}

std::string format_inspector_panel(const InspectorPanel& panel) {
    std::ostringstream stream;
    stream << panel.title;
    if (!panel.id.empty()) {
        stream << " [" << panel.id << "]";
    }
    stream << '\n';
    for (const InspectorRow& row : panel.rows) {
        stream << "  " << row.label << "=" << row.value;
        if (row.severity != InspectorRowSeverity::Info) {
            stream << " [" << to_string(row.severity) << "]";
        }
        stream << '\n';
    }
    for (const std::string& line : panel.body_lines) {
        stream << "    " << line << '\n';
    }
    return stream.str();
}

std::string format_inspector_panels(const std::vector<InspectorPanel>& panels) {
    std::ostringstream stream;
    bool first = true;
    for (const InspectorPanel& panel : panels) {
        if (panel.empty()) {
            continue;
        }
        if (!first) {
            stream << '\n';
        }
        stream << format_inspector_panel(panel);
        first = false;
    }
    return stream.str();
}

} // namespace reaktio::tools

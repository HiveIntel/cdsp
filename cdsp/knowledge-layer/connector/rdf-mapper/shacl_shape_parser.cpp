#include "shacl_shape_parser.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace rdf_mapper {

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream handle(path);
    if (!handle) {
        throw std::runtime_error("Cannot open SHACL file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << handle.rdbuf();
    return buffer.str();
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string stripComments(const std::string& input) {
    std::ostringstream output;
    std::istringstream lines(input);
    std::string line;

    while (std::getline(lines, line)) {
        bool in_string = false;
        bool in_angle_iri = false;
        std::string stripped;

        for (size_t index = 0; index < line.size(); ++index) {
            const char c = line[index];
            if (c == '"' && (index == 0 || line[index - 1] != '\\') && !in_angle_iri) {
                in_string = !in_string;
            }

            if (!in_string) {
                if (c == '<') {
                    in_angle_iri = true;
                } else if (c == '>') {
                    in_angle_iri = false;
                }
            }

            if (c == '#' && !in_string && !in_angle_iri) {
                break;
            }

            stripped += c;
        }

        output << stripped << "\n";
    }

    return output.str();
}

std::optional<std::string> findStringValue(const std::string& body,
                                           const std::string& predicate) {
    const std::regex pattern(predicate + "\\s+\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(body, match, pattern)) {
        return match[1].str();
    }

    return std::nullopt;
}

std::optional<std::string> findIriValue(const std::string& body,
                                        const std::string& predicate) {
    const std::regex pattern(predicate + R"(\s+([A-Za-z][A-Za-z0-9_-]*:[A-Za-z0-9_.-]+))");
    std::smatch match;
    if (std::regex_search(body, match, pattern)) {
        return match[1].str();
    }

    return std::nullopt;
}

std::vector<std::pair<size_t, size_t>> propertyBlockRanges(const std::string& body) {
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t search_from = 0;

    while (true) {
        const auto property_pos = body.find("sh:property", search_from);
        if (property_pos == std::string::npos) {
            break;
        }

        const auto open = body.find('[', property_pos);
        if (open == std::string::npos) {
            throw std::runtime_error("Invalid SHACL property shape: missing '['.");
        }

        int depth = 0;
        bool found_close = false;
        for (size_t index = open; index < body.size(); ++index) {
            if (body[index] == '[') {
                ++depth;
            } else if (body[index] == ']') {
                --depth;
                if (depth == 0) {
                    ranges.push_back({property_pos, index + 1});
                    search_from = index + 1;
                    found_close = true;
                    break;
                }
            }
        }

        if (!found_close) {
            throw std::runtime_error("Invalid SHACL property shape: missing ']'.");
        }
    }

    return ranges;
}

std::string removePropertyBlocks(const std::string& body,
                                 const std::vector<std::pair<size_t, size_t>>& ranges) {
    std::string output;
    size_t cursor = 0;

    for (const auto& range : ranges) {
        output += body.substr(cursor, range.first - cursor);
        cursor = range.second;
    }

    output += body.substr(cursor);
    return output;
}

std::vector<std::string> extractPropertyBlocks(
    const std::string& body,
    const std::vector<std::pair<size_t, size_t>>& ranges) {
    std::vector<std::string> blocks;

    for (const auto& range : ranges) {
        const std::string raw = body.substr(range.first, range.second - range.first);
        const auto open = raw.find('[');
        const auto close = raw.rfind(']');
        blocks.push_back(raw.substr(open + 1, close - open - 1));
    }

    return blocks;
}

PropertyShapeDef parsePropertyShape(const std::string& block) {
    PropertyShapeDef property;
    property.name = findStringValue(block, "sh:name").value_or("");
    property.path_iri = findIriValue(block, "sh:path").value_or("");

    if (property.path_iri.empty()) {
        throw std::runtime_error("Invalid property shape: missing sh:path.");
    }

    if (auto constant = findIriValue(block, "sh:hasValue")) {
        property.value_kind = PropertyValueKind::ConstantIri;
        property.constant_iri = constant;
        return property;
    }

    if (auto node_shape = findIriValue(block, "sh:node")) {
        property.value_kind = PropertyValueKind::GeneratedNodeIri;
        property.node_shape_iri = node_shape;
        return property;
    }

    if (auto datatype = findIriValue(block, "sh:datatype")) {
        if (property.name.empty()) {
            throw std::runtime_error(
                "Input literal property " + property.path_iri + " is missing sh:name.");
        }

        property.value_kind = PropertyValueKind::InputLiteral;
        property.datatype_iri = datatype;
        return property;
    }

    throw std::runtime_error(
        "Invalid property shape " + property.path_iri +
        ": expected one of sh:hasValue, sh:node, or sh:datatype.");
}

void mergePrefixes(const std::string& text, ShapeRegistry& registry) {
    const std::regex prefix_pattern(
        R"(@prefix\s+([A-Za-z][A-Za-z0-9_-]*):\s+<([^>]+)>\s*\.)");
    for (std::sregex_iterator it(text.begin(), text.end(), prefix_pattern), end; it != end; ++it) {
        registry.prefixes[(*it)[1].str()] = (*it)[2].str();
    }
}

std::vector<std::string> splitTopLevelStatements(const std::string& text) {
    std::vector<std::string> statements;
    std::string current;
    bool in_string = false;
    bool in_angle_iri = false;
    int bracket_depth = 0;

    for (size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        current += c;

        if (c == '"' && (index == 0 || text[index - 1] != '\\') && !in_angle_iri) {
            in_string = !in_string;
            continue;
        }

        if (!in_string) {
            if (c == '<') {
                in_angle_iri = true;
            } else if (c == '>') {
                in_angle_iri = false;
            } else if (!in_angle_iri) {
                if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '.' && bracket_depth == 0) {
                    statements.push_back(trim(current));
                    current.clear();
                }
            }
        }
    }

    if (!trim(current).empty()) {
        statements.push_back(trim(current));
    }

    return statements;
}

}  // namespace

ShapeRegistry ShaclShapeParser::loadFiles(const std::vector<std::filesystem::path>& paths) const {
    ShapeRegistry registry;
    for (const auto& path : paths) {
        parseFile(path, registry);
    }
    return registry;
}

ShapeRegistry ShaclShapeParser::loadDirectory(const std::filesystem::path& directory) const {
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("SHACL directory does not exist: " + directory.string());
    }

    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ttl") {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    return loadFiles(paths);
}

void ShaclShapeParser::parseFile(const std::filesystem::path& path, ShapeRegistry& registry) const {
    const std::string text = stripComments(readTextFile(path));
    mergePrefixes(text, registry);

    for (const auto& raw_statement : splitTopLevelStatements(text)) {
        const std::string statement = trim(raw_statement);
        const auto marker = statement.find(" a sh:NodeShape");
        if (marker == std::string::npos) {
            continue;
        }

        const std::string shape_iri = trim(statement.substr(0, marker));
        std::string body = statement.substr(marker + std::string(" a sh:NodeShape").size());
        if (!body.empty() && body.back() == '.') {
            body.pop_back();
        }
        body = trim(body);
        if (!body.empty() && body.front() == ';') {
            body = trim(body.substr(1));
        }

        const auto ranges = propertyBlockRanges(body);
        const std::string top_level = removePropertyBlocks(body, ranges);

        NodeShapeDef shape;
        shape.shape_iri = shape_iri;
        shape.name = findStringValue(top_level, "sh:name").value_or("");
        shape.iri_prefix = findStringValue(top_level, "val:iriPrefix").value_or("");
        shape.class_iri = findIriValue(top_level, "sh:class");
        shape.target_class_iri = findIriValue(top_level, "sh:targetClass");

        // TODO(rdf-mapper): Validation-only SHACL shapes do not define the mapper-specific
        // val:iriPrefix. Keep them available as input configuration, but do not register them as
        // executable RDF-mapping shapes.
        if (shape.iri_prefix.empty()) {
            continue;
        }
        if (!shape.class_iri.has_value() && !shape.target_class_iri.has_value()) {
            throw std::runtime_error(
                "Node shape " + shape.shape_iri + " is missing sh:class or sh:targetClass.");
        }
        if (shape.name.empty()) {
            throw std::runtime_error("Node shape " + shape.shape_iri + " is missing sh:name.");
        }
        for (const auto& block : extractPropertyBlocks(body, ranges)) {
            shape.properties.push_back(parsePropertyShape(block));
        }

        registry.node_shapes_by_iri[shape.shape_iri] = shape;
        if (shape.target_class_iri.has_value()) {
            registry.target_shapes_by_name[shape.name] = shape.shape_iri;
        }
    }
}

}  // namespace rdf_mapper

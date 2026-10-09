#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "shacl_parser.h"

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open input file: " + path.string());
    }

    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("Cannot read input file: " + path.string());
    }
    return content.str();
}

std::vector<std::filesystem::path> findTurtleFiles(const std::filesystem::path& shape_path) {
    if (std::filesystem::is_regular_file(shape_path)) {
        return {shape_path};
    }

    if (!std::filesystem::is_directory(shape_path)) {
        throw std::runtime_error("SHACL path is not a file or directory: " + shape_path.string());
    }

    std::vector<std::filesystem::path> shape_files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(shape_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ttl") {
            shape_files.push_back(entry.path());
        }
    }

    std::sort(shape_files.begin(), shape_files.end());
    return shape_files;
}

void printRdfNode(std::string_view label, const rdf_mapper::RDFNode& node,
                  std::size_t indentation) {
    std::cout << std::string(indentation, ' ') << label << ": " << node << '\n';
}

void printOptionalRdfNode(std::string_view label, const std::optional<rdf_mapper::RDFNode>& node,
                          std::size_t indentation) {
    if (node.has_value()) {
        printRdfNode(label, *node, indentation);
        return;
    }

    std::cout << std::string(indentation, ' ') << label << ": <none>\n";
}

void printPropertyShape(const rdf_mapper::PropertyShape& property, std::size_t index) {
    std::cout << "    PropertyShape[" << index << "]\n";
    printRdfNode("sh:path", property.sh_path, 8);

    const rdf_mapper::NodeShapePtr referenced_shape = property.sh_node.lock();
    if (referenced_shape) {
        printRdfNode("sh:node -> sh:targetClass", referenced_shape->sh_target_class, 8);
    } else {
        std::cout << "        sh:node: <none>\n";
    }

    printOptionalRdfNode("sh:hasValue", property.sh_has_value, 8);
    printOptionalRdfNode("sh:datatype", property.sh_datatype, 8);

    std::cout << "        vss:value: ";
    if (property.vss_value.has_value()) {
        std::cout << *property.vss_value << '\n';
    } else {
        std::cout << "<none>\n";
    }
}

void printNodeShape(const rdf_mapper::NodeShape& shape, std::size_t index) {
    std::cout << "  NodeShape[" << index << "]\n";
    printRdfNode("sh:targetClass", shape.sh_target_class, 4);
    printOptionalRdfNode("vss:path", shape.vss_path, 4);
    std::cout << "    sh:properties: " << shape.sh_properties.size() << '\n';

    for (std::size_t property_index = 0; property_index < shape.sh_properties.size();
         ++property_index) {
        printPropertyShape(shape.sh_properties[property_index], property_index);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <shacl-file-or-directory>\n";
        return 2;
    }

    try {
        const std::filesystem::path shape_path = argv[1];
        const std::vector<std::filesystem::path> shape_files = findTurtleFiles(shape_path);

        if (shape_files.empty()) {
            throw std::runtime_error("No Turtle files found at: " + shape_path.string());
        }

        rdf_mapper::SHACLParser parser;

        for (const std::filesystem::path& shape_file : shape_files) {
            const std::vector<rdf_mapper::NodeShapePtr> shapes = parser.parse(readFile(shape_file));

            std::cout << "SHACL source: " << shape_file.string() << '\n'
                      << "NodeShapes: " << shapes.size() << '\n';

            for (std::size_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
                if (!shapes[shape_index]) {
                    throw std::runtime_error("SHACL parser returned a null NodeShape");
                }
                printNodeShape(*shapes[shape_index], shape_index);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "SHACL parser demo failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

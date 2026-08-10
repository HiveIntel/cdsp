#include "rdf_generator.h"

#include <cctype>
#include <stdexcept>

namespace rdf_mapper {

namespace {

constexpr const char* kRdfTypeUri = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool isAngleIri(const std::string& value) {
    return value.size() >= 2 && value.front() == '<' && value.back() == '>';
}

bool isAbsoluteIri(const std::string& value) {
    return startsWith(value, "http://") || startsWith(value, "https://") ||
           startsWith(value, "urn:");
}

RdfNode resourceNode(const std::string& value) {
    if (isAngleIri(value) || isAbsoluteIri(value)) {
        return RdfNode::iri(value);
    }

    return RdfNode::curie(value);
}

RdfNode typeNodeForShape(const NodeShapeDef& shape) {
    if (shape.target_class_iri.has_value()) {
        return resourceNode(shape.target_class_iri.value());
    }
    if (shape.class_iri.has_value()) {
        return resourceNode(shape.class_iri.value());
    }

    throw std::runtime_error("Shape " + shape.shape_iri + " is missing a generated RDF type.");
}

std::string sanitizeLocalSuffix(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());

    for (const unsigned char c : value) {
        if (std::isalnum(c) != 0 || c == '_' || c == '-') {
            sanitized.push_back(static_cast<char>(c));
        } else {
            sanitized.push_back('_');
        }
    }

    return sanitized;
}

std::string subjectValueForShape(const NodeShapeDef& shape, const InputMessage& message) {
    if (shape.target_class_iri.has_value()) {
        const auto timestamp = message.values.find("timestamp");
        if (timestamp != message.values.end() && !timestamp->second.empty()) {
            return sanitizeLocalSuffix(timestamp->second);
        }
    }

    const auto named_value = message.values.find(shape.name);
    if (named_value != message.values.end() && !named_value->second.empty()) {
        return sanitizeLocalSuffix(named_value->second);
    }

    const auto id_value = message.values.find("id");
    if (id_value != message.values.end() && !id_value->second.empty()) {
        return sanitizeLocalSuffix(id_value->second);
    }

    throw std::runtime_error("Missing input field for shape subject: " + shape.name);
}

RdfTriple typeTriple(const std::string& subject, const NodeShapeDef& shape) {
    return {
        resourceNode(subject),
        RdfNode::iri(kRdfTypeUri),
        typeNodeForShape(shape),
    };
}

RdfTriple propertyTriple(const std::string& subject,
                         const PropertyShapeDef& property,
                         const InputMessage& message,
                         const std::string& object_subject = "") {
    switch (property.value_kind) {
        case PropertyValueKind::ConstantIri:
            if (!property.constant_iri.has_value()) {
                throw std::runtime_error("Property " + property.name + " is missing sh:hasValue.");
            }
            return {
                resourceNode(subject),
                resourceNode(property.path_iri),
                resourceNode(property.constant_iri.value()),
            };

        case PropertyValueKind::InputLiteral:
            if (!property.datatype_iri.has_value()) {
                throw std::runtime_error("Property " + property.name + " is missing sh:datatype.");
            }
            return {
                resourceNode(subject),
                resourceNode(property.path_iri),
                RdfNode::literal(message.values.at(property.name), property.datatype_iri.value()),
            };

        case PropertyValueKind::GeneratedNodeIri:
            if (object_subject.empty()) {
                throw std::runtime_error(
                    "Generated node property " + property.path_iri + " has no object subject.");
            }
            return {
                resourceNode(subject),
                resourceNode(property.path_iri),
                resourceNode(object_subject),
            };
    }

    throw std::runtime_error("Unsupported property value kind.");
}

}  // namespace

RdfGenerator::RdfGenerator(const ShapeRegistry& registry) : registry_(registry) {}

std::vector<RdfTriple> RdfGenerator::generate(const InputMessage& message, RdfFormat format) const {
    (void) format;

    std::vector<RdfTriple> triples;
    std::unordered_map<std::string, std::string> generated_nodes;
    std::unordered_set<std::string> visiting_nodes;

    generateNode(shapeForMessage(message), message, generated_nodes, visiting_nodes, triples);
    return triples;
}

const NodeShapeDef& RdfGenerator::shapeForMessage(const InputMessage& message) const {
    const std::string signal = requiredValue(message, "signal");
    const auto target_it = registry_.target_shapes_by_name.find(signal);
    if (target_it == registry_.target_shapes_by_name.end()) {
        throw std::runtime_error("Unknown signal: " + signal);
    }

    const auto shape_it = registry_.node_shapes_by_iri.find(target_it->second);
    if (shape_it == registry_.node_shapes_by_iri.end()) {
        throw std::runtime_error("Missing target shape: " + target_it->second);
    }

    return shape_it->second;
}

std::string RdfGenerator::generateNode(
    const NodeShapeDef& shape,
    const InputMessage& message,
    std::unordered_map<std::string, std::string>& generated_nodes,
    std::unordered_set<std::string>& visiting_nodes,
    std::vector<RdfTriple>& triples) const {
    const std::string id_value = subjectValueForShape(shape, message);
    const std::string node_key = shape.shape_iri + "|" + id_value;
    const std::string subject = shape.iri_prefix + id_value;

    if (visiting_nodes.find(node_key) != visiting_nodes.end()) {
        return subject;
    }

    if (generated_nodes.find(node_key) != generated_nodes.end()) {
        return subject;
    }

    visiting_nodes.insert(node_key);

    for (const auto& [_, parent_shape] : registry_.node_shapes_by_iri) {
        if (parent_shape.shape_iri == shape.shape_iri ||
            parent_shape.target_class_iri.has_value()) {
            continue;
        }

        for (const auto& property : parent_shape.properties) {
            if (property.value_kind != PropertyValueKind::GeneratedNodeIri ||
                !property.node_shape_iri.has_value() ||
                property.node_shape_iri.value() != shape.shape_iri) {
                continue;
            }

            generateNode(parent_shape, message, generated_nodes, visiting_nodes, triples);
        }
    }

    generated_nodes[node_key] = subject;
    triples.push_back(typeTriple(subject, shape));

    for (const auto& property : shape.properties) {
        switch (property.value_kind) {
            case PropertyValueKind::ConstantIri:
                triples.push_back(propertyTriple(subject, property, message));
                break;

            case PropertyValueKind::InputLiteral:
                requiredValue(message, property.name);
                triples.push_back(propertyTriple(subject, property, message));
                break;

            case PropertyValueKind::GeneratedNodeIri: {
                if (!property.node_shape_iri.has_value()) {
                    throw std::runtime_error("Property " + property.name + " is missing sh:node.");
                }

                const auto child_shape_it = registry_.node_shapes_by_iri.find(
                    property.node_shape_iri.value());
                if (child_shape_it == registry_.node_shapes_by_iri.end()) {
                    throw std::runtime_error(
                        "Missing node shape: " + property.node_shape_iri.value());
                }

                const std::string child_subject = generateNode(
                    child_shape_it->second,
                    message,
                    generated_nodes,
                    visiting_nodes,
                    triples);
                triples.push_back(propertyTriple(subject, property, message, child_subject));
                break;
            }
        }
    }

    visiting_nodes.erase(node_key);
    return subject;
}

std::string RdfGenerator::requiredValue(const InputMessage& message,
                                        const std::string& field) const {
    const auto found = message.values.find(field);
    if (found == message.values.end() || found->second.empty()) {
        throw std::runtime_error("Missing input field: " + field);
    }

    return found->second;
}

}  // namespace rdf_mapper

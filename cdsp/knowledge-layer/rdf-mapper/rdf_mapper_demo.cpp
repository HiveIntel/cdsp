#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rdf_mapper.h"

namespace {

constexpr auto kGeneratedSeconds = 1776429129;
constexpr auto kGeneratedNanos = 853211000;

// Accept a flat fixture directory as well as the module's grouped fixture layout.
std::filesystem::path fixturePath(const std::filesystem::path& directory, std::string_view group,
                                  std::string_view filename) {
    const auto flat_path = directory / filename;
    return std::filesystem::is_regular_file(flat_path) ? flat_path : directory / group / filename;
}

// Use typed data with the exact vss:path keys declared by the supplied SHACL files.
il_message::DataMessageDTO sampleMessage(std::string request_id, const std::string& vehicle_id,
                                         std::vector<il_data_point::DataPointDTO> points) {
    const auto generated = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds{kGeneratedSeconds} + std::chrono::nanoseconds{kGeneratedNanos})};

    il_message::DataMessageDTO message{il_message::ILMessageDTO{std::move(request_id), "2.0"},
                                       vehicle_id, std::move(points),
                                       std::vector<il_data_point::MetadataPointDTO>{}};
    for (const auto& point : message.data) {
        message.metadata->push_back(
            {point.path, il_data_point::TimestampsDTO{std::nullopt, generated}});
    }
    return message;
}

// RDFTriple's stream operator is diagnostic output, not a Turtle/N-Triples writer.
void printTriples(std::string_view label, const std::vector<rdf_mapper::RDFTriple>& triples) {
    std::cout << '\n' << label << " triples (" << triples.size() << "):\n";
    for (std::size_t index = 0; index < triples.size(); ++index) {
        std::cout << "  [" << index << "] " << triples[index] << '\n';
    }
}

/// Keep serialized output visibly separate from RDFTriple's diagnostic representation.
void printRdf(std::string_view label, std::string_view format, const std::string& document) {
    std::cout << '\n' << label << " RDF (" << format << "):\n" << document;
    if (document.empty() || document.back() != '\n') {
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <fixture-directory> [vehicle-id] [instance-base-iri]"
                     " [turtle|ntriples|nquads|trig]\n";
        return 2;
    }

    try {
        const std::filesystem::path fixture_directory = argv[1];
        const std::string vehicle_id = argc > 2 ? argv[2] : "VIN_12345";
        const std::string instance_base_iri = argc > 3 ? argv[3] : "https://example.org/instances/";
        const std::string_view format_name = argc > 4 ? argv[4] : "turtle";
        rdf_mapper::RDFFormat format;
        if (format_name == "turtle") {
            format = rdf_mapper::RDFFormat::Turtle;
        } else if (format_name == "ntriples") {
            format = rdf_mapper::RDFFormat::NTriples;
        } else if (format_name == "nquads") {
            format = rdf_mapper::RDFFormat::NQuads;
        } else if (format_name == "trig") {
            format = rdf_mapper::RDFFormat::TriG;
        } else {
            throw std::invalid_argument("Unsupported demo output format: " +
                                        std::string(format_name));
        }
        const auto vehicle_file = fixturePath(fixture_directory, "vehicle_shacl", "vehicle_kb.ttl");
        const auto speed_file = fixturePath(fixture_directory, "signal_shacl", "speed_obs.ttl");
        const auto location_file =
            fixturePath(fixture_directory, "signal_shacl", "current_location_obs.ttl");

        // The mappers validate/read both input files and retain their parsed shape graphs.
        const rdf_mapper::RDFMapper speed_mapper(vehicle_file, speed_file, instance_base_iri);
        const rdf_mapper::RDFMapper location_mapper(vehicle_file, location_file, instance_base_iri);
        const auto speed_message =
            sampleMessage("demo-speed", vehicle_id, {{"Vehicle.Speed", 42.5}});
        const auto location_message = sampleMessage("demo-location", vehicle_id,
                                                    {{"Vehicle.CurrentLocation.Latitude", 60.0},
                                                     {"Vehicle.CurrentLocation.Longitude", 10.0}});

        // Both mappers share the same vehicle shapes; print the structural graph once.
        const auto vehicle_triples = speed_mapper.vehicleToTriples(vehicle_id);
        const auto speed_triples = speed_mapper.signalToTriples(speed_message, vehicle_id);
        const auto location_triples = location_mapper.signalToTriples(location_message, vehicle_id);
        // Exercise the public convenience methods, not just the writer in isolation.
        const auto vehicle_rdf = speed_mapper.vehicleToRdf(vehicle_id, format);
        const auto speed_rdf = speed_mapper.signalToRdf(speed_message, vehicle_id, format);
        const auto location_rdf = location_mapper.signalToRdf(location_message, vehicle_id, format);

        std::cout << "RDF mapper demo: triples and Serd serialization\n"
                  << "Vehicle SHACL source: " << vehicle_file.string() << '\n'
                  << "Speed SHACL source: " << speed_file.string() << '\n'
                  << "Location SHACL source: " << location_file.string() << '\n'
                  << "Vehicle ID: " << vehicle_id << '\n'
                  << "Instance base IRI: " << instance_base_iri << '\n'
                  << "Sample generated time: seconds=" << kGeneratedSeconds
                  << ", nanos=" << kGeneratedNanos << '\n';
        printTriples("Vehicle", vehicle_triples);
        printTriples("Speed", speed_triples);
        printTriples("Current location", location_triples);
        printRdf("Vehicle", format_name, vehicle_rdf);
        printRdf("Speed", format_name, speed_rdf);
        printRdf("Current location", format_name, location_rdf);
    } catch (const std::exception& error) {
        std::cerr << "RDF mapper demo failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

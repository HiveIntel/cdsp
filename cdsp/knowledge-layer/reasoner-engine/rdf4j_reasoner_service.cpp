#include "rdf4j_reasoner_service.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot read RDF4J profile resource: " + path);
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string endpointHost(const ReasonerProfile::Endpoint& endpoint) {
    const char* override = std::getenv("RDF4J_HOST");
    return override != nullptr && *override != '\0' ? override : endpoint.host;
}

unsigned short endpointPort(const ReasonerProfile::Endpoint& endpoint) {
    const char* override = std::getenv("RDF4J_PORT");
    if (override == nullptr || *override == '\0') {
        return endpoint.port;
    }
    const unsigned long value = std::stoul(override);
    if (value == 0 || value > 65535) {
        throw std::invalid_argument("RDF4J_PORT must be in range 1..65535.");
    }
    return static_cast<unsigned short>(value);
}

http::verb parseMethod(const std::string& method) {
    if (method == "POST") {
        return http::verb::post;
    }
    throw std::invalid_argument("Unsupported RDF4J HTTP method: " + method);
}

}  // namespace

std::string RDF4JReasonerService::datastoreTarget(const std::string& suffix) const {
    return "/datastores/" + reasoner_profile_.endpoint.datastore + suffix;
}

std::string RDF4JReasonerService::request(const std::string& method,
                                          const std::string& target,
                                          const std::string& content_type,
                                          const std::string& accept,
                                          const std::string& body) const {
    net::io_context io_context;
    tcp::resolver resolver(io_context);
    beast::tcp_stream stream(io_context);
    const std::string host = endpointHost(reasoner_profile_.endpoint);
    const auto endpoints = resolver.resolve(host, std::to_string(endpointPort(reasoner_profile_.endpoint)));
    stream.connect(endpoints);

    http::request<http::string_body> request_message{parseMethod(method), target, 11};
    request_message.set(http::field::host, host);
    request_message.set(http::field::user_agent, "hiveintel-cdsp-rdf4j-adapter");
    request_message.set(http::field::content_type, content_type);
    request_message.set(http::field::accept, accept);
    request_message.body() = body;
    request_message.prepare_payload();
    http::write(stream, request_message);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);

    if (response.result_int() < 200 || response.result_int() >= 300) {
        throw std::runtime_error("RDF4J HTTP " + std::to_string(response.result_int()) +
                                 " for " + target + ": " + response.body());
    }
    return response.body();
}

bool RDF4JReasonerService::initializeReasoner() {
    try {
        request("POST", datastoreTarget(), "text/plain", "text/plain", "");
        for (const auto& ontology : reasoner_profile_.ontologies) {
            request("POST", datastoreTarget("/content"), "text/turtle", "text/plain",
                    readFile(ontology.string()));
        }
        return updateRulesInReasoner(
            [&]() {
                std::vector<std::string> paths;
                paths.reserve(reasoner_profile_.rules.size());
                for (const auto& rule : reasoner_profile_.rules) {
                    paths.push_back(rule.string());
                }
                return paths;
            }());
    } catch (const std::exception& error) {
        std::cerr << "RDF4J initialization failed: " << error.what() << '\n';
        return false;
    }
}

bool RDF4JReasonerService::updateDataInReasoner(const std::string& data,
                                                 const ReasonerSyntaxType& content_type) {
    if (content_type != ReasonerSyntaxType::TURTLE) {
        std::cerr << "RDF4J adapter only accepts Turtle input.\n";
        return false;
    }
    try {
        request("POST", datastoreTarget("/content"), "text/turtle", "text/plain", data);
        return true;
    } catch (const std::exception& error) {
        std::cerr << "RDF4J content upload failed: " << error.what() << '\n';
        return false;
    }
}

std::vector<nlohmann::json> RDF4JReasonerService::queryReasoner() {
    if (reasoner_profile_.queries.empty()) {
        return {};
    }
    try {
        const auto response = request("POST", datastoreTarget("/sparql"),
                                      "application/sparql-query",
                                      "application/sparql-results+json",
                                      readFile(reasoner_profile_.queries.front().string()));
        const auto result = nlohmann::json::parse(response);
        if (!result.contains("results") || !result.at("results").contains("bindings") ||
            !result.at("results").at("bindings").is_array()) {
            throw std::runtime_error("RDF4J response does not contain SPARQL bindings.");
        }
        std::vector<nlohmann::json> bindings;
        for (const auto& binding : result.at("results").at("bindings")) {
            bindings.push_back(binding);
        }
        if (!bindings.empty()) {
            std::cout << "RDF4J detected " << bindings.size() << " driving-style segment(s).\n";
        }
        return bindings;
    } catch (const std::exception& error) {
        std::cerr << "RDF4J query failed: " << error.what() << '\n';
        return {};
    }
}

bool RDF4JReasonerService::updateRulesInReasoner(const std::vector<std::string>& rule_file_paths) {
    if (rule_file_paths.empty()) {
        return true;
    }
    try {
        request("POST", datastoreTarget("/rules/reset"), "text/turtle", "text/plain",
                readFile(rule_file_paths.front()));
        for (size_t index = 1; index < rule_file_paths.size(); ++index) {
            request("POST", datastoreTarget("/rules/add"), "text/turtle", "text/plain",
                    readFile(rule_file_paths[index]));
        }
        return true;
    } catch (const std::exception& error) {
        std::cerr << "RDF4J rule upload failed: " << error.what() << '\n';
        return false;
    }
}

bool RDF4JReasonerService::updateOntologyInReasoner(
    const std::vector<std::string>& ontology_file_paths) {
    try {
        for (const auto& ontology : ontology_file_paths) {
            request("POST", datastoreTarget("/content"), "text/turtle", "text/plain",
                    readFile(ontology));
        }
        return true;
    } catch (const std::exception& error) {
        std::cerr << "RDF4J ontology upload failed: " << error.what() << '\n';
        return false;
    }
}

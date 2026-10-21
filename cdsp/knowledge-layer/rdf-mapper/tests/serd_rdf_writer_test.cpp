#include "serd_rdf_writer.h"

#include <gtest/gtest.h>
#include <serd/serd.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper_test {
namespace {

using rdf_mapper::RDFNode;
using rdf_mapper::RDFTriple;

struct ReadContext {
    SerdEnv* environment;
    std::vector<RDFTriple> triples;
    std::exception_ptr callback_error;
    bool syntax_error = false;
};

std::string nodeText(const SerdNode& node) {
    return node.buf == nullptr ? std::string{}
                               : std::string{reinterpret_cast<const char*>(node.buf), node.n_bytes};
}

std::string expandedIri(ReadContext& context, const SerdNode& node) {
    SerdNode expanded = serd_env_expand_node(context.environment, &node);
    if (expanded.buf == nullptr) {
        throw std::runtime_error("Cannot expand serialized RDF IRI");
    }
    try {
        auto value = nodeText(expanded);
        serd_node_free(&expanded);
        return value;
    } catch (...) {
        serd_node_free(&expanded);
        throw;
    }
}

RDFNode resource(ReadContext& context, const SerdNode& node) {
    return node.type == SERD_BLANK ? RDFNode::blankNode(nodeText(node))
                                   : RDFNode::iri(expandedIri(context, node));
}

SerdStatus readBase(void* handle, const SerdNode* iri) noexcept {
    auto& context = *static_cast<ReadContext*>(handle);
    return serd_env_set_base_uri(context.environment, iri);
}

SerdStatus readPrefix(void* handle, const SerdNode* name, const SerdNode* iri) noexcept {
    auto& context = *static_cast<ReadContext*>(handle);
    return serd_env_set_prefix(context.environment, name, iri);
}

SerdStatus readStatement(void* handle, SerdStatementFlags, const SerdNode* graph,
                         const SerdNode* subject, const SerdNode* predicate, const SerdNode* object,
                         const SerdNode* datatype, const SerdNode* language) noexcept {
    auto& context = *static_cast<ReadContext*>(handle);
    try {
        // RDFTriple has no graph or language field; verify the writer never adds either.
        if (graph != nullptr || language != nullptr) {
            throw std::runtime_error("Unexpected named graph or language tag in serialized RDF");
        }
        RDFNode value;
        if (object->type == SERD_LITERAL) {
            std::optional<std::string> type;
            if (datatype != nullptr) {
                type = expandedIri(context, *datatype);
            }
            value = RDFNode::literal(nodeText(*object), std::move(type));
        } else {
            value = resource(context, *object);
        }
        context.triples.push_back(
            {resource(context, *subject), resource(context, *predicate), std::move(value)});
        return SERD_SUCCESS;
    } catch (...) {
        if (!context.callback_error) {
            context.callback_error = std::current_exception();
        }
        return SERD_ERR_UNKNOWN;
    }
}

SerdStatus readError(void* handle, const SerdError*) noexcept {
    static_cast<ReadContext*>(handle)->syntax_error = true;
    return SERD_SUCCESS;
}

}  // namespace

// Shared test-only parser: the production reader deliberately remains Turtle-only.
std::vector<rdf_mapper::RDFTriple> readSerializedRdf(std::string_view document,
                                                     rdf_mapper::RDFFormat format) {
    SerdSyntax syntax;
    switch (format) {
        case rdf_mapper::RDFFormat::Turtle:
            syntax = SERD_TURTLE;
            break;
        case rdf_mapper::RDFFormat::NTriples:
            syntax = SERD_NTRIPLES;
            break;
        case rdf_mapper::RDFFormat::NQuads:
            syntax = SERD_NQUADS;
            break;
        case rdf_mapper::RDFFormat::TriG:
            syntax = SERD_TRIG;
            break;
        default:
            throw std::invalid_argument("Unsupported test reader format");
    }
    // Zero statements are valid in all four grammars. Serd 0.30's string reader
    // mishandles the initial terminator for an empty buffer; do not invoke it here.
    if (document.empty()) {
        return {};
    }
    std::unique_ptr<SerdEnv, decltype(&serd_env_free)> environment{serd_env_new(nullptr),
                                                                   serd_env_free};
    if (!environment) {
        throw std::runtime_error("Cannot create test reader environment");
    }
    ReadContext context{environment.get(), {}, {}, false};
    std::unique_ptr<SerdReader, decltype(&serd_reader_free)> reader{
        serd_reader_new(syntax, &context, nullptr, readBase, readPrefix, readStatement, nullptr),
        serd_reader_free};
    if (!reader) {
        throw std::runtime_error("Cannot create test RDF reader");
    }
    serd_reader_set_strict(reader.get(), true);
    serd_reader_set_error_sink(reader.get(), readError, &context);
    const std::string input{document};
    const auto status =
        serd_reader_read_string(reader.get(), reinterpret_cast<const uint8_t*>(input.c_str()));
    if (context.callback_error) {
        std::rethrow_exception(context.callback_error);
    }
    if (status != SERD_SUCCESS || context.syntax_error) {
        throw std::runtime_error("Serialized RDF does not parse in its requested syntax");
    }
    return std::move(context.triples);
}

}  // namespace rdf_mapper_test

namespace {

using rdf_mapper::RDFFormat;
using rdf_mapper::RDFGraph;
using rdf_mapper::RDFNode;
using rdf_mapper::RDFNodeType;
using rdf_mapper::RDFTriple;
using rdf_mapper::SerdRdfWriter;
using rdf_mapper_test::readSerializedRdf;

const std::string kExample = "https://example.org/";
const std::string kXsd = "http://www.w3.org/2001/XMLSchema#";

RDFTriple triple(RDFNode object = RDFNode::literal("value")) {
    return {RDFNode::iri(kExample + "subject"), RDFNode::iri(kExample + "property"),
            std::move(object)};
}

RDFTriple normalizedStringLiteral(RDFTriple value) {
    // In RDF 1.1 a plain literal and an explicitly typed xsd:string are equivalent.
    if (value.object.datatype == kXsd + "string") {
        value.object.datatype.reset();
    }
    return value;
}

void expectGraph(const std::vector<RDFTriple>& actual, const std::vector<RDFTriple>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    std::vector<RDFTriple> normalized;
    for (const auto& value : actual) {
        normalized.push_back(normalizedStringLiteral(value));
    }
    for (const auto& value : expected) {
        EXPECT_EQ(std::count(normalized.begin(), normalized.end(), normalizedStringLiteral(value)),
                  1)
            << value;
    }
}

class SerdRdfWriterFormatTest : public ::testing::TestWithParam<RDFFormat> {};

TEST_P(SerdRdfWriterFormatTest, PreservesResourceAndTypedLiteralGraphInDefaultGraph) {
    const std::vector<RDFTriple> input{
        triple(RDFNode::iri(kExample + "object")),
        {RDFNode::iri(kExample + "other"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("42.5", kXsd + "float")},
        {RDFNode::iri(kExample + "other"), RDFNode::iri(kExample + "time"),
         RDFNode::literal("2026-04-17T12:32:09.853211000Z", kXsd + "dateTime")}};
    expectGraph(readSerializedRdf(SerdRdfWriter{}.write(input, GetParam()), GetParam()), input);
}

TEST_P(SerdRdfWriterFormatTest, PreservesEscapingUnicodeAndEmptyLiteral) {
    const std::vector<RDFTriple> input{
        triple(RDFNode::literal("quote\" slash\\ newline\nreturn\rtab\t Tiếng Việt 🚗")),
        {RDFNode::iri(kExample + "other"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("")},
        {RDFNode::iri(kExample + "typed"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("explicit string", kXsd + "string")}};
    expectGraph(readSerializedRdf(SerdRdfWriter{}.write(input, GetParam()), GetParam()), input);
}

TEST_P(SerdRdfWriterFormatTest, ExpandsBoundCuriesInEveryResourcePositionAndDatatype) {
    const SerdRdfWriter writer{{{"ex", kExample}, {"xsd", kXsd}}};
    const std::vector<RDFTriple> input{
        {RDFNode::curie("ex:subject"), RDFNode::curie("ex:property"), RDFNode::curie("ex:object")},
        {RDFNode::curie("ex:subject"), RDFNode::curie("ex:value"),
         RDFNode::literal("5", "xsd:integer")}};
    const std::vector<RDFTriple> expected{
        triple(RDFNode::iri(kExample + "object")),
        {RDFNode::iri(kExample + "subject"), RDFNode::iri(kExample + "value"),
         RDFNode::literal("5", kXsd + "integer")}};
    expectGraph(readSerializedRdf(writer.write(input, GetParam()), GetParam()), expected);
}

TEST_P(SerdRdfWriterFormatTest, PreservesBlankNodeIdentityAndDistinctLabels) {
    const std::vector<RDFTriple> input{
        triple(RDFNode::blankNode("shared_1")),
        {RDFNode::blankNode("shared_1"), RDFNode::iri(kExample + "label"),
         RDFNode::literal("shared")},
        {RDFNode::blankNode("other-2"), RDFNode::iri(kExample + "label"),
         RDFNode::literal("other")},
        {RDFNode::blankNode("shared_1"), RDFNode::iri(kExample + "next"),
         RDFNode::blankNode("other-2")}};
    const auto output = SerdRdfWriter{}.write(input, GetParam());
    const auto parsed = readSerializedRdf(output, GetParam());
    ASSERT_EQ(parsed.size(), input.size());
    const RDFGraph graph{parsed};
    const auto shared = graph.object(RDFNode::iri(kExample + "subject"), kExample + "property");
    ASSERT_TRUE(shared.has_value());
    ASSERT_EQ(shared->type, RDFNodeType::BlankNode);
    EXPECT_EQ(graph.object(*shared, kExample + "label"), RDFNode::literal("shared"));
    const auto other = graph.object(*shared, kExample + "next");
    ASSERT_TRUE(other.has_value());
    ASSERT_EQ(other->type, RDFNodeType::BlankNode);
    EXPECT_NE(other, shared);
    EXPECT_EQ(graph.object(*other, kExample + "label"), RDFNode::literal("other"));
    EXPECT_NE(output.find("shared_1"), std::string::npos);
    EXPECT_NE(output.find("other-2"), std::string::npos);
}

TEST_P(SerdRdfWriterFormatTest, PreservesAbsoluteUnicodeIrisAndCustomDatatypeScheme) {
    const std::vector<RDFTriple> input{
        {RDFNode::iri("https://example.org/xe/ô-tô"),
         RDFNode::iri("https://example.org/thuộc-tính"), RDFNode::iri("urn:vehicle:🚗")},
        triple(RDFNode::literal("opaque lexical value", "urn:custom:datatype")),
        {RDFNode::iri(kExample + "other"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("another lexical value", "unknown:type")},
        {RDFNode::iri("http://[::1]/vehicle"), RDFNode::iri(kExample + "property"),
         RDFNode::iri(kExample + "object")},
        {RDFNode::iri(kExample + "query"), RDFNode::iri(kExample + "property"),
         RDFNode::iri("urn:example?x=\xEE\x80\x80")}};
    expectGraph(readSerializedRdf(SerdRdfWriter{}.write(input, GetParam()), GetParam()), input);
}

TEST_P(SerdRdfWriterFormatTest, EmptyInputProducesAnEmptyGraph) {
    const auto document = SerdRdfWriter{}.write({}, GetParam());
    ASSERT_EQ(document, "");
    EXPECT_TRUE(readSerializedRdf(document, GetParam()).empty());
}

INSTANTIATE_TEST_SUITE_P(SupportedSyntaxes, SerdRdfWriterFormatTest,
                         ::testing::Values(RDFFormat::Turtle, RDFFormat::NTriples,
                                           RDFFormat::NQuads, RDFFormat::TriG),
                         [](const ::testing::TestParamInfo<RDFFormat>& info) {
                             switch (info.param) {
                                 case RDFFormat::Turtle:
                                     return "Turtle";
                                 case RDFFormat::NTriples:
                                     return "NTriples";
                                 case RDFFormat::NQuads:
                                     return "NQuads";
                                 case RDFFormat::TriG:
                                     return "TriG";
                             }
                             return "Unknown";
                         });

TEST(SerdRdfWriterTest, DefaultsToTurtle) {
    const std::vector<RDFTriple> input{triple()};
    expectGraph(readSerializedRdf(SerdRdfWriter{}.write(input), RDFFormat::Turtle), input);
}

TEST(SerdRdfWriterTest, SetPrefixReplacesBindingAndAcceptsDefaultPrefix) {
    SerdRdfWriter writer{{{"ex", "https://old.example.org/"}}};
    writer.setPrefix("ex", kExample);
    writer.setPrefix("", "urn:default:");
    const std::vector<RDFTriple> input{
        {RDFNode::curie("ex:subject"), RDFNode::curie("ex:property"), RDFNode::curie(":object")}};
    expectGraph(readSerializedRdf(writer.write(input), RDFFormat::Turtle),
                {triple(RDFNode::iri("urn:default:object"))});
}

TEST(SerdRdfWriterTest, AcceptsPortablePrefixNamesAndEmptyCurieLocalPart) {
    const SerdRdfWriter writer{{{"a_b-c.d9", kExample}}};
    expectGraph(
        readSerializedRdf(writer.write({triple(RDFNode::curie("a_b-c.d9:"))}), RDFFormat::Turtle),
        {triple(RDFNode::iri(kExample))});
}

TEST(SerdRdfWriterTest, BoundDatatypePrefixTakesPrecedenceOverItsSchemeInterpretation) {
    const SerdRdfWriter writer{{{"custom", "urn:expanded:"}}};
    expectGraph(readSerializedRdf(writer.write({triple(RDFNode::literal("value", "custom:type"))}),
                                  RDFFormat::Turtle),
                {triple(RDFNode::literal("value", "urn:expanded:type"))});
}

TEST(SerdRdfWriterTest, TurtleAndTriGPreserveLexicalFormsInsteadOfAbbreviatingTypedValues) {
    const std::vector<RDFTriple> input{
        triple(RDFNode::literal("1", kXsd + "boolean")),
        {RDFNode::iri(kExample + "integer"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("not integer; <urn:injection> <urn:p> <urn:o> .", kXsd + "integer")},
        {RDFNode::iri(kExample + "decimal"), RDFNode::iri(kExample + "property"),
         RDFNode::literal("not decimal", kXsd + "decimal")}};
    // Generic RDF serialization preserves even ill-typed literals; it is not an XSD validator.
    for (const auto format : {RDFFormat::Turtle, RDFFormat::TriG}) {
        SCOPED_TRACE(static_cast<int>(format));
        expectGraph(readSerializedRdf(SerdRdfWriter{}.write(input, format), format), input);
    }
}

TEST(SerdRdfWriterTest, AutomaticXsdBindingDoesNotOverwriteAnExistingPrefix) {
    const SerdRdfWriter writer{
        {{"xsd", "urn:not-the-xsd-namespace:"}, {"xsd1", "urn:also-not-the-xsd-namespace:"}}};
    const std::vector<RDFTriple> input{
        triple(RDFNode::literal("1", kXsd + "boolean")),
        {RDFNode::curie("xsd:subject"), RDFNode::curie("xsd1:property"),
         RDFNode::literal("not integer", kXsd + "integer")}};
    const std::vector<RDFTriple> expected{input.front(),
                                          {RDFNode::iri("urn:not-the-xsd-namespace:subject"),
                                           RDFNode::iri("urn:also-not-the-xsd-namespace:property"),
                                           RDFNode::literal("not integer", kXsd + "integer")}};
    expectGraph(readSerializedRdf(writer.write(input), RDFFormat::Turtle), expected);
}

TEST(SerdRdfWriterTest, RejectsLiteralSubjectsAndNonIriPredicates) {
    auto value = triple();
    value.subject = RDFNode::literal("invalid subject");
    EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    value = triple();
    value.predicate = RDFNode::literal("invalid predicate");
    EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    value.predicate = RDFNode::blankNode("invalidPredicate");
    EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
}

TEST(SerdRdfWriterTest, RejectsUnknownNodeTypesInEveryPosition) {
    for (int position = 0; position < 3; ++position) {
        auto value = triple();
        RDFNode* nodes[] = {&value.subject, &value.predicate, &value.object};
        nodes[position]->type = static_cast<RDFNodeType>(-1);
        EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    }
}

TEST(SerdRdfWriterTest, RejectsDatatypesOnNonLiteralNodes) {
    for (int position = 0; position < 3; ++position) {
        auto value = triple(RDFNode::iri(kExample + "object"));
        RDFNode* nodes[] = {&value.subject, &value.predicate, &value.object};
        nodes[position]->datatype = kXsd + "string";
        EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    }
    auto value = triple(RDFNode::blankNode("object"));
    value.object.datatype = kXsd + "string";
    EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    value.object = RDFNode::curie("ex:object");
    value.object.datatype = kXsd + "string";
    const SerdRdfWriter writer{{{"ex", kExample}}};
    EXPECT_THROW(writer.write({value}), std::invalid_argument);
}

TEST(SerdRdfWriterTest, RejectsUnboundCuriesInEveryResourcePosition) {
    for (int position = 0; position < 3; ++position) {
        auto value = triple();
        RDFNode* nodes[] = {&value.subject, &value.predicate, &value.object};
        *nodes[position] = RDFNode::curie("unbound:node");
        EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
    }
}

TEST(SerdRdfWriterTest, RejectsRelativeOrEmptyDatatype) {
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::literal("value", "relative"))}),
                 std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::literal("value", ""))}),
                 std::invalid_argument);
}

TEST(SerdRdfWriterTest, RejectsRelativeAndIllegalIrisInEveryResourcePosition) {
    const std::vector<std::string> invalid{"",
                                           "relative/path",
                                           "/absolute/path",
                                           "https://example.org/a b",
                                           "https://example.org/a<b",
                                           "https://example.org/a>b",
                                           "https://example.org/a\"b",
                                           "https://example.org/a\\b",
                                           "https://example.org/a{b",
                                           "https://example.org/a}b",
                                           "https://example.org/a|b",
                                           "https://example.org/a^b",
                                           "https://example.org/a`b",
                                           "https://example.org/a\nb",
                                           "https://example.org/a[b]",
                                           "urn:test:\xC2\x80",
                                           "https://example.org/a#part#extra",
                                           "https://example.org/%",
                                           "https://example.org/%2",
                                           "https://example.org/%2G"};
    for (const auto& iri : invalid) {
        SCOPED_TRACE(iri);
        for (int position = 0; position < 3; ++position) {
            auto value = triple();
            RDFNode* nodes[] = {&value.subject, &value.predicate, &value.object};
            *nodes[position] = RDFNode::iri(iri);
            EXPECT_THROW(SerdRdfWriter{}.write({value}), std::invalid_argument);
        }
    }
}

TEST(SerdRdfWriterTest, RejectsMalformedUtf8InLiteralsIrisAndPrefixBindings) {
    const std::vector<std::string> invalid{
        std::string{"\x80", 1}, std::string{"\xC0\xAF", 2}, std::string{"\xE2\x82", 2},
        std::string{"\xED\xA0\x80", 3}, std::string{"\xF4\x90\x80\x80", 4}};
    for (const auto& bytes : invalid) {
        EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::literal(bytes))}),
                     std::invalid_argument);
        EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::iri(kExample + bytes))}),
                     std::invalid_argument);
        SerdRdfWriter writer;
        EXPECT_THROW(writer.setPrefix("ex", kExample + bytes), std::invalid_argument);
    }
}

TEST(SerdRdfWriterTest, RejectsEmbeddedNulInsteadOfSilentlyTruncatingText) {
    const std::string text = std::string{"left"} + '\0' + "right";
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::literal(text))}), std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::iri(kExample + text))}),
                 std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::curie("ex:" + text))}),
                 std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::blankNode(text))}), std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::literal("value", "urn:" + text))}),
                 std::invalid_argument);
    SerdRdfWriter writer;
    EXPECT_THROW(writer.setPrefix(text, kExample), std::invalid_argument);
    EXPECT_THROW(writer.setPrefix("ex", kExample + text), std::invalid_argument);
}

TEST(SerdRdfWriterTest, RejectsInvalidBlankLabelsAndAcceptsPortableLabels) {
    for (const std::string label :
         {"", "_:label", "bad label", ".leading", "trailing.", "bad:label", "bad/label", "ô"}) {
        SCOPED_TRACE(label);
        EXPECT_THROW(SerdRdfWriter{}.write({triple(RDFNode::blankNode(label))}),
                     std::invalid_argument);
    }
    for (const std::string label : {"0", "_", "a_b-c.d9"}) {
        EXPECT_NO_THROW(SerdRdfWriter{}.write({triple(RDFNode::blankNode(label))}));
    }
}

TEST(SerdRdfWriterTest, RejectsInvalidPrefixNamesAndRelativeBindings) {
    for (const std::string name :
         {"1number", "_leading", "bad:name", "bad name", ".leading", "trailing.", "ô"}) {
        SCOPED_TRACE(name);
        EXPECT_THROW((SerdRdfWriter{std::map<std::string, std::string>{{name, kExample}}}),
                     std::invalid_argument);
        SerdRdfWriter writer;
        EXPECT_THROW(writer.setPrefix(name, kExample), std::invalid_argument);
    }
    for (const std::string iri : {"", "relative", "/absolute/path", "https://example.org/a b"}) {
        SerdRdfWriter writer;
        EXPECT_THROW(writer.setPrefix("ex", iri), std::invalid_argument);
        EXPECT_THROW((SerdRdfWriter{std::map<std::string, std::string>{{"ex", iri}}}),
                     std::invalid_argument);
    }
}

TEST(SerdRdfWriterTest, RejectsUnknownFormatEvenForEmptyInput) {
    const auto invalid_format = static_cast<RDFFormat>(-1);
    EXPECT_THROW(SerdRdfWriter{}.write({triple()}, invalid_format), std::invalid_argument);
    EXPECT_THROW(SerdRdfWriter{}.write({}, invalid_format), std::invalid_argument);
}

}  // namespace

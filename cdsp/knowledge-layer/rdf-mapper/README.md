# rdf-mapper

`rdf-mapper` turns Information Layer (IL) signal messages into RDF. The
mapping is not hard-coded: it is described by two SHACL Turtle files that
follow the conventions in this document.

- **Vehicle shapes** describe the vehicle structure (`Vehicle`,
  `Vehicle.CurrentLocation`, …) and become a tree of vehicle instance IRIs.
- **Signal shapes** describe, for each IL signal path, which RDF node to create
  for one value and which properties to give it.

```text
DataMessageDTO (from il-protocol) --RDFMapper::signalToTriples--> RDF triples --SerdRdfWriter--> Turtle / N-Triples / N-Quads / TriG
```

The mapper reads only the parts of SHACL listed below. It does not run SHACL
validation: `sh:minCount`, `sh:class`, `sh:nodeKind`, `sh:name` and other
constraints may be present but are ignored.

## Quick example

Vehicle shapes (`vehicle.ttl`):

```turtle
@prefix sh: <http://www.w3.org/ns/shacl#> .
@prefix vss: <https://covesa.org/vss/latest/model#> .
@prefix vsssh: <https://covesa.org/vss/latest/shapes#> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

vsssh:Vehicle a sh:NodeShape ;
    sh:targetClass vss:Vehicle ;
    sh:property [
        sh:path vss:hasVehicle_CurrentLocation ;   # containment edge
        sh:node vsssh:Vehicle_CurrentLocation ;
    ] ;
    sh:property [
        sh:path vss:id ;                            # literal property: ignored
        sh:datatype xsd:string ;
    ] .

vsssh:Vehicle_CurrentLocation a sh:NodeShape ;
    sh:targetClass vss:Vehicle_CurrentLocation .
```

Signal shapes (`signals.ttl`):

```turtle
@prefix cdsp: <http://covesa.org/cdsp_kl#> .
@prefix sh: <http://www.w3.org/ns/shacl#> .
@prefix sosa: <http://www.w3.org/ns/sosa/> .
@prefix vss: <https://covesa.org/vss/latest/model#> .
@prefix vsssh: <https://covesa.org/vss/latest/shapes#> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

cdsp:LatitudeShape a sh:NodeShape ;
    vss:path "CurrentLocation.Latitude" ;           # exact IL response key
    sh:targetClass sosa:Observation ;
    sh:property [                                   # link to a vehicle part
        sh:path sosa:hasFeatureOfInterest ;
        sh:node vsssh:Vehicle_CurrentLocation ;
    ] ;
    sh:property [                                   # constant
        sh:path sosa:observedProperty ;
        sh:hasValue vss:latitude ;
    ] ;
    sh:property [                                   # the signal value
        sh:path sosa:hasSimpleResult ;
        vss:value "value" ;
        sh:datatype xsd:double ;
    ] ;
    sh:property [                                   # the generated timestamp
        sh:path sosa:resultTime ;
        vss:value "timestamp" ;
        sh:datatype xsd:dateTime ;
    ] .
```

```cpp
#include "rdf_mapper.h"

const rdf_mapper::RDFMapper mapper{"vehicle.ttl", "signals.ttl",
                                   "https://example.org/instances/"};
const std::string turtle = mapper.signalToRdf(message, "VIN_1805");
```

For an IL message with `CurrentLocation.Latitude = 60.5` generated at
`2026-04-17T12:32:09.853211000Z`, the result is:

```turtle
<https://example.org/instances/Vehicle_VIN_1805>
	a <https://covesa.org/vss/latest/model#Vehicle> ;
	<https://covesa.org/vss/latest/model#hasVehicle_CurrentLocation> <https://example.org/instances/Vehicle_VIN_1805/CurrentLocation> .

<https://example.org/instances/Vehicle_VIN_1805/CurrentLocation>
	a <https://covesa.org/vss/latest/model#Vehicle_CurrentLocation> .

<https://example.org/instances/Vehicle_VIN_1805/observation/CurrentLocation/Latitude/2026-04-17T12:32:09.853211000Z>
	a <http://www.w3.org/ns/sosa/Observation> ;
	<http://www.w3.org/ns/sosa/hasFeatureOfInterest> <https://example.org/instances/Vehicle_VIN_1805/CurrentLocation> ;
	<http://www.w3.org/ns/sosa/observedProperty> <https://covesa.org/vss/latest/model#latitude> ;
	<http://www.w3.org/ns/sosa/hasSimpleResult> "60.5"^^<http://www.w3.org/2001/XMLSchema#double> ;
	<http://www.w3.org/ns/sosa/resultTime> "2026-04-17T12:32:09.853211000Z"^^<http://www.w3.org/2001/XMLSchema#dateTime> .
```

The vehicle branch that the signal points to is emitted with the observation,
so each output document is self-contained.

## Writing vehicle shapes

A shape is any resource declared `a sh:NodeShape`. Every shape **without**
`vss:path` is a vehicle shape, in either file.

| Rule | Why |
|---|---|
| Each shape has one `sh:targetClass` IRI. | It becomes the instance's `rdf:type`. |
| A property with `sh:path P` and `sh:node Child` is a containment edge. | It emits `<parent> P <child>`. |
| Properties without `sh:node` (for example `sh:datatype`) are ignored. | The vehicle graph has structure only, no literal values. |
| The shapes form exactly **one tree**: one root (a shape no edge points to), every shape reachable from it, no cycles. | The root is the vehicle itself. |
| Each part has exactly one parent; a shape cannot be reused under two parents. | Its IRI is derived from its single path from the root. |
| A child's target-class local name is `<parent local name>_<Segment>`. | `Segment` becomes the child's IRI path component. |
| A vehicle shape must not point to a signal shape with `sh:node`. | Signals are not parts of the vehicle. |

Instance IRIs follow the tree:

| Target class | Instance IRI (vehicle ID `VIN_1805`) |
|---|---|
| `vss:Vehicle` (root) | `<base>Vehicle_VIN_1805` |
| `vss:Vehicle_Powertrain` | `<base>Vehicle_VIN_1805/Powertrain` |
| `vss:Vehicle_Powertrain_TractionBattery` | `<base>Vehicle_VIN_1805/Powertrain/TractionBattery` |

The local name is the part of the class IRI after the last `#` or `/`. The
vehicle ID and every segment are percent-encoded, so a `/` in the ID stays
inside one path component.

## Writing signal shapes

A signal shape is a `sh:NodeShape` with a `vss:path` literal. Each one maps one
IL data point to one RDF node.

### Shape-level rules

| Predicate | Rule |
|---|---|
| `vss:path` | The **exact** key the IL sends, case-sensitive. With `"root": "absolute"` subscriptions the IL drops the schema prefix, so `Vehicle.CurrentLocation.Latitude` arrives as `CurrentLocation.Latitude`. Must be non-empty and unique across all signal shapes. |
| `sh:targetClass` | Required. Becomes the node's `rdf:type`, and its lowercased local name becomes the IRI segment (`sosa:Observation` → `/observation/`). |

### Property bindings

Every `sh:property` of a signal shape needs a direct IRI `sh:path` and
**exactly one** of the following bindings. A property with none, or with two,
makes the constructor fail.

| Binding | Output object | Extra rules |
|---|---|---|
| `sh:node <vehicle shape>` | IRI of that vehicle part, for the caller's vehicle ID. The branch from the root to that part is emitted too. | Target must be a vehicle shape. No `sh:datatype`. |
| `sh:hasValue <IRI or literal>` | The constant, copied as is. | Not a blank node. With `sh:datatype`, the constant must be a literal of that datatype (a plain literal counts as `xsd:string`). |
| `vss:value "value"` | The data point's value as a typed literal. | `sh:datatype` required; see the table below. |
| `vss:value "timestamp"` | The point's generated time, UTC with nine fractional digits. | `sh:datatype xsd:dateTime` required. |

### Value datatypes

For `vss:value "value"`, `sh:datatype` selects how the IL value is converted.
A value of the wrong type or out of range is rejected when the message is
mapped.

| `sh:datatype` | Accepted IL value | Notes |
|---|---|---|
| `xsd:string` | string | |
| `xsd:boolean` | boolean | Written as `true` / `false`. |
| `xsd:integer`, `xsd:long` | integer | |
| `xsd:int`, `xsd:short`, `xsd:byte` | integer within the type's range | |
| `xsd:unsignedLong`, `xsd:unsignedInt`, `xsd:unsignedShort`, `xsd:unsignedByte` | integer within the type's range | `unsignedLong` is limited to `INT64_MAX` by the IL DTO. |
| `xsd:nonNegativeInteger`, `xsd:positiveInteger`, `xsd:nonPositiveInteger`, `xsd:negativeInteger` | integer within the type's range | |
| `xsd:float` | number (integer or floating point), finite, within float range | Rounded to float precision. |
| `xsd:double` | number, finite | Written with up to 17 significant digits so it reads back exactly, for example `60.1699` becomes `"60.169899999999998"`. |

Other datatypes, such as `xsd:decimal` or `xsd:anyURI`, are rejected by the
constructor.

### Signal instance IRI

```text
<base>Vehicle_<vehicle ID>/<target class local name, lowercased>/<vss:path components>/<generated time>
```

One leading `Vehicle.`, if present, is dropped from `vss:path`, and the
remaining dot components become path segments. `CurrentLocation.Latitude` (or
`Vehicle.CurrentLocation.Latitude`) with `sosa:Observation` gives
`…/observation/CurrentLocation/Latitude/<time>`.

The IRI depends only on the vehicle ID, the signal and its generated time, so
replaying the same message produces the same IRI.

## What the message must contain

`signalToTriples` takes an `il_message::DataMessageDTO` (see
[il-protocol](../connection/il-protocol/README.md)) and a vehicle ID chosen by
the caller. The message's `instance` field is not used.

- Every data path must equal some signal shape's `vss:path`.
- Every data path needs a metadata entry with the **same path** and a
  `generated` timestamp. The `received` time and the current clock are never
  used instead.
- Data paths and metadata paths must be unique within the message.
- An empty `data` vector produces no triples.
- If any point fails, the whole call throws and returns nothing.

## API

```cpp
RDFMapper(const std::filesystem::path& vehicle_shapes_file,
          const std::filesystem::path& signal_shapes_file,
          std::string instance_base_iri);

std::vector<RDFTriple> vehicleToTriples(const std::string& vehicle_id) const;
std::vector<RDFTriple> signalToTriples(const il_message::DataMessageDTO& message,
                                       const std::string& vehicle_id) const;
std::string vehicleToRdf(const std::string& vehicle_id, RDFFormat format = RDFFormat::Turtle) const;
std::string signalToRdf(const il_message::DataMessageDTO& message, const std::string& vehicle_id,
                        RDFFormat format = RDFFormat::Turtle) const;
```

- The constructor reads both files, checks every rule above and builds its
  indexes. All later calls are `const` and safe to run concurrently.
- `instance_base_iri` is an `http://` or `https://` IRI ending in `/`, with a
  plain host and optional port, and no query, fragment, user info or `.`/`..`
  segments.
- `RDFFormat` is `Turtle`, `NTriples`, `NQuads` or `TriG`. Quad formats use the
  default graph.
- `vehicleToTriples` emits the whole vehicle tree; `signalToTriples` emits only
  the branches its signals point to.

Both files are parsed together, so a signal shape can `sh:node` a vehicle shape
declared in the other file. Keep their `@prefix`/`@base` declarations
compatible and do not reuse explicit blank-node labels such as `_:p1` across
the two files.

## Troubleshooting

| Error message (start) | Cause |
|---|---|
| `Vehicle topology must have exactly one root` | Two vehicle shapes have no parent, often a helper `sh:NodeShape` without `vss:path` in the signal file. |
| `Vehicle part has more than one containment route` | A vehicle shape is the `sh:node` of two properties. |
| `Vehicle child class must extend its parent class name` | The child's class local name does not start with `<parent>_`. |
| `Signal property must have exactly one value binding` | A signal property has no binding (for example only `sh:class`), or two. |
| `Dynamic signal property requires sh:datatype` | `vss:value` without `sh:datatype`. |
| `Unsupported signal value datatype` | The datatype is not in the table above. |
| `Unsupported vss:value selector` | `vss:value` is not `"value"` or `"timestamp"`. |
| `Signal sh:node must bind a reachable vehicle shape` | The `sh:node` target is a signal shape or not in the vehicle tree. |
| `No signal shape for exact path` | The message key differs from every `vss:path` (other case, a `Vehicle.` prefix the IL does not send, or a relative-root key). |
| `Signal requires exact-path generated metadata` | The metadata entry for that path is missing or has no `generated` time. |

## Build, test and demo

The Knowledge Layer root project builds the `rdf-mapper` target (it depends on
`il-protocol` and Serd) and, with `KL_BUILD_TESTS=ON`, `rdf-mapper-tests`.
Test fixtures live in `tests/fixtures/`:

| Directory | Contents |
|---|---|
| `vehicle_shacl/` | Vehicle tree used by all tests |
| `signal_shacl/` | Speed and current-location signal shapes |
| `signal_shacl_datatypes/` | Integer-derived datatypes |
| `signal_shacl_custom_class/` | Signal with a non-SOSA target class |
| `signal_shacl_invalid/` | One rule violation per file |
| `rdf_mapper_signal/` | IL wire messages |

A runnable demo is in
[`examples/driving-style/rdf-mapper-demo`](../../../examples/driving-style/rdf-mapper-demo/).

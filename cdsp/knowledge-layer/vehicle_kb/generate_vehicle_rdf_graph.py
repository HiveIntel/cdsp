#!/usr/bin/env python3
"""Build a VIN-qualified vehicle knowledge base by traversing SHACL node shapes."""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import quote

from pyshacl import validate
from rdflib import BNode, Graph, Literal, RDF, SH, URIRef


SCRIPT_DIR = Path(__file__).resolve().parent
MODEL_FILE = SCRIPT_DIR / "vehicle_model.shacl"
OUTPUT_FILE = SCRIPT_DIR / "vehicle_graph.ttl"
@dataclass(frozen=True)
class NodeShape:
    """The SHACL information required to create one knowledge-base resource."""

    shape: URIRef | BNode
    target_class: URIRef


def local_name(value: URIRef) -> str:
    """Return an IRI's final component for slash- and hash-style namespaces."""
    return str(value).rstrip("/#").replace("#", "/").rsplit("/", 1)[-1]


def load_node_shapes(shapes: Graph) -> dict[URIRef | BNode, NodeShape]:
    """Scan all node shapes that declare a target class."""
    result: dict[URIRef | BNode, NodeShape] = {}
    for shape, target_class in shapes.subject_objects(SH.targetClass):
        if not isinstance(target_class, URIRef):
            continue
        result[shape] = NodeShape(shape, target_class)
    if not result:
        raise ValueError("vehicle_model.shacl does not contain SHACL node shapes")
    return result


def resource_for(node: NodeShape, encoded_vin: str) -> URIRef:
    """Create an instance IRI without adding any model vocabulary."""
    return URIRef(f"urn:vehicle:{encoded_vin}:{quote(local_name(node.target_class), safe='')}")


def find_vehicle(nodes: dict[URIRef | BNode, NodeShape]) -> NodeShape:
    for node in nodes.values():
        if local_name(node.target_class) == "Vehicle":
            return node
    raise ValueError("vehicle_model.shacl does not define the Vehicle node shape")


def find_property_path(shapes: Graph, shape: URIRef | BNode, name: str) -> URIRef:
    for property_shape in shapes.objects(shape, SH.property):
        if str(shapes.value(property_shape, SH.name)) != name:
            continue
        path = shapes.value(property_shape, SH.path)
        if isinstance(path, URIRef):
            return path
    raise ValueError(f"No SHACL property named {name!r} found")


def add_node_types(data: Graph, nodes: dict[URIRef | BNode, NodeShape], encoded_vin: str) -> None:
    """Emit an RDF instance of every target class found while scanning SHACL."""
    for node in nodes.values():
        resource = resource_for(node, encoded_vin)
        data.add((resource, RDF.type, node.target_class))


def add_shape_edges(
    data: Graph,
    shapes: Graph,
    nodes: dict[URIRef | BNode, NodeShape],
    encoded_vin: str,
) -> None:
    """Traverse every SHACL shape-to-shape property and write only that path."""
    for parent in nodes.values():
        parent_resource = resource_for(parent, encoded_vin)
        for property_shape in shapes.objects(parent.shape, SH.property):
            child_shape = shapes.value(property_shape, SH.node)
            path = shapes.value(property_shape, SH.path)
            child = nodes.get(child_shape)
            if child is None or not isinstance(path, URIRef):
                continue

            child_resource = resource_for(child, encoded_vin)
            data.add((parent_resource, path, child_resource))


def main() -> int:
    vin = os.environ.get("VIN_ID", "").strip()
    if not vin:
        print("Error: VIN_ID must be set.", file=sys.stderr)
        return 2
    if not MODEL_FILE.is_file():
        print(f"Error: SHACL model not found: {MODEL_FILE}", file=sys.stderr)
        return 2

    encoded_vin = quote(vin, safe="")
    shapes = Graph().parse(MODEL_FILE, format="turtle")
    nodes = load_node_shapes(shapes)
    vehicle = find_vehicle(nodes)

    data = Graph()
    data.bind("vss", str(vehicle.target_class)[: -len("Vehicle")])
    add_node_types(data, nodes, encoded_vin)
    add_shape_edges(data, shapes, nodes, encoded_vin)

    # VIN is both the SHACL-required Vehicle identifier and the value of the
    # VehicleIdentification.VIN VSS signal.
    vehicle_resource = resource_for(vehicle, encoded_vin)
    vehicle_id_path = find_property_path(shapes, vehicle.shape, "id")
    data.add((vehicle_resource, vehicle_id_path, Literal(vin)))
    for node in nodes.values():
        if local_name(node.target_class) != "Vehicle_VehicleIdentification":
            continue
        vin_path = find_property_path(shapes, node.shape, "vin")
        identification = resource_for(node, encoded_vin)
        data.add((identification, vin_path, Literal(vin)))

    conforms, report_graph, report_text = validate(
        data_graph=data,
        shacl_graph=shapes,
        inference="none",
        advanced=True,
    )
    if not conforms:
        print(report_text, file=sys.stderr)
        report_graph.serialize(destination=SCRIPT_DIR / "vehicle_graph_validation_report.ttl", format="turtle")
        return 1

    data.serialize(destination=OUTPUT_FILE, format="turtle")
    print(f"Scanned {len(nodes)} SHACL node shapes; validated and wrote {OUTPUT_FILE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

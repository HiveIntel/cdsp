/**
 * Utility functions for formatting data structures in different formats (flat/nested) and root modes (absolute/relative).
 */

/**
 * Return the schema that is always the prefix (until the first dot) of all node names.
 * Throw in case of error
 * @param nodes list of nodes with names and values
 */
export function getSchemaOrThrow(
  nodes: Array<{ name: string; value: any }>,
): string {
  return (
    nodes[0]?.name.split(".")[0] ??
    (() => {
      throw new Error("Nodes array is empty");
    })()
  );
}

/**
 * Normalize the requested path relative to the schema.
 * Returns a standardized path prefix for data structure formatting.
 * @param path the full path (may include schema prefix)
 * @param schema the schema prefix
 * @returns normalized path without schema prefix
 */
export function normalizeRequestedPath(path: string, schema: string): string {
  if (!path) {
    return "";
  }

  if (path === schema) {
    return "";
  }

  if (path.startsWith(schema + ".")) {
    return path.substring(schema.length + 1);
  }

  return path;
}

/**
 * Build flat data structure.
 * Process root first: determine base path relative to requested path.
 * Then format as flat: single-level key-value pairs.
 */
export function buildDataStructureFlat(
  nodes: Array<{ name: string; value: any }>,
  root: "absolute" | "relative",
  requestedPath: string,
  schema: string,
): Record<string, any> {
  return nodes.reduce(
    (accumulator, { name, value }) => {
      let processedName: string;

      if (root === "relative") {
        // Remove everything up to and including the requested path
        // E.g., "Vehicle.CurrentLocation.Latitude" with path "CurrentLocation" -> "Latitude"
        // E.g., "Vehicle.Speed" with path "" -> "Speed"
        const fullPath = requestedPath ? `${schema}.${requestedPath}` : schema;
        if (name.startsWith(fullPath + ".")) {
          processedName = name.substring(fullPath.length + 1);
        } else if (name === fullPath) {
          processedName = ""; // Exact match - use empty string
        } else if (name.startsWith(schema + ".")) {
          // Fall back to removing the schema prefix
          processedName = name.substring(schema.length + 1);
        } else {
          processedName = name;
        }
      } else {
        // Absolute: keep full path excluding only the schema prefix
        // E.g., "Vehicle.CurrentLocation.Latitude" -> "CurrentLocation.Latitude"
        processedName = name.includes(".")
          ? name.split(".").slice(1).join(".")
          : name;
      }

      accumulator[processedName] = value;
      return accumulator;
    },
    {} as Record<string, any>,
  );
}

/**
 * Build nested data structure.
 * Process root first: determine base path relative to requested path.
 * Then format as nested: hierarchical object structure.
 */
export function buildDataStructureAsTree(
  nodes: Array<{ name: string; value: any }>,
  root: "absolute" | "relative",
  requestedPath: string,
  schema: string,
): any {
  // First, process each node name based on root setting
  const processedNodes = nodes.map(({ name, value }) => {
    let processedName: string;

    if (root === "relative") {
      // Remove everything up to and including the requested path
      const fullPath = requestedPath ? `${schema}.${requestedPath}` : schema;
      if (name.startsWith(fullPath + ".")) {
        processedName = name.substring(fullPath.length + 1);
      } else if (name === fullPath) {
        processedName = ""; // Exact match - return value directly
      } else if (name.startsWith(schema + ".")) {
        // Fall back to removing the schema prefix
        processedName = name.substring(schema.length + 1);
      } else {
        processedName = name;
      }
    } else {
      // Absolute: keep full path excluding only the schema prefix
      processedName = name.includes(".")
        ? name.split(".").slice(1).join(".")
        : name;
    }

    return { name: processedName, value };
  });

  // Special case: if there's only one node with empty name, return the value directly
  if (processedNodes.length === 1 && processedNodes[0].name === "") {
    return processedNodes[0].value;
  }

  // Build nested structure from processed names
  const data: any = {};

  for (const { name, value } of processedNodes) {
    if (name === "") {
      // Empty path - this shouldn't happen in nested format with multiple nodes
      continue;
    }

    const pathParts = name.split(".");
    let currentLevel = data;

    for (let j = 0; j < pathParts.length; j++) {
      const key = pathParts[j];
      if (j === pathParts.length - 1) {
        // Leaf node
        currentLevel[key] = value;
      } else {
        // Intermediate node
        if (!currentLevel[key]) {
          currentLevel[key] = {};
        }
        currentLevel = currentLevel[key];
      }
    }
  }

  return data;
}

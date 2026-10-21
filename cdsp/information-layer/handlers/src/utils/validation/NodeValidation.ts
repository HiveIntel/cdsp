/**
 * Utility functions for validating and extracting leaf nodes from requests.
 * Handles recursive traversal of nested data structures and path validation.
 */

import { replaceDotsWithUnderscore } from "../../../utils/transformations";

/**
 * Extracts leaf nodes from a request path and data structure.
 * Recursively traverses nested objects to find all leaf (non-object) values.
 * All node paths are normalized to use underscores instead of dots.
 *
 * @param path The base path for the request
 * @param data Optional data object to traverse (if undefined, path itself is the leaf)
 * @returns Set of underscore-normalized leaf node paths
 */
export function extractLeafNodesFromRequest(
  path: string,
  data?: any,
): Set<string> {
  const leafNodes = new Set<string>();

  if (data && typeof data === "object") {
    for (const [key, value] of Object.entries(data)) {
      const fullPath = `${path}.${key}`;
      if (typeof value === "object" && value !== null) {
        // Recursive call for nested objects
        const childLeafNodes = extractLeafNodesFromRequest(fullPath, value);
        for (const child of childLeafNodes) {
          leafNodes.add(child); // Add all child leaf nodes
        }
      } else {
        // If it's not an object, it's a leaf node
        leafNodes.add(fullPath);
      }
    }
  } else {
    // If data is not an object, the base path itself is a leaf node
    leafNodes.add(path);
  }

  return new Set<string>(
    Array.from(leafNodes, (value) => replaceDotsWithUnderscore(value)),
  );
}

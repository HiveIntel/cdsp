/**
 * Utility for loading and parsing datapoint schema files (JSON/YAML).
 * Handles reading files, extracting datatype information, and schema transformation.
 */

import fs from "fs";
import yaml from "js-yaml";
import { replaceDotsWithUnderscore } from "../../../utils/transformations";

/**
 * Reads and parses a data points file in either JSON, YML, or YAML format.
 */
function readDataPointsFile(filePath: string): object {
  const fileContent = fs.readFileSync(filePath, "utf8");
  const filePathLower = filePath.toLowerCase();
  if (filePathLower.endsWith(".json")) {
    return JSON.parse(fileContent);
  } else if (
    filePathLower.endsWith(".yaml") ||
    filePathLower.endsWith(".yml")
  ) {
    const result = yaml.load(fileContent);
    if (typeof result === "object" && result !== null) {
      return result;
    } else {
      throw new Error("YAML content is not a valid object");
    }
  } else {
    throw new Error("Unsupported data points file format");
  }
}

/**
 * Extracts data types from a data point object recursively.
 * Traverses the object tree and collects datatype values, maintaining their hierarchy paths.
 */
function extractDataTypes(
  dataPointsObj: any,
  parentKey = "",
  result: { [key: string]: any } = {},
): { [key: string]: any } {
  for (const key in dataPointsObj) {
    if (dataPointsObj.hasOwnProperty(key)) {
      const value = dataPointsObj[key];
      const newKey = parentKey ? `${parentKey}.${key}` : key;
      const isObject = value && typeof value === "object";
      if (isObject && value.datatype) {
        result[newKey] = value.datatype;
      } else if (isObject) {
        extractDataTypes(value.children || value, newKey, result);
      }
    }
  }
  return result;
}

/**
 * Loads and processes supported data points from schema file.
 * Reads the datapoints configuration file, extracts datatypes,
 * and transforms datapoint names to use underscores instead of dots.
 *
 * @param dataPointPath Path to the datapoints schema file
 * @returns Object with transformed datapoint names as keys and datatypes as values
 */
export function loadDataPointSchema(dataPointPath: string): object {
  const dataPointObj = readDataPointsFile(dataPointPath);
  const supportedDataPoints = extractDataTypes(dataPointObj);
  const result: { [key: string]: any } = {};

  Object.entries(supportedDataPoints).forEach(([node, value]) => {
    const underscored_node = replaceDotsWithUnderscore(node);
    if (value !== null) {
      result[underscored_node] = value;
    }
  });

  return result;
}

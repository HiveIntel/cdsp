/* eslint-disable no-console */

async function main(): Promise<void> {
  try {
    const mod = await import("@iotdb/client");

    const keys = Object.keys(mod);
    console.log("[smoke] module keys:", keys);

    if (!("Session" in mod)) {
      throw new Error(
        "Expected export 'Session' not found in @iotdb/client. " +
          `Available exports: ${keys.join(", ")}`,
      );
    }

    console.log("[smoke] ✅ import works, Session export found");
  } catch (err) {
    console.error("[smoke] ❌ import failed:", err);
    process.exit(1);
  }
}

void main();

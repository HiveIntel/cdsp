/* eslint-disable no-console */

import dotenv from "dotenv";
import { Session } from "@iotdb/client";

dotenv.config();

type Env = {
  host: string;
  port: number;
  user: string;
  password: string;
};

function getEnv(): Env {
  return {
    host: process.env.IOTDB_HOST ?? "localhost",
    port: Number(process.env.IOTDB_PORT ?? "6667"),
    user: process.env.IOTDB_USER ?? "root",
    password: process.env.IOTDB_PASSWORD ?? "root",
  };
}

async function main(): Promise<void> {
  const env = getEnv();

  console.log("[smoke] starting with:", {
    host: env.host,
    port: env.port,
    user: env.user,
  });

  const session = new Session({
    host: env.host,
    port: env.port,
    username: env.user,
    password: env.password,
  });

  let opened = false;

  try {
    await session.open();
    opened = true;
    console.log("[smoke] ✅ session opened");

    const sql = "SHOW DATABASES";
    const result = await session.executeQueryStatement(sql);

    console.log("[smoke] ✅ query executed:", sql);

    // Close query handle / dataset if supported by the client build
    const maybeResult = result as {
      close?: () => Promise<void>;
      closeOperationHandle?: () => Promise<void>;
    };

    if (typeof maybeResult.close === "function") {
      await maybeResult.close();
    } else if (typeof maybeResult.closeOperationHandle === "function") {
      await maybeResult.closeOperationHandle();
    }

    console.log("[smoke] ✅ all checks passed");
  } catch (err) {
    console.error("[smoke] ❌ failed:", err);
    process.exitCode = 1;
  } finally {
    if (opened) {
      try {
        await session.close();
        console.log("[smoke] ✅ session closed");
      } catch (closeErr) {
        console.error("[smoke] ⚠️  close failed:", closeErr);
      }
    }
  }
}

void main();

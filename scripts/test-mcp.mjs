#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtempSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import readline from "node:readline";

const PROTOCOL_VERSION = "2026-07-28";
const DEFAULT_TOOLS = [
  "add",
  "get",
  "search",
  "update",
  "merge",
  "delete",
  "sql",
  "schema",
  "runs",
  "import",
  "backup",
  "mine",
  "status",
];

const binaryArg = process.argv[2] ?? "build/bin/personal-log-mcp";
const expectedArg = process.argv[3] ?? DEFAULT_TOOLS.join(",");
const binary = path.resolve(binaryArg);
const expectedTools = expectedArg
  .split(",")
  .map((s) => s.trim())
  .filter(Boolean);

const meta = {
  "io.modelcontextprotocol/protocolVersion": PROTOCOL_VERSION,
  "io.modelcontextprotocol/clientCapabilities": {},
};

function makeReq(id, method, params = {}) {
  return {
    jsonrpc: "2.0",
    id,
    method,
    params: { ...params, _meta: meta },
  };
}

function makeIsolatedEnv(temporary) {
  return {
    ...process.env,
    HOME: temporary,
    XDG_CONFIG_HOME: temporary,
    XDG_DATA_HOME: temporary,
    XDG_STATE_HOME: temporary,
    XDG_CACHE_HOME: temporary,
    TMPDIR: temporary,
    PERSONAL_LOG_DIR: path.join(temporary, "personal-log"),
    PERSONAL_LOG_MINE_CONFIG: path.join(temporary, "missing-mine-config.json"),
    BUGSINK_DSN: "",
  };
}

async function testStdio(temporary) {
  const proc = spawn(binary, ["mcp"], {
    env: makeIsolatedEnv(temporary),
    stdio: ["pipe", "pipe", "pipe"],
  });

  const stderrLines = [];
  proc.stderr.setEncoding("utf8");
  proc.stderr.on("data", (chunk) => stderrLines.push(chunk));

  const rl = readline.createInterface({ input: proc.stdout });
  const pending = new Map();

  rl.on("line", (line) => {
    const trimmed = line.trim();
    if (!trimmed) return;
    let msg;
    try {
      msg = JSON.parse(trimmed);
    } catch (err) {
      for (const { reject } of pending.values()) {
        reject(new Error(`Invalid JSON line from stdio server: ${trimmed}\n${err}`));
      }
      pending.clear();
      return;
    }
    const resolver = pending.get(msg.id);
    if (resolver) {
      pending.delete(msg.id);
      resolver.resolve(msg);
    }
  });

  function send(id, method, params = {}) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(
          new Error(
            `Timed out waiting for stdio response id=${id} (${method}). stderr:\n${stderrLines.join("")}`,
          ),
        );
      }, 10_000);
      pending.set(id, {
        resolve: (val) => {
          clearTimeout(timer);
          resolve(val);
        },
        reject: (err) => {
          clearTimeout(timer);
          reject(err);
        },
      });
      proc.stdin.write(`${JSON.stringify(makeReq(id, method, params))}\n`);
    });
  }

  try {
    const discover = await send(1, "server/discover");
    assert.equal(discover.error, undefined, `server/discover error: ${JSON.stringify(discover.error)}`);
    assert.ok(
      discover.result?.supportedVersions?.includes(PROTOCOL_VERSION),
      `stdio supportedVersions missing ${PROTOCOL_VERSION}: ${JSON.stringify(discover.result)}`,
    );
    assert.equal(
      discover.result?._meta?.["io.modelcontextprotocol/serverInfo"]?.name,
      "personal-log-mcp",
    );

    const listed = await send(2, "tools/list");
    assert.equal(listed.error, undefined, `tools/list error: ${JSON.stringify(listed.error)}`);
    const toolNames = (listed.result?.tools ?? []).map((t) => t.name);
    for (const expected of expectedTools) {
      assert.ok(toolNames.includes(expected), `stdio tools/list missing '${expected}': ${toolNames.join(", ")}`);
    }
    assert.ok(
      toolNames.includes("mcp_resource_read"),
      `stdio tools/list missing 'mcp_resource_read': ${toolNames.join(", ")}`,
    );

    const statusResp = await send(3, "tools/call", { name: "status", arguments: {} });
    assert.equal(statusResp.error, undefined);
    const statusText = statusResp.result?.content?.[0]?.text ?? "";
    assert.ok(statusText.includes("version"), `unexpected status payload: ${statusText}`);

    const legacyInit = await send(4, "initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "legacy-test", version: "1.0.0" },
    });
    assert.equal(legacyInit.error?.code, -32601, `expected -32601 for legacy initialize: ${JSON.stringify(legacyInit)}`);

    proc.stdin.end();
    const exitCode = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("Timed out waiting for stdio exit")), 5_000);
      proc.once("exit", (code, signal) => {
        clearTimeout(timer);
        if (signal) reject(new Error(`stdio exited via signal ${signal}`));
        else resolve(code);
      });
    });
    assert.equal(exitCode, 0, `stdio exited with ${exitCode}. stderr:\n${stderrLines.join("")}`);
  } finally {
    rl.close();
    if (proc.exitCode === null) proc.kill("SIGKILL");
  }
}

async function testHttp(temporary) {
  const proc = spawn(
    binary,
    ["http", "--host", "127.0.0.1", "--port", "0", "--allow-ephemeral-port"],
    {
      env: makeIsolatedEnv(temporary),
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

  const stderrLines = [];
  let endpointUrl = "";

  try {
    endpointUrl = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error(`Timed out waiting for HTTP listening URL. stderr:\n${stderrLines.join("")}`));
      }, 10_000);

      proc.stderr.setEncoding("utf8");
      proc.stderr.on("data", (chunk) => {
        stderrLines.push(chunk);
        const text = stderrLines.join("");
        const match = text.match(/listening on (http:\/\/127\.0\.0\.1:\d+\/mcp)/);
        if (match) {
          clearTimeout(timer);
          resolve(match[1]);
        }
      });

      proc.once("exit", (code, signal) => {
        clearTimeout(timer);
        reject(
          new Error(
            `HTTP server exited prematurely (code=${code}, signal=${signal}). stderr:\n${stderrLines.join("")}`,
          ),
        );
      });
    });

    async function postMcp(id, method, params = {}) {
      const headers = {
        "Content-Type": "application/json",
        Accept: "application/json, text/event-stream",
        "MCP-Protocol-Version": PROTOCOL_VERSION,
        "Mcp-Method": method,
      };
      if (method === "tools/call" && typeof params.name === "string") {
        headers["Mcp-Name"] = params.name;
      } else if (method === "resources/read" && typeof params.uri === "string") {
        headers["Mcp-Name"] = params.uri;
      }
      const resp = await fetch(endpointUrl, {
        method: "POST",
        headers,
        body: JSON.stringify(makeReq(id, method, params)),
      });
      const text = await resp.text();
      return { status: resp.status, json: JSON.parse(text) };
    }

    const discover = await postMcp(10, "server/discover");
    assert.equal(discover.status, 200);
    assert.equal(discover.json.error, undefined);
    assert.ok(
      discover.json.result?.supportedVersions?.includes(PROTOCOL_VERSION),
      `http supportedVersions missing ${PROTOCOL_VERSION}: ${JSON.stringify(discover.json.result)}`,
    );
    assert.equal(
      discover.json.result?._meta?.["io.modelcontextprotocol/serverInfo"]?.name,
      "personal-log-mcp",
    );

    const listed = await postMcp(11, "tools/list");
    assert.equal(listed.status, 200);
    assert.equal(listed.json.error, undefined);
    const toolNames = (listed.json.result?.tools ?? []).map((t) => t.name);
    for (const expected of expectedTools) {
      assert.ok(toolNames.includes(expected), `http tools/list missing '${expected}': ${toolNames.join(", ")}`);
    }
    assert.ok(toolNames.includes("mcp_resource_read"));

    const statusResp = await postMcp(12, "tools/call", { name: "status", arguments: {} });
    assert.equal(statusResp.status, 200);
    assert.equal(statusResp.json.error, undefined);
    assert.ok((statusResp.json.result?.content?.[0]?.text ?? "").includes("version"));

    const legacyInit = await postMcp(13, "initialize");
    assert.equal(legacyInit.status, 404);
    assert.equal(legacyInit.json.error?.code, -32601);

    const getResp = await fetch(endpointUrl, { method: "GET" });
    assert.equal(getResp.status, 405);

    const exitPromise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("Timed out waiting for HTTP SIGTERM shutdown")), 5_000);
      proc.once("exit", (code, signal) => {
        clearTimeout(timer);
        if (signal) reject(new Error(`HTTP server exited via signal ${signal} instead of clean 0`));
        else resolve(code);
      });
    });
    proc.kill("SIGTERM");
    const exitCode = await exitPromise;
    assert.equal(exitCode, 0, `HTTP server exited with ${exitCode}. stderr:\n${stderrLines.join("")}`);
  } finally {
    if (proc.exitCode === null) proc.kill("SIGKILL");
  }
}

const temporary = mkdtempSync(path.join(tmpdir(), "personal-log-mcp-test-"));
try {
  await testStdio(temporary);
  assert.deepEqual(
    readdirSync(temporary),
    [],
    `stdio discovery created unexpected files in ${temporary}`,
  );
  await testHttp(temporary);
  assert.deepEqual(
    readdirSync(temporary),
    [],
    `http discovery created unexpected files in ${temporary}`,
  );
  console.log(
    `personal-log-mcp stdio + HTTP July ${PROTOCOL_VERSION} check passed (${expectedTools.length} tools + mcp_resource_read).`,
  );
} finally {
  rmSync(temporary, { recursive: true, force: true });
}

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";

const firmware = fs.readFileSync(new URL("../src/main.cpp", import.meta.url), "utf8");
const config = fs.readFileSync(new URL("../include/S88AdapterConfig.h", import.meta.url), "utf8");

// Independent protocol reference calculations for documented golden vectors.
function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit++)
      crc = ((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1) & 0xffff;
  }
  return crc.toString(16).toUpperCase().padStart(4, "0");
}

function frame(bytes) {
  const count = bytes.length;
  return `S88:${count.toString(16).toUpperCase().padStart(2, "0")}:${bytes
    .map(value => value.toString(16).toUpperCase().padStart(2, "0")).join("")}*${crc16([count, ...bytes])}\n`;
}

test("HEX framing encodes zero and 0x02 safely and matches CRC16 vectors", () => {
  assert.equal(frame([0x02, 0x00, 0xff, 0x01]), "S88:04:0200FF01*66BC\n");
  assert.equal(frame([0x01, 0x00]), "S88:02:0100*91CD\n");
  assert.equal(frame([0x00, 0x00]), "S88:02:0000*A2FC\n");
  assert.equal(frame([0x00, 0x00, 0x01, 0x00]), "S88:04:00000100*AB3B\n");
});

test("firmware uses HEX default only in volatile RAM, with old terminal commands intact", () => {
  assert.match(config, /S88_ADAPTER_VERSION "0\.6\.0"/);
  assert.match(firmware, /serialView = SerialView::Hex;/);
  assert.match(firmware, /equalsIgnoreCase\(command, "VIEW"\)/);
  assert.match(firmware, /equalsIgnoreCase\(view, "BIN"\)/);
  assert.match(firmware, /equalsIgnoreCase\(view, "HEX"\)/);
  for (const cmd of ["HELP", "STATUS", "INFO", "READ", "SET", "SAVE", "DEFAULTS", "LOG", "REBOOT"]) {
    assert.ok(firmware.includes(`equalsIgnoreCase(command, "${cmd}")`), cmd);
  }
  assert.doesNotMatch(firmware, /EEPROM\.update\([^;]*serialView/);
});

test("firmware streams startup, changed and heartbeat snapshots and retains I2C", () => {
  assert.match(firmware, /printS88Data\(\);\n}\n\nvoid loop\(\)/);
  assert.match(firmware, /snapshotPending = true;/);
  assert.match(firmware, /Config::MIN_SERIAL_INTERVAL_MS/);
  assert.match(firmware, /Config::LOG_INTERVAL_MS/);
  assert.match(firmware, /Wire\.onRequest\(onI2CRequest\)/);
  assert.match(firmware, /Wire\.onReceive\(onI2CReceive\)/);
  assert.match(firmware, /crc16Update\(0xFFFFU, byteCount\)/);
  assert.match(firmware, /Serial\.write\('\*'\)/);
});

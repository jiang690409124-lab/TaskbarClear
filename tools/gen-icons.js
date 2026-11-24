'use strict';

const fs = require('fs');
const path = require('path');

const OUT_DIR = path.join(__dirname, '..', 'res');
const FINAL_SIZE = 32;
const SS = 4; // 超采样倍数
const W = FINAL_SIZE * SS;

function createCanvas(w, h) {
  return new Uint8ClampedArray(w * h * 4); // RGBA，直通 alpha
}

function blend(canvas, w, x, y, r, g, b, a) {
  if (x < 0 || y < 0 || x >= w || y >= w) return;
  const i = (y * w + x) * 4;
  const sa = a / 255;
  const da = canvas[i + 3] / 255;
  const oa = sa + da * (1 - sa);
  if (oa <= 0) return;
  canvas[i]     = Math.round((r * sa + canvas[i]     * da * (1 - sa)) / oa);
  canvas[i + 1] = Math.round((g * sa + canvas[i + 1] * da * (1 - sa)) / oa);
  canvas[i + 2] = Math.round((b * sa + canvas[i + 2] * da * (1 - sa)) / oa);
  canvas[i + 3] = Math.round(oa * 255);
}

function insideRoundRect(px, py, x0, y0, x1, y1, radius) {
  const cx = Math.min(Math.max(px, x0 + radius), x1 - radius);
  const cy = Math.min(Math.max(py, y0 + radius), y1 - radius);
  const dx = px - cx;
  const dy = py - cy;
  return dx * dx + dy * dy <= radius * radius;
}

function fillRoundRect(canvas, w, x0, y0, x1, y1, radius, r, g, b, a) {
  for (let y = 0; y < w; y++) {
    for (let x = 0; x < w; x++) {
      if (insideRoundRect(x + 0.5, y + 0.5, x0, y0, x1, y1, radius)) {
        blend(canvas, w, x, y, r, g, b, a);
      }
    }
  }
}

function renderIcon(kind) {
  const canvas = createCanvas(W, W);
  const s = SS;
  const px = (v) => v * s;

  // 逻辑坐标：胶囊 (3.5,10) - (28.5,22)，圆角 6
  const x0 = px(3.5);
  const y0 = px(10);
  const x1 = px(28.5);
  const y1 = px(22);
  const r = px(6);

  if (kind === 'on') {
    fillRoundRect(canvas, W, x0, y0, x1, y1, r, 255, 255, 255, 235);
    fillRoundRect(canvas, W, x0 + s, y0 + s, x1 - s, y1 - s, r - s, 41, 140, 235, 200);
    fillRoundRect(canvas, W, x0 + px(2), y0 + px(1.5), x1 - px(2), y0 + px(3.5), px(2), 255, 255, 255, 110);
  } else {
    fillRoundRect(canvas, W, x0, y0, x1, y1, r, 60, 60, 60, 255);
    fillRoundRect(canvas, W, x0 + s, y0 + s, x1 - s, y1 - s, r - s, 120, 120, 120, 255);
  }

  const out = Buffer.alloc(FINAL_SIZE * FINAL_SIZE * 4);
  const n = SS * SS;
  for (let y = 0; y < FINAL_SIZE; y++) {
    for (let x = 0; x < FINAL_SIZE; x++) {
      let rSum = 0;
      let gSum = 0;
      let bSum = 0;
      let aSum = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const i = ((y * SS + sy) * W + (x * SS + sx)) * 4;
          const a = canvas[i + 3];
          rSum += canvas[i] * a;
          gSum += canvas[i + 1] * a;
          bSum += canvas[i + 2] * a;
          aSum += a;
        }
      }
      const o = (y * FINAL_SIZE + x) * 4;
      if (aSum === 0) {
        out[o] = 0;
        out[o + 1] = 0;
        out[o + 2] = 0;
        out[o + 3] = 0;
      } else {
        out[o] = Math.round(rSum / aSum);
        out[o + 1] = Math.round(gSum / aSum);
        out[o + 2] = Math.round(bSum / aSum);
        out[o + 3] = Math.round(aSum / n);
      }
    }
  }
  return out;
}

function buildIco(rgba, size) {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2); // 1 = icon
  header.writeUInt16LE(1, 4); // 1 张图

  const maskRowBytes = Math.ceil(size / 32) * 4;
  const xor = Buffer.alloc(size * size * 4);
  const mask = Buffer.alloc(maskRowBytes * size);

  for (let y = 0; y < size; y++) {
    const srcY = size - 1 - y; // ICO 像素自下而上
    for (let x = 0; x < size; x++) {
      const src = (srcY * size + x) * 4;
      const dst = (y * size + x) * 4;
      xor[dst] = rgba[src + 2];     // B
      xor[dst + 1] = rgba[src + 1]; // G
      xor[dst + 2] = rgba[src];     // R
      xor[dst + 3] = rgba[src + 3]; // A
      if (rgba[src + 3] < 8) {
        mask[y * maskRowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }

  const bih = Buffer.alloc(40);
  bih.writeUInt32LE(40, 0);
  bih.writeInt32LE(size, 4);
  bih.writeInt32LE(size * 2, 8); // XOR + AND 两张位图
  bih.writeUInt16LE(1, 12);
  bih.writeUInt16LE(32, 14);
  bih.writeUInt32LE(0, 16); // BI_RGB
  bih.writeUInt32LE(xor.length + mask.length, 20);

  const payload = Buffer.concat([bih, xor, mask]);

  const entry = Buffer.alloc(16);
  entry[0] = size >= 256 ? 0 : size;
  entry[1] = size >= 256 ? 0 : size;
  entry[2] = 0;
  entry[3] = 0;
  entry.writeUInt16LE(1, 4);
  entry.writeUInt16LE(32, 6);
  entry.writeUInt32LE(payload.length, 8);
  entry.writeUInt32LE(6 + 16, 12);

  return Buffer.concat([header, entry, payload]);
}

fs.mkdirSync(OUT_DIR, { recursive: true });
const onIco = buildIco(renderIcon('on'), FINAL_SIZE);
const offIco = buildIco(renderIcon('off'), FINAL_SIZE);
fs.writeFileSync(path.join(OUT_DIR, 'app_on.ico'), onIco);
fs.writeFileSync(path.join(OUT_DIR, 'app_off.ico'), offIco);
console.log(`app_on.ico ${onIco.length} bytes`);
console.log(`app_off.ico ${offIco.length} bytes`);
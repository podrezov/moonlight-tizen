'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = process.env.CONTROLLER_WIDGET_DIR || path.resolve(__dirname, '../../wasm');
const utils = fs.readFileSync(path.join(root, 'static/js/utils.js'), 'utf8');
const controller = fs.readFileSync(path.join(root, 'platform/gamepad.js'), 'utf8');

function xbox(index = 0, timestamp = 0, mapping = 'standard') {
  return {
    id: 'Xbox Wireless Controller (Vendor: 045e Product: 02fd)', index,
    connected: true, mapping, timestamp,
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
    axes: [0, 0, 0, 0]
  };
}

function browser(pads = []) {
  const listeners = new Map();
  let poll;
  const context = vm.createContext({
    navigator: { getGamepads: () => pads },
    console: { log() {}, warn() {}, error() {} },
    setInterval(fn) { poll = fn; return 1; }, clearInterval() {},
    CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options.detail; } },
    window: {
      addEventListener(type, fn) {
        const callbacks = listeners.get(type) || [];
        callbacks.push(fn); listeners.set(type, callbacks);
      },
      dispatchEvent(event) { for (const fn of listeners.get(event.type) || []) fn(event); }
    }
  });
  vm.runInContext(utils, context, { filename: 'utils.js' });
  return {
    pads, context,
    mask: () => vm.runInContext('getConnectedGamepadMask()', context),
    startUi() {
      vm.runInContext(controller, context, { filename: 'gamepad.js' });
      vm.runInContext('Controller.startWatching()', context);
      for (const gamepad of pads) if (gamepad) context.window.dispatchEvent({ type: 'gamepadconnected', gamepad });
    },
    poll: () => poll()
  };
}

test('positive timestamp keeps the standard Xbox in its physical launch slot', () => {
  const b = browser([null, null, null, xbox(3, 123)]);
  assert.equal(b.mask(), 8);
});

for (const timestamp of [0, undefined, NaN, Infinity, -1]) {
  test(`button transition recognizes Xbox with unusable timestamp ${timestamp}`, () => {
    const p = xbox(0, timestamp); p.timestamp = timestamp;
    const b = browser([p]);
    assert.equal(b.mask(), 0);
    p.buttons[0].pressed = true; p.buttons[0].value = 1;
    assert.equal(b.mask(), 1);
    p.buttons[0].pressed = false; p.buttons[0].value = 0;
    assert.equal(b.mask(), 1);
  });
}

test('constant nonzero axes and held phantom buttons do not establish validity', () => {
  const p = xbox(); p.axes = [-1, -1, 0.5, 0.5]; p.buttons[0].pressed = true; p.buttons[0].value = 1;
  const b = browser([p]);
  for (let i = 0; i < 20; ++i) assert.equal(b.mask(), 0);
});

test('small noise is ignored but cumulative axis movement establishes validity', () => {
  const p = xbox(); const b = browser([p]);
  assert.equal(b.mask(), 0);
  for (const value of [0.002, 0.004, 0.006, 0.008]) {
    p.axes[0] = value; assert.equal(b.mask(), 0);
  }
  p.axes[0] = 0.02;
  assert.equal(b.mask(), 1);
});

test('nonstandard mapping supports analog-trigger transitions', () => {
  const p = xbox(0, 0, ''); const b = browser([p]);
  assert.equal(b.mask(), 0);
  p.buttons[6].value = 0.5;
  assert.equal(b.mask(), 1);
});

test('nonfinite axes do not validate a placeholder or poison the baseline', () => {
  const p = xbox(); p.axes[0] = NaN; const b = browser([p]);
  assert.equal(b.mask(), 0);
  p.axes[0] = Infinity; assert.equal(b.mask(), 0);
  p.axes[0] = 0; assert.equal(b.mask(), 0);
  p.axes[0] = 0.5; assert.equal(b.mask(), 1);
});

test('disconnect and reconnect require fresh input evidence', () => {
  const p = xbox(); const b = browser([p]); b.mask();
  p.buttons[0].pressed = true; assert.equal(b.mask(), 1);
  p.connected = false; assert.equal(b.mask(), 0);
  p.connected = true; assert.equal(b.mask(), 0);
  p.buttons[0].pressed = false; assert.equal(b.mask(), 1);
});

test('identity replacement and layout changes cannot reuse pending evidence', () => {
  const p = xbox(); const b = browser([p]); b.mask();
  p.id = 'Other Controller'; p.axes[0] = 0.5; assert.equal(b.mask(), 0);
  p.axes.push(0); p.axes[0] = 0.8; assert.equal(b.mask(), 0);
  p.axes[0] = 0.9; assert.equal(b.mask(), 1);
});

test('validated identity replacement waits for its own evidence', () => {
  const p = xbox(0, 1); const b = browser([p]); assert.equal(b.mask(), 1);
  p.timestamp = 0; p.id = 'Replacement'; assert.equal(b.mask(), 0);
  p.buttons[1].pressed = true; assert.equal(b.mask(), 1);
});

test('sparse simultaneous controllers retain indices including bit 15', () => {
  const pads = Array(16).fill(null); pads[3] = xbox(3); pads[15] = xbox(15, 1);
  const b = browser(pads); assert.equal(b.mask(), 32768);
  pads[3].buttons[0].pressed = true; assert.equal(b.mask(), 32776);
  pads[15] = null; assert.equal(b.mask(), 8);
});

test('positive timestamp history survives timestamp reset', () => {
  const p = xbox(0, 123); const b = browser([p]); assert.equal(b.mask(), 1);
  p.timestamp = 0; assert.equal(b.mask(), 1);
});

test('UI polling records a zero-timestamp controller before the first stream launch', () => {
  const p = xbox(); const b = browser([p]); b.startUi(); b.poll();
  p.buttons[0].pressed = true; p.buttons[0].value = 1;
  b.poll();
  assert.equal(b.mask(), 1);
});

test('UI polling clears removed slots even when the browser array shrinks', () => {
  const p = xbox(3, 123); const b = browser([null, null, null, p]); b.startUi(); b.poll();
  assert.equal(b.mask(), 8);
  b.pads.length = 0; b.poll();
  b.pads[3] = xbox(3); b.poll();
  assert.equal(b.mask(), 0);
});

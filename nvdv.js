#!/usr/bin/env bun

import { parseArgs } from 'util';

const { values: { get, set, toggle, enable, disable, verbose } } = parseArgs({
  args: process.argv.slice(2),
  options: {
    get: { short: 'g', type: 'boolean' },
    set: { short: 's', type: 'string' },
    toggle: { short: 't', type: 'boolean' },
    enable: { short: 'e', type: 'boolean' },
    disable: { short: 'd', type: 'boolean' },
    verbose: { short: 'v', type: 'boolean' },
  },
});

/**
 * @typedef {{
 *   version: number,
 *   current: number,
 *   min: number,
 *   max: number,
 * }} DV
 *
 * @type {{
 *   getDigitalVibrance: () => DV,
 *   setDigitalVibrance: (value: number) => void,
 *   toggleDigitalVibrance: () => void,
 *   enableDigitalVibrance: () => void,
 *   disableDigitalVibrance: () => void,
 * }}
 */
const nvdv = require('./build/Release/binding.node');

(() => {
  const { current, min, max, version } = nvdv.getDigitalVibrance();

  verbose && console.log(`NV_DISPLAY_DVC_VERSION: ${version}`);

  if (get) {
    if (!verbose) return console.log(current);
    console.log(`Current Digital Vibrance: ${current}`);
    console.log(`Minimum Digital Vibrance: ${min}`);
    console.log(`Maximum Digital Vibrance: ${max}`);
    return;
  }

  if (set) {
    const level = +set;
    level === current || nvdv.setDigitalVibrance(level);
    verbose && console.log(`Set Digital Vibrance: ${current} -> ${level}`);
    return;
  }

  if (toggle) {
    nvdv.toggleDigitalVibrance();
    verbose && console.log(`Toggle Digital Vibrance: ${current} -> ${current > min ? min : max}`);
    return;
  }

  if (enable) {
    current > min || nvdv.enableDigitalVibrance();
    verbose && console.log(`Enable Digital Vibrance: ${current} -> ${max}`);
    return;
  }

  if (disable) {
    current === min || nvdv.disableDigitalVibrance();
    verbose && console.log(`Disable Digital Vibrance: ${current} -> ${min}`);
    return;
  }
})();

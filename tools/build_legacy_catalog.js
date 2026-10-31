#!/usr/bin/env node
/*
 * Builds resources/legacy_genes.json from the original Electron app's
 * js/gene_index.js.  The legacy catalog is only used when importing DNA
 * produced by that application: its Base64 payload has the same 104 slots but
 * a different tail order and a few re-indexed options.
 *
 * Usage:
 *   node tools/build_legacy_catalog.js [path/to/gene_index.js]
 */
'use strict';

const fs = require('fs');
const path = require('path');

const source = process.argv[2]
    || path.resolve(__dirname, '..', '..', 'CK3_DNA_Manager', 'js', 'gene_index.js');
const out = path.resolve(__dirname, '..', 'resources', 'legacy_genes.json');

// eslint-disable-next-line import/no-dynamic-require, global-require
const legacy = require(path.resolve(source));
const keys = Object.keys(legacy);

const colors = ['hair_color', 'skin_color', 'eye_color'];
const genes = colors.map((key) => ({
    key,
    kind: 'color',
}));

for (const key of keys) {
    const options = Object.entries(legacy[key]).map(([id, index]) => ({ id, index }));
    options.sort((a, b) => a.index - b.index);
    genes.push({ key, kind: 'enum', options });
}

const catalog = {
    version: 1,
    source: 'CK3_DNA_Manager (Electron) js/gene_index.js',
    base64GeneCount: genes.length,
    genes,
};

fs.writeFileSync(out, `${JSON.stringify(catalog, null, 2)}\n`, 'utf8');
console.log(`Wrote ${out}: ${genes.length} legacy genes`);

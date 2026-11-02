import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { GeneCatalog } from '../js/core/catalog.js';
import * as Codec from '../js/core/codec.js';
import { importLibrary } from '../js/core/legacy-importer.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const assets = path.join(here, '..', 'assets');
const catalog = GeneCatalog.fromJson(JSON.parse(fs.readFileSync(path.join(assets, 'genes.json'), 'utf8')));
const legacyCatalog = GeneCatalog.fromJson(JSON.parse(fs.readFileSync(path.join(assets, 'legacy_genes.json'), 'utf8')));

function blankData() {
  const data = Codec.createDnaData();
  for (let i = 0; i < catalog.base64GeneCount(); i += 1) {
    const gene = catalog.byBase64Index(i);
    if (gene.kind === 'color') data.colors.set(gene.key, [0, 0, 0, 0]);
    else data.genes.set(gene.key, {
      first: { variant: 0, intensity: 0 },
      second: { variant: 0, intensity: 0 },
    });
  }
  return data;
}

test('app export import', async () => {
  const data = blankData();
  data.genes.set('gene_hair_type', {
    first: { variant: 1, intensity: 100 },
    second: { variant: 2, intensity: 50 },
  });
  data.extras.push({
    key: 'mod_gene_x',
    bytes: Uint8Array.from([1, 2, 3, 4]),
    kind: Codec.ExtraGeneKind.Modded,
    firstOption: '',
    secondOption: '',
  });
  const base64 = Codec.toBase64(data, catalog);
  const rulerDesigner = Codec.toRuler(data, catalog);
  const rulerMeta = Codec.serializeRulerMetadata(data);

  const root = {
    format: 'ck3-dna-manager-qt',
    version: 1,
    entries: [
      {
        name: 'Imported Modern',
        notes: 'note',
        favorite: true,
        base64,
        geneCount: 105,
        moddedGeneCount: 1,
        createdAt: 1000,
        updatedAt: 2000,
      },
      {
        name: 'Imported Ruler',
        rulerDesigner,
        rulerMeta,
        base64,
        createdAt: 3000,
        updatedAt: 4000,
      },
    ],
  };

  const result = await importLibrary(root, { catalog, legacyCatalog });
  assert.equal(result.ok, true);
  assert.equal(result.imported, 2);
  assert.equal(result.skipped, 0);
  assert.equal(result.entries[0].favorite, true);
  assert.equal(result.entries[0].name, 'Imported Modern');
  assert.equal(result.entries[0].notes, 'note');
  assert.equal(result.entries[0].createdAt, 1000);
  assert.equal(result.entries[0].updatedAt, 2000);
  assert.equal(result.entries[1].rulerMeta.includes('ck3-ruler-meta'), true);
});

test('legacy array import', async () => {
  const bytes = new Uint8Array(416);
  bytes[99 * 4] = 20;
  const legacyDna = Codec.bytesToBase64(bytes);
  const result = await importLibrary([
    { id: '1600000000000', name: 'Old Entry', dna: legacyDna },
    { remove: true, id: '2' },
    { id: '3', name: 'Empty', dna: '' },
  ], { catalog, legacyCatalog });
  assert.equal(result.ok, true);
  assert.equal(result.imported, 1);
  assert.equal(result.skipped, 2);
  assert.equal(result.entries[0].name, 'Old Entry');
  assert.equal(result.entries[0].createdAt, 1600000000000);
  assert.equal(result.entries[0].dna.length, 556);
});

test('unsupported and empty files', async () => {
  const unsupported = await importLibrary({ hello: 'world' }, { catalog, legacyCatalog });
  assert.equal(unsupported.ok, false);
  assert.equal(unsupported.unsupportedFormat, true);

  const version = await importLibrary({ format: 'ck3-dna-manager-qt', version: 2, entries: [] }, { catalog, legacyCatalog });
  assert.equal(version.ok, false);
  assert.equal(version.unsupportedVersion, true);

  const empty = await importLibrary({ format: 'ck3-dna-manager-qt', version: 1, entries: [] }, { catalog, legacyCatalog });
  assert.equal(empty.ok, false);
  assert.equal(empty.emptyFile, true);
});

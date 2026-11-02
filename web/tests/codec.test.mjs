import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { GeneCatalog } from '../js/core/catalog.js';
import * as Codec from '../js/core/codec.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const assets = path.join(here, '..', 'assets');
const genes = JSON.parse(fs.readFileSync(path.join(assets, 'genes.json'), 'utf8'));
const legacyGenes = JSON.parse(fs.readFileSync(path.join(assets, 'legacy_genes.json'), 'utf8'));
const catalog = GeneCatalog.fromJson(genes);
const legacyCatalog = GeneCatalog.fromJson(legacyGenes);

test('catalog structure', () => {
  assert.equal(catalog.base64GeneCount(), 104);
  assert.equal(catalog.genes.length, 106);
  assert.equal(catalog.categoriesInUse().length, 12);
  assert.equal(catalog.byBase64Index(0).key, 'hair_color');
  assert.equal(catalog.byBase64Index(99).key, 'gene_hair_type');
  assert.equal(catalog.byBase64Index(100).key, 'gene_baldness');
  assert.equal(catalog.byBase64Index(101).key, 'eye_accessory');
  assert.equal(catalog.byBase64Index(103).key, 'eyelashes_accessory');
  assert.equal(catalog.byBase64Index(104), null);
});

test('modern round trip', () => {
  const data = Codec.createDnaData();
  for (let i = 0; i < catalog.base64GeneCount(); i += 1) {
    const gene = catalog.byBase64Index(i);
    if (gene.kind === 'color') data.colors.set(gene.key, [i, i + 1, i + 2, i + 3]);
    else data.genes.set(gene.key, {
      first: { variant: i % 5, intensity: i % 256 },
      second: { variant: (i + 1) % 5, intensity: (i + 2) % 256 },
    });
  }
  const bytes = Codec.toBytes(data, catalog);
  assert.equal(bytes.length, 416);
  const parsed = Codec.fromBytes(bytes, Codec.Base64Hint.Modern, catalog, legacyCatalog);
  assert.equal(parsed.ok, true);
  assert.equal(parsed.format, Codec.DnaFormat.ModernBase64);
  assert.equal(parsed.geneCount, 104);
  assert.equal(parsed.legacyConverted, false);
  assert.equal(Codec.toBase64(data, catalog), Codec.bytesToBase64(bytes));
});

test('modded round trip', () => {
  const data = Codec.createDnaData();
  for (let i = 0; i < catalog.base64GeneCount(); i += 1) {
    const gene = catalog.byBase64Index(i);
    if (gene.kind === 'color') data.colors.set(gene.key, [1, 2, 3, 4]);
    else data.genes.set(gene.key, {
      first: { variant: 0, intensity: 0 },
      second: { variant: 0, intensity: 0 },
    });
  }
  for (let i = 0; i < 13; i += 1) {
    data.extras.push({
      key: `extra_${105 + i}`,
      bytes: Uint8Array.from([i, i + 1, i + 2, i + 3]),
      kind: Codec.ExtraGeneKind.Modded,
      firstOption: '',
      secondOption: '',
    });
  }
  const bytes = Codec.toBytes(data, catalog);
  assert.equal(bytes.length, 468);
  const base64 = Codec.toBase64(data, catalog);
  assert.equal(base64.length, 624);
  const parsed = Codec.fromBytes(bytes, Codec.Base64Hint.Auto, catalog, legacyCatalog);
  assert.equal(parsed.ok, true);
  assert.equal(parsed.geneCount, 117);
  assert.equal(parsed.data.extras.length, 13);
  assert.deepEqual(Array.from(parsed.data.extras[0].bytes), [0, 1, 2, 3]);
  assert.deepEqual(Array.from(parsed.data.extras[12].bytes), [12, 13, 14, 15]);
});

test('legacy detection and conversion', () => {
  const bytes = new Uint8Array(416);
  bytes[99 * 4] = 20;
  assert.equal(Codec.looksLikeLegacy(bytes, catalog), true);
  const parsed = Codec.fromBytes(bytes, Codec.Base64Hint.Auto, catalog, legacyCatalog);
  assert.equal(parsed.ok, true);
  assert.equal(parsed.format, Codec.DnaFormat.LegacyBase64);
  assert.equal(parsed.legacyConverted, true);
  assert.equal(parsed.geneCount, 104);
});

test('ruler round trip and metadata', () => {
  const data = Codec.createDnaData();
  for (let i = 0; i < catalog.base64GeneCount(); i += 1) {
    const gene = catalog.byBase64Index(i);
    if (gene.kind === 'color') data.colors.set(gene.key, [10, 20, 30, 40]);
    else data.genes.set(gene.key, {
      first: { variant: 0, intensity: 100 },
      second: { variant: 0, intensity: 50 },
    });
  }
  data.extras.push({
    key: 'mod_gene_x',
    bytes: Uint8Array.from([5, 77, 9, 88]),
    kind: Codec.ExtraGeneKind.Modded,
    firstOption: 'option_a',
    secondOption: 'option_b',
  });
  data.extras.push({
    key: 'beards',
    bytes: Uint8Array.from([2, 3, 4, 5]),
    kind: Codec.ExtraGeneKind.RulerOnly,
    firstOption: '',
    secondOption: '',
  });
  data.ruler.valid = true;
  data.ruler.order = ['mod_gene_x', 'gene_hair_type', 'beards'];
  data.ruler.rawTokens.set('gene_hair_type', ['hair_wavy', '100', 'hair_curly', '50']);

  const text = Codec.toRuler(data, catalog);
  assert.match(text, /^ruler_designer_\d+=\{/);
  assert.match(text, /genes=\{/);
  assert.match(text, /mod_gene_x=\{/);
  assert.match(text, /beards=\{/);

  const parsed = Codec.parse(text, Codec.Base64Hint.Auto, catalog, legacyCatalog);
  assert.equal(parsed.ok, true);
  assert.equal(parsed.format, Codec.DnaFormat.RulerDesigner);
  assert.equal(parsed.data.extras.length, 2);

  const meta = Codec.serializeRulerMetadata(parsed.data);
  assert.match(meta, /ck3-ruler-meta/);
  const restored = Codec.createDnaData();
  for (let i = 0; i < catalog.base64GeneCount(); i += 1) {
    const gene = catalog.byBase64Index(i);
    if (gene.kind === 'color') restored.colors.set(gene.key, [0, 0, 0, 0]);
    else restored.genes.set(gene.key, {
      first: { variant: 0, intensity: 0 },
      second: { variant: 0, intensity: 0 },
    });
  }
  restored.extras = parsed.data.extras
    .filter((extra) => extra.kind === Codec.ExtraGeneKind.Modded)
    .map((extra) => ({ ...extra, bytes: Uint8Array.from(extra.bytes) }));
  Codec.applyRulerMetadata(restored, meta);
  assert.equal(restored.ruler.valid, true);
  assert.equal(restored.extras.length, 2);
  assert.equal(restored.extras[0].key, 'mod_gene_x');
});

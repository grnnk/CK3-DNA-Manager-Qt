// Import app exports and the original Electron CK3 DNA Manager JSON.
// Ported from src/core/LegacyImporter.cpp.

import {
  Base64Hint,
  DnaFormat,
  applyRulerMetadata,
  parse,
  serializeRulerMetadata,
  toBytes,
  bytesToBase64,
} from './codec.js';

function valueToIdString(value) {
  if (typeof value === 'string') return value.trim();
  if (typeof value === 'number' && Number.isFinite(value)) return String(Math.trunc(value));
  return '';
}

function valueToTimestamp(value) {
  if (typeof value === 'number' && Number.isFinite(value)) return Math.trunc(value);
  if (typeof value === 'string' && /^\d+$/.test(value.trim())) return Number(value);
  return 0;
}

export function detectFormat(root) {
  if (Array.isArray(root)) return { format: 'legacy', version: 1 };
  if (!root || typeof root !== 'object') return { format: 'unknown', version: 1 };

  const format = typeof root.format === 'string' ? root.format : '';
  if (format === 'ck3-dna-manager-qt') {
    return { format: 'app', version: Number.isInteger(root.version) ? root.version : 1 };
  }
  if (!format && Array.isArray(root.entries)) return { format: 'app', version: 1 };
  if (Array.isArray(root.data)) return { format: 'legacy', version: 1 };
  return { format: 'unknown', version: 1 };
}

export async function importLibrary(root, {
  catalog,
  legacyCatalog,
  makePortraitFromBase64 = null,
  makePortraitById = null,
  progress = null,
} = {}) {
  const result = {
    ok: false,
    unsupportedFormat: false,
    unsupportedVersion: false,
    emptyFile: false,
    error: '',
    imported: 0,
    portraits: 0,
    skipped: 0,
    warnings: [],
    entries: [],
  };

  const detected = detectFormat(root);
  if (detected.format === 'unknown') {
    result.unsupportedFormat = true;
    result.error = 'This file is not a supported CK3 DNA Manager library or character export.';
    return result;
  }
  if (detected.format === 'app' && detected.version > 1) {
    result.unsupportedVersion = true;
    result.error = `The export version ${detected.version} is not supported.`;
    return result;
  }

  let items;
  if (detected.format === 'app') {
    if (!root || !Array.isArray(root.entries)) {
      result.unsupportedFormat = true;
      result.error = 'This file is not a supported CK3 DNA Manager library or character export.';
      return result;
    }
    items = root.entries;
  } else {
    items = Array.isArray(root) ? root : root.data;
  }

  if (!Array.isArray(items) || items.length === 0) {
    result.emptyFile = true;
    result.error = 'The selected file contains no characters.';
    return result;
  }

  for (let index = 0; index < items.length; index += 1) {
    if (progress) progress(index, items.length);
    const item = items[index];
    if (!item || typeof item !== 'object' || Array.isArray(item)) {
      result.skipped += 1;
      continue;
    }
    if (Object.prototype.hasOwnProperty.call(item, 'remove')) {
      result.skipped += 1;
      continue;
    }

    if (detected.format === 'app') {
      await importExportEntry(item, result, {
        catalog,
        legacyCatalog,
        makePortraitFromBase64,
      });
    } else {
      await importLegacyEntry(item, result, {
        catalog,
        legacyCatalog,
        makePortraitById,
      });
    }
  }

  if (progress) progress(items.length, items.length);
  result.ok = true;
  return result;
}

async function importExportEntry(item, result, { catalog, legacyCatalog, makePortraitFromBase64 }) {
  const name = (typeof item.name === 'string' ? item.name : '').trim() || 'Imported DNA';
  let parsed = null;

  if (typeof item.base64 === 'string' && item.base64.trim()) {
    const candidate = parse(item.base64, Base64Hint.Modern, catalog, legacyCatalog);
    if (candidate.ok) parsed = candidate;
  }

  if (typeof item.rulerDesigner === 'string' && item.rulerDesigner.trim()) {
    const candidate = parse(item.rulerDesigner, Base64Hint.Auto, catalog, legacyCatalog);
    if (candidate.ok) parsed = candidate;
  }

  if (!parsed) {
    result.skipped += 1;
    result.warnings.push(`Skipped "${name}": no DNA payload was found.`);
    return;
  }

  if (typeof item.rulerMeta === 'string' && item.rulerMeta) {
    applyRulerMetadata(parsed.data, item.rulerMeta);
  }

  const dna = bytesToBase64(toBytes(parsed.data, catalog));
  const rulerMeta = serializeRulerMetadata(parsed.data);
  const entry = {
    id: typeof item.id === 'string' ? item.id : '',
    name,
    notes: typeof item.notes === 'string' ? item.notes : '',
    dna,
    rulerMeta,
    favorite: item.favorite === true,
    createdAt: valueToTimestamp(item.createdAt),
    updatedAt: valueToTimestamp(item.updatedAt),
    hasPortrait: false,
    portraitDataUrl: '',
    thumbnailDataUrl: '',
  };

  if (typeof item.portraitPng === 'string' && item.portraitPng && makePortraitFromBase64) {
    try {
      const portrait = await makePortraitFromBase64(item.portraitPng);
      if (portrait) {
        entry.hasPortrait = true;
        entry.portraitDataUrl = portrait.portraitDataUrl;
        entry.thumbnailDataUrl = portrait.thumbnailDataUrl;
        result.portraits += 1;
      }
    } catch (error) {
      result.warnings.push(`Skipped portrait for "${name}": ${error.message}`);
    }
  }

  result.entries.push(entry);
  result.imported += 1;
}

async function importLegacyEntry(item, result, { catalog, legacyCatalog, makePortraitById }) {
  const legacyId = valueToIdString(item.id);
  const name = (typeof item.name === 'string' ? item.name : '').trim() || 'Imported DNA';
  const dnaText = typeof item.dna === 'string' ? item.dna : '';

  if (!dnaText.trim()) {
    result.skipped += 1;
    result.warnings.push(`Skipped "${name}": empty DNA.`);
    return;
  }

  const parsed = parse(dnaText, Base64Hint.Legacy, catalog, legacyCatalog);
  if (!parsed.ok) {
    result.skipped += 1;
    result.warnings.push(`Skipped "${name}": ${parsed.error}`);
    return;
  }

  const createdHint = Number.parseInt(legacyId, 10);
  const createdAt = Number.isFinite(createdHint) && createdHint > 946684800000 ? createdHint : 0;
  const entry = {
    name,
    notes: '',
    dna: bytesToBase64(toBytes(parsed.data, catalog)),
    rulerMeta: serializeRulerMetadata(parsed.data),
    favorite: false,
    createdAt,
    updatedAt: 0,
    hasPortrait: false,
    portraitDataUrl: '',
    thumbnailDataUrl: '',
  };

  if (legacyId && makePortraitById) {
    try {
      const portrait = await makePortraitById(legacyId);
      if (portrait) {
        entry.hasPortrait = true;
        entry.portraitDataUrl = portrait.portraitDataUrl;
        entry.thumbnailDataUrl = portrait.thumbnailDataUrl;
        result.portraits += 1;
      }
    } catch (error) {
      result.warnings.push(`Skipped portrait for "${name}": ${error.message}`);
    }
  }

  result.entries.push(entry);
  result.imported += 1;
}

export function formatSummary(result) {
  const parts = [];
  if (result.format === DnaFormat.LegacyBase64) parts.push('legacy');
  return parts.join('');
}

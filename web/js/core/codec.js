// Conversion between CK3 Base64 DNA, Ruler Designer text and DnaData.
// Ported from src/core/DnaCodec.cpp.

const BYTES_PER_GENE = 4;
const VANILLA_GENE_COUNT = 104;

export const Base64Hint = Object.freeze({
  Auto: 'auto',
  Modern: 'modern',
  Legacy: 'legacy',
});

export const DnaFormat = Object.freeze({
  Invalid: 'invalid',
  ModernBase64: 'modern',
  LegacyBase64: 'legacy',
  RulerDesigner: 'ruler',
});

export const ExtraGeneKind = Object.freeze({
  Modded: 'modded',
  RulerOnly: 'ruler-only',
});

export function createDnaData() {
  return {
    genes: new Map(),
    colors: new Map(),
    extras: [],
    ruler: {
      valid: false,
      prefix: '',
      suffix: '',
      order: [],
      rawTokens: new Map(),
    },
  };
}

export function cloneDnaData(data) {
  const copy = createDnaData();
  for (const [key, pair] of data.genes) {
    copy.genes.set(key, {
      first: { variant: pair.first.variant, intensity: pair.first.intensity },
      second: { variant: pair.second.variant, intensity: pair.second.intensity },
    });
  }
  for (const [key, color] of data.colors) copy.colors.set(key, [...color]);
  copy.extras = data.extras.map((extra) => ({
    key: extra.key,
    bytes: Uint8Array.from(extra.bytes),
    kind: extra.kind,
    firstOption: extra.firstOption,
    secondOption: extra.secondOption,
  }));
  copy.ruler = {
    valid: data.ruler.valid,
    prefix: data.ruler.prefix,
    suffix: data.ruler.suffix,
    order: [...data.ruler.order],
    rawTokens: new Map(
      [...data.ruler.rawTokens].map(([key, values]) => [key, [...values]]),
    ),
  };
  return copy;
}

export function bytesToBase64(bytes) {
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

export function base64ToBytes(text) {
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function readFour(bytes, geneIndex) {
  const offset = geneIndex * BYTES_PER_GENE;
  return [
    bytes[offset],
    bytes[offset + 1],
    bytes[offset + 2],
    bytes[offset + 3],
  ];
}

function pairFromBytes(values) {
  return {
    first: { variant: values[0], intensity: values[1] },
    second: { variant: values[2], intensity: values[3] },
  };
}

function normalizeBase64(text) {
  let compact = '';
  for (const character of text) {
    if (/\s/.test(character)) continue;
    if (character === '-') compact += '+';
    else if (character === '_') compact += '/';
    else compact += character;
  }

  if (!compact) return { input: '', error: 'The DNA code is empty.' };

  for (const character of compact) {
    if (!/[A-Za-z0-9+/=]/.test(character)) {
      return { input: '', error: 'The DNA code contains an invalid Base64 character.' };
    }
  }

  let input = compact;
  while (input.length % 4 !== 0) input += '=';
  return { input, error: '' };
}

function hasGenesBlock(text) {
  return /\bgenes\s*=\s*\{/.test(text);
}

function tokenizeRuler(text) {
  const tokens = [];
  let i = 0;

  while (i < text.length) {
    const character = text[i];

    if (/\s/.test(character)) {
      i += 1;
      continue;
    }

    if (character === '#') {
      while (i < text.length && text[i] !== '\n') i += 1;
      continue;
    }

    if (character === '=') {
      tokens.push({ kind: 'equal', text: '=', start: i, end: i + 1 });
      i += 1;
      continue;
    }

    if (character === '{') {
      tokens.push({ kind: 'lbrace', text: '{', start: i, end: i + 1 });
      i += 1;
      continue;
    }

    if (character === '}') {
      tokens.push({ kind: 'rbrace', text: '}', start: i, end: i + 1 });
      i += 1;
      continue;
    }

    if (character === '"') {
      const tokenStart = i;
      i += 1;
      let value = '';
      let closed = false;
      while (i < text.length) {
        const current = text[i];
        if (current === '"') {
          closed = true;
          i += 1;
          break;
        }
        if (current === '\\' && i + 1 < text.length) {
          value += text[i + 1];
          i += 2;
          continue;
        }
        value += current;
        i += 1;
      }
      if (!closed) {
        return { tokens: [], error: 'Unterminated string in Ruler Designer code.' };
      }
      tokens.push({ kind: 'string', text: value, start: tokenStart, end: i });
      continue;
    }

    const start = i;
    while (i < text.length) {
      const current = text[i];
      if (/\s/.test(current) || current === '{' || current === '}'
          || current === '=' || current === '"' || current === '#') {
        break;
      }
      i += 1;
    }
    if (i === start) {
      i += 1;
      continue;
    }
    tokens.push({ kind: 'word', text: text.slice(start, i), start, end: i });
  }

  return { tokens, error: '' };
}

function numericWord(token) {
  if (token.kind !== 'word') return null;
  if (!/^-?\d+$/.test(token.text)) return null;
  return Number.parseInt(token.text, 10);
}

function byteValue(token, warnings, geneKey) {
  const value = numericWord(token);
  if (value === null) {
    if (warnings) warnings.push(`Expected a number for ${geneKey}, using 0.`);
    return 0;
  }
  return clamp(value, 0, 255);
}

function variantValue(token, catalog, geneKey, warnings) {
  const numeric = numericWord(token);
  if (numeric !== null) return clamp(numeric, 0, 255);

  const index = catalog.optionIndexById(geneKey, token.text);
  if (index >= 0) return index;

  if (warnings) warnings.push(`Unknown option "${token.text}" for ${geneKey}, using 0.`);
  return 0;
}

function optionToken(catalog, key, index) {
  const id = catalog.optionId(key, index);
  return id ? `"${id}"` : String(index);
}

function extraKeyForSlot(geneSlot) {
  return `extra_${geneSlot + 1}`;
}

function isRulerOnlyKey(key, catalog) {
  const gene = catalog.byKey(key);
  if (gene && !gene.inBase64) return true;
  return new Set(['clothes', 'clothing', 'custom_beards', 'custom_hair']).has(key);
}

function quotedRulerString(value) {
  return `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

function extraValueToken(extra, first) {
  const option = first ? extra.firstOption : extra.secondOption;
  if (option) return quotedRulerString(option);
  return String(first ? extra.bytes[0] : extra.bytes[2]);
}

function extraStrength(extra, first) {
  return String(first ? extra.bytes[1] : extra.bytes[3]);
}

export function looksLikeLegacy(bytes, catalog) {
  if (bytes.length !== VANILLA_GENE_COUNT * BYTES_PER_GENE) return false;

  const modernHairType = bytes[99 * BYTES_PER_GENE];
  const modernBaldness = bytes[100 * BYTES_PER_GENE];
  const hairType = catalog.byBase64Index(99);
  const baldness = catalog.byBase64Index(100);
  const hairMax = hairType ? hairType.options.length - 1 : 4;
  const baldnessMax = baldness ? baldness.options.length - 1 : 1;
  return modernHairType > Math.max(hairMax, 4) || modernBaldness > Math.max(baldnessMax, 1);
}

export function parse(text, hint, catalog, legacyCatalog) {
  const trimmed = text.trim();
  if (!trimmed) {
    return {
      ok: false,
      error: 'The DNA code is empty.',
      warnings: [],
      data: createDnaData(),
      format: DnaFormat.Invalid,
      legacyConverted: false,
      geneCount: 0,
    };
  }

  if (trimmed.includes('ruler_designer') || hasGenesBlock(trimmed)) {
    return parseRuler(trimmed, catalog, legacyCatalog);
  }

  const normalized = normalizeBase64(trimmed);
  if (!normalized.input) {
    return {
      ok: false,
      error: normalized.error,
      warnings: [],
      data: createDnaData(),
      format: DnaFormat.Invalid,
      legacyConverted: false,
      geneCount: 0,
    };
  }

  let bytes;
  try {
    bytes = base64ToBytes(normalized.input);
  } catch {
    bytes = new Uint8Array(0);
  }
  if (bytes.length === 0) {
    return {
      ok: false,
      error: 'The DNA code could not be decoded as Base64.',
      warnings: [],
      data: createDnaData(),
      format: DnaFormat.Invalid,
      legacyConverted: false,
      geneCount: 0,
    };
  }

  return parseBase64Bytes(bytes, hint, catalog, legacyCatalog);
}

export function fromBytes(bytes, hint, catalog, legacyCatalog) {
  return parseBase64Bytes(bytes, hint, catalog, legacyCatalog);
}

function parseBase64Bytes(bytes, hint, catalog, legacyCatalog) {
  const result = {
    ok: false,
    error: '',
    warnings: [],
    data: createDnaData(),
    format: DnaFormat.Invalid,
    legacyConverted: false,
    geneCount: 0,
  };

  if (bytes.length === 0) {
    result.error = 'The DNA payload is empty.';
    return result;
  }
  if (bytes.length % BYTES_PER_GENE !== 0) {
    result.error = `The DNA payload has an invalid length (${bytes.length} bytes).`;
    return result;
  }

  const geneCount = bytes.length / BYTES_PER_GENE;
  if (geneCount < VANILLA_GENE_COUNT) {
    result.error = `The DNA payload contains ${geneCount} genes; at least ${VANILLA_GENE_COUNT} are required.`;
    return result;
  }

  let useLegacy = hint === Base64Hint.Legacy
    || (hint === Base64Hint.Auto && looksLikeLegacy(bytes, catalog));
  if (useLegacy && geneCount > VANILLA_GENE_COUNT) {
    useLegacy = false;
    result.warnings.push(
      'Legacy format only contains 104 genes; the extra genes were kept as modern data.',
    );
  }

  const raw = createDnaData();
  for (let index = 0; index < VANILLA_GENE_COUNT; index += 1) {
    const gene = catalog.byBase64Index(index);
    if (!gene) {
      result.error = `The gene catalog is missing Base64 slot ${index}.`;
      return result;
    }
    const values = readFour(bytes, index);
    if (gene.kind === 'color') raw.colors.set(gene.key, values);
    else raw.genes.set(gene.key, pairFromBytes(values));
  }

  for (let index = VANILLA_GENE_COUNT; index < geneCount; index += 1) {
    raw.extras.push({
      key: extraKeyForSlot(index),
      bytes: Uint8Array.from(readFour(bytes, index)),
      kind: ExtraGeneKind.Modded,
      firstOption: '',
      secondOption: '',
    });
  }

  if (useLegacy) {
    result.data = convertLegacy(raw, catalog, legacyCatalog, result.warnings);
    result.format = DnaFormat.LegacyBase64;
    result.legacyConverted = true;
  } else {
    result.data = raw;
    result.format = DnaFormat.ModernBase64;
  }

  result.geneCount = geneCount;
  result.ok = true;
  return result;
}

function convertLegacy(legacy, catalog, legacyCatalog, warnings) {
  const converted = createDnaData();
  converted.extras = legacy.extras.map((extra) => ({
    key: extra.key,
    bytes: Uint8Array.from(extra.bytes),
    kind: extra.kind,
    firstOption: extra.firstOption,
    secondOption: extra.secondOption,
  }));

  for (let index = 0; index < catalog.base64GeneCount(); index += 1) {
    const gene = catalog.byBase64Index(index);
    if (!gene) continue;

    if (gene.kind === 'color') {
      converted.colors.set(gene.key, [...(legacy.colors.get(gene.key) || [0, 0, 0, 0])]);
      continue;
    }

    const source = legacy.genes.get(gene.key) || {
      first: { variant: 0, intensity: 0 },
      second: { variant: 0, intensity: 0 },
    };
    const legacyGene = legacyCatalog.byKey(gene.key);

    const convertAllele = (allele) => {
      const result = { variant: 0, intensity: allele.intensity };
      if (!legacyGene) return result;

      const oldOption = legacyCatalog.optionByIndex(gene.key, allele.variant);
      if (!oldOption) {
        if (warnings) warnings.push(`Legacy ${gene.key} has an unknown variant ${allele.variant}.`);
        return result;
      }

      const modernIndex = catalog.optionIndexById(gene.key, oldOption.id);
      if (modernIndex < 0) {
        if (warnings) warnings.push(`Legacy option "${oldOption.id}" no longer exists for ${gene.key}.`);
      } else {
        result.variant = modernIndex;
      }
      return result;
    };

    converted.genes.set(gene.key, {
      first: convertAllele(source.first),
      second: convertAllele(source.second),
    });
  }

  return converted;
}

export function toBytes(data, catalog) {
  const bytes = [];
  for (let index = 0; index < catalog.base64GeneCount(); index += 1) {
    const gene = catalog.byBase64Index(index);
    if (!gene) {
      bytes.push(0, 0, 0, 0);
      continue;
    }
    if (gene.kind === 'color') {
      const values = data.colors.get(gene.key) || [0, 0, 0, 0];
      for (let i = 0; i < 4; i += 1) bytes.push(clamp(values[i] || 0, 0, 255));
    } else {
      const pair = data.genes.get(gene.key) || {
        first: { variant: 0, intensity: 0 },
        second: { variant: 0, intensity: 0 },
      };
      bytes.push(
        clamp(pair.first.variant, 0, 255),
        clamp(pair.first.intensity, 0, 255),
        clamp(pair.second.variant, 0, 255),
        clamp(pair.second.intensity, 0, 255),
      );
    }
  }

  for (const extra of data.extras) {
    if (extra.kind !== ExtraGeneKind.Modded) continue;
    for (let i = 0; i < 4; i += 1) bytes.push(clamp(extra.bytes[i] || 0, 0, 255));
  }

  return Uint8Array.from(bytes);
}

export function toBase64(data, catalog) {
  return bytesToBase64(toBytes(data, catalog));
}

export function toRuler(data, catalog) {
  const designerId = 1000000000 + Math.floor(Math.random() * 9000000000);
  let result = `ruler_designer_${designerId}={`;
  const hasMetadata = data.ruler.valid;

  result += hasMetadata && data.ruler.prefix ? data.ruler.prefix : '\n\t';
  result += 'genes={\n';

  const emittedBase64 = new Set();
  const emittedExtras = new Array(data.extras.length).fill(false);

  const emitGene = (gene) => {
    const raw = data.ruler.rawTokens.get(gene.key);
    if (raw && raw.length === 4) {
      result += `\t\t${gene.key}={ ${raw.join(' ')} }\n`;
      return;
    }

    if (gene.kind === 'color') {
      const values = data.colors.get(gene.key) || [0, 0, 0, 0];
      result += `\t\t${gene.key}={ ${values[0]} ${values[1]} ${values[2]} ${values[3]} }\n`;
      return;
    }

    const pair = data.genes.get(gene.key) || {
      first: { variant: 0, intensity: 0 },
      second: { variant: 0, intensity: 0 },
    };
    result += `\t\t${gene.key}={ ${optionToken(catalog, gene.key, pair.first.variant)} ${pair.first.intensity} ${optionToken(catalog, gene.key, pair.second.variant)} ${pair.second.intensity} }\n`;
  };

  const emitExtra = (extra) => {
    const raw = data.ruler.rawTokens.get(extra.key);
    if (raw && raw.length === 4) {
      result += `\t\t${extra.key}={ ${raw.join(' ')} }\n`;
      return;
    }
    result += `\t\t${extra.key}={ ${extraValueToken(extra, true)} ${extraStrength(extra, true)} ${extraValueToken(extra, false)} ${extraStrength(extra, false)} }\n`;
  };

  const findExtra = (key) => {
    for (let i = 0; i < data.extras.length; i += 1) {
      if (!emittedExtras[i] && data.extras[i].key === key) return i;
    }
    return -1;
  };

  if (hasMetadata && data.ruler.order.length > 0) {
    for (const key of data.ruler.order) {
      const gene = catalog.byKey(key);
      if (gene && gene.inBase64) {
        emitGene(gene);
        emittedBase64.add(key);
        continue;
      }
      const extraIndex = findExtra(key);
      if (extraIndex >= 0) {
        emitExtra(data.extras[extraIndex]);
        emittedExtras[extraIndex] = true;
      }
    }
  }

  for (let index = 0; index < catalog.base64GeneCount(); index += 1) {
    const gene = catalog.byBase64Index(index);
    if (gene && !emittedBase64.has(gene.key)) {
      emitGene(gene);
      emittedBase64.add(gene.key);
    }
  }

  for (let index = 0; index < data.extras.length; index += 1) {
    if (!emittedExtras[index]) emitExtra(data.extras[index]);
  }

  result += '\t}';
  result += hasMetadata && data.ruler.suffix ? data.ruler.suffix : '\n';
  result += '}';
  return result;
}

export function serializeRulerMetadata(data) {
  if (!data.ruler.valid) return '';

  const root = {
    format: 'ck3-ruler-meta',
    version: 1,
    prefix: data.ruler.prefix,
    suffix: data.ruler.suffix,
    order: [...data.ruler.order],
    extras: data.extras.map((extra) => {
      const object = {
        key: extra.key,
        kind: extra.kind === ExtraGeneKind.RulerOnly ? 'ruler-only' : 'modded',
        bytes: Array.from(extra.bytes),
      };
      if (extra.firstOption) object.firstOption = extra.firstOption;
      if (extra.secondOption) object.secondOption = extra.secondOption;
      return object;
    }),
    rawTokens: {},
  };

  for (const [key, values] of data.ruler.rawTokens) root.rawTokens[key] = [...values];
  return JSON.stringify(root);
}

export function applyRulerMetadata(data, json) {
  if (!json) return;

  let root;
  try {
    root = JSON.parse(json);
  } catch {
    return;
  }
  if (!root || root.format !== 'ck3-ruler-meta' || root.version !== 1) return;

  const metadata = {
    valid: true,
    prefix: typeof root.prefix === 'string' ? root.prefix : '',
    suffix: typeof root.suffix === 'string' ? root.suffix : '',
    order: Array.isArray(root.order) ? root.order.filter((value) => typeof value === 'string') : [],
    rawTokens: new Map(),
  };

  if (root.rawTokens && typeof root.rawTokens === 'object') {
    for (const [key, values] of Object.entries(root.rawTokens)) {
      if (!Array.isArray(values)) continue;
      const tokens = values.filter((value) => typeof value === 'string');
      if (tokens.length === 4) metadata.rawTokens.set(key, tokens);
    }
  }

  const loadedExtras = [];
  if (Array.isArray(root.extras)) {
    for (const value of root.extras) {
      if (!value || typeof value !== 'object') continue;
      const extra = {
        key: typeof value.key === 'string' ? value.key : '',
        kind: value.kind === 'ruler-only' ? ExtraGeneKind.RulerOnly : ExtraGeneKind.Modded,
        bytes: new Uint8Array(4),
        firstOption: typeof value.firstOption === 'string' ? value.firstOption : '',
        secondOption: typeof value.secondOption === 'string' ? value.secondOption : '',
      };
      const byteValues = Array.isArray(value.bytes) ? value.bytes : [];
      for (let i = 0; i < 4 && i < byteValues.length; i += 1) {
        extra.bytes[i] = clamp(Number(byteValues[i]) || 0, 0, 255);
      }
      if (extra.key) loadedExtras.push(extra);
    }
  }

  let byteExtraIndex = 0;
  for (const saved of loadedExtras) {
    if (saved.kind !== ExtraGeneKind.Modded) continue;
    if (byteExtraIndex < data.extras.length) {
      const target = data.extras[byteExtraIndex];
      byteExtraIndex += 1;
      target.key = saved.key;
      target.kind = ExtraGeneKind.Modded;
      target.firstOption = saved.firstOption;
      target.secondOption = saved.secondOption;
    } else {
      data.extras.push(saved);
    }
  }

  for (const saved of loadedExtras) {
    if (saved.kind === ExtraGeneKind.RulerOnly) data.extras.push(saved);
  }

  data.ruler = metadata;
}

function parseRuler(text, catalog, legacyCatalog) {
  const result = {
    ok: false,
    error: '',
    warnings: [],
    data: createDnaData(),
    format: DnaFormat.Invalid,
    legacyConverted: false,
    geneCount: 0,
  };

  const tokenized = tokenizeRuler(text);
  if (tokenized.error) {
    result.error = tokenized.error;
    return result;
  }
  const tokens = tokenized.tokens;

  let genesToken = -1;
  for (let i = 0; i + 2 < tokens.length; i += 1) {
    if (tokens[i].kind === 'word' && tokens[i].text === 'genes'
        && tokens[i + 1].kind === 'equal' && tokens[i + 2].kind === 'lbrace') {
      genesToken = i;
      break;
    }
  }
  if (genesToken < 0) {
    result.error = 'No genes block was found in the Ruler Designer code.';
    return result;
  }

  const genesOpenToken = genesToken + 2;
  let genesCloseToken = -1;
  let genesDepth = 0;
  for (let tokenIndex = genesOpenToken; tokenIndex < tokens.length; tokenIndex += 1) {
    if (tokens[tokenIndex].kind === 'lbrace') genesDepth += 1;
    else if (tokens[tokenIndex].kind === 'rbrace') {
      genesDepth -= 1;
      if (genesDepth === 0) {
        genesCloseToken = tokenIndex;
        break;
      }
    }
  }

  let outerOpenToken = -1;
  for (let tokenIndex = 0; tokenIndex < genesToken; tokenIndex += 1) {
    if (tokens[tokenIndex].kind === 'lbrace') {
      outerOpenToken = tokenIndex;
      break;
    }
  }

  let outerCloseToken = -1;
  if (outerOpenToken >= 0) {
    let outerDepth = 0;
    for (let tokenIndex = outerOpenToken; tokenIndex < tokens.length; tokenIndex += 1) {
      if (tokens[tokenIndex].kind === 'lbrace') outerDepth += 1;
      else if (tokens[tokenIndex].kind === 'rbrace') {
        outerDepth -= 1;
        if (outerDepth === 0) {
          outerCloseToken = tokenIndex;
          break;
        }
      }
    }
  }

  const data = createDnaData();
  data.ruler.valid = true;
  if (outerOpenToken >= 0) {
    data.ruler.prefix = text.slice(tokens[outerOpenToken].end, tokens[genesToken].start);
  }
  if (genesCloseToken >= 0 && outerCloseToken > genesCloseToken) {
    data.ruler.suffix = text.slice(tokens[genesCloseToken].end, tokens[outerCloseToken].start);
  }

  let recognized = 0;
  let i = genesToken + 3;
  let depth = 1;

  while (i < tokens.length && depth > 0) {
    const token = tokens[i];
    if (token.kind === 'lbrace') {
      depth += 1;
      i += 1;
      continue;
    }
    if (token.kind === 'rbrace') {
      depth -= 1;
      i += 1;
      continue;
    }
    if (depth !== 1 || token.kind !== 'word' || i + 2 >= tokens.length
        || tokens[i + 1].kind !== 'equal' || tokens[i + 2].kind !== 'lbrace') {
      i += 1;
      continue;
    }

    const key = token.text;
    i += 3;

    const values = [];
    let innerDepth = 1;
    while (i < tokens.length && innerDepth > 0) {
      const valueToken = tokens[i];
      if (valueToken.kind === 'lbrace') {
        innerDepth += 1;
        i += 1;
        continue;
      }
      if (valueToken.kind === 'rbrace') {
        innerDepth -= 1;
        i += 1;
        continue;
      }
      if (innerDepth === 1 && (valueToken.kind === 'word' || valueToken.kind === 'string')) {
        values.push(valueToken);
      }
      i += 1;
    }

    if (values.length !== 4) {
      result.warnings.push(`Skipped ${key}: expected 4 values, found ${values.length}.`);
      continue;
    }

    recognized += 1;
    data.ruler.order.push(key);
    data.ruler.rawTokens.set(
      key,
      values.map((value) => text.slice(value.start, value.end)),
    );

    const gene = catalog.byKey(key);
    if (gene && gene.inBase64) {
      if (gene.kind === 'color') {
        const color = [0, 0, 0, 0];
        for (let v = 0; v < 4; v += 1) {
          color[v] = byteValue(values[v], result.warnings, key);
        }
        data.colors.set(key, color);
      } else {
        data.genes.set(key, {
          first: {
            variant: variantValue(values[0], catalog, key, result.warnings),
            intensity: byteValue(values[1], result.warnings, key),
          },
          second: {
            variant: variantValue(values[2], catalog, key, result.warnings),
            intensity: byteValue(values[3], result.warnings, key),
          },
        });
      }
      continue;
    }

    const extra = {
      key,
      kind: isRulerOnlyKey(key, catalog) ? ExtraGeneKind.RulerOnly : ExtraGeneKind.Modded,
      bytes: new Uint8Array(4),
      firstOption: '',
      secondOption: '',
    };

    const firstNumber = numericWord(values[0]);
    if (firstNumber !== null) {
      extra.bytes[0] = clamp(firstNumber, 0, 255);
    } else {
      extra.firstOption = values[0].text;
      extra.bytes[0] = 0;
    }
    extra.bytes[1] = byteValue(values[1], result.warnings, key);

    const secondNumber = numericWord(values[2]);
    if (secondNumber !== null) {
      extra.bytes[2] = clamp(secondNumber, 0, 255);
    } else {
      extra.secondOption = values[2].text;
      extra.bytes[2] = 0;
    }
    extra.bytes[3] = byteValue(values[3], result.warnings, key);
    data.extras.push(extra);
  }

  if (recognized === 0) {
    result.error = 'The Ruler Designer code does not contain any supported genes.';
    return result;
  }

  for (let index = 0; index < catalog.base64GeneCount(); index += 1) {
    const gene = catalog.byBase64Index(index);
    if (!gene) continue;
    if (gene.kind === 'color') {
      if (!data.colors.has(gene.key)) data.colors.set(gene.key, [0, 0, 0, 0]);
    } else if (!data.genes.has(gene.key)) {
      data.genes.set(gene.key, {
        first: { variant: 0, intensity: 0 },
        second: { variant: 0, intensity: 0 },
      });
    }
  }

  const moddedExtras = data.extras.filter((extra) => extra.kind === ExtraGeneKind.Modded).length;
  result.data = data;
  result.format = DnaFormat.RulerDesigner;
  result.geneCount = catalog.base64GeneCount() + moddedExtras;
  result.ok = true;
  return result;
}

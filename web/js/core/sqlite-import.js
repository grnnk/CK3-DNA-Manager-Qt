// Read-only import of the desktop app's SQLite `dna.db` using sql.js.
// The desktop database is never modified by the web app.

import * as Codec from './codec.js';

let sqlPromise = null;

function embeddedWasmBinary() {
  const encoded = globalThis.__CK3DNA_SQL_WASM_BASE64__;
  if (!encoded) return null;
  try {
    const binary = atob(encoded);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
    return bytes;
  } catch {
    return null;
  }
}

export async function loadSqlJs() {
  if (!sqlPromise) {
    sqlPromise = (async () => {
      if (typeof globalThis.initSqlJs !== 'function') {
        const scriptUrl = new URL('vendor/sql.js/sql-wasm.js', document.baseURI);
        await new Promise((resolve, reject) => {
          const script = document.createElement('script');
          script.src = scriptUrl.href;
          script.onload = resolve;
          script.onerror = () => reject(new Error('Could not load the SQLite reader.'));
          document.head.appendChild(script);
        });
      }
      if (typeof globalThis.initSqlJs !== 'function') {
        throw new Error('The SQLite reader did not initialise.');
      }
      const wasmBinary = embeddedWasmBinary();
      const wasmUrl = new URL('vendor/sql.js/sql-wasm.wasm', document.baseURI).href;
      return globalThis.initSqlJs({
        wasmBinary: wasmBinary || undefined,
        locateFile: (file) => (file.endsWith('.wasm') ? wasmUrl : file),
      });
    })();
  }
  return sqlPromise;
}

function toBytes(value) {
  if (value instanceof Uint8Array) return value;
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (Array.isArray(value)) return Uint8Array.from(value);
  if (typeof value === 'string') {
    try {
      const binary = atob(value);
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
      return bytes;
    } catch {
      return null;
    }
  }
  return null;
}

function detectMime(bytes) {
  if (!bytes || bytes.length < 4) return 'image/png';
  if (bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47) return 'image/png';
  if (bytes[0] === 0xff && bytes[1] === 0xd8) return 'image/jpeg';
  if (bytes[0] === 0x47 && bytes[1] === 0x49 && bytes[2] === 0x46) return 'image/gif';
  if (bytes[0] === 0x42 && bytes[1] === 0x4d) return 'image/bmp';
  if (bytes[0] === 0x52 && bytes[1] === 0x49 && bytes[2] === 0x46 && bytes[3] === 0x46) return 'image/webp';
  return 'image/png';
}

function bytesToDataUrl(value) {
  const bytes = toBytes(value);
  if (!bytes || bytes.length === 0) return '';
  return `data:${detectMime(bytes)};base64,${Codec.bytesToBase64(bytes)}`;
}

export async function importSqliteDatabase(buffer, { catalog, legacyCatalog } = {}) {
  const result = {
    ok: false,
    error: '',
    entries: [],
    warnings: [],
    skipped: 0,
  };

  let database;
  try {
    const SQL = await loadSqlJs();
    database = new SQL.Database(new Uint8Array(buffer));
  } catch (error) {
    result.error = error.message || 'The SQLite database could not be opened.';
    return result;
  }

  try {
    const tableCheck = database.exec(
      "SELECT name FROM sqlite_master WHERE type='table' AND name='entries'",
    );
    if (!tableCheck.length) {
      result.error = 'This database does not contain a CK3 DNA Manager entries table.';
      return result;
    }

    const columnsResult = database.exec('PRAGMA table_info(entries)');
    const columns = new Set((columnsResult[0]?.values || []).map((row) => String(row[1])));
    const hasRulerMeta = columns.has('ruler_meta');
    const hasPortrait = columns.has('portrait');
    const hasThumbnail = columns.has('thumbnail');

    const select = [
      'id',
      'name',
      'notes',
      'dna',
      hasPortrait ? 'portrait' : 'NULL AS portrait',
      hasThumbnail ? 'thumbnail' : 'NULL AS thumbnail',
      'favorite',
      'created_at',
      'updated_at',
      hasRulerMeta ? 'ruler_meta' : "'' AS ruler_meta",
    ].join(', ');

    const statement = database.prepare(`SELECT ${select} FROM entries ORDER BY favorite DESC, updated_at DESC`);
    try {
      while (statement.step()) {
        const row = statement.getAsObject();
        const dnaBytes = toBytes(row.dna);
        if (!dnaBytes || dnaBytes.length === 0) {
          result.skipped += 1;
          result.warnings.push(`Skipped "${row.name || 'Untitled'}": empty DNA.`);
          continue;
        }

        let data;
        try {
          const parsed = Codec.fromBytes(dnaBytes, Codec.Base64Hint.Modern, catalog, legacyCatalog);
          if (!parsed.ok) {
            result.skipped += 1;
            result.warnings.push(`Skipped "${row.name || 'Untitled'}": ${parsed.error}`);
            continue;
          }
          data = parsed.data;
          Codec.applyRulerMetadata(data, typeof row.ruler_meta === 'string' ? row.ruler_meta : '');
        } catch (error) {
          result.skipped += 1;
          result.warnings.push(`Skipped "${row.name || 'Untitled'}": ${error.message}`);
          continue;
        }

        const portraitDataUrl = hasPortrait ? bytesToDataUrl(row.portrait) : '';
        const thumbnailDataUrl = hasThumbnail ? bytesToDataUrl(row.thumbnail) : '';
        result.entries.push({
          id: row.id ? String(row.id) : '',
          name: row.name ? String(row.name) : 'Imported DNA',
          notes: row.notes ? String(row.notes) : '',
          dna: Codec.bytesToBase64(Codec.toBytes(data, catalog)),
          rulerMeta: Codec.serializeRulerMetadata(data),
          favorite: Number(row.favorite) !== 0,
          createdAt: Number(row.created_at) || 0,
          updatedAt: Number(row.updated_at) || 0,
          hasPortrait: Boolean(portraitDataUrl),
          portraitDataUrl,
          thumbnailDataUrl,
        });
      }
    } finally {
      statement.free();
    }

    result.ok = true;
    return result;
  } catch (error) {
    result.error = error.message || 'The SQLite database could not be read.';
    return result;
  } finally {
    database?.close();
  }
}

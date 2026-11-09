// JSON export/import and library merge helpers.

import * as Codec from '../core/codec.js';
import * as Images from '../core/images.js';
import { importLibrary } from '../core/legacy-importer.js';
import { importSqliteDatabase } from '../core/sqlite-import.js';
import { buildExport, entryToExport } from '../storage/local-store.js';
import { state } from './state.js';
import { countsForData, dataForEntry, sanitizeFileName, setStatus } from './shared.js';
import { loadEntry, maybeLeaveCurrent } from './detail.js';
import { showError, showInfo } from './dialogs.js';
import { scheduleFolderSync } from './folder.js';
import { refreshEntries, sortedEntries } from './library.js';
import { ensureThumbnail, portraitPngFor } from './portraits.js';

export async function exportEntries(entries, filename) {
  const objects = [];
  for (const entry of entries) {
    const data = dataForEntry(entry);
    const counts = data ? countsForData(data) : { total: 0, modded: 0 };
    const portraitPng = await portraitPngFor(entry);
    objects.push(entryToExport(entry, {
      portraitPng,
      geneCount: counts.total,
      moddedGeneCount: counts.modded,
      rulerDesigner: data ? Codec.toRuler(data, state.catalog) : '',
    }));
  }
  const payload = buildExport(objects);
  Images.downloadText(JSON.stringify(payload, null, 2), `${sanitizeFileName(filename)}.json`);
  setStatus(state.i18n.t('msg.exported'));
}

export async function exportCurrent() {
  if (!state.currentId) return;
  if (!(await maybeLeaveCurrent())) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  await exportEntries([entry], entry.name);
}

export async function exportAll() {
  if (!(await maybeLeaveCurrent())) return;
  if (state.entries.length === 0) {
    await showInfo(state.i18n.t('action.exportAll'), state.i18n.t('library.emptyTitle'));
    return;
  }
  await exportEntries(state.entries, state.i18n.t('library.title'));
}

export function mergeEntriesFromSource(entries) {
  const summary = { imported: 0, updated: 0, skipped: 0 };
  for (const entry of entries) {
    const existing = entry.id ? state.store.get(entry.id) : null;
    if (existing) {
      if ((entry.updatedAt || 0) > (existing.updatedAt || 0)) {
        try {
          state.store.update({
            ...existing,
            name: entry.name,
            notes: entry.notes,
            dna: entry.dna,
            rulerMeta: entry.rulerMeta,
            favorite: entry.favorite,
            updatedAt: entry.updatedAt,
          }, { preserveTimestamp: true });
          if (entry.hasPortrait && entry.portraitDataUrl) {
            try {
              state.store.setPortrait(existing.id, entry.portraitDataUrl, entry.thumbnailDataUrl || entry.portraitDataUrl);
            } catch {
              try {
                const saved = state.store.get(existing.id);
                if (saved) state.store.update({ ...saved, hasPortrait: false });
              } catch {
                // Keep the entry even when its portrait cannot be stored.
              }
            }
          }
          summary.updated += 1;
        } catch {
          summary.skipped += 1;
        }
      } else {
        summary.skipped += 1;
      }
      continue;
    }

    const signature = `${entry.name}\u0000${entry.dna}`;
    if (state.entries.some((candidate) => `${candidate.name}\u0000${candidate.dna}` === signature)) {
      summary.skipped += 1;
      continue;
    }

    try {
      const record = state.store.insert({ ...entry, id: entry.id || undefined });
      if (entry.hasPortrait && entry.portraitDataUrl) {
        try {
          state.store.setPortrait(record.id, entry.portraitDataUrl, entry.thumbnailDataUrl || entry.portraitDataUrl);
        } catch {
          try {
            state.store.update({ ...record, hasPortrait: false });
          } catch {
            // Keep the entry even when its portrait cannot be stored.
          }
        }
      }
      summary.imported += 1;
    } catch {
      summary.skipped += 1;
    }
  }
  state.entries = state.store.entries();
  return summary;
}

export async function buildSyncPayload() {
  const objects = [];
  for (const entry of state.entries) {
    const data = dataForEntry(entry);
    const counts = data ? countsForData(data) : { total: 0, modded: 0 };
    const portraitPng = await portraitPngFor(entry);
    const object = entryToExport(entry, {
      portraitPng,
      geneCount: counts.total,
      moddedGeneCount: counts.modded,
      rulerDesigner: data ? Codec.toRuler(data, state.catalog) : '',
    });
    object.id = entry.id;
    objects.push(object);
  }
  const payload = buildExport(objects);
  payload.webSync = true;
  return payload;
}

export async function importDatabaseFile(file) {
  setStatus('Importing database…', 0);
  try {
    const buffer = await file.arrayBuffer();
    const result = await importSqliteDatabase(buffer, {
      catalog: state.catalog,
      legacyCatalog: state.legacyCatalog,
    });
    if (!result.ok) {
      await showError(state.i18n.t('msg.importTitle'), result.error);
      setStatus(state.i18n.t('status.ready'));
      return;
    }
    for (const entry of result.entries) await ensureThumbnail(entry);
    const summary = mergeEntriesFromSource(result.entries);
    refreshEntries();
    const first = sortedEntries()[0];
    if (first) loadEntry(first.id);
    await showInfo(
      state.i18n.t('msg.importTitle'),
      `Read ${result.entries.length} entries from ${file.name}. Imported ${summary.imported}, updated ${summary.updated}, skipped ${summary.skipped}.`,
      result.warnings,
    );
    scheduleFolderSync();
    setStatus(state.i18n.t('status.ready'));
  } catch (error) {
    await showError(state.i18n.t('msg.importTitle'), error.message || String(error));
    setStatus(state.i18n.t('status.ready'));
  }
}

export async function importFromFile(file) {
  if (!file) return;
  const looksLikeSqlite = /\.(db|sqlite|sqlite3)$/i.test(file.name || '');
  if (looksLikeSqlite) {
    await importDatabaseFile(file);
    return;
  }
  try {
    const header = await file.slice(0, 16).text();
    if (header.startsWith('SQLite format 3')) {
      await importDatabaseFile(file);
      return;
    }
  } catch {
    // Fall through to JSON parsing.
  }
  setStatus('Importing…', 0);
  let root;
  try {
    const text = await file.text();
    root = JSON.parse(text);
  } catch (error) {
    await showError(state.i18n.t('msg.importTitle'), error.message);
    setStatus(state.i18n.t('status.ready'));
    return;
  }

  const result = await importLibrary(root, {
    catalog: state.catalog,
    legacyCatalog: state.legacyCatalog,
    makePortraitFromBase64: async (base64) => {
      const blob = Images.base64ToBlob(base64, 'image/png');
      const image = await Images.loadImage(blob);
      const portraitBlob = await Images.encodePortrait(image, 1600);
      const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
      if (!portraitBlob || !thumbnailBlob) return null;
      return {
        portraitDataUrl: await Images.blobToDataUrl(portraitBlob),
        thumbnailDataUrl: await Images.blobToDataUrl(thumbnailBlob),
      };
    },
  });

  if (!result.ok) {
    await showError(state.i18n.t('msg.importTitle'), result.error);
    setStatus(state.i18n.t('status.ready'));
    return;
  }

  let firstId = null;
  for (const entry of result.entries) {
    try {
      const record = state.store.insert({
        name: entry.name,
        notes: entry.notes,
        dna: entry.dna,
        rulerMeta: entry.rulerMeta,
        favorite: entry.favorite,
        createdAt: entry.createdAt,
        updatedAt: entry.updatedAt,
        hasPortrait: entry.hasPortrait,
      });
      if (!firstId) firstId = record.id;
      if (entry.hasPortrait && entry.portraitDataUrl) {
        try {
          state.store.setPortrait(record.id, entry.portraitDataUrl, entry.thumbnailDataUrl);
        } catch (error) {
          try {
            state.store.update({ ...record, hasPortrait: false });
          } catch {
            // The entry itself is already saved.
          }
          result.warnings.push(`Could not store portrait for "${entry.name}": ${error.message}`);
        }
      }
    } catch (error) {
      result.warnings.push(`Could not import "${entry.name}": ${error.message}`);
    }
  }

  refreshEntries();
  if (firstId) loadEntry(firstId);
  scheduleFolderSync();
  const summary = state.i18n.t('msg.importSummary', result.imported, result.portraits, result.skipped);
  await showInfo(state.i18n.t('msg.importTitle'), summary, result.warnings);
  setStatus(state.i18n.t('status.ready'));
}

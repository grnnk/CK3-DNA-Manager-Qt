// Optional File System Access folder sync.

import * as Folder from '../storage/folder-store.js';
import { importLibrary } from '../core/legacy-importer.js';
import { importSqliteDatabase } from '../core/sqlite-import.js';
import { state } from './state.js';
import { persistSettings, setStatus } from './shared.js';
import { loadEntry } from './detail.js';
import { showError, showInfo } from './dialogs.js';
import { refreshEntries, sortedEntries } from './library.js';
import { ensureThumbnail, portraitFromPngBase64 } from './portraits.js';
import { buildSyncPayload, mergeEntriesFromSource } from './transfer.js';

export async function syncToFolder({ silent = true } = {}) {
  if (!state.folder.connected || !state.folder.handle || state.folder.syncing) return false;
  state.folder.syncing = true;
  try {
    const payload = await buildSyncPayload();
    await Folder.writeSyncLibrary(state.folder.handle, payload);
    state.folder.error = '';
    if (!silent) setStatus('Library synced to the connected folder.');
    return true;
  } catch (error) {
    state.folder.error = error.message || String(error);
    if (!silent) await showError('Folder sync failed', state.folder.error);
    return false;
  } finally {
    state.folder.syncing = false;
  }
}

export function scheduleFolderSync() {
  if (!state.folder.connected) return;
  if (state.folder.syncTimer) clearTimeout(state.folder.syncTimer);
  state.folder.syncTimer = setTimeout(() => {
    state.folder.syncTimer = null;
    void syncToFolder({ silent: true });
  }, 900);
}

export async function pullFolderLibrary(handle) {
  const summary = { imported: 0, updated: 0, skipped: 0, warnings: [], source: 'none', failed: false };
  const root = await Folder.readSyncLibrary(handle);
  if (root) {
    summary.source = 'sync';
    const result = await importLibrary(root, {
      catalog: state.catalog,
      legacyCatalog: state.legacyCatalog,
      makePortraitFromBase64: portraitFromPngBase64,
    });
    if (result.ok) {
      const merged = mergeEntriesFromSource(result.entries);
      summary.imported += merged.imported;
      summary.updated += merged.updated;
      summary.skipped += merged.skipped;
      summary.warnings.push(...result.warnings);
    } else {
      summary.failed = true;
      summary.warnings.push(result.error);
    }
  } else {
    const database = await Folder.findDatabaseFile(handle);
    if (database) {
      summary.source = 'db';
      const buffer = await database.file.arrayBuffer();
      const result = await importSqliteDatabase(buffer, {
        catalog: state.catalog,
        legacyCatalog: state.legacyCatalog,
      });
      if (result.ok) {
        for (const entry of result.entries) await ensureThumbnail(entry);
        const merged = mergeEntriesFromSource(result.entries);
        summary.imported += merged.imported;
        summary.updated += merged.updated;
        summary.skipped += merged.skipped;
        summary.warnings.push(...result.warnings);
      } else {
        summary.failed = true;
        summary.warnings.push(result.error);
      }
    }
  }
  return summary;
}

export async function connectFolder() {
  if (!Folder.isSupported()) {
    await showError(
      'Folder access unavailable',
      'This browser does not support the File System Access API. Use Chrome, Edge or another Chromium-based browser, or import/export JSON files instead.',
    );
    return;
  }
  try {
    const handle = await window.showDirectoryPicker({ id: 'ck3dna-library', mode: 'readwrite' });
    const permission = await Folder.requestPermission(handle, 'readwrite');
    if (permission !== 'granted') {
      await showError('Folder permission denied', 'The browser did not grant write access to the selected folder.');
      return;
    }
    await Folder.saveDirectoryHandle(handle);
    state.folder.handle = handle;
    state.folder.name = handle.name;
    state.folder.connected = true;
    state.folder.needsPermission = false;
    state.folder.error = '';
    state.startupSource = 'folder';
    persistSettings({ startupSource: 'folder' });

    setStatus('Reading folder library…', 0);
    const summary = await pullFolderLibrary(handle);
    const synced = summary.failed ? false : await syncToFolder({ silent: true });
    if (!summary.failed && !synced && state.folder.error) summary.warnings.push(state.folder.error);
    refreshEntries();
    if (state.currentId && state.store.get(state.currentId)) loadEntry(state.currentId);
    else {
      const first = sortedEntries()[0];
      if (first) loadEntry(first.id);
    }
    await showInfo(
      'Folder connected',
      `Connected to "${handle.name}". Imported ${summary.imported}, updated ${summary.updated}, skipped ${summary.skipped}. ${summary.failed
        ? 'The existing library file could not be read, so it was left untouched.'
        : (synced ? 'Changes will be written to web-sync/library.json.' : 'The folder was connected, but the sync file could not be written.')}`,
      summary.warnings,
    );
    setStatus('Folder connected.');
  } catch (error) {
    if (error.name === 'AbortError') return;
    await showError('Could not connect the folder', error.message || String(error));
  }
}

export async function reconnectFolder() {
  if (!state.folder.handle) return;
  const permission = await Folder.requestPermission(state.folder.handle, 'readwrite');
  if (permission !== 'granted') {
    state.folder.needsPermission = true;
    await showError('Folder permission denied', 'The browser did not grant access to the remembered folder.');
    return;
  }
  state.folder.connected = true;
  state.folder.needsPermission = false;
  const summary = await pullFolderLibrary(state.folder.handle);
  const synced = summary.failed ? false : await syncToFolder({ silent: true });
  if (!summary.failed && !synced && state.folder.error) summary.warnings.push(state.folder.error);
  refreshEntries();
  if (state.currentId && state.store.get(state.currentId)) loadEntry(state.currentId);
  else {
    const first = sortedEntries()[0];
    if (first) loadEntry(first.id);
  }
  await showInfo(
    'Folder reconnected',
    `Imported ${summary.imported}, updated ${summary.updated}, skipped ${summary.skipped}.${summary.failed
      ? ' The existing library file could not be read, so it was left untouched.'
      : (synced ? '' : ' The sync file could not be written.')}`,
    summary.warnings,
  );
}

export async function disconnectFolder() {
  if (state.folder.syncTimer) clearTimeout(state.folder.syncTimer);
  try {
    await Folder.clearDirectoryHandle();
  } catch {
    // The handle is also cleared from memory below.
  }
  state.folder = {
    handle: null,
    name: '',
    connected: false,
    needsPermission: false,
    syncTimer: null,
    syncing: false,
    error: '',
  };
  state.startupSource = 'local';
  persistSettings({ startupSource: 'local' });
  setStatus('Folder disconnected.');
}

export async function restoreFolder() {
  if (!Folder.isSupported()) return;
  try {
    const handle = await Folder.loadDirectoryHandle();
    if (!handle) return;
    state.folder.handle = handle;
    state.folder.name = handle.name;
    const permission = await Folder.queryPermission(handle, 'readwrite');
    if (permission !== 'granted') {
      state.folder.needsPermission = true;
      return;
    }
    state.folder.connected = true;
    state.folder.needsPermission = false;
    if (state.startupSource === 'folder') {
      const summary = await pullFolderLibrary(handle);
      if (summary.imported || summary.updated) refreshEntries();
      if (!summary.failed) await syncToFolder({ silent: true });
    }
  } catch (error) {
    state.folder.error = error.message || String(error);
  }
}

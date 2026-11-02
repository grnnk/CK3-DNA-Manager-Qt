import { GeneCatalog } from '../core/catalog.js';
import * as Codec from '../core/codec.js';
import { I18n } from '../core/i18n.js';
import * as Images from '../core/images.js';
import { importLibrary } from '../core/legacy-importer.js';
import { importSqliteDatabase } from '../core/sqlite-import.js';
import * as Folder from '../storage/folder-store.js';
import { LocalStore, buildExport, entryToExport } from '../storage/local-store.js';

const $ = (id) => document.getElementById(id);

const state = {
  catalog: null,
  legacyCatalog: null,
  i18n: null,
  store: null,
  entries: [],
  currentId: null,
  currentData: null,
  baselineData: null,
  detailsDirty: false,
  geneDirty: false,
  sort: 'updated',
  search: '',
  geneSearch: '',
  geneCategory: '',
  activeTab: 'overview',
  codeTab: 'base64',
  collapsedSections: new Set(),
  statusTimer: null,
  add: null,
  menuOpen: false,
  startupSource: 'local',
  folder: {
    handle: null,
    name: '',
    connected: false,
    needsPermission: false,
    syncTimer: null,
    syncing: false,
    error: '',
  },
};

function nameCollator() {
  return new Intl.Collator(state.i18n?.locale() || undefined, { sensitivity: 'base', numeric: true });
}

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function escapeAttr(value) {
  return escapeHtml(value);
}

function formatDate(ms) {
  if (!ms) return '—';
  try {
    return new Intl.DateTimeFormat(state.i18n.locale(), {
      dateStyle: 'short',
      timeStyle: 'short',
    }).format(new Date(ms));
  } catch {
    return new Date(ms).toLocaleString();
  }
}

function formatBytes(bytes) {
  if (!bytes) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function geneCountForEntry(entry) {
  try {
    return Math.floor(atob(entry.dna).length / 4);
  } catch {
    return 0;
  }
}

function moddedCountForEntry(entry) {
  return Math.max(0, geneCountForEntry(entry) - 104);
}

function dataForEntry(entry) {
  try {
    const bytes = Codec.base64ToBytes(entry.dna);
    const parsed = Codec.fromBytes(bytes, Codec.Base64Hint.Modern, state.catalog, state.legacyCatalog);
    if (!parsed.ok) return null;
    Codec.applyRulerMetadata(parsed.data, entry.rulerMeta);
    return parsed.data;
  } catch {
    return null;
  }
}

function countsForData(data) {
  const modded = data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.Modded).length;
  const rulerOnly = data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.RulerOnly).length;
  return {
    total: state.catalog.base64GeneCount() + modded,
    modded,
    rulerOnly,
  };
}

function categoryLabel(category) {
  const key = GeneCatalog.categoryTranslationKey(category);
  const value = state.i18n.value(key);
  return value === key ? category : value;
}

function subtitleFor(entry) {
  const firstLine = (entry.notes || '').split(/\r?\n/).find((line) => line.trim());
  if (firstLine) return firstLine.trim();
  const count = geneCountForEntry(entry);
  const modded = moddedCountForEntry(entry);
  return `${state.i18n.t('detail.genesCount', count)}${modded ? ` · ${state.i18n.t('detail.moddedCount', modded)}` : ''}`;
}

function setStatus(text, timeout = 4000) {
  const element = $('status-text');
  element.textContent = text;
  if (state.statusTimer) clearTimeout(state.statusTimer);
  if (timeout > 0) {
    state.statusTimer = setTimeout(() => {
      element.textContent = state.i18n.t('status.ready');
    }, timeout);
  }
}

function showWelcome() {
  state.currentId = null;
  state.currentData = null;
  state.baselineData = null;
  state.detailsDirty = false;
  state.geneDirty = false;
  $('welcome').hidden = false;
  $('content').hidden = true;
  renderList();
  updateActionStates();
}

function sortedEntries() {
  const filtered = state.entries.filter((entry) => {
    if (!state.search) return true;
    const needle = state.search.toLowerCase();
    return `${entry.name}\n${entry.notes}`.toLowerCase().includes(needle);
  });

  const collator = nameCollator();
  return filtered.sort((a, b) => {
    if (a.favorite !== b.favorite) return a.favorite ? -1 : 1;
    let result = 0;
    if (state.sort === 'created') result = (b.createdAt || 0) - (a.createdAt || 0);
    else if (state.sort === 'name') result = collator.compare(a.name || '', b.name || '');
    else result = (b.updatedAt || 0) - (a.updatedAt || 0);
    if (result !== 0) return result;
    return String(a.id).localeCompare(String(b.id));
  });
}

function renderList() {
  const visible = sortedEntries();
  const total = state.entries.length;
  $('entry-count').textContent = state.search
    ? state.i18n.t('library.filteredCount', visible.length, total)
    : state.i18n.t('library.count', total);

  const list = $('entry-list');
  const empty = $('list-empty');

  if (total === 0) {
    list.innerHTML = '';
    empty.hidden = false;
    $('list-empty-title').textContent = state.i18n.t('library.emptyTitle');
    $('list-empty-hint').textContent = state.i18n.t('library.emptyHint');
    return;
  }

  if (visible.length === 0) {
    list.innerHTML = '';
    empty.hidden = false;
    $('list-empty-title').textContent = state.i18n.t('library.noMatches');
    $('list-empty-hint').textContent = state.i18n.t('library.noMatchesHint');
    return;
  }

  empty.hidden = true;
  list.innerHTML = visible.map((entry) => {
    const thumb = entry.hasPortrait
      ? (state.store.getThumbnail(entry.id) || state.store.getPortrait(entry.id))
      : null;
    const selected = entry.id === state.currentId ? ' selected' : '';
    const star = entry.favorite ? '<span class="entry-star">★</span>' : '';
    const thumbHtml = thumb
      ? `<img src="${escapeAttr(thumb)}" alt="">`
      : '<span>DNA</span>';
    return `
      <div class="entry-item${selected}" data-id="${escapeAttr(entry.id)}" role="option" aria-selected="${entry.id === state.currentId}">
        <div class="entry-thumb">${thumbHtml}</div>
        <div class="entry-main">
          <div class="entry-name">${escapeHtml(entry.name)}</div>
          <div class="entry-subtitle">${escapeHtml(subtitleFor(entry))}</div>
          <div class="entry-date">${escapeHtml(formatDate(entry.updatedAt))}</div>
        </div>
        ${star}
      </div>
    `;
  }).join('');
}

function refreshEntries() {
  state.entries = state.store.entries();
  renderList();
  updateActionStates();
}

function renderHeader() {
  const entry = state.currentId ? state.store.get(state.currentId) : null;
  if (!entry) return;
  $('detail-title').textContent = entry.name;
  $('favorite').textContent = entry.favorite ? '★' : '☆';
  $('favorite').classList.toggle('checked', entry.favorite);
  $('favorite').title = state.i18n.t(entry.favorite ? 'detail.unfavorite' : 'detail.favorite');
  document.title = `${state.i18n.t('app.title')} — ${entry.name}`;
}

function renderMeta() {
  const entry = state.currentId ? state.store.get(state.currentId) : null;
  if (!entry || !state.currentData) {
    $('meta').textContent = '';
    return;
  }
  const counts = countsForData(state.currentData);
  const parts = [
    `${state.i18n.t('detail.created')}: ${formatDate(entry.createdAt)}`,
    `${state.i18n.t('detail.updated')}: ${formatDate(entry.updatedAt)}`,
    state.i18n.t('detail.genesCount', counts.total),
  ];
  if (counts.modded) parts.push(state.i18n.t('detail.moddedCount', counts.modded));
  if (counts.rulerOnly) parts.push(state.i18n.t('detail.rulerOnlyCount', counts.rulerOnly));
  $('meta').textContent = parts.join('    ·    ');
}

function renderPortrait() {
  const entry = state.currentId ? state.store.get(state.currentId) : null;
  const image = $('portrait-image');
  const placeholder = $('portrait-placeholder');
  const url = entry?.hasPortrait ? state.store.getPortrait(entry.id) : null;

  if (url) {
    image.src = url;
    image.hidden = false;
    placeholder.hidden = true;
  } else {
    image.removeAttribute('src');
    image.hidden = true;
    placeholder.hidden = false;
  }

  $('change-portrait').disabled = !entry;
  $('paste-portrait').disabled = !entry;
  $('remove-portrait').disabled = !entry || !entry.hasPortrait;
  $('paste-portrait').title = state.i18n.t('action.pasteImageHint');
}

function renderCodes() {
  if (!state.currentData) {
    $('base64-code').value = '';
    $('ruler-code').value = '';
    return;
  }
  $('base64-code').value = Codec.toBase64(state.currentData, state.catalog);
  $('ruler-code').value = Codec.toRuler(state.currentData, state.catalog);
  $('base64-code').hidden = state.codeTab !== 'base64';
  $('ruler-code').hidden = state.codeTab !== 'ruler';
  document.querySelectorAll('.code-tab').forEach((tab) => {
    tab.classList.toggle('active', tab.dataset.codeTab === state.codeTab);
  });

  const hasExtras = state.currentData.extras.length > 0;
  $('base64-code').title = hasExtras ? state.i18n.t('genes.moddedHint') : '';
}

function renderTabs() {
  $('tab-overview').classList.toggle('active', state.activeTab === 'overview');
  $('tab-genes').classList.toggle('active', state.activeTab === 'genes');
  $('tab-overview').setAttribute('aria-selected', String(state.activeTab === 'overview'));
  $('tab-genes').setAttribute('aria-selected', String(state.activeTab === 'genes'));
  $('overview-panel').hidden = state.activeTab !== 'overview';
  $('genes-panel').hidden = state.activeTab !== 'genes';
}

function updateActionStates() {
  const hasCurrent = Boolean(state.currentId);
  const entry = hasCurrent ? state.store.get(state.currentId) : null;

  for (const id of ['edit-genes', 'copy-base64', 'copy-ruler', 'duplicate-entry', 'export-entry', 'delete-entry']) {
    const element = $(id);
    if (element) element.disabled = !hasCurrent;
  }
  $('save-details').disabled = !hasCurrent || (!state.detailsDirty && !state.geneDirty);
  $('change-portrait').disabled = !hasCurrent;
  $('paste-portrait').disabled = !hasCurrent;
  $('remove-portrait').disabled = !hasCurrent || !entry?.hasPortrait;

  const requiresCurrent = new Set([
    'export', 'save', 'copy-base64', 'copy-ruler', 'duplicate', 'delete',
  ]);
  document.querySelectorAll('[data-action]').forEach((item) => {
    const action = item.dataset.action;
    if (requiresCurrent.has(action)) item.disabled = !hasCurrent;
  });
}

function updateDirtyUi() {
  const label = $('dirty-label');
  label.textContent = state.i18n.t(state.geneDirty ? 'genes.unsaved' : 'genes.noChanges');
  label.classList.toggle('is-dirty', state.geneDirty);
  $('save-genes').disabled = !state.geneDirty;
  $('revert-genes').disabled = !state.geneDirty;
  $('save-details').disabled = !state.currentId || (!state.detailsDirty && !state.geneDirty);
}

function renderDetail() {
  const entry = state.currentId ? state.store.get(state.currentId) : null;
  if (!entry) {
    showWelcome();
    return;
  }
  $('welcome').hidden = true;
  $('content').hidden = false;
  $('name').value = entry.name;
  $('notes').value = entry.notes || '';
  renderHeader();
  renderMeta();
  renderPortrait();
  renderCodes();
  renderTabs();
  renderGeneEditor();
  updateDirtyUi();
  updateActionStates();
}

function loadEntry(id) {
  const entry = state.store.get(id);
  if (!entry) {
    showWelcome();
    return;
  }

  let data = null;
  let parseError = '';
  try {
    const bytes = Codec.base64ToBytes(entry.dna);
    const parsed = Codec.fromBytes(bytes, Codec.Base64Hint.Modern, state.catalog, state.legacyCatalog);
    if (parsed.ok) {
      data = parsed.data;
      Codec.applyRulerMetadata(data, entry.rulerMeta);
    } else {
      parseError = parsed.error;
    }
  } catch (error) {
    parseError = error.message;
  }

  state.currentId = id;
  state.currentData = data || Codec.createDnaData();
  state.baselineData = Codec.cloneDnaData(state.currentData);
  state.detailsDirty = false;
  state.geneDirty = false;
  state.activeTab = 'overview';
  state.codeTab = 'base64';
  renderDetail();
  renderList();
  if (parseError) setStatus(`${state.i18n.t('msg.parseError')}: ${parseError}`, 7000);
}

async function maybeLeaveCurrent() {
  if (!state.currentId) return true;
  if (!state.geneDirty) {
    if (state.detailsDirty) {
      if (!saveEntry(false)) return false;
    }
    return true;
  }

  const choice = await showUnsavedDialog();
  if (choice === 'cancel') return false;
  if (choice === 'save' && !saveEntry(false)) return false;
  return true;
}

async function selectEntry(id) {
  if (id === state.currentId) return;
  if (!(await maybeLeaveCurrent())) {
    renderList();
    return;
  }
  loadEntry(id);
}

function saveEntry(showMessage = true) {
  if (!state.currentId) return false;
  const entry = state.store.get(state.currentId);
  if (!entry) return false;

  const name = $('name').value.trim() || state.i18n.t('add.nameFallback');
  const notes = $('notes').value;
  const dna = Codec.bytesToBase64(Codec.toBytes(state.currentData, state.catalog));
  const rulerMeta = Codec.serializeRulerMetadata(state.currentData);

  try {
    state.store.update({
      ...entry,
      name,
      notes,
      dna,
      rulerMeta,
    });
  } catch (error) {
    void showError(state.i18n.t('msg.databaseError'), error.message);
    return false;
  }

  state.baselineData = Codec.cloneDnaData(state.currentData);
  state.detailsDirty = false;
  state.geneDirty = false;
  refreshEntries();
  renderHeader();
  renderMeta();
  renderCodes();
  updateDirtyUi();
  updateActionStates();
  scheduleFolderSync();
  if (showMessage) setStatus(state.i18n.t('msg.saved'));
  return true;
}

async function toggleFavorite() {
  if (!state.currentId) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  try {
    state.store.update({ ...entry, favorite: !entry.favorite });
  } catch (error) {
    await showError(state.i18n.t('msg.databaseError'), error.message);
    return;
  }
  refreshEntries();
  renderHeader();
  renderMeta();
  renderList();
  scheduleFolderSync();
}

async function duplicateCurrent() {
  if (!state.currentId) return;
  if (!(await maybeLeaveCurrent())) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  const portrait = entry.hasPortrait ? state.store.getPortrait(entry.id) : '';
  const thumbnail = entry.hasPortrait ? state.store.getThumbnail(entry.id) : '';
  const name = `${entry.name} (${state.i18n.t('msg.duplicateSuffix')})`;
  let record;
  try {
    record = state.store.insert({
      name,
      notes: entry.notes,
      dna: entry.dna,
      rulerMeta: entry.rulerMeta,
      favorite: entry.favorite,
      createdAt: Date.now(),
      updatedAt: Date.now(),
      hasPortrait: Boolean(portrait),
    });
  } catch (error) {
    await showError(state.i18n.t('msg.databaseError'), error.message);
    return;
  }
  if (portrait) {
    try {
      state.store.setPortrait(record.id, portrait, thumbnail || portrait);
    } catch (error) {
      try {
        state.store.update({ ...record, hasPortrait: false });
      } catch {
        // The entry itself is already saved.
      }
      await showError(state.i18n.t('msg.databaseError'), error.message);
    }
  }
  refreshEntries();
  loadEntry(record.id);
  scheduleFolderSync();
  setStatus(state.i18n.t('msg.saved'));
}

async function deleteCurrent() {
  if (!state.currentId) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  const confirmed = await showConfirm({
    title: state.i18n.t('msg.deleteTitle'),
    text: state.i18n.t('msg.deleteText', entry.name),
    confirmLabel: state.i18n.t('msg.yes'),
    cancelLabel: state.i18n.t('msg.no'),
    danger: true,
  });
  if (!confirmed) return;
  try {
    state.store.remove(entry.id);
  } catch (error) {
    await showError(state.i18n.t('msg.databaseError'), error.message);
    return;
  }
  state.currentId = null;
  state.currentData = null;
  state.detailsDirty = false;
  state.geneDirty = false;
  refreshEntries();
  showWelcome();
  scheduleFolderSync();
  setStatus(state.i18n.t('msg.deleted'));
}

function sanitizeFileName(name) {
  const value = (name || '').replace(/[\\/:*?"<>|]/g, '_').replace(/\s+/g, ' ').trim();
  return value || 'dna';
}

async function portraitPngFor(entry) {
  if (!entry.hasPortrait) return '';
  const dataUrl = state.store.getPortrait(entry.id);
  if (!dataUrl) return '';
  try {
    return await Images.dataUrlToPngBase64(dataUrl);
  } catch {
    return '';
  }
}

async function exportEntries(entries, filename) {
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

async function exportCurrent() {
  if (!state.currentId) return;
  if (!(await maybeLeaveCurrent())) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  await exportEntries([entry], entry.name);
}

async function exportAll() {
  if (!(await maybeLeaveCurrent())) return;
  if (state.entries.length === 0) {
    await showInfo(state.i18n.t('action.exportAll'), state.i18n.t('library.emptyTitle'));
    return;
  }
  await exportEntries(state.entries, state.i18n.t('library.title'));
}

async function portraitFromPngBase64(base64) {
  const blob = Images.base64ToBlob(base64, 'image/png');
  const image = await Images.loadImage(blob);
  const portraitBlob = await Images.encodePortrait(image, 1600);
  const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
  if (!portraitBlob || !thumbnailBlob) return null;
  return {
    portraitDataUrl: await Images.blobToDataUrl(portraitBlob),
    thumbnailDataUrl: await Images.blobToDataUrl(thumbnailBlob),
  };
}

async function ensureThumbnail(entry) {
  if (!entry.hasPortrait || entry.thumbnailDataUrl || !entry.portraitDataUrl) return entry;
  try {
    const image = await Images.loadImage(entry.portraitDataUrl);
    const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
    if (thumbnailBlob) entry.thumbnailDataUrl = await Images.blobToDataUrl(thumbnailBlob);
  } catch {
    entry.thumbnailDataUrl = entry.portraitDataUrl;
  }
  return entry;
}

function persistSettings(patch) {
  try {
    const settings = state.store.getSettings();
    state.store.saveSettings({ ...settings, ...patch });
  } catch {
    // Preferences are best-effort; the session still works.
  }
}

function mergeEntriesFromSource(entries) {
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

async function buildSyncPayload() {
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

async function syncToFolder({ silent = true } = {}) {
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

function scheduleFolderSync() {
  if (!state.folder.connected) return;
  if (state.folder.syncTimer) clearTimeout(state.folder.syncTimer);
  state.folder.syncTimer = setTimeout(() => {
    state.folder.syncTimer = null;
    void syncToFolder({ silent: true });
  }, 900);
}

async function pullFolderLibrary(handle) {
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

async function connectFolder() {
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

async function reconnectFolder() {
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

async function disconnectFolder() {
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

async function restoreFolder() {
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

async function importDatabaseFile(file) {
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

async function copyText(text, messageKey) {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      document.execCommand('copy');
      area.remove();
    }
    setStatus(state.i18n.t(messageKey));
  } catch {
    await showError(state.i18n.t('msg.error'), 'The clipboard could not be written.');
  }
}

async function copyBase64() {
  if (!state.currentData) return;
  await copyText(Codec.toBase64(state.currentData, state.catalog), 'msg.copiedBase64');
}

async function copyRuler() {
  if (!state.currentData) return;
  await copyText(Codec.toRuler(state.currentData, state.catalog), 'msg.copiedRuler');
}

async function copyCodeTab() {
  if (state.codeTab === 'base64') await copyBase64();
  else await copyRuler();
}

async function setPortraitFromBlob(blob) {
  if (!state.currentId) {
    await openAddDialog(blob);
    return;
  }
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  try {
    const image = await Images.loadImage(blob);
    const portraitBlob = await Images.encodePortrait(image, 1600);
    const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
    if (!portraitBlob || !thumbnailBlob) throw new Error('The image could not be encoded.');
    const portraitDataUrl = await Images.blobToDataUrl(portraitBlob);
    const thumbnailDataUrl = await Images.blobToDataUrl(thumbnailBlob);
    state.store.setPortrait(entry.id, portraitDataUrl, thumbnailDataUrl);
    state.store.update({ ...entry, hasPortrait: true });
    refreshEntries();
    renderPortrait();
    renderMeta();
    scheduleFolderSync();
    setStatus(state.i18n.t('msg.portraitUpdated'));
  } catch (error) {
    await showError(state.i18n.t('msg.imageError', error.message), error.message);
  }
}

async function removePortrait() {
  if (!state.currentId) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  try {
    state.store.removePortrait(entry.id);
    state.store.update({ ...entry, hasPortrait: false });
  } catch (error) {
    await showError(state.i18n.t('msg.databaseError'), error.message);
    return;
  }
  refreshEntries();
  renderPortrait();
  renderMeta();
  scheduleFolderSync();
  setStatus(state.i18n.t('msg.portraitRemoved'));
}

async function pastePortrait() {
  const blob = await Images.readClipboardImage();
  if (!blob) {
    setStatus(state.i18n.t('action.pasteImageHint'), 5000);
    return;
  }
  await setPortraitFromBlob(blob);
}

function renderGeneCategories() {
  const select = $('gene-category');
  const current = state.geneCategory;
  const categories = state.catalog.categoriesInUse().filter((category) =>
    state.catalog.genes.some((gene) => gene.inBase64 && gene.category === category));
  select.innerHTML = [
    `<option value="">${escapeHtml(state.i18n.t('genes.allCategories'))}</option>`,
    ...categories.map((category) =>
      `<option value="${escapeAttr(category)}">${escapeHtml(categoryLabel(category))}</option>`),
  ].join('');
  select.value = categories.includes(current) ? current : '';
  state.geneCategory = select.value;
}

function optionHtml(gene, selected) {
  const options = gene.options.map((option) =>
    `<option value="${option.index}"${option.index === selected ? ' selected' : ''}>${escapeHtml(option.label)}</option>`);
  if (!gene.options.some((option) => option.index === selected)) {
    options.unshift(`<option value="${selected}" selected>${escapeHtml(state.i18n.t('genes.unknownOption', selected))}</option>`);
  }
  return options.join('');
}

function geneRowHtml(gene) {
  const label = `
    <div class="gene-label">
      <div class="gene-name">${escapeHtml(gene.name)}</div>
      <div class="gene-key">${escapeHtml(gene.key)}</div>
    </div>`;

  if (gene.kind === 'color') {
    const values = state.currentData.colors.get(gene.key) || [0, 0, 0, 0];
    return `
      <div class="gene-row" data-gene-key="${escapeAttr(gene.key)}">
        ${label}
        <span class="allele-tag">${escapeHtml(state.i18n.t('genes.colorValues'))}</span>
        <div class="color-values">
          ${values.map((value, index) => `
            <input class="input number" type="number" min="0" max="255" value="${value}"
              data-gene-key="${escapeAttr(gene.key)}" data-color-index="${index}"
              aria-label="${escapeAttr(`${gene.name} ${index + 1}`)}">
          `).join('')}
        </div>
      </div>`;
  }

  const pair = state.currentData.genes.get(gene.key) || {
    first: { variant: 0, intensity: 0 },
    second: { variant: 0, intensity: 0 },
  };
  return `
    <div class="gene-row" data-gene-key="${escapeAttr(gene.key)}">
      ${label}
      <span class="allele-tag">A1</span>
      <select class="input" data-gene-key="${escapeAttr(gene.key)}" data-side="first" aria-label="${escapeAttr(`${gene.name} A1`)}">
        ${optionHtml(gene, pair.first.variant)}
      </select>
      <input class="input number" type="number" min="0" max="255" value="${pair.first.intensity}"
        data-gene-key="${escapeAttr(gene.key)}" data-side="first" data-field="intensity"
        aria-label="${escapeAttr(`${gene.name} A1 strength`)}">
      <span class="allele-tag">A2</span>
      <select class="input" data-gene-key="${escapeAttr(gene.key)}" data-side="second" aria-label="${escapeAttr(`${gene.name} A2`)}">
        ${optionHtml(gene, pair.second.variant)}
      </select>
      <input class="input number" type="number" min="0" max="255" value="${pair.second.intensity}"
        data-gene-key="${escapeAttr(gene.key)}" data-side="second" data-field="intensity"
        aria-label="${escapeAttr(`${gene.name} A2 strength`)}">
    </div>`;
}

function extraDisplayName(extra) {
  if (extra.key.startsWith('extra_')) {
    const slot = Number.parseInt(extra.key.slice('extra_'.length), 10);
    if (Number.isFinite(slot)) return state.i18n.t('genes.extraGene', slot);
  }
  return extra.key;
}

function extraRowHtml(extra, index) {
  const kind = extra.kind === Codec.ExtraGeneKind.RulerOnly
    ? state.i18n.t('genes.rulerOnlyKind')
    : state.i18n.t('genes.moddedKind');
  const firstVariant = extra.firstOption
    ? `<div class="extra-option" title="${escapeAttr(extra.firstOption)}">${escapeHtml(extra.firstOption)}</div>`
    : `<input class="input number" type="number" min="0" max="255" value="${extra.bytes[0]}"
        data-extra-index="${index}" data-side="first" data-field="variant">`;
  const secondVariant = extra.secondOption
    ? `<div class="extra-option" title="${escapeAttr(extra.secondOption)}">${escapeHtml(extra.secondOption)}</div>`
    : `<input class="input number" type="number" min="0" max="255" value="${extra.bytes[2]}"
        data-extra-index="${index}" data-side="second" data-field="variant">`;

  return `
    <div class="gene-row" data-extra-index="${index}">
      <div class="gene-label">
        <div class="gene-name">${escapeHtml(extraDisplayName(extra))}</div>
        <div class="gene-key">${escapeHtml(kind)}</div>
      </div>
      <span class="allele-tag">A1</span>
      ${firstVariant}
      <input class="input number" type="number" min="0" max="255" value="${extra.bytes[1]}"
        data-extra-index="${index}" data-side="first" data-field="strength">
      <span class="allele-tag">A2</span>
      ${secondVariant}
      <input class="input number" type="number" min="0" max="255" value="${extra.bytes[3]}"
        data-extra-index="${index}" data-side="second" data-field="strength">
    </div>`;
}

function renderGeneEditor() {
  const content = $('gene-content');
  if (!state.currentData) {
    content.innerHTML = '';
    $('gene-count').textContent = '';
    updateDirtyUi();
    return;
  }

  const query = state.geneSearch.trim().toLowerCase();
  const categoryFilter = state.geneCategory;
  let shown = 0;
  const total = state.catalog.base64GeneCount() + state.currentData.extras.length;
  const sections = [];

  for (const category of state.catalog.categoryOrder) {
    if (categoryFilter && category !== categoryFilter) continue;
    const genes = state.catalog.genes.filter((gene) => gene.inBase64 && gene.category === category
      && (!query || gene.name.toLowerCase().includes(query) || gene.key.toLowerCase().includes(query)));
    if (genes.length === 0) continue;
    shown += genes.length;
    const collapsed = state.collapsedSections.has(category);
    sections.push(`
      <div class="gene-section">
        <button class="section-header" data-section="${escapeAttr(category)}">
          <span class="section-arrow">${collapsed ? '▸' : '▾'}</span>
          <span>${escapeHtml(categoryLabel(category))} · ${genes.length}</span>
        </button>
        <div class="section-body"${collapsed ? ' hidden' : ''}>
          ${genes.map(geneRowHtml).join('')}
        </div>
      </div>`);
  }

  if (!categoryFilter && state.currentData.extras.length > 0) {
    const extras = state.currentData.extras.filter((extra) => {
      if (!query) return true;
      return extra.key.toLowerCase().includes(query)
        || extraDisplayName(extra).toLowerCase().includes(query);
    });
    if (extras.length > 0) {
      shown += extras.length;
      const collapsed = state.collapsedSections.has('__extras');
      sections.push(`
        <div class="gene-section">
          <button class="section-header" data-section="__extras">
            <span class="section-arrow">${collapsed ? '▸' : '▾'}</span>
            <span>${escapeHtml(state.i18n.t('genes.nonVanilla'))} · ${extras.length}</span>
          </button>
          <div class="section-body"${collapsed ? ' hidden' : ''}>
            <div class="extra-hint">${escapeHtml(state.i18n.t('genes.moddedHint'))}</div>
            ${extras.map((extra) => {
              const index = state.currentData.extras.indexOf(extra);
              return extraRowHtml(extra, index);
            }).join('')}
          </div>
        </div>`);
    }
  }

  content.innerHTML = sections.length
    ? sections.join('')
    : `<div class="no-matches">${escapeHtml(state.i18n.t('genes.noMatches'))}</div>`;

  $('gene-count').textContent = query || categoryFilter
    ? state.i18n.t('genes.countFiltered', shown, total)
    : state.i18n.t('genes.count', total);

  updateDirtyUi();
}

function markGeneDirty() {
  if (!state.currentId) return;
  state.geneDirty = true;
  updateDirtyUi();
}

function clearRawTokens(key) {
  if (state.currentData) state.currentData.ruler.rawTokens.delete(key);
}

function handleGeneChange(event) {
  if (!state.currentData) return;
  const target = event.target;
  if (!(target instanceof HTMLElement)) return;

  if (target.dataset.extraIndex !== undefined) {
    const index = Number(target.dataset.extraIndex);
    const extra = state.currentData.extras[index];
    if (!extra) return;
    const side = target.dataset.side;
    const field = target.dataset.field;
    const value = Math.max(0, Math.min(255, Number(target.value) || 0));

    if (field === 'variant') {
      extra.bytes[side === 'first' ? 0 : 2] = value;
      if (side === 'first') extra.firstOption = '';
      else extra.secondOption = '';
      clearRawTokens(extra.key);
      markGeneDirty();
      return;
    }

    if (field === 'strength') {
      extra.bytes[side === 'first' ? 1 : 3] = value;
      clearRawTokens(extra.key);
      markGeneDirty();
      return;
    }
    return;
  }

  const key = target.dataset.geneKey;
  if (!key) return;

  if (target.dataset.colorIndex !== undefined) {
    const color = state.currentData.colors.get(key) || [0, 0, 0, 0];
    const index = Number(target.dataset.colorIndex);
    color[index] = Math.max(0, Math.min(255, Number(target.value) || 0));
    state.currentData.colors.set(key, color);
    clearRawTokens(key);
    markGeneDirty();
    return;
  }

  const pair = state.currentData.genes.get(key);
  if (!pair) return;
  const side = target.dataset.side === 'second' ? pair.second : pair.first;
  if (target.dataset.field === 'intensity') {
    side.intensity = Math.max(0, Math.min(255, Number(target.value) || 0));
  } else {
    side.variant = Number(target.value) || 0;
  }
  clearRawTokens(key);
  markGeneDirty();
}

function revertGenes() {
  if (!state.baselineData) return;
  state.currentData = Codec.cloneDnaData(state.baselineData);
  state.geneDirty = false;
  renderGeneEditor();
  renderCodes();
  renderMeta();
  updateDirtyUi();
}

function saveGenes() {
  saveEntry(true);
}

function buildMenus() {
  const bar = $('menubar');
  bar.innerHTML = '';
  const languages = state.i18n.availableLanguages();

  const menus = [
    {
      label: 'menu.file',
      items: [
        { action: 'new', label: 'action.newEntry', shortcut: 'Ctrl+N' },
        { action: 'import', label: 'action.importLegacy', shortcut: 'Ctrl+Shift+O' },
        { separator: true },
        { action: 'export', label: 'action.exportEntry', shortcut: 'Ctrl+E' },
        { action: 'export-all', label: 'action.exportAll', shortcut: 'Ctrl+Shift+E' },
        { separator: true },
        { action: 'storage', label: 'Storage & backup' },
      ],
    },
    {
      label: 'menu.edit',
      items: [
        { action: 'save', label: 'action.save', shortcut: 'Ctrl+S' },
        { separator: true },
        { action: 'copy-base64', label: 'action.copyBase64', shortcut: 'Ctrl+Shift+C' },
        { action: 'copy-ruler', label: 'action.copyRuler', shortcut: 'Ctrl+Shift+R' },
        { separator: true },
        { action: 'duplicate', label: 'action.duplicate', shortcut: 'Ctrl+D' },
        { action: 'delete', label: 'action.delete' },
      ],
    },
    {
      label: 'menu.view',
      items: [
        { action: 'find', label: 'action.find', shortcut: 'Ctrl+F' },
        { separator: true },
        { heading: 'app.language' },
        ...languages.map((code) => ({
          action: `language:${code}`,
          label: state.i18n.languageName(code),
          check: code === state.i18n.current,
        })),
      ],
    },
    {
      label: 'menu.help',
      items: [
        { action: 'about', label: 'action.about' },
      ],
    },
  ];

  for (const definition of menus) {
    const menu = document.createElement('div');
    menu.className = 'menu';

    const button = document.createElement('button');
    button.className = 'menu-button';
    button.textContent = state.i18n.t(definition.label);
    menu.appendChild(button);

    const popup = document.createElement('div');
    popup.className = 'menu-popup';
    for (const item of definition.items) {
      if (item.separator) {
        const separator = document.createElement('div');
        separator.className = 'menu-separator';
        popup.appendChild(separator);
        continue;
      }
      if (item.heading) {
        const heading = document.createElement('div');
        heading.className = 'field-label';
        heading.style.padding = '6px 24px 4px';
        heading.textContent = state.i18n.t(item.heading);
        popup.appendChild(heading);
        continue;
      }
      const buttonItem = document.createElement('button');
      buttonItem.className = 'menu-item';
      buttonItem.dataset.action = item.action;
      buttonItem.innerHTML = `<span>${escapeHtml(item.label.startsWith('language:') ? item.label : state.i18n.t(item.label))}</span>${item.shortcut ? `<span class="shortcut">${escapeHtml(item.shortcut)}</span>` : ''}${item.check ? '<span>✓</span>' : ''}`;
      popup.appendChild(buttonItem);
    }

    menu.appendChild(popup);
    bar.appendChild(menu);
  }

  bar.querySelectorAll('.menu-button').forEach((button) => {
    button.addEventListener('click', (event) => {
      event.stopPropagation();
      const menu = button.parentElement;
      const isOpen = menu.classList.contains('open');
      bar.querySelectorAll('.menu').forEach((candidate) => candidate.classList.remove('open'));
      if (!isOpen) menu.classList.add('open');
    });
  });

  bar.querySelectorAll('.menu-item').forEach((item) => {
    item.addEventListener('click', (event) => {
      event.stopPropagation();
      const action = item.dataset.action;
      bar.querySelectorAll('.menu').forEach((candidate) => candidate.classList.remove('open'));
      handleAction(action);
    });
  });

  updateActionStates();
}

function handleAction(action) {
  if (action.startsWith('language:')) {
    changeLanguage(action.slice('language:'.length));
    return;
  }
  switch (action) {
    case 'new': void openAddDialog(); break;
    case 'import': $('file-input').click(); break;
    case 'export': void exportCurrent(); break;
    case 'export-all': void exportAll(); break;
    case 'storage': void openStorageDialog(); break;
    case 'save': saveEntry(true); break;
    case 'copy-base64': void copyBase64(); break;
    case 'copy-ruler': void copyRuler(); break;
    case 'duplicate': void duplicateCurrent(); break;
    case 'delete': void deleteCurrent(); break;
    case 'find': $('search').focus(); $('search').select(); break;
    case 'about': void openAboutDialog(); break;
    default: break;
  }
}

function buildLanguageSelect() {
  const select = $('language');
  select.innerHTML = state.i18n.availableLanguages().map((code) =>
    `<option value="${escapeAttr(code)}">${escapeHtml(state.i18n.languageName(code))}</option>`).join('');
  select.value = state.i18n.current;
}

function applyTranslations() {
  document.title = state.i18n.t('app.title');
  document.querySelectorAll('[data-i18n]').forEach((element) => {
    element.textContent = state.i18n.t(element.dataset.i18n);
  });
  document.querySelectorAll('[data-i18n-placeholder]').forEach((element) => {
    element.placeholder = state.i18n.t(element.dataset.i18nPlaceholder);
  });

  const sort = $('sort');
  sort.querySelector('option[value="updated"]').textContent = state.i18n.t('library.sortUpdated');
  sort.querySelector('option[value="created"]').textContent = state.i18n.t('library.sortCreated');
  sort.querySelector('option[value="name"]').textContent = state.i18n.t('library.sortName');
  sort.value = state.sort;

  buildLanguageSelect();
  renderGeneCategories();
  renderList();
  if (state.currentId) {
    renderHeader();
    renderMeta();
    renderCodes();
    renderGeneEditor();
    renderPortrait();
    updateDirtyUi();
    updateActionStates();
  } else {
    updateActionStates();
  }
}

function changeLanguage(code) {
  if (!state.i18n.setLanguage(code)) return;
  try {
    const settings = state.store.getSettings();
    state.store.saveSettings({ ...settings, language: code });
  } catch {
    // Language still applies for this session even if the preference cannot be stored.
  }
  buildMenus();
  applyTranslations();
  setStatus(state.i18n.t('status.ready'), 3000);
}

function openModal(html) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.innerHTML = html;
  $('modal-root').appendChild(backdrop);
  return backdrop;
}

function closeModal(backdrop) {
  if (backdrop?.parentElement) backdrop.remove();
}

function topModal() {
  const root = $('modal-root');
  return root.lastElementChild;
}

function showConfirm({ title, text, confirmLabel, cancelLabel, danger = false }) {
  return new Promise((resolve) => {
    const backdrop = openModal(`
      <div class="dialog" role="dialog" aria-modal="true">
        <div class="dialog-header"><h2 class="dialog-title">${escapeHtml(title)}</h2></div>
        <div class="dialog-body"><p>${escapeHtml(text)}</p></div>
        <div class="dialog-footer">
          <button class="btn" data-confirm="cancel">${escapeHtml(cancelLabel)}</button>
          <button class="btn ${danger ? 'danger' : 'primary'}" data-confirm="ok">${escapeHtml(confirmLabel)}</button>
        </div>
      </div>`);
    const finish = (value) => {
      closeModal(backdrop);
      resolve(value);
    };
    backdrop.querySelector('[data-confirm="cancel"]').addEventListener('click', () => finish(false));
    backdrop.querySelector('[data-confirm="ok"]').addEventListener('click', () => finish(true));
    backdrop.addEventListener('click', (event) => {
      if (event.target === backdrop) finish(false);
    });
  });
}

function showUnsavedDialog() {
  return new Promise((resolve) => {
    const backdrop = openModal(`
      <div class="dialog" role="dialog" aria-modal="true">
        <div class="dialog-header"><h2 class="dialog-title">${escapeHtml(state.i18n.t('msg.unsavedTitle'))}</h2></div>
        <div class="dialog-body"><p>${escapeHtml(state.i18n.t('msg.unsavedText'))}</p></div>
        <div class="dialog-footer">
          <button class="btn" data-choice="cancel">${escapeHtml(state.i18n.t('msg.cancel'))}</button>
          <button class="btn danger" data-choice="discard">${escapeHtml(state.i18n.t('msg.discard'))}</button>
          <button class="btn primary" data-choice="save">${escapeHtml(state.i18n.t('msg.save'))}</button>
        </div>
      </div>`);
    const finish = (value) => {
      closeModal(backdrop);
      resolve(value);
    };
    backdrop.querySelector('[data-choice="cancel"]').addEventListener('click', () => finish('cancel'));
    backdrop.querySelector('[data-choice="discard"]').addEventListener('click', () => finish('discard'));
    backdrop.querySelector('[data-choice="save"]').addEventListener('click', () => finish('save'));
    backdrop.addEventListener('click', (event) => {
      if (event.target === backdrop) finish('cancel');
    });
  });
}

function showInfo(title, text, warnings = []) {
  return new Promise((resolve) => {
    const warningHtml = warnings.length
      ? `<pre style="white-space:pre-wrap;color:var(--warning);font-size:12px;max-height:180px;overflow:auto">${escapeHtml(warnings.slice(0, 8).join('\n'))}</pre>`
      : '';
    const backdrop = openModal(`
      <div class="dialog" role="dialog" aria-modal="true" style="width:560px">
        <div class="dialog-header"><h2 class="dialog-title">${escapeHtml(title)}</h2></div>
        <div class="dialog-body"><p>${escapeHtml(text)}</p>${warningHtml}</div>
        <div class="dialog-footer"><button class="btn primary" data-close>${escapeHtml(state.i18n.t('msg.ok'))}</button></div>
      </div>`);
    const finish = () => {
      closeModal(backdrop);
      resolve();
    };
    backdrop.querySelector('[data-close]').addEventListener('click', finish);
    backdrop.addEventListener('click', (event) => {
      if (event.target === backdrop) finish();
    });
  });
}

function showError(title, text) {
  return showInfo(title, text);
}

async function openAboutDialog() {
  const backdrop = openModal(`
    <div class="dialog about-dialog" role="dialog" aria-modal="true">
      <div class="about-header">
        <img class="about-icon" src="./assets/app.png" alt="">
        <div>
          <div class="about-title">${escapeHtml(state.i18n.t('about.title'))}</div>
          <div class="about-tagline">${escapeHtml(state.i18n.t('about.version', '1.0.0'))}</div>
        </div>
      </div>
      <div class="dialog-body">
        <p class="about-text">${escapeHtml(state.i18n.t('about.tagline'))}</p>
        <p class="about-text">${escapeHtml(state.i18n.t('about.description'))}</p>
        <div class="about-links">
          ${escapeHtml(state.i18n.t('about.repository'))} <a href="https://github.com/grnnk/CK3-DNA-Manager-Qt" target="_blank" rel="noopener">grnnk/CK3-DNA-Manager-Qt</a><br>
          ${escapeHtml(state.i18n.t('about.original'))} <a href="https://github.com/Welshtokki/CK3_DNA_Manager" target="_blank" rel="noopener">Welshtokki/CK3_DNA_Manager</a><br>
          ${escapeHtml(state.i18n.t('about.qt'))} <a href="https://www.qt.io" target="_blank" rel="noopener">qt.io</a>
        </div>
      </div>
      <div class="dialog-footer"><button class="btn primary" data-close>${escapeHtml(state.i18n.t('msg.close'))}</button></div>
    </div>`);
  backdrop.querySelector('[data-close]').addEventListener('click', () => closeModal(backdrop));
  backdrop.addEventListener('click', (event) => {
    if (event.target === backdrop) closeModal(backdrop);
  });
}

async function openStorageDialog() {
  const usage = state.store.usageBytes();
  let folderStatus = 'Not connected';
  if (state.folder.connected) folderStatus = `Connected: ${state.folder.name}`;
  else if (state.folder.needsPermission) folderStatus = `Reconnect needed: ${state.folder.name}`;
  else if (state.folder.handle) folderStatus = `Remembered: ${state.folder.name}`;

  const backdrop = openModal(`
    <div class="dialog storage-dialog" role="dialog" aria-modal="true">
      <div class="dialog-header"><h2 class="dialog-title">Storage &amp; backup</h2></div>
      <div class="dialog-body">
        <p>This web version stores its library in this browser's localStorage. Export a JSON backup regularly, or connect a folder to keep a synced copy next to the desktop database.</p>
        <div class="storage-row"><span>Entries</span><strong>${state.entries.length}</strong></div>
        <div class="storage-row"><span>Estimated storage used</span><strong>${formatBytes(usage)}</strong></div>
        <div class="storage-row"><span>Primary storage</span><strong>Browser localStorage</strong></div>
        <div class="storage-row"><span>Library folder</span><strong>${escapeHtml(folderStatus)}</strong></div>
        <div class="storage-row">
          <span>Startup source</span>
          <select id="startup-source" class="input" style="width:220px">
            <option value="local"${state.startupSource === 'local' ? ' selected' : ''}>Browser localStorage</option>
            <option value="folder"${state.startupSource === 'folder' ? ' selected' : ''}>Connected folder</option>
          </select>
        </div>
        ${state.folder.error ? `<p style="color:var(--danger)">${escapeHtml(state.folder.error)}</p>` : ''}
        <p style="margin-top:16px">A connected folder is read from and written to <code>web-sync/library.json</code>. If that file does not exist, the app can read <code>dna.db</code> once and import it. The desktop database is never modified.</p>
        ${Folder.isSupported() ? '' : '<p style="color:var(--warning)">This browser does not support direct folder access. JSON import/export and .db import still work.</p>'}
      </div>
      <div class="dialog-footer" style="flex-wrap:wrap;justify-content:flex-start">
        ${state.folder.connected
          ? '<button class="btn" data-storage="sync">Sync now</button>'
          : (state.folder.handle
            ? '<button class="btn" data-storage="reconnect">Reconnect folder</button>'
            : '<button class="btn" data-storage="connect">Connect folder</button>')}
        <button class="btn" data-storage="disconnect"${state.folder.handle ? '' : ' disabled'}>Disconnect</button>
        <button class="btn" data-storage="import-db">Import .db</button>
        <button class="btn" data-storage="import">Import JSON</button>
        <button class="btn" data-storage="export">Export all</button>
        <button class="btn danger" data-storage="clear">Clear library</button>
        <button class="btn primary" data-storage="close">Close</button>
      </div>
    </div>`);

  backdrop.querySelector('[data-storage="close"]').addEventListener('click', () => closeModal(backdrop));
  backdrop.querySelector('[data-storage="export"]').addEventListener('click', () => {
    closeModal(backdrop);
    void exportAll();
  });
  backdrop.querySelector('[data-storage="import"]').addEventListener('click', () => {
    closeModal(backdrop);
    $('file-input').click();
  });
  backdrop.querySelector('[data-storage="import-db"]').addEventListener('click', () => {
    closeModal(backdrop);
    $('file-input').click();
  });
  backdrop.querySelector('[data-storage="connect"]')?.addEventListener('click', () => {
    closeModal(backdrop);
    void connectFolder();
  });
  backdrop.querySelector('[data-storage="reconnect"]')?.addEventListener('click', () => {
    closeModal(backdrop);
    void reconnectFolder();
  });
  backdrop.querySelector('[data-storage="sync"]')?.addEventListener('click', () => {
    closeModal(backdrop);
    void syncToFolder({ silent: false });
  });
  backdrop.querySelector('[data-storage="disconnect"]')?.addEventListener('click', () => {
    closeModal(backdrop);
    void disconnectFolder();
  });
  backdrop.querySelector('#startup-source')?.addEventListener('change', (event) => {
    state.startupSource = event.target.value === 'folder' ? 'folder' : 'local';
    persistSettings({ startupSource: state.startupSource });
    setStatus(state.startupSource === 'folder' ? 'Startup source: connected folder.' : 'Startup source: browser storage.');
  });
  backdrop.querySelector('[data-storage="clear"]').addEventListener('click', async () => {
    closeModal(backdrop);
    const confirmed = await showConfirm({
      title: 'Clear library',
      text: 'Delete every entry and portrait stored in this browser? This cannot be undone.',
      confirmLabel: 'Delete',
      cancelLabel: state.i18n.t('msg.cancel'),
      danger: true,
    });
    if (!confirmed) return;
    if (state.folder.connected) await disconnectFolder();
    try {
      state.store.clear();
    } catch (error) {
      await showError(state.i18n.t('msg.databaseError'), error.message);
      return;
    }
    state.entries = [];
    state.currentId = null;
    state.currentData = null;
    showWelcome();
    refreshEntries();
    scheduleFolderSync();
    setStatus('Library cleared.');
  });
  backdrop.addEventListener('click', (event) => {
    if (event.target === backdrop) closeModal(backdrop);
  });
}

async function setAddPortrait(blob) {
  if (!state.add) return;
  try {
    const image = await Images.loadImage(blob);
    const portraitBlob = await Images.encodePortrait(image, 1600);
    const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
    if (!portraitBlob || !thumbnailBlob) throw new Error('The image could not be encoded.');
    if (!state.add) return;
    state.add.portraitDataUrl = await Images.blobToDataUrl(portraitBlob);
    state.add.thumbnailDataUrl = await Images.blobToDataUrl(thumbnailBlob);
    if (!state.add) return;
    renderAddPortrait();
    updateAddCreateButton();
  } catch (error) {
    await showError(state.i18n.t('msg.imageError', error.message), error.message);
  }
}

function renderAddPortrait() {
  if (!state.add) return;
  const image = document.querySelector('#add-portrait-image');
  const placeholder = document.querySelector('#add-portrait-placeholder');
  if (!image || !placeholder) return;
  if (state.add.portraitDataUrl) {
    image.src = state.add.portraitDataUrl;
    image.hidden = false;
    placeholder.hidden = true;
  } else {
    image.removeAttribute('src');
    image.hidden = true;
    placeholder.hidden = false;
  }
}

function updateAddCreateButton() {
  if (!state.add) return;
  const button = document.querySelector('#add-create');
  if (button) button.disabled = !state.add.parsed;
}

function validateAdd() {
  if (!state.add) return;
  const dna = document.querySelector('#add-dna');
  const status = document.querySelector('#add-status');
  const text = dna.value.trim();
  state.add.text = dna.value;

  if (!text) {
    status.className = 'status-message';
    status.textContent = state.i18n.t('add.pasteHint');
    state.add.parsed = null;
    updateAddCreateButton();
    return;
  }

  const parsed = Codec.parse(text, state.add.hint, state.catalog, state.legacyCatalog);
  state.add.parsed = parsed.ok ? parsed : null;
  if (!parsed.ok) {
    status.className = 'status-message error';
    status.textContent = parsed.error;
    updateAddCreateButton();
    return;
  }

  let message = '';
  if (parsed.format === Codec.DnaFormat.LegacyBase64) {
    message = state.i18n.t('add.detectedLegacy', parsed.geneCount);
  } else if (parsed.format === Codec.DnaFormat.RulerDesigner) {
    message = state.i18n.t('add.detectedRuler', parsed.geneCount);
  } else {
    message = state.i18n.t('add.detectedModern', parsed.geneCount);
  }

  const modded = parsed.data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.Modded).length;
  const rulerOnly = parsed.data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.RulerOnly).length;
  if (modded) message += state.i18n.t('add.moddedSuffix', modded);
  if (rulerOnly) message += state.i18n.t('add.rulerOnlySuffix', rulerOnly);
  if (parsed.warnings.length) message += `\n${state.i18n.t('add.warnings', parsed.warnings.length)}`;

  status.className = `status-message ${parsed.warnings.length ? 'warning' : 'ok'}`;
  status.textContent = message;
  status.title = parsed.warnings.join('\n');
  updateAddCreateButton();
}

async function openAddDialog(initialImage = null) {
  if (state.add) return;
  if (!(await maybeLeaveCurrent())) return;

  const backdrop = openModal(`
    <div class="dialog add-dialog" role="dialog" aria-modal="true">
      <div class="dialog-header"><h2 class="dialog-title">${escapeHtml(state.i18n.t('add.title'))}</h2></div>
      <div class="add-body">
        <div class="add-left">
          <label class="field-label" for="add-name">${escapeHtml(state.i18n.t('add.name'))}</label>
          <input id="add-name" class="input" placeholder="${escapeAttr(state.i18n.t('add.namePlaceholder'))}">
          <label class="field-label" for="add-dna">${escapeHtml(state.i18n.t('add.dna'))}</label>
          <textarea id="add-dna" class="input add-dna" spellcheck="false" placeholder="${escapeAttr(state.i18n.t('add.dnaPlaceholder'))}"></textarea>
          <div class="format-row">
            <label class="field-label" for="add-format">${escapeHtml(state.i18n.t('add.format'))}</label>
            <select id="add-format" class="input">
              <option value="auto">${escapeHtml(state.i18n.t('add.formatAuto'))}</option>
              <option value="modern">${escapeHtml(state.i18n.t('add.formatModern'))}</option>
              <option value="legacy">${escapeHtml(state.i18n.t('add.formatLegacy'))}</option>
            </select>
          </div>
          <div id="add-status" class="status-message">${escapeHtml(state.i18n.t('add.pasteHint'))}</div>
        </div>
        <div class="add-right">
          <label class="field-label">${escapeHtml(state.i18n.t('add.portrait'))}</label>
          <div id="add-portrait" class="portrait add-portrait" tabindex="0">
            <img id="add-portrait-image" alt="" hidden>
            <div id="add-portrait-placeholder" class="portrait-placeholder">${escapeHtml(state.i18n.t('add.dropImage'))}</div>
          </div>
          <div class="add-image-buttons">
            <button id="add-choose" class="btn">${escapeHtml(state.i18n.t('add.chooseImage'))}</button>
            <button id="add-paste" class="btn">${escapeHtml(state.i18n.t('action.pasteImage'))}</button>
            <button id="add-remove" class="btn danger">${escapeHtml(state.i18n.t('add.removeImage'))}</button>
          </div>
        </div>
      </div>
      <div class="dialog-footer">
        <button id="add-cancel" class="btn">${escapeHtml(state.i18n.t('msg.cancel'))}</button>
        <button id="add-create" class="btn primary" disabled>${escapeHtml(state.i18n.t('add.create'))}</button>
      </div>
    </div>`);

  state.add = {
    parsed: null,
    hint: Codec.Base64Hint.Auto,
    portraitDataUrl: '',
    thumbnailDataUrl: '',
  };

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = 'image/*';
  fileInput.hidden = true;
  backdrop.appendChild(fileInput);

  let timer = null;
  const dna = backdrop.querySelector('#add-dna');
  const name = backdrop.querySelector('#add-name');
  const format = backdrop.querySelector('#add-format');
  const portrait = backdrop.querySelector('#add-portrait');

  dna.addEventListener('input', () => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(validateAdd, 220);
  });
  name.addEventListener('input', updateAddCreateButton);
  format.addEventListener('change', () => {
    state.add.hint = format.value;
    validateAdd();
  });

  const choose = () => fileInput.click();
  backdrop.querySelector('#add-choose').addEventListener('click', choose);
  portrait.addEventListener('click', choose);
  fileInput.addEventListener('change', async () => {
    const file = fileInput.files?.[0];
    fileInput.value = '';
    if (file) await setAddPortrait(file);
  });
  backdrop.querySelector('#add-paste').addEventListener('click', async () => {
    const blob = await Images.readClipboardImage();
    if (!blob) {
      setStatus(state.i18n.t('action.pasteImageHint'), 5000);
      return;
    }
    await setAddPortrait(blob);
  });
  backdrop.querySelector('#add-remove').addEventListener('click', () => {
    state.add.portraitDataUrl = '';
    state.add.thumbnailDataUrl = '';
    renderAddPortrait();
    updateAddCreateButton();
  });

  portrait.addEventListener('dragover', (event) => {
    event.preventDefault();
    portrait.classList.add('drag-active');
  });
  portrait.addEventListener('dragleave', () => portrait.classList.remove('drag-active'));
  portrait.addEventListener('drop', async (event) => {
    event.preventDefault();
    portrait.classList.remove('drag-active');
    const file = [...(event.dataTransfer?.files || [])].find(Images.isImageFile);
    if (file) await setAddPortrait(file);
  });

  const close = () => {
    if (timer) clearTimeout(timer);
    state.add = null;
    closeModal(backdrop);
  };
  backdrop.querySelector('#add-cancel').addEventListener('click', close);
  backdrop.addEventListener('click', (event) => {
    if (event.target === backdrop) close();
  });

  backdrop.querySelector('#add-create').addEventListener('click', async () => {
    validateAdd();
    if (!state.add?.parsed) return;
    const parsed = state.add.parsed;
    const entryName = name.value.trim() || state.i18n.t('add.nameFallback');
    try {
      const record = state.store.insert({
        name: entryName,
        notes: '',
        dna: Codec.bytesToBase64(Codec.toBytes(parsed.data, state.catalog)),
        rulerMeta: Codec.serializeRulerMetadata(parsed.data),
        favorite: false,
        createdAt: Date.now(),
        updatedAt: Date.now(),
        hasPortrait: Boolean(state.add.portraitDataUrl),
      });
      if (state.add.portraitDataUrl) {
        try {
          state.store.setPortrait(record.id, state.add.portraitDataUrl, state.add.thumbnailDataUrl);
        } catch (error) {
          try {
            state.store.update({ ...record, hasPortrait: false });
          } catch {
            // The entry itself is already saved.
          }
          await showError(state.i18n.t('msg.databaseError'), error.message);
        }
      }
      close();
      refreshEntries();
      loadEntry(record.id);
      scheduleFolderSync();
      setStatus(state.i18n.t('msg.saved'));
    } catch (error) {
      await showError(state.i18n.t('msg.databaseError'), error.message);
    }
  });

  if (initialImage) {
    await setAddPortrait(initialImage);
  }
  name.focus();
}

async function importFromFile(file) {
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

function setupEvents() {
  $('search').addEventListener('input', () => {
    state.search = $('search').value;
    renderList();
  });

  $('sort').addEventListener('change', () => {
    state.sort = $('sort').value;
    try {
      const settings = state.store.getSettings();
      state.store.saveSettings({ ...settings, sort: state.sort });
    } catch {
      // Keep the sort for this session.
    }
    renderList();
  });

  $('language').addEventListener('change', () => changeLanguage($('language').value));

  $('entry-list').addEventListener('click', (event) => {
    const item = event.target.closest('.entry-item');
    if (item?.dataset.id) void selectEntry(item.dataset.id);
  });

  $('new-button').addEventListener('click', () => void openAddDialog());
  $('welcome-new').addEventListener('click', () => void openAddDialog());
  $('import-button').addEventListener('click', () => $('file-input').click());
  $('welcome-import').addEventListener('click', () => $('file-input').click());
  $('file-input').addEventListener('change', () => {
    const file = $('file-input').files?.[0];
    $('file-input').value = '';
    if (file) void importFromFile(file);
  });

  $('favorite').addEventListener('click', () => void toggleFavorite());
  $('tab-overview').addEventListener('click', () => {
    state.activeTab = 'overview';
    renderTabs();
  });
  $('tab-genes').addEventListener('click', () => {
    state.activeTab = 'genes';
    renderTabs();
    renderGeneEditor();
  });
  $('edit-genes').addEventListener('click', () => {
    state.activeTab = 'genes';
    renderTabs();
    renderGeneEditor();
  });

  $('name').addEventListener('input', () => {
    state.detailsDirty = true;
    updateDirtyUi();
    updateActionStates();
    if (state.currentId) {
      const entry = state.store.get(state.currentId);
      if (entry) {
        const title = $('name').value.trim() || state.i18n.t('add.nameFallback');
        $('detail-title').textContent = title;
        document.title = `${state.i18n.t('app.title')} — ${title}`;
      }
    }
  });
  $('notes').addEventListener('input', () => {
    state.detailsDirty = true;
    updateDirtyUi();
    updateActionStates();
  });
  $('save-details').addEventListener('click', () => saveEntry(true));
  $('copy-base64').addEventListener('click', () => void copyBase64());
  $('copy-ruler').addEventListener('click', () => void copyRuler());
  $('duplicate-entry').addEventListener('click', () => void duplicateCurrent());
  $('export-entry').addEventListener('click', () => void exportCurrent());
  $('delete-entry').addEventListener('click', () => void deleteCurrent());

  document.querySelectorAll('.code-tab').forEach((tab) => {
    tab.addEventListener('click', () => {
      state.codeTab = tab.dataset.codeTab;
      renderCodes();
    });
  });
  $('copy-code').addEventListener('click', () => void copyCodeTab());

  $('change-portrait').addEventListener('click', () => $('image-input').click());
  $('image-input').addEventListener('change', () => {
    const file = $('image-input').files?.[0];
    $('image-input').value = '';
    if (file) void setPortraitFromBlob(file);
  });
  $('paste-portrait').addEventListener('click', () => void pastePortrait());
  $('remove-portrait').addEventListener('click', () => void removePortrait());

  const portrait = $('portrait');
  portrait.addEventListener('click', () => {
    if (state.currentId) $('image-input').click();
  });
  portrait.addEventListener('dragover', (event) => {
    event.preventDefault();
    portrait.classList.add('drag-active');
  });
  portrait.addEventListener('dragleave', () => portrait.classList.remove('drag-active'));
  portrait.addEventListener('drop', async (event) => {
    event.preventDefault();
    portrait.classList.remove('drag-active');
    const file = [...(event.dataTransfer?.files || [])].find(Images.isImageFile);
    if (file) await setPortraitFromBlob(file);
  });

  $('gene-search').addEventListener('input', () => {
    state.geneSearch = $('gene-search').value;
    renderGeneEditor();
  });
  $('gene-category').addEventListener('change', () => {
    state.geneCategory = $('gene-category').value;
    renderGeneEditor();
  });
  $('gene-content').addEventListener('change', handleGeneChange);
  $('gene-content').addEventListener('input', (event) => {
    if (event.target.matches('input[type="number"]')) handleGeneChange(event);
  });
  $('gene-content').addEventListener('click', (event) => {
    const header = event.target.closest('.section-header');
    if (!header) return;
    const section = header.dataset.section;
    const body = header.nextElementSibling;
    if (!body) return;
    const collapsed = !body.hidden;
    body.hidden = collapsed;
    header.querySelector('.section-arrow').textContent = collapsed ? '▸' : '▾';
    if (collapsed) state.collapsedSections.add(section);
    else state.collapsedSections.delete(section);
  });

  $('revert-genes').addEventListener('click', revertGenes);
  $('save-genes').addEventListener('click', saveGenes);

  document.addEventListener('click', (event) => {
    if (!event.target.closest('.menu')) {
      document.querySelectorAll('.menu').forEach((menu) => menu.classList.remove('open'));
    }
  });

  document.addEventListener('keydown', (event) => {
    const mod = event.ctrlKey || event.metaKey;
    const key = event.key.toLowerCase();
    if (key === 'escape') {
      const modal = topModal();
      if (modal) {
        const dismiss = modal.querySelector(
          '[data-confirm="cancel"], [data-choice="cancel"], [data-close], #add-cancel, [data-storage="close"]',
        );
        if (dismiss) dismiss.click();
        else modal.remove();
        return;
      }
      document.querySelectorAll('.menu').forEach((menu) => menu.classList.remove('open'));
      return;
    }
    if (!mod) return;
    let handled = true;
    if (key === 'n' && !event.shiftKey) void openAddDialog();
    else if (key === 's') saveEntry(true);
    else if (key === 'o' && event.shiftKey) $('file-input').click();
    else if (key === 'e' && event.shiftKey) void exportAll();
    else if (key === 'e') void exportCurrent();
    else if (key === 'c' && event.shiftKey) void copyBase64();
    else if (key === 'r' && event.shiftKey) void copyRuler();
    else if (key === 'd') void duplicateCurrent();
    else if (key === 'f') { $('search').focus(); $('search').select(); }
    else handled = false;
    if (handled) event.preventDefault();
  });

  document.addEventListener('paste', (event) => {
    const image = Images.imageFromClipboardEvent(event);
    if (!image) return;
    if (Images.clipboardEventHasText(event)) return;
    event.preventDefault();
    if (state.add) {
      void setAddPortrait(image);
    } else if (document.querySelector('#modal-root')?.children.length) {
      return;
    } else if (state.currentId) {
      void setPortraitFromBlob(image);
    } else {
      void openAddDialog(image);
    }
  });

  window.addEventListener('beforeunload', (event) => {
    if (state.geneDirty || state.detailsDirty) {
      event.preventDefault();
      event.returnValue = '';
    }
  });
}

async function boot() {
  const embedded = globalThis.__CK3DNA_ASSETS__;
  const base = new URL('./assets/', document.baseURI);
  const [genes, legacy, translations] = embedded
    ? [embedded.genes, embedded.legacy, embedded.translations]
    : await Promise.all([
      fetch(new URL('genes.json', base)).then((response) => response.json()),
      fetch(new URL('legacy_genes.json', base)).then((response) => response.json()),
      fetch(new URL('translations.json', base)).then((response) => response.json()),
    ]);

  state.catalog = GeneCatalog.fromJson(genes);
  state.legacyCatalog = GeneCatalog.fromJson(legacy);
  state.i18n = new I18n();
  state.i18n.load(translations);
  state.store = new LocalStore();

  const settings = state.store.getSettings();
  const systemLanguage = (navigator.language || 'en').slice(0, 2).toLowerCase();
  const language = state.i18n.availableLanguages().includes(settings.language)
    ? settings.language
    : (state.i18n.availableLanguages().includes(systemLanguage) ? systemLanguage : 'en');
  state.i18n.setLanguage(language);
  state.sort = ['updated', 'created', 'name'].includes(settings.sort) ? settings.sort : 'updated';
  state.startupSource = settings.startupSource === 'folder' ? 'folder' : 'local';

  state.entries = state.store.entries();
  await restoreFolder();
  state.entries = state.store.entries();
  buildMenus();
  buildLanguageSelect();
  applyTranslations();
  setupEvents();

  if (state.entries.length > 0) {
    const first = sortedEntries()[0];
    loadEntry(first.id);
  } else {
    showWelcome();
  }
  setStatus(state.i18n.t('status.ready'), 4000);
}

boot().catch((error) => {
  console.error(error);
  document.body.innerHTML = `<pre style="color:#e07b6a;padding:24px;white-space:pre-wrap">Failed to start CK3 DNA Manager:\n${escapeHtml(error.stack || error.message)}</pre>`;
});

// Detail-pane rendering and entry-level actions.

import * as Codec from '../core/codec.js';
import { state } from './state.js';
import { $, countsForData, formatDate, setStatus } from './shared.js';
import { showConfirm, showError, showUnsavedDialog } from './dialogs.js';
import { scheduleFolderSync } from './folder.js';
import { renderGeneEditor } from './gene-editor.js';
import { refreshEntries, renderList, showWelcome } from './library.js';

export function renderHeader() {
  const entry = state.currentId ? state.store.get(state.currentId) : null;
  if (!entry) return;
  $('detail-title').textContent = entry.name;
  $('favorite').textContent = entry.favorite ? '★' : '☆';
  $('favorite').classList.toggle('checked', entry.favorite);
  $('favorite').title = state.i18n.t(entry.favorite ? 'detail.unfavorite' : 'detail.favorite');
  document.title = `${state.i18n.t('app.title')} — ${entry.name}`;
}

export function renderMeta() {
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

export function renderPortrait() {
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

export function renderCodes() {
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

export function renderTabs() {
  $('tab-overview').classList.toggle('active', state.activeTab === 'overview');
  $('tab-genes').classList.toggle('active', state.activeTab === 'genes');
  $('tab-overview').setAttribute('aria-selected', String(state.activeTab === 'overview'));
  $('tab-genes').setAttribute('aria-selected', String(state.activeTab === 'genes'));
  $('overview-panel').hidden = state.activeTab !== 'overview';
  $('genes-panel').hidden = state.activeTab !== 'genes';
}

export function updateActionStates() {
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

export function updateDirtyUi() {
  const label = $('dirty-label');
  label.textContent = state.i18n.t(state.geneDirty ? 'genes.unsaved' : 'genes.noChanges');
  label.classList.toggle('is-dirty', state.geneDirty);
  $('save-genes').disabled = !state.geneDirty;
  $('revert-genes').disabled = !state.geneDirty;
  $('save-details').disabled = !state.currentId || (!state.detailsDirty && !state.geneDirty);
}

export function renderDetail() {
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

export function loadEntry(id) {
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

export async function maybeLeaveCurrent() {
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

export async function selectEntry(id) {
  if (id === state.currentId) return;
  if (!(await maybeLeaveCurrent())) {
    renderList();
    return;
  }
  loadEntry(id);
}

export function saveEntry(showMessage = true) {
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

export async function toggleFavorite() {
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

export async function duplicateCurrent() {
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

export async function deleteCurrent() {
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

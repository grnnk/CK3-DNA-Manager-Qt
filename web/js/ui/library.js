// Library list, search, sorting, and welcome-state rendering.

import { state } from './state.js';
import { $, escapeAttr, escapeHtml, formatDate, nameCollator, subtitleFor } from './shared.js';
import { updateActionStates } from './detail.js';

export function showWelcome() {
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

export function sortedEntries() {
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

export function renderList() {
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

export function refreshEntries() {
  state.entries = state.store.entries();
  renderList();
  updateActionStates();
}

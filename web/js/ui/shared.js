// DOM, formatting, and catalog helpers shared by the web UI.

import * as Codec from '../core/codec.js';
import { GeneCatalog } from '../core/catalog.js';
import { state } from './state.js';

export const $ = (id) => document.getElementById(id);

export function nameCollator() {
  return new Intl.Collator(state.i18n?.locale() || undefined, { sensitivity: 'base', numeric: true });
}

export function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

export function escapeAttr(value) {
  return escapeHtml(value);
}

export function formatDate(ms) {
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

export function formatBytes(bytes) {
  if (!bytes) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

export function geneCountForEntry(entry) {
  try {
    return Math.floor(atob(entry.dna).length / 4);
  } catch {
    return 0;
  }
}

export function moddedCountForEntry(entry) {
  return Math.max(0, geneCountForEntry(entry) - 104);
}

export function dataForEntry(entry) {
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

export function countsForData(data) {
  const modded = data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.Modded).length;
  const rulerOnly = data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.RulerOnly).length;
  return {
    total: state.catalog.base64GeneCount() + modded,
    modded,
    rulerOnly,
  };
}

export function categoryLabel(category) {
  const key = GeneCatalog.categoryTranslationKey(category);
  const value = state.i18n.value(key);
  return value === key ? category : value;
}

export function subtitleFor(entry) {
  const firstLine = (entry.notes || '').split(/\r?\n/).find((line) => line.trim());
  if (firstLine) return firstLine.trim();
  const count = geneCountForEntry(entry);
  const modded = moddedCountForEntry(entry);
  return `${state.i18n.t('detail.genesCount', count)}${modded ? ` · ${state.i18n.t('detail.moddedCount', modded)}` : ''}`;
}

export function setStatus(text, timeout = 4000) {
  const element = $('status-text');
  element.textContent = text;
  if (state.statusTimer) clearTimeout(state.statusTimer);
  if (timeout > 0) {
    state.statusTimer = setTimeout(() => {
      element.textContent = state.i18n.t('status.ready');
    }, timeout);
  }
}

export function sanitizeFileName(name) {
  const value = (name || '').replace(/[\\/:*?"<>|]/g, '_').replace(/\s+/g, ' ').trim();
  return value || 'dna';
}

export function persistSettings(patch) {
  try {
    const settings = state.store.getSettings();
    state.store.saveSettings({ ...settings, ...patch });
  } catch {
    // Preferences are best-effort; the session still works.
  }
}

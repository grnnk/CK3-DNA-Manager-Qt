// Gene editor rendering and edit handling.

import * as Codec from '../core/codec.js';
import { state } from './state.js';
import { $, categoryLabel, escapeAttr, escapeHtml } from './shared.js';
import { renderCodes, renderMeta, saveEntry, updateDirtyUi } from './detail.js';

export function renderGeneCategories() {
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

export function optionHtml(gene, selected) {
  const options = gene.options.map((option) =>
    `<option value="${option.index}"${option.index === selected ? ' selected' : ''}>${escapeHtml(option.label)}</option>`);
  if (!gene.options.some((option) => option.index === selected)) {
    options.unshift(`<option value="${selected}" selected>${escapeHtml(state.i18n.t('genes.unknownOption', selected))}</option>`);
  }
  return options.join('');
}

export function geneRowHtml(gene) {
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

export function extraDisplayName(extra) {
  if (extra.key.startsWith('extra_')) {
    const slot = Number.parseInt(extra.key.slice('extra_'.length), 10);
    if (Number.isFinite(slot)) return state.i18n.t('genes.extraGene', slot);
  }
  return extra.key;
}

export function extraRowHtml(extra, index) {
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

export function renderGeneEditor() {
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

export function markGeneDirty() {
  if (!state.currentId) return;
  state.geneDirty = true;
  updateDirtyUi();
}

export function clearRawTokens(key) {
  if (state.currentData) state.currentData.ruler.rawTokens.delete(key);
}

export function handleGeneChange(event) {
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

export function revertGenes() {
  if (!state.baselineData) return;
  state.currentData = Codec.cloneDnaData(state.baselineData);
  state.geneDirty = false;
  renderGeneEditor();
  renderCodes();
  renderMeta();
  updateDirtyUi();
}

export function saveGenes() {
  saveEntry(true);
}

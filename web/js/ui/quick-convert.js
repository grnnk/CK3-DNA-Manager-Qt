// Quick Base64 <-> Ruler Designer conversion without saving an entry.

import * as Codec from '../core/codec.js';
import { state } from './state.js';
import { escapeAttr, escapeHtml } from './shared.js';
import { closeModal, openModal } from './dialogs.js';
import { copyText } from './clipboard.js';

function conversionMessage(parsed) {
  if (parsed.format === Codec.DnaFormat.LegacyBase64) {
    return state.i18n.t('add.detectedLegacy', parsed.geneCount);
  }
  if (parsed.format === Codec.DnaFormat.RulerDesigner) {
    return state.i18n.t('add.detectedRuler', parsed.geneCount);
  }
  return state.i18n.t('add.detectedModern', parsed.geneCount);
}

export function openQuickConvertDialog() {
  if (document.querySelector('#quick-input')) return;

  const backdrop = openModal(`
    <div class="dialog quick-dialog" role="dialog" aria-modal="true">
      <div class="dialog-header"><h2 class="dialog-title">${escapeHtml(state.i18n.t('quick.title'))}</h2></div>
      <div class="quick-body">
        <div class="quick-input-column">
          <label class="field-label" for="quick-input">${escapeHtml(state.i18n.t('add.dna'))}</label>
          <textarea id="quick-input" class="input quick-input" spellcheck="false" placeholder="${escapeAttr(state.i18n.t('add.dnaPlaceholder'))}"></textarea>
          <div class="format-row">
            <label class="field-label" for="quick-format">${escapeHtml(state.i18n.t('add.format'))}</label>
            <select id="quick-format" class="input">
              <option value="auto">${escapeHtml(state.i18n.t('add.formatAuto'))}</option>
              <option value="modern">${escapeHtml(state.i18n.t('add.formatModern'))}</option>
              <option value="legacy">${escapeHtml(state.i18n.t('add.formatLegacy'))}</option>
            </select>
          </div>
          <div id="quick-status" class="status-message">${escapeHtml(state.i18n.t('quick.hint'))}</div>
        </div>
        <div class="quick-output-column">
          <label class="field-label">${escapeHtml(state.i18n.t('quick.output'))}</label>
          <div class="quick-output-block">
            <div class="quick-output-header">
              <span class="field-label">${escapeHtml(state.i18n.t('detail.base64Tab'))}</span>
              <button id="quick-copy-base64" class="btn" disabled>${escapeHtml(state.i18n.t('detail.copyBase64'))}</button>
            </div>
            <textarea id="quick-base64" class="input quick-output" readonly></textarea>
          </div>
          <div class="quick-output-block">
            <div class="quick-output-header">
              <span class="field-label">${escapeHtml(state.i18n.t('detail.rulerTab'))}</span>
              <button id="quick-copy-ruler" class="btn" disabled>${escapeHtml(state.i18n.t('detail.copyRuler'))}</button>
            </div>
            <textarea id="quick-ruler" class="input quick-output" readonly></textarea>
          </div>
        </div>
      </div>
      <div class="dialog-footer">
        <button id="quick-close" class="btn primary" data-close>${escapeHtml(state.i18n.t('msg.close'))}</button>
      </div>
    </div>`);

  const input = backdrop.querySelector('#quick-input');
  const format = backdrop.querySelector('#quick-format');
  const status = backdrop.querySelector('#quick-status');
  const base64 = backdrop.querySelector('#quick-base64');
  const ruler = backdrop.querySelector('#quick-ruler');
  const copyBase64 = backdrop.querySelector('#quick-copy-base64');
  const copyRuler = backdrop.querySelector('#quick-copy-ruler');

  let timer = null;
  const clearOutput = () => {
    base64.value = '';
    ruler.value = '';
    copyBase64.disabled = true;
    copyRuler.disabled = true;
  };

  const validate = () => {
    const text = input.value.trim();
    if (!text) {
      clearOutput();
      status.className = 'status-message';
      status.textContent = state.i18n.t('quick.hint');
      status.title = '';
      return;
    }

    const parsed = Codec.parse(text, format.value, state.catalog, state.legacyCatalog);
    if (!parsed.ok) {
      clearOutput();
      status.className = 'status-message error';
      status.textContent = parsed.error;
      status.title = '';
      return;
    }

    let message = conversionMessage(parsed);
    const modded = parsed.data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.Modded).length;
    const rulerOnly = parsed.data.extras.filter((extra) => extra.kind === Codec.ExtraGeneKind.RulerOnly).length;
    if (modded) message += state.i18n.t('add.moddedSuffix', modded);
    if (rulerOnly) message += state.i18n.t('add.rulerOnlySuffix', rulerOnly);
    if (parsed.warnings.length) message += `\n${state.i18n.t('add.warnings', parsed.warnings.length)}`;

    status.className = `status-message ${parsed.warnings.length ? 'warning' : 'ok'}`;
    status.textContent = message;
    status.title = parsed.warnings.join('\n');

    base64.value = Codec.toBase64(parsed.data, state.catalog);
    ruler.value = Codec.toRuler(parsed.data, state.catalog);
    copyBase64.disabled = !base64.value;
    copyRuler.disabled = !ruler.value;
  };

  input.addEventListener('input', () => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(validate, 180);
  });
  format.addEventListener('change', validate);

  copyBase64.addEventListener('click', async () => {
    if (await copyText(base64.value, 'msg.copiedBase64')) {
      status.className = 'status-message ok';
      status.textContent = state.i18n.t('msg.copiedBase64');
      status.title = '';
    }
  });
  copyRuler.addEventListener('click', async () => {
    if (await copyText(ruler.value, 'msg.copiedRuler')) {
      status.className = 'status-message ok';
      status.textContent = state.i18n.t('msg.copiedRuler');
      status.title = '';
    }
  });

  const close = () => {
    if (timer) clearTimeout(timer);
    closeModal(backdrop);
  };
  backdrop.querySelector('#quick-close').addEventListener('click', close);
  backdrop.addEventListener('click', (event) => {
    if (event.target === backdrop) close();
  });

  validate();
  input.focus();
}

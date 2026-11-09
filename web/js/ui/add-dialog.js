// New DNA entry dialog.

import * as Codec from '../core/codec.js';
import * as Images from '../core/images.js';
import { state } from './state.js';
import { escapeAttr, escapeHtml, setStatus } from './shared.js';
import { loadEntry, maybeLeaveCurrent } from './detail.js';
import { closeModal, openModal, showError } from './dialogs.js';
import { scheduleFolderSync } from './folder.js';
import { refreshEntries } from './library.js';

export async function setAddPortrait(blob) {
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

export function renderAddPortrait() {
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

export function updateAddCreateButton() {
  if (!state.add) return;
  const button = document.querySelector('#add-create');
  if (button) button.disabled = !state.add.parsed;
}

export function validateAdd() {
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

export async function openAddDialog(initialImage = null) {
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

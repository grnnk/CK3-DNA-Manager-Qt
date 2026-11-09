// Reusable modal, confirmation, and information dialogs.

import { state } from './state.js';
import { $, escapeHtml } from './shared.js';

export function openModal(html) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.innerHTML = html;
  $('modal-root').appendChild(backdrop);
  return backdrop;
}

export function closeModal(backdrop) {
  if (backdrop?.parentElement) backdrop.remove();
}

export function topModal() {
  const root = $('modal-root');
  return root.lastElementChild;
}

export function showConfirm({ title, text, confirmLabel, cancelLabel, danger = false }) {
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

export function showUnsavedDialog() {
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

export function showInfo(title, text, warnings = []) {
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

export function showError(title, text) {
  return showInfo(title, text);
}

export async function openAboutDialog() {
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

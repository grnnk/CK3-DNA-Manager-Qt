// Menu bar, language menu, and translation application.

import { state } from './state.js';
import { $, escapeAttr, escapeHtml, setStatus } from './shared.js';
import { openAddDialog } from './add-dialog.js';
import { copyBase64, copyRuler } from './clipboard.js';
import { deleteCurrent, duplicateCurrent, renderCodes, renderHeader, renderMeta, renderPortrait, saveEntry, updateActionStates, updateDirtyUi } from './detail.js';
import { openAboutDialog } from './dialogs.js';
import { renderGeneCategories, renderGeneEditor } from './gene-editor.js';
import { renderList } from './library.js';
import { openQuickConvertDialog } from './quick-convert.js';
import { openStorageDialog } from './storage-dialog.js';
import { exportAll, exportCurrent } from './transfer.js';

export function buildMenus() {
  const bar = $('menubar');
  bar.innerHTML = '';
  const languages = state.i18n.availableLanguages();

  const menus = [
    {
      label: 'menu.file',
      items: [
        { action: 'new', label: 'action.newEntry', shortcut: 'Ctrl+N' },
        { action: 'quick-convert', label: 'action.quickConvert' },
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

export function handleAction(action) {
  if (action.startsWith('language:')) {
    changeLanguage(action.slice('language:'.length));
    return;
  }
  switch (action) {
    case 'new': void openAddDialog(); break;
    case 'quick-convert': openQuickConvertDialog(); break;
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

export function buildLanguageSelect() {
  const select = $('language');
  select.innerHTML = state.i18n.availableLanguages().map((code) =>
    `<option value="${escapeAttr(code)}">${escapeHtml(state.i18n.languageName(code))}</option>`).join('');
  select.value = state.i18n.current;
}

export function applyTranslations() {
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

export function changeLanguage(code) {
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

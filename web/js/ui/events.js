// DOM event wiring for the web client.

import * as Images from '../core/images.js';
import { state } from './state.js';
import { $ } from './shared.js';
import { openAddDialog, setAddPortrait } from './add-dialog.js';
import { copyBase64, copyCodeTab, copyRuler } from './clipboard.js';
import { deleteCurrent, duplicateCurrent, renderCodes, renderTabs, saveEntry, selectEntry, toggleFavorite, updateActionStates, updateDirtyUi } from './detail.js';
import { topModal } from './dialogs.js';
import { handleGeneChange, renderGeneEditor, revertGenes, saveGenes } from './gene-editor.js';
import { renderList } from './library.js';
import { changeLanguage } from './menus.js';
import { pastePortrait, removePortrait, setPortraitFromBlob } from './portraits.js';
import { openQuickConvertDialog } from './quick-convert.js';
import { exportAll, exportCurrent, importFromFile } from './transfer.js';

export function setupEvents() {
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
  $('quick-convert-button').addEventListener('click', openQuickConvertDialog);
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

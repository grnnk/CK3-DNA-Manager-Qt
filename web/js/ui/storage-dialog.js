// Storage and backup dialog plus folder controls.

import * as Folder from '../storage/folder-store.js';
import { state } from './state.js';
import { $, escapeHtml, formatBytes, persistSettings, setStatus } from './shared.js';
import { closeModal, openModal, showConfirm, showError } from './dialogs.js';
import { connectFolder, disconnectFolder, reconnectFolder, scheduleFolderSync, syncToFolder } from './folder.js';
import { refreshEntries, showWelcome } from './library.js';
import { exportAll } from './transfer.js';

export async function openStorageDialog() {
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

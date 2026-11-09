// Portrait import, encoding, and removal.

import * as Images from '../core/images.js';
import { state } from './state.js';
import { setStatus } from './shared.js';
import { openAddDialog } from './add-dialog.js';
import { renderMeta, renderPortrait } from './detail.js';
import { showError } from './dialogs.js';
import { scheduleFolderSync } from './folder.js';
import { refreshEntries } from './library.js';

export async function portraitPngFor(entry) {
  if (!entry.hasPortrait) return '';
  const dataUrl = state.store.getPortrait(entry.id);
  if (!dataUrl) return '';
  try {
    return await Images.dataUrlToPngBase64(dataUrl);
  } catch {
    return '';
  }
}

export async function portraitFromPngBase64(base64) {
  const blob = Images.base64ToBlob(base64, 'image/png');
  const image = await Images.loadImage(blob);
  const portraitBlob = await Images.encodePortrait(image, 1600);
  const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
  if (!portraitBlob || !thumbnailBlob) return null;
  return {
    portraitDataUrl: await Images.blobToDataUrl(portraitBlob),
    thumbnailDataUrl: await Images.blobToDataUrl(thumbnailBlob),
  };
}

export async function ensureThumbnail(entry) {
  if (!entry.hasPortrait || entry.thumbnailDataUrl || !entry.portraitDataUrl) return entry;
  try {
    const image = await Images.loadImage(entry.portraitDataUrl);
    const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
    if (thumbnailBlob) entry.thumbnailDataUrl = await Images.blobToDataUrl(thumbnailBlob);
  } catch {
    entry.thumbnailDataUrl = entry.portraitDataUrl;
  }
  return entry;
}

export async function setPortraitFromBlob(blob) {
  if (!state.currentId) {
    await openAddDialog(blob);
    return;
  }
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  try {
    const image = await Images.loadImage(blob);
    const portraitBlob = await Images.encodePortrait(image, 1600);
    const thumbnailBlob = await Images.makeThumbnail(image, 96, 120);
    if (!portraitBlob || !thumbnailBlob) throw new Error('The image could not be encoded.');
    const portraitDataUrl = await Images.blobToDataUrl(portraitBlob);
    const thumbnailDataUrl = await Images.blobToDataUrl(thumbnailBlob);
    state.store.setPortrait(entry.id, portraitDataUrl, thumbnailDataUrl);
    state.store.update({ ...entry, hasPortrait: true });
    refreshEntries();
    renderPortrait();
    renderMeta();
    scheduleFolderSync();
    setStatus(state.i18n.t('msg.portraitUpdated'));
  } catch (error) {
    await showError(state.i18n.t('msg.imageError', error.message), error.message);
  }
}

export async function removePortrait() {
  if (!state.currentId) return;
  const entry = state.store.get(state.currentId);
  if (!entry) return;
  try {
    state.store.removePortrait(entry.id);
    state.store.update({ ...entry, hasPortrait: false });
  } catch (error) {
    await showError(state.i18n.t('msg.databaseError'), error.message);
    return;
  }
  refreshEntries();
  renderPortrait();
  renderMeta();
  scheduleFolderSync();
  setStatus(state.i18n.t('msg.portraitRemoved'));
}

export async function pastePortrait() {
  const blob = await Images.readClipboardImage();
  if (!blob) {
    setStatus(state.i18n.t('action.pasteImageHint'), 5000);
    return;
  }
  await setPortraitFromBlob(blob);
}

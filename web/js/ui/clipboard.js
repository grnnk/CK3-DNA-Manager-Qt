// Clipboard helpers for DNA and Ruler Designer codes.

import * as Codec from '../core/codec.js';
import { state } from './state.js';
import { setStatus } from './shared.js';
import { showError } from './dialogs.js';

export async function copyText(text, messageKey) {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      document.execCommand('copy');
      area.remove();
    }
    setStatus(state.i18n.t(messageKey));
    return true;
  } catch {
    await showError(state.i18n.t('msg.error'), 'The clipboard could not be written.');
    return false;
  }
}

export async function copyBase64() {
  if (!state.currentData) return;
  await copyText(Codec.toBase64(state.currentData, state.catalog), 'msg.copiedBase64');
}

export async function copyRuler() {
  if (!state.currentData) return;
  await copyText(Codec.toRuler(state.currentData, state.catalog), 'msg.copiedRuler');
}

export async function copyCodeTab() {
  if (state.codeTab === 'base64') await copyBase64();
  else await copyRuler();
}

import type { DisplaySettingsHook } from '../../lib/settings';
import { Notice } from '../../ui/Notice';
import { Mirror } from './Mirror';

const SAVE_TEXT = {
  idle: 'Changes apply right away.',
  saving: 'Saving…',
  saved: 'Saved on the clock.',
  error: '',
} as const;

/** The live mirror and the autosave line, on top of the Display and Pet tabs. */
export function LiveTop({ hook }: { hook: DisplaySettingsHook }) {
  return (
    <div class="display-top">
      <Mirror />
      <p class={`autosave autosave--${hook.saveState}`} role="status">
        {SAVE_TEXT[hook.saveState]}
      </p>
      {hook.error && !hook.error.field ? <Notice tone="fail" title={hook.error.message} live /> : null}
    </div>
  );
}

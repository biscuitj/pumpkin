// The captive-portal page: what a phone opens after joining the clock's setup
// hotspot. Built as its own single file (setup.html) so it stays small and
// works before the clock has internet or a name.
import '../style.css';
import './setup.css';
import { render } from 'preact';
import { Setup } from './Setup';

render(<Setup />, document.getElementById('app')!);

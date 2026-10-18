// Base styles first, so component styles can build on them.
import './style.css';
import { render } from 'preact';
import { App } from './app';
import { startPolling } from './state';

render(<App />, document.getElementById('app')!);
startPolling();

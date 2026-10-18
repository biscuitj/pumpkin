import { defineConfig, loadEnv, type Plugin } from 'vite';
import preact from '@preact/preset-vite';
import { mockClock } from './mock/clock.ts';

export default defineConfig(({ mode }) => {
  // PUMPKIN_HOST from .env.local or the shell, e.g. 192.168.1.50 or keuken.local
  const host = loadEnv(mode, process.cwd(), 'PUMPKIN_').PUMPKIN_HOST;

  if (mode === 'development') {
    console.info(host ? `[pumpkin] /api → http://${host}` : '[pumpkin] /api → mock clock (set PUMPKIN_HOST for a real one)');
  }

  // Two single-file pages, built one after the other (`vite build`, then
  // `vite build --mode setup`) so they never share a chunk:
  // index.html is the dashboard, setup.html the captive-portal Wi-Fi setup.
  const setup = mode === 'setup';

  return {
    plugins: [preact(), host ? null : mockClock(), inlineIntoHtml()],
    build: {
      target: 'es2020',
      outDir: 'dist',
      emptyOutDir: !setup,
      rolldownOptions: { input: setup ? 'setup.html' : 'index.html' },
      cssCodeSplit: false,
      assetsInlineLimit: Number.MAX_SAFE_INTEGER,
      modulePreload: { polyfill: false },
    },
    server: {
      host: true,
      proxy: host ? { '/api': { target: `http://${host}`, changeOrigin: true } } : undefined,
    },
  };
});

/**
 * Inline every emitted JS/CSS file into the page so the build is one file,
 * which the firmware embeds (gzipped) as a single asset. Fails the build if
 * anything else would have to be served separately.
 */
function inlineIntoHtml(): Plugin {
  const reEscape = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

  return {
    name: 'pumpkin:inline-into-html',
    apply: 'build',
    enforce: 'post',
    generateBundle(_, bundle) {
      const pages = Object.keys(bundle).filter((n) => n.endsWith('.html'));
      if (pages.length !== 1) this.error(`expected one HTML page per build, got ${pages.join(', ') || 'none'}`);
      const pageName = pages[0];
      const html = bundle[pageName];
      if (html.type !== 'asset') this.error(`${pageName} is not an asset`);
      let page = String(html.source);

      for (const [name, file] of Object.entries(bundle)) {
        if (name === pageName) continue;
        const ref = `[^"]*${reEscape(name)}`;

        if (file.type === 'chunk') {
          const tag = new RegExp(`<script[^>]*src="${ref}"[^>]*></script>`);
          if (!tag.test(page)) this.error(`no <script> tag for ${name}`);
          const code = file.code.replace(/<\/script/gi, '<\\/script');
          page = page.replace(tag, () => `<script type="module">${code}</script>`);
        } else if (name.endsWith('.css')) {
          const tag = new RegExp(`<link[^>]*href="${ref}"[^>]*>`);
          if (!tag.test(page)) this.error(`no <link> tag for ${name}`);
          page = page.replace(tag, () => `<style>${String(file.source)}</style>`);
        } else {
          this.error(`${name} can't be inlined; the dashboard must build to one file`);
        }
        delete bundle[name];
      }

      html.source = page;
    },
  };
}

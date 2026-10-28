/* SPDX-License-Identifier: AGPL-3.0-only */
(async () => {
  const status = document.getElementById('loading');
  try {
    // No service worker, no isolation, no threads: said with what turns
    // them off -- a private window, a privacy setting, a page not over HTTPS
    // -- rather than "not supported", which a tester's up-to-date browser
    // was told.
    if (!('serviceWorker' in navigator))
      throw new Error(isSecureContext
        ? 'mux needs service workers, and this browser has them turned off here: a private window, or a privacy setting or extension, does that. Open mux in a normal window, or allow service workers for this site.'
        : 'mux needs a secure (HTTPS) page: open it over https://.');
    // GitHub Pages cannot configure response headers. A service worker
    // supplies isolation for the client's network and decoding workers.
    await navigator.serviceWorker.register('./worker.js', {scope:'./',updateViaCache:'none'});
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller) {
      await new Promise(resolve => navigator.serviceWorker.addEventListener('controllerchange',resolve,{once:true}));
    }
    // The page isolated only once the worker serves it: reloaded for that,
    // twice at most -- a reload that came before the worker took the page
    // (a slow activation) was counted as a failure at once. Where it still
    // is not, what was seen is said, for a report.
    if (!crossOriginIsolated) {
      const tries = Number(sessionStorage.getItem('mux-isolation-reload') || 0);
      if (tries >= 2) {
        sessionStorage.removeItem('mux-isolation-reload');
        throw new Error('Cross-origin isolation could not be enabled. Please allow service workers for this site and reload. ' +
          `(served by the worker: ${navigator.serviceWorker.controller ? 'yes' : 'no'}; ${navigator.userAgent})`);
      }
      sessionStorage.setItem('mux-isolation-reload',String(tries + 1));
      location.reload();
      return;
    }
    sessionStorage.removeItem('mux-isolation-reload');
    await Promise.all([document.fonts.load('14px "Open Sans"'),document.fonts.load('600 14px "Open Sans"')]);
    const response = await fetch('./build.json',{cache:'no-store'});
    if (!response.ok) throw new Error('Could not load mux. Please reload.');
    const build = await response.json(), base = new URL('./',location.href);
    const asset = name => {const url = new URL(name,base);url.searchParams.set('v',build.revision);return url.href;};
    window.Module = {
      locateFile:asset,
      mainScriptUrlOrBlob:asset(build.program+'.js'),
      arguments:new URLSearchParams(location.search).has('demo') ? ['--demo']:[],
      setStatus(message) {if (message) status.textContent = 'Loading mux…';},
      printErr(message) {console.error(message);},
      onExit(code) {if (code) {status.style.display = 'grid';status.textContent = 'mux could not start. Please reload.';}},
      onAbort(reason) {status.hidden = false;status.style.display = 'grid';status.textContent = 'mux could not start: '+reason;},
      onRuntimeInitialized() {status.style.display = 'none';}
    };
    const script = document.createElement('script');
    script.src = asset(build.program+'.js');
    script.onerror = () => {status.style.display = 'grid';status.textContent = 'Could not load mux. Please reload.';};
    document.body.append(script);
  } catch (error) {status.textContent = error.message || String(error);}
})();

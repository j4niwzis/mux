/* SPDX-License-Identifier: AGPL-3.0-only */
(async () => {
  const status = document.getElementById('loading');
  try {
    if (!('serviceWorker' in navigator)) throw new Error('This browser does not support the features mux needs.');
    // GitHub Pages cannot configure response headers. A service worker
    // supplies isolation for the client's network and decoding workers.
    await navigator.serviceWorker.register('./worker.js', {scope:'./',updateViaCache:'none'});
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller) {
      await new Promise(resolve => navigator.serviceWorker.addEventListener('controllerchange',resolve,{once:true}));
    }
    if (!crossOriginIsolated) {
      if (sessionStorage.getItem('mux-isolation-reload')) throw new Error('Cross-origin isolation could not be enabled. Please allow service workers and reload.');
      sessionStorage.setItem('mux-isolation-reload','1');
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

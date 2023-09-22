<script lang="ts">
  import { onMount } from 'svelte';
  import create_fn from "./Hello";
  import initModule from "./Warlock.js";

  let message:string = "";
  let sum:number = 0;

  onMount(async () => {
    var Module = {
      preRun: [],
      postRun: [],
      print: (function() {
        return function(text) {
          if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
          // These replacements are necessary if you render to raw HTML
          //text = text.replace(/&/g, "&amp;");
          //text = text.replace(/</g, "&lt;");
          //text = text.replace(/>/g, "&gt;");
          //text = text.replace('\n', '<br>', 'g');
          console.log(text);
        };
      })(),
      printErr: function(text) {
        if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
        if (0) { // XXX disabled for safety typeof dump == 'function') {
          dump(text + '\n'); // fast, straight to the real console
        } else {
          console.error(text);
        }
      },
      canvas: (function() {
        var canvas = document.getElementById('canvas');
        // As a default initial behavior, pop up an alert when webgl context is lost. To make your
        // application robust, you may want to override this behavior before shipping!
        // See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
        //canvas.addEventListener("webglcontextlost", function(e) { alert('WebGL context lost. You will need to reload the page.'); e.preventDefault(); }, false);
        return canvas;
      })(),
      setStatus: function(text) {
        if (!Module.setStatus.last) Module.setStatus.last = { time: Date.now(), text: '' };
        if (text === Module.setStatus.text) return;
        var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
        var now = Date.now();
        if (m && now - Date.now() < 30) return; // if this is a progress update, skip it if too soon
        if (m) {
          text = m[1];
        }
      },
      totalDependencies: 0,
      monitorRunDependencies: function(left) {
        this.totalDependencies = Math.max(this.totalDependencies, left);
        //Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies-left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
      }
    };
    //Module.setStatus('Downloading...');
    window.onerror = function() {
      Module.setStatus('Exception thrown, see JavaScript console');
      Module.setStatus = function(text) {
        if (text) Module.printErr('[post-exception status] ' + text);
      };
    };


    var mod:HelloModule = await create_fn();
    message = mod.hello();
    sum = mod.add(3,4);
    initModule(Module);
  });

</script>

<svelte:head>
	<title>Warlock</title>
	<meta name="description" content="Warlock Web App" />
</svelte:head>

<h1>Emscripten+WASM in SvelteKit</h1>
<ul>
  <li>Message: {message}
  <li>3 + 4 = {sum}
  <li>Check the developer console for the output of main().
</ul>

  <canvas class="emscripten" id="canvas" oncontextmenu="event.preventDefault()"></canvas>

<style>
  main {
    text-align: left;
    background: lightGray;
    width: 100%;
    padding: 10px;
  }

  canvas.emscripten
  {
    aspect-ratio:3200/2000;
    border: 0px none;
    background-color: #cccccc;
    position: static;
    top: 0px;
    left: 0px;
    margin: 0px;
    width: 100%;
    height: 100%;
    overflow: hidden;
    display: block;
  }

  .data {
    font-family: monospace;
    font-size: 12pt;
    word-wrap: break-word;
    display: block;
  }
</style>


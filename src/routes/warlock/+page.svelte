<script lang="ts">
  import { onMount } from 'svelte';
  import create_fn from "./Hello";

  let message:string = "";
  let sum:number = 0;

  onMount(async () => {
    var mod:HelloModule = await create_fn();
    message = mod.hello();
    sum = mod.add(3,4);
  });
</script>

<svelte:head>
	<title>Warlock</title>
	<meta name="description" content="Warlock Web App" />
</svelte:head>

<h1>Simple Example of Emscripten+WASM in SvelteKit</h1>
<ul>
  <li>Message: {message}
  <li>3 + 4 = {sum}
  <li>Check the developer console for the output of main().
</ul>

<style>
  main {
    text-align: left;
    background: lightGray;
    width: 100%;
    padding: 10px;
  }

  .data {
    font-family: monospace;
    font-size: 12pt;
    word-wrap: break-word;
    display: block;
  }
</style>

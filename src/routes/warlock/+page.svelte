<script>
  import Emscripten from '$lib/svelte-emscripten';
  import {default as create_module} from './Test.js';
</script>

<svelte:head>
	<title>Warlock</title>
	<meta name="description" content="Warlock Web App" />
</svelte:head>

<Emscripten
  module={create_module}
  canvas={true}
  console={true}
  verticalOrientation={false}
  options={
    {
      autorun: true,
      wasmPath: './Test.wasm'
    }
  }
/>

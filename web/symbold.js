// Thin wrapper over c/build/symbold.wasm, shared by the browser UI and the Node agreement test.

const ENOSYS = 52;

export async function createRuntime(wasmBytes) {
  const nosys = () => ENOSYS;
  const wasi = new Proxy({
    proc_exit: (code) => { throw new Error(`wasm proc_exit(${code})`); },
  }, { get: (t, k) => t[k] ?? nosys });
  const { instance } = await WebAssembly.instantiate(wasmBytes, { wasi_snapshot_preview1: wasi });
  const w = instance.exports;
  w._initialize();

  const bytes = () => new Uint8Array(w.memory.buffer);
  const cString = (ptr) => {
    const m = bytes();
    let end = ptr;
    while (m[end]) end++;
    return new TextDecoder().decode(m.subarray(ptr, end));
  };

  return {
    // Throws with the C validator's message if the program is rejected.
    loadProgram(programBytes) {
      const data = new Uint8Array(programBytes);
      const ptr = w.alloc(data.length);
      if (!ptr) throw new Error("wasm out of memory");
      try {
        bytes().set(data, ptr);
        if (w.load(ptr, data.length) !== 0) throw new Error(cString(w.error()));
      } finally {
        w.dealloc(ptr);
      }
      return w.n_instructions();
    },
    // image: 784 uint8 pixels, row-major, ink high. Returns { label, cost }; label 255 is UNKNOWN.
    predict(image) {
      if (image.length !== 784) throw new Error("image must have 784 pixels");
      bytes().set(image, w.image());
      const label = w.predict();
      if (label < 0) throw new Error("no program loaded");
      return { label, cost: w.cost() };
    },
  };
}

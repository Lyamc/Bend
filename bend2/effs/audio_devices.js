// Audio input devices on Windows: the waveIn names, or a default stub.
function audio_devices() {
  return io_done({ $: CID(Con), head: "default", tail: { $: CID(Nil) } });
}
io_eff(CID(Audio.devices), audio_devices);

// Up to max float samples from the input; the JS device is silent.
function audio_read(audio, max) {
  const n = Math.min(Number(max), 8192);
  let xs = { $: CID(Nil) };
  for (let i = 0; i < n; i += 1) {
    xs = { $: CID(Con), head: 0, tail: xs };
  }
  return io_tup(audio, io_done(xs));
}
io_eff(CID(Audio.read), audio_read);

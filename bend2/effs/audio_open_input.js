// A silent input device: reads answer zeros at the requested rate.
function audio_open_input(name, rate) {
  if (rate < 8000 || rate > 192000) return io_fail(22);
  if (name !== "default") return io_fail(19);
  return io_done({ rate, queued: 0, at: performance.now(), input: true });
}
io_eff(CID(Audio.open_input), audio_open_input);

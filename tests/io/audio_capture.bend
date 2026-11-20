# io (io_audio_capture): the input half of the Audio kit on both lanes.
# Enumerates input devices, opens the default one, reads four samples and
# closes. A machine without a microphone answers silence rather than
# failing, so the frames' count and the success path are what is checked;
# the samples themselves depend on the device and are never printed.
import Base

law count:
  for xs: List<&2, F32>
  for n: U32
  U32

def count(xs, n):
  match xs:
    case Nil{}:
      n
    case Con{h, t}:
      count(t, (n + 1 : U32))

def show_devices(r: Result<&1, &1, U32 & String, List<String>>) -> IO(Unit):
  match r:
    case Done{xs}:
      IO.print("devices")
    case Fail{e}:
      IO.print("devices failed")

law read:
  for m: Audio & Result<&1, &1, U32 & String, List<&2, F32>>
  IO(Unit)

def read(m):
  (a, r) = m
  do IO<Unit>:
    xs : List<&2, F32> <- IO.pass(List<&2, F32>, r)
    u  : Unit <- IO.print(U32.show(count(xs, 0)))
    c  : Unit <- Audio.close(a)
    return Unit{}

def opened(r: Result<&1, &1, U32 & String, Audio>) -> IO(Unit):
  match r:
    case Done{a}:
      do IO<Unit>:
        m : Audio & Result<&1, &1, U32 & String, List<&2, F32>> <- Audio.read(a, 4)
        read(m)
    case Fail{e}:
      IO.print("open failed")

def main() -> IO(Unit):
  do IO<Unit>:
    d : Result<&1, &1, U32 & String, List<String>> <- Audio.devices()
    p : Unit <- show_devices(d)
    a : Result<&1, &1, U32 & String, Audio> <- Audio.open_input("default", 44100)
    opened(a)

#|devices
#|4

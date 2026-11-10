# windows: skip
# the program's main module: it greets forever, by an @unsafe loop
# The Linux suite only runs tests/<namespace>/*.bend, so this nested
# fixture is never executed there. Running it is an infinite loop.
import Base
import ./greet.bend as G

def serve?() -> IO(Unit):
  do IO<Unit>:
    u : Unit <- IO.print(G.greet("you"))
    serve()

def main() -> IO(Unit):
  serve()

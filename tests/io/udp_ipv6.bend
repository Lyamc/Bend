# io (io_udp_ipv6): a numeric IPv6 literal is a valid host, on both lanes.
# Binds upward from a base port on ::1, sends one datagram to itself,
# receives it whole and prints its payload and the peer address the kernel
# reports. This also checks the two lanes spell the IPv6 peer the same way.
# The handle threads through each Result on the outside, so a Fail cannot
# lose it, and the bind retries are a structural descent on tries.
import Base

law print_end:
  for s: Socket
  for p: String & U32 & String
  IO(U32)

def print_end(s, p):
  (host, from, data) = p
  do IO<U32>:
    w : Unit <- IO.print(data ++ " from " ++ host)
    c : Unit <- Socket.close(s)
    return 0

law print_go:
  for m: Socket & Result<&1, &1, (U32 & String), String & U32 & String>
  IO(U32)

def print_go(m):
  (s, r) = m
  do IO<U32>:
    p : String & U32 & String <- IO.pass(String & U32 & String, r)
    print_end(s, p)

law send_go:
  for m: Socket & Result<&1, &1, (U32 & String) & String, Unit>
  IO(Socket)

def send_go(m):
  (s, r) = m
  match r:
    case Done{u}:
      IO.pure(Socket, s)
    case Fail{(error, data)}:
      (code, message) = error
      IO.die(Socket, code, message)

law main_go:
  for b: Socket & U32
  IO(U32)

def main_go(b):
  (s, port) = b
  do IO<U32>:
    sm : Socket & Result<&1, &1, (U32 & String) & String, Unit> <- UDP.send_to(s, "::1", port, "bend-udp")
    s2 : Socket <- send_go(sm)
    m  : Socket & Result<&1, &1, (U32 & String), String & U32 & String> <- UDP.recv_from(s2, 64)
    print_go(m)

law udp_retry:
  for port : U32
  for r    : Result<&1, &1, (U32 & String), Socket>
  for rest : IO(Socket & U32)
  IO(Socket & U32)

def udp_retry(port, r, rest):
  match r:
    case Done{s}:
      IO.pure(Socket & U32, (s, port))
    case Fail{e}:
      rest

law udp_bind_any:
  for tries : Nat
  for +port : U32
  IO(Socket & U32)

def udp_bind_any(tries, port):
  match tries:
    case 0n:
      IO.die(Socket & U32, 1, "no free udp port")
    case 1n+t:
      rest = udp_bind_any(t, (port + 1 : U32))
      do IO<Socket & U32>:
        r : Result<&1, &1, (U32 & String), Socket> <- UDP.bind("::1", port)
        udp_retry(port, r, rest)

law main:
  IO(U32)

def main():
  do IO<U32>:
    b : Socket & U32 <- udp_bind_any(32n, 19611)
    main_go(b)

#|bend-udp from ::1

# io (io_udp_ipv6_bad_address): an IPv6 literal is either a full eight
# groups or one "::" filling the missing ones; a bad group, two "::", a
# short literal without "::" and an out-of-range embedded IPv4 quad are
# all refused with EINVAL before any socket sees them. Both lanes use the
# one shared parser, so the same six probes answer identically.
import Base

law answer:
  for m: Socket & Result<&1, &1, (U32 & String) & String, Unit>
  IO(Socket)

def answer(m):
  (s, r) = m
  match r:
    case Done{value}:
      do IO<Socket>:
        p : Unit <- IO.print("sent")
        return s
    case Fail{(error, data)}:
      (code, message) = error
      do IO<Socket>:
        p : Unit <- IO.print(message ++ ", kept " ++ data)
        return s

law probe:
  for s    : Socket
  for host : String
  IO(Socket)

def probe(s, host):
  do IO<Socket>:
    m : Socket & Result<&1, &1, (U32 & String) & String, Unit> <- UDP.send_to(s, host, 9, "x")
    answer(m)

law main:
  IO(U32)

def main():
  do IO<U32>:
    s : Socket <- IO.try(Socket, UDP.bind("::1", 0))
    a : Socket <- probe(s, "::1::2")
    b : Socket <- probe(a, "gg::1")
    c : Socket <- probe(b, "1:2:3")
    d : Socket <- probe(c, "::1:2:3:4:5:6:7:8:9")
    e : Socket <- probe(d, "::ffff:1.2.3.999")
    n : Socket <- probe(e, "::1")
    x : Unit <- Socket.close(n)
    return 0

#|Invalid argument, kept x
#|Invalid argument, kept x
#|Invalid argument, kept x
#|Invalid argument, kept x
#|Invalid argument, kept x
#|sent

@0xaadf1d022a59186b;

using Grain = import "/sandstorm/grain.capnp";

interface TestInterface extends (Grain.AppPersistent(Text)) {
  foo @0 (a :Text) -> (b: Text);
}

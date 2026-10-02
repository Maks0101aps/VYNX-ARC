fn main() {
    cxx_build::bridge("src/ffi.rs")
        .std("c++20")
        .compile("vynx-arc-bridge");
    println!("cargo:rerun-if-changed=src/ffi.rs");
}

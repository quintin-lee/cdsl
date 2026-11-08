fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rustc-link-search=native=../../build");
    println!("cargo:rustc-link-lib=cdsl");
}

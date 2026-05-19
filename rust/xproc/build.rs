use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("manifest dir"));
    let repo_root = manifest_dir
        .parent()
        .and_then(|p| p.parent())
        .expect("repo root")
        .to_path_buf();

    let build_dir = env::var("XPROC_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build"));
    let capi_dir = build_dir.join("capi");

    println!("cargo:rerun-if-env-changed=XPROC_BUILD_DIR");

    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-arg-examples=-Wl,-rpath,{}", build_dir.display());
        println!("cargo:rustc-link-arg-examples=-Wl,-rpath,{}", capi_dir.display());
        println!("cargo:rustc-link-arg-tests=-Wl,-rpath,{}", build_dir.display());
        println!("cargo:rustc-link-arg-tests=-Wl,-rpath,{}", capi_dir.display());
    }
}

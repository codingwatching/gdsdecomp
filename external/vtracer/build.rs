extern crate cbindgen;

use std::{env, path::PathBuf};

fn main() {
    let crate_dir = PathBuf::from(
        env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR env var is not defined"),
    );

    let package_name = env::var("CARGO_PKG_NAME").expect("CARGO_PKG_NAME env var is not defined");

    let config = cbindgen::Config::from_file("cbindgen.toml")
        .expect("Unable to find cbindgen.toml configuration file");

    if let Ok(writer) = cbindgen::generate_with_config(crate_dir.clone(), config) {
        // \note CMake sets this environment variable before invoking Cargo so
        //       that it can direct the generated header file into its
        //       out-of-source build directory for post-processing.

        let header_file = format!("{}.h", package_name);
        let header_path = if let Ok(target_dir) = env::var("CBINDGEN_TARGET_DIR") {
			if !std::path::Path::new(&target_dir).exists() {
				std::fs::create_dir_all(target_dir.clone()).expect("Failed to create target directory");
			}
            PathBuf::from(target_dir).join(header_file.clone())
        } else {
            PathBuf::from("include")
                .join("vtracer")
                .join(header_file.clone())
        };
        // Write to an in-memory buffer first, then compare it to the expected content.
        let mut generated_bytes = Vec::new();
        writer.write(&mut generated_bytes);
        let generated_content =
            String::from_utf8(generated_bytes).expect("Failed to decode generated header content");

        if header_path.exists() {
            let expected_content = std::fs::read_to_string(header_path.clone())
                .expect("Failed to read expected content");
            if expected_content == generated_content {
                println!("Header content matches expected content, not regenerating");
                return;
            }
        }

        println!("cargo:warning=Regenerated header at path: {}", header_path.display());
        std::fs::write(header_path, generated_content).expect("Failed to write generated header to file");
    }
}

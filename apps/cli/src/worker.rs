fn main() {
    if vynx_arc_core::worker::serve().is_err() {
        // Diagnostics must never format a request or credential.
        std::process::exit(1);
    }
}

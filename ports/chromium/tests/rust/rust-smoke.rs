use std::cell::Cell;

thread_local! {
    static TLS_VALUE: Cell<u32> = const { Cell::new(0) };
}

#[no_mangle]
pub extern "C" fn alos_rust_smoke_thread(seed: u32) -> u32 {
    TLS_VALUE.with(|slot| {
        slot.set(seed);
        let mut values = Vec::new();
        values.push(seed);
        values.push(seed + 3);
        let label = String::from("alos-rust");
        slot.set(slot.get() + values.iter().sum::<u32>() + label.len() as u32);
        slot.get()
    })
}
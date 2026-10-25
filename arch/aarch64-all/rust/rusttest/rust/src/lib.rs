/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The Rust half of rusttest.resource.

    A no_std staticlib linked into the module by its C shell. The C side
    owns everything AROS: the RomTag, the library base, the LVO table and
    the register convention. Rust exports plain extern "C" functions that
    the LVOs call, and imports three flat C functions back: AllocVec,
    FreeVec and a panic report. The global allocator sits on exec AllocVec,
    so alloc's collections work inside the module.
*/

#![no_std]

extern crate alloc;

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};
use core::ptr;
use core::sync::atomic::{AtomicUsize, Ordering};

extern "C" {
    fn rusttest_c_allocvec(size: usize) -> *mut u8;
    fn rusttest_c_freevec(block: *mut u8);
    fn rusttest_c_panic() -> !;
}

/* "RUST": RustTestVersion returns it so a caller can tell that the value
 * came out of Rust code. */
const RUSTTEST_MAGIC: u32 = 0x5255_5354;
/* The largest RustTestAlloc count; above it the call returns 0. */
const RUSTTEST_MAX_COUNT: u32 = 65536;

const FNV_OFFSET: u32 = 2_166_136_261;
const FNV_PRIME: u32 = 16_777_619;

/* AllocVec aligns to MEM_BLOCKSIZE, but a Layout can ask for more. Each
 * block is over-allocated, the returned pointer is aligned inside it, and
 * the AllocVec pointer is kept in the word just below for dealloc. */
struct ExecAllocator;

const HEADER: usize = core::mem::size_of::<usize>();

static LIVE_BLOCKS: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for ExecAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let align = layout.align().max(HEADER);
        let total = match layout.size().checked_add(align + HEADER) {
            Some(total) => total,
            None => return ptr::null_mut(),
        };
        let base = rusttest_c_allocvec(total) as usize;
        if base == 0 {
            return ptr::null_mut();
        }
        let user = (base + HEADER + (align - 1)) & !(align - 1);
        ((user - HEADER) as *mut usize).write(base);
        LIVE_BLOCKS.fetch_add(1, Ordering::Relaxed);
        user as *mut u8
    }

    unsafe fn dealloc(&self, block: *mut u8, _layout: Layout) {
        let base = ((block as usize - HEADER) as *const usize).read();
        LIVE_BLOCKS.fetch_sub(1, Ordering::Relaxed);
        rusttest_c_freevec(base as *mut u8);
    }
}

#[global_allocator]
static ALLOCATOR: ExecAllocator = ExecAllocator;

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { rusttest_c_panic() }
}

#[inline]
fn fnv(hash: u32, value: u32) -> u32 {
    (hash ^ value).wrapping_mul(FNV_PRIME)
}

#[no_mangle]
pub extern "C" fn rusttest_rs_version() -> u32 {
    RUSTTEST_MAGIC
}

#[no_mangle]
pub extern "C" fn rusttest_rs_add(a: i32, b: i32) -> i32 {
    a.wrapping_add(b)
}

/* Builds a Vec<u32> of count Knuth-hashed words one push at a time, a
 * String of count/10 lines and a BTreeMap of count entries inserted in
 * descending key order, then folds them with FNV-1a in that order. All of
 * it is freed before the call returns. RustTest computes the same digest
 * in C. */
#[no_mangle]
pub extern "C" fn rusttest_rs_alloc_checksum(count: u32) -> u32 {
    if count == 0 || count > RUSTTEST_MAX_COUNT {
        return 0;
    }
    let mut words: Vec<u32> = Vec::new();
    for i in 0..count {
        words.push(i.wrapping_mul(2_654_435_761));
    }
    let mut text = String::new();
    for _ in 0..count / 10 {
        text.push_str("rust-resource\n");
    }
    let mut map: BTreeMap<u32, u32> = BTreeMap::new();
    for i in 0..count {
        map.insert(count - 1 - i, i.wrapping_mul(3));
    }

    let mut hash = FNV_OFFSET;
    for word in &words {
        hash = fnv(hash, *word);
    }
    for byte in text.bytes() {
        hash = fnv(hash, byte as u32);
    }
    for (key, value) in &map {
        hash = fnv(hash, *key);
        hash = fnv(hash, *value);
    }
    hash
}

#[no_mangle]
pub extern "C" fn rusttest_rs_live_blocks() -> u32 {
    LIVE_BLOCKS.load(Ordering::Relaxed) as u32
}

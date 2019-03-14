//! hashtable support

use libc::c_void;
use std::ptr;

use remacs_macros::lisp_fn;

use crate::{
    data::aref,
    lisp::{ExternalPtr, LispObject},
    lists::{list, put},
    remacs_sys::{
        gc_aset, hash_clear, hash_lookup, hash_put, hash_remove_from_table, Fcopy_sequence,
    },
    remacs_sys::{
        pvec_type, EmacsDouble, EmacsInt, EmacsUint, Lisp_Hash_Table, Lisp_Type, CHECK_IMPURE,
    },
    remacs_sys::{Qhash_table_p, Qhash_table_test},
    symbols::LispSymbolRef,
};

pub type LispHashTableRef = ExternalPtr<Lisp_Hash_Table>;

#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug)]
pub enum HashLookupResult {
    Missing(EmacsUint),
    Found(isize),
}
use self::HashLookupResult::{Found, Missing};

impl LispHashTableRef {
    pub fn empty() -> Self {
        Self::new(ptr::null_mut())
    }

    pub fn allocate() -> LispHashTableRef {
        let vec_ptr = allocate_pseudovector!(Lisp_Hash_Table, count, pvec_type::PVEC_HASH_TABLE);
        LispHashTableRef::new(vec_ptr)
    }

    pub unsafe fn copy(&mut self, other: LispHashTableRef) {
        ptr::copy_nonoverlapping(other.as_ptr(), self.as_mut(), 1);
    }

    pub fn set_next_weak(&mut self, other: LispHashTableRef) {
        self.next_weak = other.as_ptr() as *mut Lisp_Hash_Table;
    }

    pub fn get_next_weak(self) -> LispHashTableRef {
        LispHashTableRef::new(self.next_weak)
    }

    pub fn set_hash(&mut self, hash: LispObject) {
        self.hash = hash;
    }

    pub fn get_hash(self) -> LispObject {
        self.hash
    }

    pub fn set_next(&mut self, next: LispObject) {
        self.next = next;
    }

    pub fn get_next(self) -> LispObject {
        self.next
    }

    pub fn set_index(&mut self, index: LispObject) {
        self.index = index;
    }

    pub fn get_index(self) -> LispObject {
        self.index
    }

    pub fn get_key_and_value(self) -> LispObject {
        self.key_and_value
    }

    pub fn set_key_and_value(&mut self, key_and_value: LispObject) {
        self.key_and_value = key_and_value;
    }

    pub fn get_weak(self) -> LispObject {
        self.weak
    }

    pub fn get_hash_value(self, idx: isize) -> LispObject {
        aref(self.key_and_value, (2 * idx + 1) as EmacsInt)
    }

    pub fn set_hash_value(self, idx: isize, value: LispObject) {
        unsafe { gc_aset(self.key_and_value, 2 * idx + 1, value) };
    }

    pub fn get_hash_key(self, idx: isize) -> LispObject {
        aref(self.key_and_value, (2 * idx) as EmacsInt)
    }

    pub fn get_hash_hash(self, idx: isize) -> LispObject {
        aref(self.hash, idx as EmacsInt)
    }

    pub fn lookup(self, key: LispObject) -> HashLookupResult {
        // This allows `self` to be immutable.
        let mutself = self.as_ptr() as *mut Lisp_Hash_Table;
        let mut hash = 0;
        let idx = unsafe { hash_lookup(mutself, key, &mut hash) };
        if idx < 0 {
            Missing(hash)
        } else {
            Found(idx)
        }
    }

    pub fn put(mut self, key: LispObject, value: LispObject, hash: EmacsUint) -> isize {
        unsafe { hash_put(self.as_mut(), key, value, hash) }
    }

    pub fn remove(mut self, key: LispObject) {
        unsafe { hash_remove_from_table(self.as_mut(), key) };
    }

    pub fn size(self) -> usize {
        unsafe { self.get_next().as_vector_unchecked().len() }
    }

    pub fn clear(mut self) {
        unsafe { hash_clear(self.as_mut()) }
    }

    pub fn check_impure(self, object: LispHashTableRef) {
        unsafe { CHECK_IMPURE(object.into(), self.as_ptr() as *mut c_void) };
    }
}

impl From<LispObject> for LispHashTableRef {
    fn from(o: LispObject) -> Self {
        if o.is_hash_table() {
            LispHashTableRef::new(o.get_untaggedptr() as *mut Lisp_Hash_Table)
        } else {
            wrong_type!(Qhash_table_p, o);
        }
    }
}

impl From<LispHashTableRef> for LispObject {
    fn from(h: LispHashTableRef) -> Self {
        let object = LispObject::tag_ptr(h, Lisp_Type::Lisp_Vectorlike);
        debug_assert!(
            object.is_vectorlike() && object.get_untaggedptr() == h.as_ptr() as *mut c_void
        );

        debug_assert!(object.is_hash_table());
        object
    }
}

impl LispObject {
    pub fn is_hash_table(self) -> bool {
        self.as_vectorlike()
            .map_or(false, |v| v.is_pseudovector(pvec_type::PVEC_HASH_TABLE))
    }
}

/// An iterator used for iterating over the indices
/// of the `key_and_value` vector of a `Lisp_Hash_Table`.
/// Equivalent to a `for (i = 0; i < HASH_TABLE_SIZE(h); ++i)`
/// loop in the C layer.
pub struct HashTableIter<'a> {
    table: &'a LispHashTableRef,
    current: usize,
}

impl<'a> Iterator for HashTableIter<'a> {
    type Item = isize;

    fn next(&mut self) -> Option<isize> {
        // This is duplicating `LispHashTableRef::size` to keep inline with the old C code,
        // in which the len of the vector could technically change while iterating. While
        // I don't know if any code actually uses that behavior, I'm going to avoid making
        // this use size to keep it consistent.
        let next_vector = unsafe { self.table.get_next().as_vector_unchecked() };
        if self.current < next_vector.len() {
            let cur = self.current;
            self.current += 1;
            Some(cur as isize)
        } else {
            None
        }
    }
}

/// An iterator used for looping over the keys and values
/// contained in a `Lisp_Hash_Table`.
pub struct KeyAndValueIter<'a>(HashTableIter<'a>);

impl<'a> Iterator for KeyAndValueIter<'a> {
    type Item = (LispObject, LispObject);

    fn next(&mut self) -> Option<(LispObject, LispObject)> {
        while let Some(idx) = self.0.next() {
            let is_not_nil = self.0.table.get_hash_hash(idx).is_not_nil();
            if is_not_nil {
                let key = self.0.table.get_hash_key(idx);
                let value = self.0.table.get_hash_value(idx);
                return Some((key, value));
            }
        }

        None
    }
}

// The references are necessary to satisfy the lifetime requirements.
// Otherwise, the iterator would contain a copy which might no longer be valid.
impl LispHashTableRef {
    pub fn indices(&self) -> HashTableIter {
        HashTableIter {
            table: self,
            current: 0,
        }
    }

    pub fn iter(&self) -> KeyAndValueIter {
        KeyAndValueIter(self.indices())
    }
}

/// Return a copy of hash table TABLE.
/// Keys and values are not copied, only the table itself is.
#[lisp_fn]
pub fn copy_hash_table(mut table: LispHashTableRef) -> LispHashTableRef {
    let mut new_table = LispHashTableRef::allocate();
    unsafe { new_table.copy(table) };
    assert_ne!(new_table.as_ptr(), table.as_ptr());

    let key_and_value = unsafe { Fcopy_sequence(new_table.get_key_and_value()) };
    let hash = unsafe { Fcopy_sequence(new_table.get_hash()) };
    let next = unsafe { Fcopy_sequence(new_table.get_next()) };
    let index = unsafe { Fcopy_sequence(new_table.get_index()) };
    new_table.set_key_and_value(key_and_value);
    new_table.set_hash(hash);
    new_table.set_next(next);
    new_table.set_index(index);

    if new_table.get_weak().is_not_nil() {
        new_table.set_next_weak(table.get_next_weak());
        table.set_next_weak(new_table);
    }

    new_table
}

/// Look up KEY in TABLE and return its associated value.
/// If KEY is not found, return DFLT which defaults to nil.
#[lisp_fn(min = "2")]
pub fn gethash(key: LispObject, hash_table: LispHashTableRef, dflt: LispObject) -> LispObject {
    match hash_table.lookup(key) {
        Found(idx) => hash_table.get_hash_value(idx),
        Missing(_) => dflt,
    }
}

/// Associate KEY with VALUE in hash table TABLE.
/// If KEY is already present in table, replace its current value with
/// VALUE.  In any case, return VALUE.
#[lisp_fn]
pub fn puthash(key: LispObject, value: LispObject, hash_table: LispHashTableRef) -> LispObject {
    hash_table.check_impure(hash_table);

    match hash_table.lookup(key) {
        Found(idx) => {
            hash_table.set_hash_value(idx, value);
        }
        Missing(hash) => {
            hash_table.put(key, value, hash);
        }
    }

    value
}

/// Remove KEY from TABLE.
#[lisp_fn]
pub fn remhash(key: LispObject, hash_table: LispHashTableRef) {
    hash_table.check_impure(hash_table);
    hash_table.remove(key);
}

/// Call FUNCTION for all entries in hash table TABLE.
/// FUNCTION is called with two arguments, KEY and VALUE.
/// `maphash' always returns nil.
#[lisp_fn]
pub fn maphash(function: LispObject, hash_table: LispHashTableRef) {
    for (key, value) in hash_table.iter() {
        call!(function, key, value);
    }
}

/// Return t if OBJ is a Lisp hash table object.
#[lisp_fn]
pub fn hash_table_p(obj: LispObject) -> bool {
    obj.is_hash_table()
}

/// Return the number of elements in TABLE.
#[lisp_fn]
pub fn hash_table_count(table: LispHashTableRef) -> EmacsInt {
    table.count as EmacsInt
}

/// Return the current rehash threshold of TABLE.
#[lisp_fn]
pub fn hash_table_rehash_threshold(table: LispHashTableRef) -> EmacsDouble {
    EmacsDouble::from(table.rehash_threshold)
}

/// Return the size of TABLE.
/// The size can be used as an argument to `make-hash-table' to create
/// a hash table than can hold as many elements as TABLE holds
/// without need for resizing.
#[lisp_fn]
pub fn hash_table_size(table: LispHashTableRef) -> EmacsInt {
    table.size() as EmacsInt
}

/// Return the test TABLE uses.
#[lisp_fn]
pub fn hash_table_test(table: LispHashTableRef) -> LispObject {
    table.test.name
}

/// Return the weakness of TABLE.
#[lisp_fn]
pub fn hash_table_weakness(table: LispHashTableRef) -> LispObject {
    table.get_weak()
}

/// Clear hash table TABLE and return it.
#[lisp_fn]
pub fn clrhash(hash_table: LispHashTableRef) -> LispHashTableRef {
    hash_table.check_impure(hash_table);
    hash_table.clear();
    hash_table
}

/// Define a new hash table test with name NAME, a symbol.
///
/// In hash tables created with NAME specified as test, use TEST to
/// compare keys, and HASH for computing hash codes of keys.
///
/// TEST must be a function taking two arguments and returning non-nil if
/// both arguments are the same.  HASH must be a function taking one
/// argument and returning an object that is the hash code of the argument.
/// It should be the case that if (eq (funcall HASH x1) (funcall HASH x2))
/// returns nil, then (funcall TEST x1 x2) also returns nil.
#[lisp_fn]
pub fn define_hash_table_test(
    name: LispSymbolRef,
    test: LispObject,
    hash: LispObject,
) -> LispObject {
    let sym = Qhash_table_test;
    put(name, sym, list(&[test, hash]))
}

include!(concat!(env!("OUT_DIR"), "/hashtable_exports.rs"));
